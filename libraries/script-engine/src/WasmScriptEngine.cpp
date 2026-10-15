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
#include <QtCore/QFileInfo>
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
#include <SharedUtil.h>
#include <shared/LocalFileAccessGate.h>

#include <HostInterface.h>
#include <ObjectTable.h>
#include <SignalRelay.h>

#include "ScriptCache.h"
#include "ScriptEngineLogging.h"
#include "ScriptEngines.h"

namespace {

const QString WASM_SUFFIX = QStringLiteral("wasm");
const int MIN_TIMEOUT_FOR_COARSE_TIMER = 200;  // below this a Qt timer is made precise, as the QtScript engine does
const int MAX_SCRIPT_QUITTING_TIME_MS = 500;

} // namespace

WasmScriptEngine::WasmScriptEngine(Context context, const QString& fileNameString) :
    ScriptEngine(context, NO_SCRIPT, fileNameString),
    _objects(new ObjectTable()),
    _codec(new ValueCodec(_objects.get())),
    _relay(new SignalRelay(this)) {
    if (context == ENTITY_CLIENT_SCRIPT || context == ENTITY_SERVER_SCRIPT) {
        // an entities engine hosts every entity script of the world: more room
        _limits.memoryBytes = int64_t(1024) * 1024 * 1024;
    }
}

WasmScriptEngine::~WasmScriptEngine() {
    stopTimers();
    _host.reset();
    std::lock_guard<std::mutex> lock(_instanceMutex);
    _instance.reset();
}

bool WasmScriptEngine::isWasmScript(const QString& fileNameOrUrl) {
    QUrl url(fileNameOrUrl);
    QString path = url.isValid() && !url.scheme().isEmpty() ? url.path() : fileNameOrUrl;
    return QFileInfo(path).suffix().toLower() == WASM_SUFFIX;
}

std::shared_ptr<WasmRuntime> WasmScriptEngine::runtime() {
    static std::shared_ptr<WasmRuntime> shared = std::make_shared<WasmRuntime>();
    return shared;
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
    Q_UNUSED(fun);
    Q_UNUSED(numArguments);
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
    scriptInfoMessage("WebAssembly script engine starting:" + getFilename());

    if (!_isInitialized) {
        // the QtScript engine's own initialization: it registers the standard globals (through this class's
        // registerGlobalObject) and the QtScript conversions on the carried engine, which the serializer uses
        init();
    }
    adoptScriptGlobals();
    installSerializer();

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
    if (_moduleBytes.isEmpty()) {
        scriptErrorMessage("No module to run: " + _fileNameString);
        return false;
    }
    QString error;
    _module = runtime()->compile(_moduleBytes, error);
    if (!_module) {
        scriptErrorMessage("Cannot compile " + getFilename() + ": " + error);
        return false;
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
    if (_moduleRunning && _host) {
        QString error;
        ++_callDepth;
        _host->callScriptStop(error);
        --_callDepth;
    }
    ++_callDepth;
    emit scriptEnding();
    --_callDepth;
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
