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
//  Lifecycle, as the QtScript engine's: loadURL fetches the module's bytes; scriptLoaded makes ScriptEngines
//  run the initializers (the registrations) and runInThread; run() on the engine's thread registers the
//  standard globals, instantiates the module, calls its script_start and loops at SCRIPT_FPS, emitting update
//  and processing events (signals, timers) until stop(); then script_stop, scriptEnding, finished.
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
class ObjectTable;
class QTimer;
class SignalRelay;

class WasmScriptEngine : public ScriptEngine {
    Q_OBJECT
public:
    WasmScriptEngine(Context context, const QString& fileNameString);
    ~WasmScriptEngine() override;

    /// True for a name or URL ending in .wasm (what the factory dispatches on).
    static bool isWasmScript(const QString& fileNameOrUrl);
    /// The runtime every module of this process is compiled by.
    static std::shared_ptr<WasmRuntime> runtime();

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

    /// What the engine has done: host calls, bytes crossed, frames, the module's memory.
    Q_INVOKABLE QVariantMap statistics() const;

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

    QByteArray _moduleBytes;
    WasmInstance::Limits _limits;
    std::unique_ptr<ObjectTable> _objects;
    std::unique_ptr<ValueCodec> _codec;
    SignalRelay* _relay { nullptr };  // a child, so it moves to the engine's thread with it
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
