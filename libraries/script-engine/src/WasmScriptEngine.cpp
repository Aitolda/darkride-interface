//
//  WasmScriptEngine.cpp
//  libraries/script-engine/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "WasmScriptEngine.h"

#include <chrono>

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtScript/QScriptEngine>
#include <QtScript/QScriptValue>
#include <QtScript/QScriptValueIterator>

#include <AddressManager.h>
#include <EntityScriptingInterface.h>
#include <LocationScriptingInterface.h>
#include <NumericalConstants.h>
#include <PathUtils.h>
#include <ResourceManager.h>
#include <SettingHandle.h>
#include <SharedUtil.h>
#include <shared/LocalFileAccessGate.h>

#include <FunctionTwins.h>
#include <HostInterface.h>
#include <ObjectTable.h>
#include <SignalRelay.h>

#include "BatchLoader.h"
#include "HttpRequestObject.h"
#include "ScriptCache.h"
#include "ScriptEngineLogging.h"
#include "ScriptEngines.h"
#include "WebSocketObject.h"

namespace {

const QString WASM_SUFFIX = QStringLiteral("wasm");
const QString JS_SUFFIX = QStringLiteral("js");
const int MIN_TIMEOUT_FOR_COARSE_TIMER = 200;  // below this a Qt timer is made precise, as the QtScript engine does
const int MAX_SCRIPT_QUITTING_TIME_MS = 500;
const int MAX_MODULE_ID_LENGTH = 4096;
const int MAX_DEBUG_VALUE_LENGTH = 80;

Setting::Handle<bool> runJavaScriptOnWebAssemblySetting { "scripting/runJavaScriptOnWebAssembly", false };

// The engine as a callback target: a callback decoded by its codec calls back through it. It outlives the
// engine as a weak reference in the callbacks; a dead engine answers nothing.
class EngineCallbackTarget : public CallbackTarget {
public:
    explicit EngineCallbackTarget(WasmScriptEngine* engine) : _engine(engine) {}
    bool invoke(int32_t callback, const QVariantList& arguments, QVariant& result, QString& error) override {
        if (!_engine) {
            error = "the script engine is gone";
            return false;
        }
        if (QThread::currentThread() != _engine->thread()) {
            error = "a callback can only be called for its result on the script's thread";
            return false;
        }
        return _engine->invokeCallback(callback, arguments, result, error);
    }
    void post(int32_t callback, const QVariantList& arguments) override {
        if (_engine) {
            QMetaObject::invokeMethod(_engine, "postCallback", Qt::QueuedConnection, Q_ARG(int, int(callback)), Q_ARG(QVariantList, arguments));
        }
    }
private:
    QPointer<WasmScriptEngine> _engine;
};

QString suffixOf(const QString& fileNameOrUrl) {
    QUrl url(fileNameOrUrl);
    QString path = url.isValid() && !url.scheme().isEmpty() ? url.path() : fileNameOrUrl;
    return QFileInfo(path).suffix().toLower();
}

} // namespace

WasmScriptEngine::WasmScriptEngine(Context context, const QString& scriptContents, const QString& fileNameString) :
    ScriptEngine(context, scriptContents, fileNameString),
    _runsJavaScript(!isWasmScript(fileNameString)),
    _objects(new ObjectTable()),
    _codec(new ValueCodec(_objects.get())),
    _relay(new SignalRelay(this)),
    _http(new HttpRequestFactory(this)),
    _sockets(new WebSocketFactory(this)) {
    _callbackTarget = std::make_shared<EngineCallbackTarget>(this);
    _codec->setCallbackTarget(_callbackTarget);
    if (context == ENTITY_CLIENT_SCRIPT || context == ENTITY_SERVER_SCRIPT) {
        // an entities engine hosts every entity script of the world: more room
        _limits.memoryBytes = int64_t(1024) * 1024 * 1024;
    }
    if (_runsJavaScript) {
        // a JavaScript script may run as long as it did on QtScript, which had no limit; stop() still interrupts it
        _limits.callTimeout = std::chrono::milliseconds(0);
    }
}

WasmScriptEngine::~WasmScriptEngine() {
    stopTimers();
    _host.reset();
    std::lock_guard<std::mutex> lock(_instanceMutex);
    _instance.reset();
}

bool WasmScriptEngine::isWasmScript(const QString& fileNameOrUrl) {
    return suffixOf(fileNameOrUrl) == WASM_SUFFIX;
}

bool WasmScriptEngine::isJavaScriptFile(const QString& fileNameOrUrl) {
    return suffixOf(fileNameOrUrl) == JS_SUFFIX;
}

std::shared_ptr<WasmRuntime> WasmScriptEngine::runtime() {
    static std::shared_ptr<WasmRuntime> shared = std::make_shared<WasmRuntime>();
    return shared;
}

bool WasmScriptEngine::runJavaScriptOnWebAssembly() {
    // DARKRIDE_RUN_JS_ON_WASM=1 in the environment turns the setting on for one client (test runs)
    if (qEnvironmentVariableIsSet("DARKRIDE_RUN_JS_ON_WASM")) {
        return qEnvironmentVariable("DARKRIDE_RUN_JS_ON_WASM") == "1";
    }
    return runJavaScriptOnWebAssemblySetting.get();
}

void WasmScriptEngine::setRunJavaScriptOnWebAssembly(bool on) {
    runJavaScriptOnWebAssemblySetting.set(on);
}

QString WasmScriptEngine::javascriptGuestPath() {
    QString fromEnvironment = qEnvironmentVariable("DARKRIDE_JS_WASM");
    if (!fromEnvironment.isEmpty()) {
        return fromEnvironment;
    }
#if defined(Q_OS_MAC)
    return QCoreApplication::applicationDirPath() + "/../Resources/wasm/darkride-js.wasm";
#else
    return QCoreApplication::applicationDirPath() + "/resources/wasm/darkride-js.wasm";
#endif
}

// The guest is one module for every JavaScript script of the process: compiled once, shared.
std::shared_ptr<WasmModule> WasmScriptEngine::javascriptGuestModule(QString& error) {
    static std::mutex mutex;
    static std::shared_ptr<WasmModule> cached;
    static QString cachedPath;
    std::lock_guard<std::mutex> lock(mutex);
    const QString path = javascriptGuestPath();
    if (cached && cachedPath == path) {
        return cached;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "the JavaScript guest module is not at " + path;
        return nullptr;
    }
    auto started = usecTimestampNow();
    std::shared_ptr<WasmModule> module = runtime()->compile(file.readAll(), error);
    if (module) {
        qCDebug(scriptengine) << "JavaScript guest" << path << "compiled in" << (usecTimestampNow() - started) / 1000 << "ms";
        cached = module;
        cachedPath = path;
    }
    return module;
}

bool WasmScriptEngine::hasValidScriptSuffix(const QString& scriptFileName) {
    return isWasmScript(scriptFileName) || ScriptEngine::hasValidScriptSuffix(scriptFileName);
}

// --- registrations ---------------------------------------------------------------------------------------------

void WasmScriptEngine::registerGlobalObject(const QString& name, QObject* object) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "registerGlobalObject", Q_ARG(const QString&, name), Q_ARG(QObject*, object));
        return;
    }
    if (!object) {
        qCDebug(scriptengine) << "WasmScriptEngine: global" << name << "is null, not registered";
        return;
    }
    if (_objects->global(name) == ObjectTable::NULL_HANDLE) {
        _objects->registerGlobal(name, object);
    }
}

void WasmScriptEngine::registerGetterSetter(const QString& name, QScriptEngine::FunctionSignature getter,
                                            QScriptEngine::FunctionSignature setter, const QString& parent) {
    Q_UNUSED(setter);
    const QString fullName = parent.isEmpty() ? name : parent + "." + name;
    if (getter == &LocationScriptingInterface::locationGetter) {
        // `location` is the address manager behind a getter; a module gets the object itself
        registerGlobalObject(fullName, DependencyManager::get<AddressManager>().data());
        return;
    }
    qCDebug(scriptengine) << "WasmScriptEngine: the property" << fullName << "is a QtScript getter and setter, not available to a module";
}

void WasmScriptEngine::registerFunction(const QString& name, QScriptEngine::FunctionSignature fun, int numArguments) {
    Q_UNUSED(numArguments);
    // a library may have registered an object that stands in for the function (FunctionTwins)
    FunctionTwins::Factory factory = FunctionTwins::find(reinterpret_cast<const void*>(fun));
    if (factory) {
        QObject* twin = factory();
        if (twin) {
            twin->setParent(this);
            registerGlobalObject(name, twin);
            return;
        }
    }
    qCDebug(scriptengine) << "WasmScriptEngine: the function" << name << "is a QtScript function, not available to a module";
}

void WasmScriptEngine::registerFunction(const QString& parent, const QString& name, QScriptEngine::FunctionSignature fun,
                                        int numArguments) {
    Q_UNUSED(fun);
    Q_UNUSED(numArguments);
    qCDebug(scriptengine) << "WasmScriptEngine: the function" << parent + "." + name << "is a QtScript function, not available to a module";
}

void WasmScriptEngine::registerValue(const QString& valueName, QScriptValue value) {
    Q_UNUSED(value);
    qCDebug(scriptengine) << "WasmScriptEngine: the value" << valueName << "is a QtScript value, not available to a module";
}

// --- loading ----------------------------------------------------------------------------------------------------

void WasmScriptEngine::loadURL(const QUrl& scriptURL, bool reload) {
    if (_isRunning) {
        return;
    }
    if (_runsJavaScript) {
        // the text, as the QtScript engine fetches it
        ScriptEngine::loadURL(scriptURL, reload);
        return;
    }
    QUrl url = expandScriptUrl(scriptURL);
    _fileNameString = url.toString();
    _isReloading = reload;
    if (!hasValidScriptSuffix(_fileNameString)) {
        scriptErrorMessage("File extension of file: " + _fileNameString + " is not a currently supported script type");
        emit errorLoadingScript(_fileNameString);
        return;
    }
    const int maxRetries = 0;
    auto scriptCache = DependencyManager::get<ScriptCache>();
    scriptCache->getScriptBytes(url.toString(), [this](const QString& url, const QByteArray& bytes, bool success, const QString& status) {
        qCDebug(scriptengine) << "loadURL (module)" << url << status << QThread::currentThread();
        if (!success || bytes.isEmpty()) {
            scriptErrorMessage("ERROR Loading module (" + status + "):" + url);
            emit errorLoadingScript(_fileNameString);
            return;
        }
        _moduleBytes = bytes;
        emit scriptLoaded(url);
    }, reload, maxRetries);
}

// --- running ----------------------------------------------------------------------------------------------------

void WasmScriptEngine::run() {
    if (QThread::currentThread() != qApp->thread() && _context == Context::CLIENT_SCRIPT) {
        hifi::scripting::setLocalAccessSafeThread(true);
    }
    QSharedPointer<ScriptEngines> scriptEngines(_scriptEngines);
    if (!scriptEngines || scriptEngines->isStopped()) {
        return;
    }
    scriptInfoMessage((_runsJavaScript ? "JavaScript on the WebAssembly script engine starting:" : "WebAssembly script engine starting:") + getFilename());

    if (!_isInitialized) {
        // the QtScript engine's own initialization: it registers the standard globals (through this class's
        // registerGlobalObject) and the QtScript conversions on the carried engine, which the serializer uses
        init();
    }
    adoptScriptGlobals();
    installSerializer();
    // the network objects XMLHttpRequest and WebSocket are built on in the guest
    registerGlobalObject("Http", _http);
    registerGlobalObject("Sockets", _sockets);

    _isRunning = true;
    emit runningStateChanged();

    if (startModule()) {
        loop();
    }
    stopModule();

    emit finished(_fileNameString, qSharedPointerCast<ScriptEngine>(sharedFromThis()));
    hifi::scripting::setLocalAccessSafeThread(false);
    _isRunning = false;
    emit runningStateChanged();
    emit doneRunning();
}

// A few hosts put an object on the QtScript global object directly (MyAvatar: globalObject().setProperty with
// newQObject) rather than through registerGlobalObject. The carried engine received those; the module gets
// them too, under the same names, so it sees the globals a JavaScript script sees.
void WasmScriptEngine::adoptScriptGlobals() {
    QScriptValueIterator it(globalObject());
    int adopted = 0;
    while (it.hasNext()) {
        it.next();
        QScriptValue value = it.value();
        if (!value.isQObject()) {
            continue;
        }
        QObject* object = value.toQObject();
        if (object && _objects->global(it.name()) == ObjectTable::NULL_HANDLE) {
            _objects->registerGlobal(it.name(), object);
            ++adopted;
        }
    }
    qCDebug(scriptengine) << "WasmScriptEngine:" << _objects->globalNames().size() << "globals for the module," << adopted << "adopted from the QtScript global object";
}

bool WasmScriptEngine::startModule() {
    QString error;
    if (_runsJavaScript) {
        if (_scriptContents.isEmpty()) {
            scriptErrorMessage("No script to run: " + _fileNameString);
            return false;
        }
        _module = javascriptGuestModule(error);
        if (!_module) {
            scriptErrorMessage("Cannot run " + getFilename() + " on WebAssembly: " + error);
            return false;
        }
    } else {
        if (_moduleBytes.isEmpty()) {
            scriptErrorMessage("No module to run: " + _fileNameString);
            return false;
        }
        _module = runtime()->compile(_moduleBytes, error);
        if (!_module) {
            scriptErrorMessage("Cannot compile " + getFilename() + ": " + error);
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(_instanceMutex);
        _instance = std::make_unique<WasmInstance>(_module, _limits);
    }
    _host = std::make_unique<HostInterface>(*_instance, *_objects, *_codec, *_relay);
    _host->setLogger([this](int level, const QString& text) {
        switch (level) {
            case HostInterface::Print:
                scriptPrintedMessage(text);
                break;
            case HostInterface::Info:
                scriptInfoMessage(text);
                break;
            case HostInterface::Warning:
                scriptWarningMessage(text);
                break;
            case HostInterface::Error:
                scriptErrorMessage(text);
                break;
            default:
                qCDebug(scriptengine).noquote() << "[" + getFilename() + "]" << text;
                break;
        }
    });
    _host->setFailureHandler([this](const QString& entry, const QString& failure) { onModuleFailure(entry, failure); });
    _host->install();
    _instance->enableWasi();
    if (!_instance->instantiate(error)) {
        scriptErrorMessage("Cannot instantiate " + getFilename() + ": " + error);
        return false;
    }
    for (const char* required : { "alloc", "script_start" }) {
        if (!_instance->hasExport(required)) {
            scriptErrorMessage(QString("%1 does not export %2, which a script module must").arg(getFilename(), required));
            return false;
        }
    }
    if (_instance->hasExport("_initialize")) {
        // a reactor's initializer (its C runtime's constructors), before anything else is called
        std::vector<WasmValue> results;
        ++_callDepth;
        bool initialized = _instance->call("_initialize", {}, results, error);
        --_callDepth;
        if (!initialized) {
            scriptErrorMessage(getFilename() + " failed to initialize: " + error);
            return false;
        }
    }
    _moduleRunning = true;
    ++_callDepth;
    bool started = _host->callScriptStart(error);
    --_callDepth;
    return started && !_isFinished;
}

void WasmScriptEngine::loop() {
    using clock = std::chrono::steady_clock;
    const std::chrono::microseconds FRAME(USECS_PER_SECOND / SCRIPT_FPS + 1);
    QSharedPointer<ScriptEngines> scriptEngines(_scriptEngines);
    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
    clock::time_point startTime = clock::now();
    int thisFrame = 0;
    _lastUpdate = usecTimestampNow();

    while (!_isFinished) {
        // a steady SCRIPT_FPS: the next frame is on the schedule from the start, and a script that fell more
        // than a frame behind starts a new schedule rather than catching up in a burst
        clock::time_point target = startTime + (++thisFrame * FRAME);
        clock::time_point now = clock::now();
        if (now > target + FRAME) {
            startTime = now;
            thisFrame = 0;
            target = now;
        }
        auto sleepFor = std::chrono::duration_cast<std::chrono::milliseconds>(target - now);
        if (!scriptEngines->getBypassScriptThrottling() && sleepFor > std::chrono::milliseconds(0)) {
            // the sleep is an event loop: queued signals, timers and invocations from other threads run here
            QEventLoop eventLoop;
            QTimer timer;
            timer.setSingleShot(true);
            timer.setTimerType(Qt::PreciseTimer);
            connect(&timer, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
            timer.start(int(sleepFor.count()));
            eventLoop.exec();
        } else {
            QCoreApplication::processEvents();
        }
        if (_isFinished) {
            break;
        }
        // entity edits the script queued go out, as in the QtScript engine's loop
        auto packetSender = entityScriptingInterface->getEntityPacketSender();
        if (packetSender->serversExist()) {
            packetSender->releaseQueuedMessages();
            if (!packetSender->isThreaded()) {
                packetSender->process();
            }
        }
        qint64 nowUsecs = usecTimestampNow();
        if (_emitScriptUpdates() && _lastUpdate < nowUsecs) {
            float deltaTime = float(nowUsecs - _lastUpdate) / float(USECS_PER_SECOND);
            auto before = clock::now();
            ++_callDepth;
            emit update(deltaTime);
            --_callDepth;
            _updateMicroseconds += std::chrono::duration<double, std::micro>(clock::now() - before).count();
            ++_frames;
        }
        _lastUpdate = nowUsecs;
    }
}

void WasmScriptEngine::stopModule() {
    scriptInfoMessage("WebAssembly script engine stopping:" + getFilename());
    stopTimers();
    // the signal first, while the module can still answer it; then its own stop
    ++_callDepth;
    emit scriptEnding();
    --_callDepth;
    if (_moduleRunning && _host) {
        QString error;
        ++_callDepth;
        _host->callScriptStop(error);
        --_callDepth;
    }
    _moduleRunning = false;
    _relay->disconnectAll();
    _host.reset();
    {
        std::lock_guard<std::mutex> lock(_instanceMutex);
        _instance.reset();
    }
    _module.reset();
}

void WasmScriptEngine::onModuleFailure(const QString& entry, const QString& error) {
    scriptErrorMessage(QString("%1 failed in %2: %3 (the script is stopped)").arg(getFilename(), entry, error));
    _isStopping = true;
    if (!_isFinished) {
        _isFinished = true;
        emit runningStateChanged();
    }
}

void WasmScriptEngine::stop(bool marshal) {
    _isStopping = true;
    if (marshal) {
        QMetaObject::invokeMethod(this, "stop");
        return;
    }
    if (!_isFinished) {
        _isFinished = true;
        emit runningStateChanged();
    }
    if (QThread::currentThread() != thread() && _callDepth > 0) {
        // the module is inside a call: make it return now rather than at its own pace
        std::lock_guard<std::mutex> lock(_instanceMutex);
        if (_instance) {
            _instance->requestStop();
        }
    }
}

void WasmScriptEngine::waitTillDoneRunning() {
    stop();
    auto workerThread = thread();
    if (workerThread == QThread::currentThread()) {
        qCWarning(scriptengine) << "WasmScriptEngine::waitTillDoneRunning called on the script's own thread:" << getFilename();
        return;
    }
    if (_isThreaded && workerThread && workerThread->isRunning()) {
        workerThread->quit();
        if (!workerThread->wait(MAX_SCRIPT_QUITTING_TIME_MS)) {
            {
                std::lock_guard<std::mutex> lock(_instanceMutex);
                if (_instance) {
                    _instance->requestStop();
                }
            }
            if (!workerThread->wait(MAX_SCRIPT_QUITTING_TIME_MS)) {
                qCWarning(scriptengine) << "WebAssembly script engine did not stop, terminating its thread:" << getFilename();
                workerThread->terminate();
            }
        }
        scriptInfoMessage("Script Engine has stopped:" + getFilename());
    }
}

// --- the serializer: QtScript's conversions for the types that have no QVariant ones yet ----------------------

void WasmScriptEngine::installSerializer() {
    const int scriptValueType = QMetaType::type("QScriptValue");  // registered by QtScript at run time
    _codec->setFallback(
        [this, scriptValueType](const QVariant& value, QCborValue& out) {
            const int type = value.userType();
            if (type == scriptValueType) {
                return false;
            }
            QScriptValue converted = qScriptValueFromValue_helper(this, type, value.constData());
            if (!converted.isValid()) {
                return false;
            }
            QVariant plain = converted.toVariant();
            if (plain.userType() == type) {
                return false;  // no conversion registered: QtScript wrapped the value as it was
            }
            out = _codec->toCbor(plain);
            return true;
        },
        [this, scriptValueType](const QCborValue& value, int targetType, QVariant& out) {
            if (targetType == scriptValueType) {
                return false;
            }
            QVariant plain = _codec->fromCbor(value);
            QScriptValue scriptValue = qScriptValueFromValue_helper(this, plain.userType(), plain.constData());
            if (!scriptValue.isValid()) {
                return false;
            }
            void* buffer = QMetaType::create(targetType);
            if (!buffer) {
                return false;
            }
            bool ok = qscriptvalue_cast_helper(scriptValue, targetType, buffer);
            if (ok) {
                out = QVariant(targetType, buffer);
            }
            QMetaType::destroy(targetType, buffer);
            return ok;
        });
}

// --- timers -------------------------------------------------------------------------------------------------------

int WasmScriptEngine::addTimer(const ScriptCallback& callback, int intervalMS, bool singleShot) {
    QSharedPointer<ScriptEngines> scriptEngines(_scriptEngines);
    if (!scriptEngines || scriptEngines->isStopped()) {
        scriptWarningMessage("Script.setTimeout() while shutting down is ignored... parent script:" + getFilename());
        return 0;
    }
    if (!callback.isValid()) {
        scriptWarningMessage("Script.setTimeout(): no callback given");
        return 0;
    }
    auto timer = new QTimer(this);
    timer->setSingleShot(singleShot);
    if (intervalMS < MIN_TIMEOUT_FOR_COARSE_TIMER) {
        timer->setTimerType(Qt::PreciseTimer);
    }
    const int id = _nextTimer++;
    connect(timer, &QTimer::timeout, this, [this, id] {
        auto found = _timers.find(id);
        if (found == _timers.end()) {
            return;
        }
        Timer entry = found.value();
        if (entry.singleShot) {
            _timers.erase(found);
            entry.timer->deleteLater();
        }
        if (_host && _moduleRunning && !_isFinished) {
            QString error;
            ++_callDepth;
            _host->deliverCallback(entry.callback.id, QVariantList(), error);
            --_callDepth;
        }
    });
    _timers.insert(id, Timer { timer, callback, singleShot });
    timer->start(intervalMS);
    return id;
}

void WasmScriptEngine::removeTimer(int id) {
    auto found = _timers.find(id);
    if (found == _timers.end()) {
        return;
    }
    found.value().timer->stop();
    found.value().timer->deleteLater();
    _timers.erase(found);
}

void WasmScriptEngine::stopTimers() {
    const QList<int> ids = _timers.keys();
    for (int id : ids) {
        removeTimer(id);
    }
}

int WasmScriptEngine::setTimeout(const ScriptCallback& callback, int timeoutMS) {
    return addTimer(callback, timeoutMS, true);
}

int WasmScriptEngine::setInterval(const ScriptCallback& callback, int intervalMS) {
    return addTimer(callback, intervalMS, false);
}

void WasmScriptEngine::clearTimeout(int timer) {
    removeTimer(timer);
}

void WasmScriptEngine::clearInterval(int timer) {
    removeTimer(timer);
}

// --- the Script API the JavaScript shim builds on -------------------------------------------------------------------

QVariantMap WasmScriptEngine::scriptSource() const {
    QVariantMap program;
    program["fileName"] = _fileNameString;
    program["source"] = _scriptContents;
    return program;
}

// The hosts put a few plain objects of numbers on the QtScript global object (DriveKeys, PickType); the carried
// engine has them, and a guest gets them as values. Objects with functions or objects inside are not values.
QVariantMap WasmScriptEngine::scriptGlobalValues() const {
    QVariantMap values;
    QScriptValueIterator it(globalObject());
    while (it.hasNext()) {
        it.next();
        QScriptValue value = it.value();
        if (!value.isObject() || value.isFunction() || value.isQObject() || value.isArray() || value.isError() ||
            value.isRegExp() || value.isDate()) {
            continue;
        }
        QVariantMap members;
        bool plain = true;
        QScriptValueIterator member(value);
        while (member.hasNext()) {
            member.next();
            QScriptValue v = member.value();
            if (v.isNumber() || v.isString() || v.isBool()) {
                members[member.name()] = v.toVariant();
            } else {
                plain = false;
                break;
            }
        }
        if (plain && !members.isEmpty()) {
            values[it.name()] = members;
        }
    }
    return values;
}

QString WasmScriptEngine::resolvePathFrom(const QString& path, const QString& parentUrl) const {
    QUrl url(path);
    // a full URL already, or a Windows path like "c:/"
    if (path.startsWith("/") || url.scheme().length() == 1) {
        url = QUrl::fromLocalFile(path);
    }
    if (!url.isRelative()) {
        return expandScriptUrl(url).toString();
    }
    QUrl parentURL(parentUrl);
    if (parentURL.isRelative()) {
        parentURL = QUrl(_fileNameString);
        if (parentURL.isRelative() && url.path().contains("/")) {
            parentURL = QUrl::fromLocalFile(_fileNameString);
        }
    }
    return expandScriptUrl(parentURL.resolved(url)).toString();
}

QVariantMap WasmScriptEngine::resolveInclude(const QString& file, const QString& parentUrl) const {
    QVariantMap result;
    QString normalized = DependencyManager::get<ResourceManager>()->normalizeURL(file);
    QUrl thisURL;
    bool isStandardLibrary = false;
    if (normalized.startsWith("/~/")) {
        thisURL = expandScriptUrl(QUrl::fromLocalFile(expandScriptPath(normalized)));
        if (!PathUtils::defaultScriptsLocation().isParentOf(thisURL)) {
            result["reason"] = "outside of standard libraries";
            return result;
        }
        isStandardLibrary = true;
    } else {
        thisURL = QUrl(resolvePathFrom(normalized, parentUrl));
    }
    bool disallowOutsideFiles = thisURL.isLocalFile() && !isStandardLibrary && !currentSandboxURL.isLocalFile();
    if (disallowOutsideFiles && !PathUtils::isDescendantOf(thisURL, currentSandboxURL)) {
        result["reason"] = "outside of original entity script " + currentSandboxURL.toString();
        return result;
    }
    result["url"] = thisURL.toString();
    return result;
}

// Script.require.resolve's rules, as the QtScript engine's _requireResolve has them, with the error returned
// in the record (the guest throws it).
QVariantMap WasmScriptEngine::resolveModule(const QString& moduleId, const QString& relativeTo, const QString& parentUrl) const {
    QVariantMap result;
    QString displayId = moduleId.length() > MAX_DEBUG_VALUE_LENGTH ? moduleId.left(MAX_DEBUG_VALUE_LENGTH) + "..." : moduleId;
    auto fail = [&](const QString& details, const QString& type = QString("Error")) {
        result["error"] = QString("Cannot find module '%1' (%2)").arg(displayId, details);
        result["type"] = type;
        return result;
    };
    QUrl url(moduleId);
    int idLength = url.toString().length();
    if (idLength < 1 || idLength > MAX_MODULE_ID_LENGTH) {
        return fail(QString("rejecting invalid module id size (%1 chars [1,%2])").arg(idLength).arg(MAX_MODULE_ID_LENGTH), "RangeError");
    }
    QRegularExpression qualified("^\\w+:|^/|^[.]{1,2}(/|$)");
    QUrl defaultScriptsLoc = PathUtils::defaultScriptsLocation();
    if (!relativeTo.isEmpty()) {
        url = QUrl(relativeTo).resolved(moduleId);
        url = QUrl(resolvePathFrom(url.toString(), parentUrl));
    } else if (qualified.match(moduleId).hasMatch()) {
        url = QUrl(resolvePathFrom(moduleId, parentUrl));
    } else {
        // a "system" module, by bare name
        url = defaultScriptsLoc;
        url.setPath(QString("%1/modules/%2.js").arg(defaultScriptsLoc.path(), moduleId));
        if (!QFileInfo(url.toLocalFile()).isFile()) {
            if (!moduleId.contains("./")) {
                QUrl unanchoredUrl(resolvePathFrom("./" + moduleId, parentUrl));
                if (QFileInfo(unanchoredUrl.toLocalFile()).isFile()) {
                    return fail(QString("relative module ids must be anchored; use './%1' instead").arg(moduleId));
                }
            }
            return fail("system module not found");
        }
    }
    if (url.isRelative()) {
        return fail("could not resolve module id");
    }
    if (url.isLocalFile()) {
        QFileInfo file(url.toLocalFile());
        QUrl canonical = url;
        if (file.exists()) {
            canonical.setPath(file.canonicalFilePath());
        }
        bool disallowOutsideFiles = !defaultScriptsLoc.isParentOf(canonical) && !currentSandboxURL.isLocalFile();
        if (disallowOutsideFiles && !PathUtils::isDescendantOf(canonical, currentSandboxURL)) {
            return fail(QString("path '%1' outside of origin script '%2' '%3'")
                            .arg(PathUtils::stripFilename(url), PathUtils::stripFilename(currentSandboxURL), canonical.toString()));
        }
        if (!file.exists()) {
            return fail("path does not exist: " + url.toLocalFile());
        }
        if (!file.isFile()) {
            return fail("path is not a file: " + url.toLocalFile());
        }
    }
    result["url"] = url.toString();
    return result;
}

// The text of a script through the cache, waited for on this thread (an event loop, as the QtScript engine's
// include and require wait), with a stop seen in time.
QVariantMap WasmScriptEngine::fetchScript(const QString& url, bool forceDownload) {
    QVariantMap result;
    result["url"] = url;
    result["success"] = false;
    if (isStopping()) {
        result["status"] = "Stopped";
        return result;
    }
    auto scriptCache = DependencyManager::get<ScriptCache>();
    if (forceDownload) {
        scriptCache->deleteScript(url);
    }
    BatchLoader* loader = new BatchLoader(QList<QUrl>({ QUrl(url) }));
    connect(loader, &BatchLoader::finished, this, [this, url, &result](const QMap<QUrl, QString>& data, const QMap<QUrl, QString>& status) {
        if (isStopping()) {
            result["status"] = "Stopped";
            return;
        }
        QString contents = data[QUrl(url)];
        result["status"] = status[QUrl(url)];
        result["success"] = !contents.isNull() && ScriptCache::isSuccessStatus(status[QUrl(url)]);
        result["contents"] = contents;
    });
    connect(this, &QObject::destroyed, loader, &QObject::deleteLater);
    const int MAX_RETRIES = 1;
    loader->start(MAX_RETRIES);
    if (!loader->isFinished()) {
        QEventLoop loop;
        QTimer monitor;
        connect(loader, &BatchLoader::finished, &loop, &QEventLoop::quit);
        connect(&monitor, &QTimer::timeout, this, [this, &loop] {
            if (isStopping()) {
                loop.exit(-1);
            }
        });
        monitor.start(500);
        loop.exec();
    }
    loader->deleteLater();
    return result;
}

bool WasmScriptEngine::dumpApi(const QString& path, const QVariantMap& globals) {
    QJsonObject root;
    root["context"] = getContext();
    root["type"] = getTypeAsString();
    root["fileName"] = _fileNameString;
    root["engine"] = _runsJavaScript ? "JavaScript on WebAssembly" : "WebAssembly";
    root["globals"] = QJsonObject::fromVariantMap(globals);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        scriptWarningMessage("Script.dumpApi: cannot write " + path);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

// --- callbacks called from the host side -----------------------------------------------------------------------------

bool WasmScriptEngine::invokeCallback(int32_t callback, const QVariantList& arguments, QVariant& result, QString& error) {
    if (!_host || !_moduleRunning || _isFinished) {
        error = "the script is not running";
        return false;
    }
    ++_callDepth;
    bool ok = _host->callCallback(callback, arguments, result, error);
    --_callDepth;
    return ok;
}

void WasmScriptEngine::postCallback(int callback, const QVariantList& arguments) {
    if (_host && _moduleRunning && !_isFinished) {
        QString error;
        ++_callDepth;
        _host->deliverCallback(int32_t(callback), arguments, error);
        --_callDepth;
    }
}

// --- statistics -----------------------------------------------------------------------------------------------------

QVariantMap WasmScriptEngine::statistics() const {
    QVariantMap stats;
    stats["frames"] = qulonglong(_frames);
    stats["updateMeanMicroseconds"] = _frames ? _updateMicroseconds / double(_frames) : 0.0;
    stats["timers"] = _timers.size();
    stats["objects"] = _objects->size();
    stats["connections"] = _relay->connectionCount();
    if (_host) {
        const auto& host = _host->statistics();
        stats["calls"] = qulonglong(host.calls);
        stats["gets"] = qulonglong(host.gets);
        stats["sets"] = qulonglong(host.sets);
        stats["signalsDelivered"] = qulonglong(host.signalsDelivered);
        stats["callbacksDelivered"] = qulonglong(host.callbacksDelivered);
        stats["bytesToGuest"] = qulonglong(host.bytesToGuest);
        stats["bytesFromGuest"] = qulonglong(host.bytesFromGuest);
    }
    if (_instance) {
        stats["memoryBytes"] = qulonglong(_instance->memorySize());
    }
    return stats;
}
