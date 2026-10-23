//
//  ApplicationShutdown.cpp
//  interface/src
//
//  Application's shutdown, in its order: onAboutToQuit (the avatar's entity data saved, the input and display
//  plug-ins deactivated, the active display plug-in's name kept), cleanupBeforeQuit (the domain told, the scripts
//  stopped, the script engines, QML and audio destroyed, the settings saved) and the destructor (the avatars out of
//  physics, the graphics engine, the caches, the node list, the window). The members as they stood in
//  Application.cpp; a home of their own until an owner takes them (ARCHITECTURE.md §5.4 step 4).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "Application.h"
#include "ApplicationServices.h"
#include "ImportExtensions.h"
#include "ApplicationSettingNames.h"
#include "LambdaEvent.h"
#include "ClientOptions.h"
#include "DeadlockWatchdog.h"
#include <chrono>
#include <thread>
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gl/Config.h>
#include <QtCore/QResource>
#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QCommandLineParser>
#include <QtCore/QMimeData>
#include <QtCore/QThreadPool>
#include <QtCore/QFileSelector>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGui/QClipboard>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtGui/QDesktopServices>
#include <QtNetwork/QLocalServer>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickWindow>
#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QMessageBox>
#include <QtMultimedia/QMediaPlayer>
#include <QFontDatabase>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <gl/QOpenGLContextWrapper.h>
#include <gl/GLHelpers.h>
#include <gl/GLWindow.h>
#include <gl/GLHelpers.h>
#include <shared/FileUtils.h>
#include <shared/QtHelpers.h>
#include <shared/PlatformHelper.h>
#include <shared/GlobalAppProperties.h>
#include <GeometryUtil.h>
#include <StatTracker.h>
#include <Trace.h>
#include <StartupProfiler.h>
#include <ResourceScriptingInterface.h>
#include <AccountManager.h>
#include <AddressManager.h>
#include <AnimDebugDraw.h>
#include <BuildInfo.h>
#include <AnimationCacheScriptingInterface.h>
#include <AssetClient.h>
#include <AssetUpload.h>
#include <AudioInjectorManager.h>
#include <AvatarBookmarks.h>
#include <CrashHelpers.h>
#include <CursorManager.h>
#include <VirtualPadManager.h>
#include <DebugDraw.h>
#include <DeferredLightingEffect.h>
#include <EntityNodeData.h>
#include <EntityScriptClient.h>
#include <EntityScriptServerLogClient.h>
#include <EntityScriptingInterface.h>
#include <ErrorDialog.h>
#include <FileScriptingInterface.h>
#include <Finally.h>
#include <FingerprintUtils.h>
#include <FramebufferCache.h>
#include <gpu/Batch.h>
#include <gpu/Context.h>
#include <input-plugins/InputPlugin.h>
#include <controllers/UserInputMapper.h>
#include <controllers/InputRecorder.h>
#include <controllers/ScriptingInterface.h>
#include <controllers/StateController.h>
#include <LogHandler.h>
#include "LocationBookmarks.h"
#include <LocationScriptingInterface.h>
#include <MainWindow.h>
#include <MappingRequest.h>
#include <MessagesClient.h>
#include <hfm/ModelFormatRegistry.h>
#include <model-networking/ModelCacheScriptingInterface.h>
#include <procedural/MaterialCacheScriptingInterface.h>
#include <material-networking/TextureCacheScriptingInterface.h>
#include <ModelEntityItem.h>
#include <NetworkAccessManager.h>
#include <NetworkingConstants.h>
#include <ObjectMotionState.h>
#include <OctalCode.h>
#include <OctreeSceneStats.h>
#include <OffscreenUi.h>
#include <gl/OffscreenGLCanvas.h>
#include <ui/OffscreenQmlSurfaceCache.h>
#include <PathUtils.h>
#include <PerfStat.h>
#include <PhysicsEngine.h>
#include <PhysicsHelpers.h>
#include <plugins/CodecPlugin.h>
#include <plugins/PluginManager.h>
#include <plugins/PluginUtils.h>
#include <plugins/InputConfiguration.h>
#include <RecordingScriptingInterface.h>
#include <render/EngineStats.h>
#include <SecondaryCamera.h>
#include <ResourceCache.h>
#include <ResourceRequest.h>
#include <SceneScriptingInterface.h>
#include <ScriptEngines.h>
#include <ScriptCache.h>
#include <ShapeEntityItem.h>
#include <SoundCacheScriptingInterface.h>
#include <ui/TabletScriptingInterface.h>
#include <ui/ToolbarScriptingInterface.h>
#include <ui/types/ContextAwareProfile.h>
#include <FileDialogHelper.h>
#include <Tooltip.h>
#include <udt/PacketHeaders.h>
#include <UsersScriptingInterface.h>
#include <recording/ClipCache.h>
#include <recording/Deck.h>
#include <recording/Recorder.h>
#include <shared/StringHelpers.h>
#include <QmlWebWindowClass.h>
#include <QmlFragmentClass.h>
#include <Preferences.h>
#include <display-plugins/CompositorHelper.h>
#include <display-plugins/hmd/HmdDisplayPlugin.h>
#include <display-plugins/RefreshRateController.h>
#include <avatars-renderer/ScriptAvatar.h>
#include <RenderableEntityItem.h>
#include <RenderableTextEntityItem.h>
#include <RenderableWebEntityItem.h>
#include <StencilMaskPass.h>
#include <procedural/ProceduralMaterialCache.h>
#include "recording/ClipCache.h"
#include "AudioClient.h"
#include "audio/AudioScope.h"
#include "avatar/AvatarManager.h"
#include "avatar/MyHead.h"
#include "avatar/AvatarImporter.h"
#include "avatar/MyCharacterController.h"
#include "CrashRecoveryHandler.h"
#include "CrashHandler.h"
#include "DiscoverabilityManager.h"
#include "GLCanvas.h"
#include "InterfaceDynamicFactory.h"
#include "InterfaceLogging.h"
#include "LODManager.h"
#include "ModelPackager.h"
#include "scripting/Audio.h"
#include "scripting/TestScriptingInterface.h"
#include "scripting/PlatformInfoScriptingInterface.h"
#include "scripting/AssetMappingsScriptingInterface.h"
#include "scripting/ClipboardScriptingInterface.h"
#include "scripting/DesktopScriptingInterface.h"
#include "scripting/AccountServicesScriptingInterface.h"
#include "scripting/HMDScriptingInterface.h"
#include "scripting/MenuScriptingInterface.h"
#include "graphics-scripting/GraphicsScriptingInterface.h"
#include "scripting/SettingsScriptingInterface.h"
#include "scripting/WindowScriptingInterface.h"
#include "scripting/ControllerScriptingInterface.h"
#include "scripting/RatesScriptingInterface.h"
#include "scripting/SelectionScriptingInterface.h"
#include "editor/Editor.h"
#include "editor/ImpostorBaker.h"
#include "editor/ui/ViewportContainer.h"
#include "scripting/TTSScriptingInterface.h"
#include "scripting/KeyboardScriptingInterface.h"
#include "scripting/PerformanceScriptingInterface.h"
#include "scripting/RenderScriptingInterface.h"
#include "scripting/ChatScriptingInterface.h"
#include "scripting/DiskCacheScriptingInterface.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include "SpeechRecognizer.h"
#endif
#include "ui/ResourceImageItem.h"
#include "ui/AddressBarDialog.h"
#include "ui/AvatarInputs.h"
#include "ui/DialogsManager.h"
#include "ui/LoginDialog.h"
#include "ui/Snapshot.h"
#include "ui/SnapshotAnimated.h"
#include "ui/StandAloneJSConsole.h"
#include "ui/Stats.h"
#include "ui/AnimStats.h"
#include "ui/DomainConnectionModel.h"
#include "ui/Keyboard.h"
#include "ui/InteractiveWindow.h"
#include "Util.h"
#include "InterfaceParentFinder.h"
#include "ui/OctreeStatsProvider.h"
#include "avatar/GrabManager.h"
#include <GPUIdent.h>
#include <gl/GLHelpers.h>
#include <EntityScriptClient.h>
#include <ModelScriptingInterface.h>
#include <PickManager.h>
#include <PointerManager.h>
#include <raypick/RayPickScriptingInterface.h>
#include <raypick/LaserPointerScriptingInterface.h>
#include <raypick/PickScriptingInterface.h>
#include <raypick/PointerScriptingInterface.h>
#include <raypick/RayPick.h>
#include <raypick/MouseTransformNode.h>
#include <FadeEffect.h>
#include "ResourceRequestObserver.h"
#include "webbrowser/WebBrowserSuggestionsEngine.h"
#include <DesktopPreviewProvider.h>
#include "AboutUtil.h"
#if defined(Q_OS_WIN)
#include <VersionHelpers.h>
#include <Windows.h>
#endif
#if defined(Q_OS_ANDROID)
#include <android/log.h>
#include "AndroidHelper.h"
#endif
#if defined(Q_OS_MACOS)
#include "AppNapDisabler.h"
#endif
#include "graphics/RenderEventHandler.h"

using namespace std;

QSharedPointer<OffscreenUi> getOffscreenUI();      // Application.cpp's
extern Setting::Handle<bool> loginDialogPoppedUp;  // Application.cpp's
extern QTimer locationUpdateTimer;                 // Application.cpp's; the constructor starts it
extern QUuid _keyboardFocusHighlightID;            // Application.cpp's: the keyboard-focus highlight entity

void Application::onAboutToQuit() {
    setCrashAnnotation("shutdown", "1");

    // quickly save AvatarEntityData before the EntityTree is dismantled
    getMyAvatar()->saveAvatarEntityDataToSettings();

    emit beforeAboutToQuit();

    if (getLoginDialogPoppedUp() && _firstRun.get()) {
        _firstRun.set(false);
    }

    foreach(auto inputPlugin, PluginManager::getInstance()->getInputPlugins()) {
        if (inputPlugin->isActive()) {
            inputPlugin->deactivate();
        }
    }

    // The active display plugin needs to be loaded before the menu system is active,
    // so its persisted explicitly here
    Setting::Handle<QString>{ ACTIVE_DISPLAY_PLUGIN_SETTING_NAME }.set(getActiveDisplayPlugin()->getName());

    loginDialogPoppedUp.set(false);

    getActiveDisplayPlugin()->deactivate();
    if (_autoSwitchDisplayModeSupportedHMDPlugin
        && _autoSwitchDisplayModeSupportedHMDPlugin->isSessionActive()) {
        _autoSwitchDisplayModeSupportedHMDPlugin->endSession();
    }
    // Hide Running Scripts dialog so that it gets destroyed in an orderly manner; prevents warnings at shutdown.
    getOffscreenUI()->hide("RunningScripts");

    _aboutToQuit = true;

    cleanupBeforeQuit();

    if (_crashOnShutdown) {
        // triggered by crash menu
        crash::nullDeref();
    }

    getRefreshRateManager().setRefreshRateRegime(RefreshRateManager::RefreshRateRegime::SHUTDOWN);
}

void Application::cleanupBeforeQuit() {
    // add a logline indicating if QTWEBENGINE_REMOTE_DEBUGGING is set or not
    QString webengineRemoteDebugging = QProcessEnvironment::systemEnvironment().value("QTWEBENGINE_REMOTE_DEBUGGING", "false");
    qCDebug(interfaceapp) << "QTWEBENGINE_REMOTE_DEBUGGING =" << webengineRemoteDebugging;

    DependencyManager::prepareToExit();

    if (tracing::enabled()) {
        auto tracer = DependencyManager::get<tracing::Tracer>();
        tracer->stopTracing();
        auto outputFile = property(hifi::properties::TRACING).toString();
        tracer->serialize(outputFile);
    }

    // Stop third party processes so that they're not left running in the event of a subsequent shutdown crash.
    AnimDebugDraw::getInstance().shutdown();

    // FIXME: once we move to shared pointer for the INputDevice we shoud remove this naked delete:
    _applicationStateDevice.reset();

    {
        if (_keyboardFocusHighlightID != UNKNOWN_ENTITY_ID) {
            DependencyManager::get<EntityScriptingInterface>()->deleteEntity(_keyboardFocusHighlightID);
            _keyboardFocusHighlightID = UNKNOWN_ENTITY_ID;
        }
    }

    {
        auto nodeList = DependencyManager::get<NodeList>();

        // send the domain a disconnect packet, force stoppage of domain-server check-ins
        nodeList->getDomainHandler().disconnect("Quitting");
        nodeList->setIsShuttingDown(true);

        // tell the packet receiver we're shutting down, so it can drop packets
        nodeList->getPacketReceiver().setShouldDropPackets(true);
    }

    getEntities()->shutdown(); // tell the entities system we're shutting down, so it will stop running scripts

    // Clear any queued processing (I/O, FBX/OBJ/Texture parsing)
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone();

    DependencyManager::destroy<RecordingScriptingInterface>();

    // FIXME: Something is still holding on to the ScriptEnginePointers contained in ScriptEngines, and they hold backpointers to ScriptEngines,
    // so this doesn't shut down properly

    // CPM Commenting this out below makes much faster shutdown and takes care of the 30s shutdown delay
    // It does come at the expense of scripts not being able to shut down promptly.  TO DO
    // Look at the ScriptEngine shutdownScripting() function itself and see if there's a way to speed it up

    DependencyManager::get<ScriptEngines>()->shutdownScripting(); // stop all currently running global scripts

    // These classes hold ScriptEnginePointers, so they must be destroyed before ScriptEngines
    // Must be done after shutdownScripting in case any scripts try to access these things
    {
        DependencyManager::destroy<StandAloneJSConsole>();
        EntityTreePointer tree = getEntities()->getTree();
        tree->setSimulation(nullptr);
        DependencyManager::destroy<EntityTreeRenderer>();
    }
    DependencyManager::destroy<ScriptEngines>();

    bool keepMeLoggedIn = Setting::Handle<bool>(KEEP_ME_LOGGED_IN_SETTING_NAME, true).get();
    if (!keepMeLoggedIn) {
        DependencyManager::get<AccountManager>()->removeAccountFromFile();
    }

    _displayPlugin.reset();
    PluginManager::getInstance()->shutdown();

    // Cleanup all overlays after the scripts, as scripts might add more
    _overlays.cleanupAllOverlays();

    // first stop all timers directly or by invokeMethod
    // depending on what thread they run in
    locationUpdateTimer.stop();

    // Wait for the settings thread to shut down, and save the settings one last time when it's safe
    if (_settingsGuard.wait()) {
        // save state
        saveSettings();
    }

    _window->saveGeometry();

    // stop QML
    DependencyManager::destroy<TabletScriptingInterface>();
    DependencyManager::destroy<ToolbarScriptingInterface>();
    DependencyManager::destroy<OffscreenUi>();

    DependencyManager::destroy<OffscreenQmlSurfaceCache>();

    // destroy Audio so it and its threads have a chance to go down safely
    // this must happen after QML, as there are unexplained audio crashes originating in qtwebengine
    AudioInjector::setLocalAudioInterface(nullptr);
    DependencyManager::destroy<AudioClient>();
    DependencyManager::destroy<AudioScriptingInterface>();

    // The PointerManager must be destroyed before the PickManager because when a Pointer is deleted,
    // it accesses the PickManager to delete its associated Pick
    DependencyManager::destroy<PointerManager>();
    DependencyManager::destroy<PickManager>();
    DependencyManager::destroy<KeyboardScriptingInterface>();
    DependencyManager::destroy<Keyboard>();

    qCDebug(interfaceapp) << "Application::cleanupBeforeQuit() complete";
}

Application::~Application() {
    // remove avatars from physics engine
    auto avatarManager = DependencyManager::get<AvatarManager>();
    avatarManager->clearOtherAvatars();
    auto myCharacterController = getMyAvatar()->getCharacterController();
    myCharacterController->clearDetailedMotionStates();

    PhysicsEngine::Transaction transaction;
    avatarManager->buildPhysicsTransaction(transaction);
    _physicsEngine->processTransaction(transaction);
    avatarManager->handleProcessedPhysicsTransaction(transaction);
    avatarManager->deleteAllAvatars();

    _physicsEngine->setCharacterController(nullptr);

    // the _shapeManager should have zero references
    _shapeManager.collectGarbage();
    assert(_shapeManager.getNumShapes() == 0);

    // shutdown graphics engine
    _graphicsEngine.shutdown();

    _gameWorkload.shutdown();

    DependencyManager::destroy<Preferences>();
    PlatformHelper::shutdown();

    _entityClipboard->eraseAllOctreeElements();
    _entityClipboard.reset();

    _octreeProcessor.terminate();
    _entityEditSender.terminate();

    DependencyManager::destroy<PluginManager>();

    DependencyManager::destroy<CompositorHelper>(); // must be destroyed before the FramebufferCache

    DependencyManager::destroy<SoundCacheScriptingInterface>();

    DependencyManager::destroy<AudioInjectorManager>();
    DependencyManager::destroy<AvatarManager>();
    DependencyManager::destroy<AnimationCacheScriptingInterface>();
    DependencyManager::destroy<AnimationCache>();
    DependencyManager::destroy<FramebufferCache>();
    DependencyManager::destroy<MaterialCacheScriptingInterface>();
    DependencyManager::destroy<MaterialCache>();
    DependencyManager::destroy<TextureCacheScriptingInterface>();
    DependencyManager::destroy<TextureCache>();
    DependencyManager::destroy<ModelCacheScriptingInterface>();
    DependencyManager::destroy<ModelCache>();
    DependencyManager::destroy<ModelFormatRegistry>();
    DependencyManager::destroy<ScriptCache>();
    DependencyManager::destroy<SoundCacheScriptingInterface>();
    DependencyManager::destroy<SoundCache>();
    DependencyManager::destroy<OctreeStatsProvider>();
    DependencyManager::destroy<GeometryCache>();

    DependencyManager::get<ResourceManager>()->cleanup();

    // remove the NodeList from the DependencyManager
    DependencyManager::destroy<NodeList>();
    // The window takes ownership of the menu, so this has the side effect of destroying it.
    _window->setMenuBar(nullptr);

    _window->deleteLater();

    // Can't log to file past this point, FileLogger about to be deleted
    qInstallMessageHandler(LogHandler::verboseMessageHandler);

#ifdef Q_OS_MACOS
    // 10/16/2019 - Disabling this call. This causes known crashes (A), and it is not
    // fully understood whether it might cause other unknown crashes (B).
    //
    // (A) Although we try to shutdown the ScriptEngine threads in onAboutToQuit, there is
    // currently no guarantee that they have stopped. Waiting on them to stop has so far appeared to
    // never return on Mac, causing the application to hang on shutdown. Because ScriptEngines
    // may still be running, they may end up receiving events that are triggered from this processEvents call,
    // and then try to access resources that are no longer available at this point in time.
    // If the ScriptEngine threads were fully destroyed before getting here, this would
    // not be an issue.
    //
    // (B) It seems likely that a bunch of potential event handlers are dependent on Application
    // and other common dependencies to be available and not destroyed or in the middle of being
    // destroyed.
#endif
}
