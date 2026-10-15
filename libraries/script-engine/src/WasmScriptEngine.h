//
//  WasmScriptEngine.h
//  libraries/script-engine/src
//
//  The script engine that runs a WebAssembly module. It derives from ScriptEngine for the transition: the
//  application, the agent and the entity script server register their objects on a ScriptEnginePointer with
//  registerGlobalObject, and this engine records them in its object table instead of a QScriptValue. The
//  module reaches them through the host interface (libraries/wasm-scripting) by reflection. The QScriptEngine
//  this class carries through its base is never run as a script engine; it serves as the serializer for the
//  types that only have QtScript conversions, until those get QVariant ones.
//
//  Two kinds of script run here: a module (a .wasm file, run as it is) and a JavaScript file, which runs
//  inside the JavaScript guest (tools/wasm-js, darkride-js.wasm: QuickJS-NG plus a shim that gives the
//  script the objects it had on QtScript). Which engine a .js file loads on is the setting
//  runJavaScriptOnWebAssembly (Developer > Scripting in the client); .wasm files always load here.
//
//  Lifecycle, as the QtScript engine's: loadURL fetches the script; scriptLoaded makes ScriptEngines run the
//  initializers (the registrations) and runInThread; run() on the engine's thread registers the standard
//  globals, instantiates the module, calls its script_start and loops at SCRIPT_FPS, emitting update and
//  processing events (signals, timers) until stop(); then scriptEnding, script_stop, finished.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_WasmScriptEngine_h
#define hifi_WasmScriptEngine_h

#include <atomic>
#include <memory>
#include <mutex>

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QVariantMap>

#include <ValueCodec.h>
#include <WasmRuntime.h>

#include "ScriptEngine.h"

class HostInterface;
class HttpRequestFactory;
class ObjectTable;
class QTimer;
class SignalRelay;
class WebSocketFactory;

class WasmScriptEngine : public ScriptEngine {
    Q_OBJECT
public:
    /// A .wasm name runs as a module; any other name is a JavaScript file for the guest. The contents, when
    /// given, are the script (an agent hands them over); otherwise loadURL fetches them.
    WasmScriptEngine(Context context, const QString& scriptContents, const QString& fileNameString);
    ~WasmScriptEngine() override;

    /// True for a name or URL ending in .wasm (what the factory dispatches on).
    static bool isWasmScript(const QString& fileNameOrUrl);
    /// True for a name or URL ending in .js.
    static bool isJavaScriptFile(const QString& fileNameOrUrl);
    /// The runtime every module of this process is compiled by.
    static std::shared_ptr<WasmRuntime> runtime();

    /// The setting: JavaScript client scripts load on this engine instead of the QtScript one.
    static bool runJavaScriptOnWebAssembly();
    static void setRunJavaScriptOnWebAssembly(bool on);
    /// Where the JavaScript guest module is: $DARKRIDE_JS_WASM, else resources/wasm/darkride-js.wasm beside
    /// the executable (Contents/Resources/wasm on macOS).
    static QString javascriptGuestPath();

    bool runsJavaScript() const { return _runsJavaScript; }

    /// The module's bytes, when they do not come through loadURL (tests, an agent).
    void setModuleBytes(const QByteArray& bytes) { _moduleBytes = bytes; }
    void setLimits(const WasmInstance::Limits& limits) { _limits = limits; }
    const WasmInstance::Limits& limits() const { return _limits; }

    // The ScriptEngine hooks the hosts call.
    void registerGlobalObject(const QString& name, QObject* object) override;
    void registerGetterSetter(const QString& name, QScriptEngine::FunctionSignature getter,
                              QScriptEngine::FunctionSignature setter, const QString& parent = QString("")) override;
    void registerFunction(const QString& name, QScriptEngine::FunctionSignature fun, int numArguments = -1) override;
    void registerFunction(const QString& parent, const QString& name, QScriptEngine::FunctionSignature fun,
                          int numArguments = -1) override;
    void registerValue(const QString& valueName, QScriptValue value) override;
    void loadURL(const QUrl& scriptURL, bool reload) override;
    bool hasValidScriptSuffix(const QString& scriptFileName) override;
    void run() override;
    void stop(bool marshal = false) override;
    void waitTillDoneRunning() override;

    // The Script API a module uses for timers: a guest function as the callback, a timer id back.
    Q_INVOKABLE int setTimeout(const ScriptCallback& callback, int timeoutMS);
    Q_INVOKABLE int setInterval(const ScriptCallback& callback, int intervalMS);
    Q_INVOKABLE void clearTimeout(int timer);
    Q_INVOKABLE void clearInterval(int timer);
    using ScriptEngine::setTimeout;
    using ScriptEngine::setInterval;
    using ScriptEngine::clearTimeout;
    using ScriptEngine::clearInterval;

    // The Script API the JavaScript shim builds its include, require and resolvePath on (a native module may
    // use them too). Paths are resolved against the parent URL the caller tracks (the file whose code runs).
    /// The script itself: {"fileName", "source"}.
    Q_INVOKABLE QVariantMap scriptSource() const;
    /// The plain-value objects the hosts put on the global object (DriveKeys, PickType): name to map.
    Q_INVOKABLE QVariantMap scriptGlobalValues() const;
    /// Script.resolvePath with the parent given.
    Q_INVOKABLE QString resolvePathFrom(const QString& path, const QString& parentUrl) const;
    /// Script.include's rules: {"url"} or {"reason"} when the file is not to be included.
    Q_INVOKABLE QVariantMap resolveInclude(const QString& file, const QString& parentUrl) const;
    /// Script.require.resolve's rules: {"url"} or {"error", "type"}.
    Q_INVOKABLE QVariantMap resolveModule(const QString& moduleId, const QString& relativeTo, const QString& parentUrl) const;
    /// A script's text through the script cache, synchronously: {"url", "status", "success", "contents"}.
    Q_INVOKABLE QVariantMap fetchScript(const QString& url, bool forceDownload);
    /// Script.dumpApi for this engine: the globals record the guest composed, written under the usual root.
    Q_INVOKABLE bool dumpApi(const QString& path, const QVariantMap& globals);
    using ScriptEngine::dumpApi;

    /// What the engine has done: host calls, bytes crossed, frames, the module's memory.
    Q_INVOKABLE QVariantMap statistics() const;

    /// A guest function called with arguments, on this thread, for its result (what a ScriptCallback's call
    /// comes to); postCallback queues one without a result, from any thread.
    bool invokeCallback(int32_t callback, const QVariantList& arguments, QVariant& result, QString& error);
    Q_INVOKABLE void postCallback(int callback, const QVariantList& arguments);

private:
    void adoptScriptGlobals();
    bool startModule();
    void stopModule();
    void loop();
    void onModuleFailure(const QString& entry, const QString& error);
    void installSerializer();
    int addTimer(const ScriptCallback& callback, int intervalMS, bool singleShot);
    void removeTimer(int id);
    void stopTimers();
    static std::shared_ptr<WasmModule> javascriptGuestModule(QString& error);

    bool _runsJavaScript { false };
    QByteArray _moduleBytes;
    WasmInstance::Limits _limits;
    std::unique_ptr<ObjectTable> _objects;
    std::unique_ptr<ValueCodec> _codec;
    std::shared_ptr<CallbackTarget> _callbackTarget;
    SignalRelay* _relay { nullptr };  // a child, so it moves to the engine's thread with it
    HttpRequestFactory* _http { nullptr };
    WebSocketFactory* _sockets { nullptr };
    std::shared_ptr<WasmModule> _module;
    std::unique_ptr<WasmInstance> _instance;
    std::unique_ptr<HostInterface> _host;
    std::mutex _instanceMutex;  // guards _instance against requestStop from another thread
    std::atomic<bool> _moduleRunning { false };
    std::atomic<int> _callDepth { 0 };

    struct Timer {
        QTimer* timer;
        ScriptCallback callback;
        bool singleShot;
    };
    QHash<int, Timer> _timers;
    int _nextTimer { 1 };

    uint64_t _frames { 0 };
    double _updateMicroseconds { 0.0 };
};

#endif // hifi_WasmScriptEngine_h
