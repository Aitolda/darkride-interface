//
//  Application.cpp
//  interface/src
//
//  Created by Andrzej Kapolka on 5/10/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Modified by Caitlyn Meeks on 12/24/19
//  Copyright 2019 Tivoli Cloud VR
//


#include "Application.h"
#include "ApplicationServices.h"
#include "ImportExtensions.h"
#include "ApplicationConstants.h"
#include "KeyboardFocusHighlight.h"
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

// On Windows PC, NVidia Optimus laptop, we want to enable NVIDIA GPU
// FIXME seems to be broken.
extern "C" {
 _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
#endif

#if defined(Q_OS_ANDROID)
#include <android/log.h>
#include "AndroidHelper.h"
#endif

#if defined(Q_OS_MACOS)
// On Mac OS, disable App Nap to prevent audio glitches while running in the background
#include "AppNapDisabler.h"
static AppNapDisabler appNapDisabler;   // disabled, while in scope
#endif

#include "graphics/RenderEventHandler.h"

Q_LOGGING_CATEGORY(trace_app_input_mouse, "trace.app.input.mouse")

using namespace std;


// The window title's client label (clientLabel, labelledTitle) is in ApplicationDisplay.cpp, with updateWindowTitle.


// For processing on QThreadPool, we target a number of threads after reserving some
// based on how many are being consumed by the application and the display plugin.  However,
// we will never drop below the 'min' value
const int MIN_PROCESSING_THREAD_POOL_SIZE = 2;   // declared in ApplicationConstants.h

// The import extensions (ImportExtensions.h declares them) and the table of importers by extension are in ApplicationImport.cpp.


const QString KEEP_ME_LOGGED_IN_SETTING_NAME = "keepMeLoggedIn";   // declared in ApplicationConstants.h

#if defined(Q_OS_ANDROID)
#endif

// MyNativeEventFilter (the Windows messages that identify and raise the running instance) is in ApplicationStartup.cpp.


// messageHandler (Qt's messages into the log) is in ApplicationStartup.cpp.



// setupEssentials (what runs before the members exist: the version switches, the crash check, the resources, the
// plug-ins, the registrations) is in ApplicationStartup.cpp, with the statically provided plug-in lists it needs.

// The keyboard-focus highlight's id and expansion factor (KeyboardFocusHighlight.h) are defined in ApplicationKeyboardFocus.cpp.


// The settings' defaults are in ApplicationStartup.cpp, beside the constructor that reads them.

QSharedPointer<OffscreenUi> getOffscreenUI() {
    return DependencyManager::get<OffscreenUi>();
}

// The constructor is in ApplicationStartup.cpp.

// setFailedToConnectToEntityServer is in ApplicationDomain.cpp.

void Application::updateVerboseLogging() {
    auto menu = Menu::getInstance();
    if (!menu) {
        return;
    }
    bool enable = menu->isOptionChecked(MenuOption::VerboseLogging);

    QString rules =
        "hifi.*.info=%1\n"
        "hifi.audio-stream.debug=false\n"
        "hifi.audio-stream.info=false";
    rules = rules.arg(enable ? "true" : "false");
    QLoggingCategory::setFilterRules(rules);
}

// domainConnectionRefused (the popup when a domain turns the client away) is in ApplicationDomain.cpp.

QString Application::getUserAgent() {
    if (QThread::currentThread() != thread()) {
        QString userAgent;

        BLOCKING_INVOKE_METHOD(this, "getUserAgent", Q_RETURN_ARG(QString, userAgent));

        return userAgent;
    }

    QString userAgent = (
        "Darkride/" + 
        (BuildInfo::BUILD_TYPE == BuildInfo::BuildType::Stable ? BuildInfo::VERSION : "dev")
    );

    auto formatPluginName = [](QString name) -> QString { return name.trimmed().replace(" ", "-");  };

    // For each plugin, add to userAgent
    auto displayPlugins = PluginManager::getInstance()->getDisplayPlugins();
    for (auto& dp : displayPlugins) {
        if (dp->isActive() && dp->isHmd()) {
            userAgent += " " + formatPluginName(dp->getName());
        }
    }
    auto inputPlugins= PluginManager::getInstance()->getInputPlugins();
    for (auto& ip : inputPlugins) {
        if (ip->isActive()) {
            userAgent += " " + formatPluginName(ip->getName());
        }
    }
    // for codecs, we include all of them, even if not active
    auto codecPlugins = PluginManager::getInstance()->getCodecPlugins();
    for (auto& cp : codecPlugins) {
        userAgent += " " + formatPluginName(cp->getName());
    }

    return userAgent;
}

// toggleTabletUI is in ApplicationUi.cpp.

// The cursor (checkChangeCursor, showCursor, updateSystemCursor) is in ApplicationCursor.cpp.

void Application::updateHeartbeat() const {
    DeadlockWatchdogThread::updateHeartbeat();
}

// The shutdown sequence (onAboutToQuit, cleanupBeforeQuit, ~Application) is in ApplicationShutdown.cpp.

// The GL context and its share contexts, the display plug-ins and the render engine's start (initializeGL,
// initializeDisplayPlugins, initializeRenderEngine) are in ApplicationDisplay.cpp.

// The sign-in screen and the interface's bring-up (showLoginScreen, initializeUi) are in ApplicationUi.cpp.


// The desktop's QML context (onDesktopRootContextCreated, onDesktopRootItemCreated) is in ApplicationScripting.cpp.

// userKickConfirmation (the ban question) is in ApplicationDialogs.cpp.

// The context properties of every other QML surface (setupQmlSurface) are in ApplicationScripting.cpp.

// The camera's per-frame update (updateCamera) is in ApplicationFrame.cpp.

void Application::runTests() {
    runTimingTests();
    runUnitTests();
}

// The preferences' setters (setFieldOfView to setMiniTabletEnabled) are in ApplicationSettings.cpp.

// showHelp (the controls reference on the tablet) is in ApplicationUi.cpp.

// The window's resize (resizeEvent, resizeGL, with verticalFieldOfView) is in ApplicationDisplay.cpp.

// goToStartingLocation (where the client goes first) is in ApplicationDomain.cpp.

// The import wrappers for a dropped or opened file (importJSONFromURL, importSVOFromURL, importFromZIP) are in ApplicationImport.cpp.

// The serverless domains and the interstitial mode (isServerlessMode, setIsInterstitialMode, setIsServerlessMode,
// prepareServerlessDomainContents, loadServerlessDomain, loadErrorDomain) are in ApplicationDomain.cpp.

// importImage is in ApplicationImport.cpp.

// thread-safe
// onPresent (the display plug-in's present callback) is in ApplicationDisplay.cpp.

// The event routing (notify, event, eventFilter, the key, mouse, touch, wheel and drop handlers) is in
// ApplicationEvents.cpp.

// acceptSnapshot is in ApplicationImport.cpp.

// The frame loop's tick (idle, with the Windows CPU monitor it starts) is in ApplicationFrame.cpp.

// getMouse (the reticle's position) is in ApplicationCamera.cpp.

// exportEntities (both forms) is in ApplicationImport.cpp.

// loadSettings and saveSettings are in ApplicationSettings.cpp.

// importEntities and pasteEntities are in ApplicationImport.cpp.

// init (the avatar manager, the entity tree renderer, the physics engine, the entity simulation and the game workload
// brought up; the interface stage calls it) is in ApplicationStartup.cpp.

// The sign-in gate (pauseUntilLoginDetermined, resumeAfterLoginDialogActionTaken) is in ApplicationUi.cpp.

// The avatar's scripts (loadAvatarScripts, unloadAvatarScripts) are in ApplicationAvatar.cpp.

// The per-frame helpers (updateLOD, pushPostUpdateLambda, updateMyAvatarLookAtPosition, updateThreads) are in ApplicationFrame.cpp.

// The overlays (toggleOverlays, setOverlaysVisible, centerUI) are in ApplicationUi.cpp.

// The camera modes (cycleCamera, cameraModeChanged, changeViewAsNeeded, cameraMenuChanged) are in ApplicationCamera.cpp.

// requeryOctree and resetPhysicsReadyInformation are in ApplicationDomain.cpp.

// reloadResourceCaches (the domain's content reloaded) is in ApplicationDomain.cpp.

// rotationModeChanged is in ApplicationCamera.cpp.

// The keyboard focus and its highlight (setKeyboardFocusHighlight, getKeyboardFocusEntity, setKeyboardFocusEntity) are in
// ApplicationKeyboardFocus.cpp.

// The simulation step and what frames it (updateDialogs, updateSecondaryCameraViewFrustum, the physics gate
// setArrivalWaitsForNearby/tryToEnablePhysics, update, updateRenderArgs) is in ApplicationFrame.cpp.

// The per-frame queries to the servers (queryAvatars, sendNackPackets, queryOctree) are in ApplicationDomain.cpp.

// isHMDMode is in ApplicationDisplay.cpp.

float Application::getNumCollisionObjects() const {
    return _physicsEngine ? _physicsEngine->getNumCollisionObjects() : 0;
}

// getTargetRenderFrameRate and getDesirableApplicationGeometry are in ApplicationDisplay.cpp.

// computePickRay is in ApplicationCamera.cpp.

// getMyAvatar and getAvatarPosition are in ApplicationAvatar.cpp.

// copyViewFrustum and copyDisplayViewFrustum are in ApplicationCamera.cpp.

// resetSensors is in ApplicationCamera.cpp.

// updateWindowTitle is in ApplicationDisplay.cpp.

// The connection's events and the octree accounting (clearDomainOctreeDetails, domainURLChanged, goToErrorDomainURL,
// resettingDomain, nodeAdded, nodeActivated, nodeKilled, trackIncomingOctreePacket, gpuTextureMemSizeStable,
// processOctreeStats) are in ApplicationDomain.cpp.

// The script API's globals (registerScriptEngineWithApplicationServices) are in ApplicationScripting.cpp.

// The URL dispatch (canAcceptURL, acceptURL, with the table of importers by extension) is in ApplicationImport.cpp.

// setSessionUUID is in ApplicationDomain.cpp.

// The asks for a dropped or opened URL (askToSetAvatarUrl, askToLoadScript, askToWearAvatarAttachmentUrl,
// displayAvatarAttachmentWarning), showDialog, showScriptLogs and showAssetServerWidget are in ApplicationDialogs.cpp.

// The import of content into the world (addAssetToWorld and its steps, handleUnzip) is in ApplicationImport.cpp.

// The dialogs and windows (packageModel, loadDialog, importAvatarDialog, putAvatarInMyFiles, the previous script location,
// loadScriptURLDialog, getSampleSound, the LOD tools, entity statistics and domain connection dialogs, the log windows) are
// in ApplicationDialogs.cpp.

// Snapshots (addSnapshotOperator, takeSnapshotOperators, takeSnapshot, takeSecondaryCameraSnapshot,
// takeSecondaryCamera360Snapshot, shareSnapshot) are in ApplicationSnapshots.cpp.

// getRenderResolutionScale is in ApplicationDisplay.cpp.

// notifyPacketVersionMismatch is in ApplicationDomain.cpp.

// checkSkeleton is in ApplicationAvatar.cpp.

// The window's state changes (activeChanged, windowMinimizedChanged) are in ApplicationDisplay.cpp.

void Application::postLambdaEvent(const std::function<void()>& f) {
    if (this->thread() == QThread::currentThread()) {
        f();
    } else {
        QCoreApplication::postEvent(this, new LambdaEvent(f));
    }
}

void Application::sendLambdaEvent(const std::function<void()>& f) {
    if (this->thread() == QThread::currentThread()) {
        f();
    } else {
        LambdaEvent event(f);
        QCoreApplication::sendEvent(this, &event);
    }
}

// initPlugins (the display and input plug-ins' enable lists from the command line) is in ApplicationDisplay.cpp.

// The canvas, UI and device sizes, focus and raise, the active display plug-in and the display modes (getCanvasSize,
// getRenderingGeometry, getUiSize, getRecommendedHUDRect, getDeviceSize, isThrottleRendering, hasFocus, setFocus, raise,
// getDevicePixelRatio, getActiveDisplayPlugin, updateDisplayMode, setDisplayPlugin, switchDisplayMode) are in
// ApplicationDisplay.cpp.

void Application::setShowBulletWireframe(bool value) {
    _physicsEngine->setShowBulletWireframe(value);
}

void Application::setShowBulletAABBs(bool value) {
    _physicsEngine->setShowBulletAABBs(value);
}

void Application::setShowBulletContactPoints(bool value) {
    _physicsEngine->setShowBulletContactPoints(value);
}

void Application::setShowBulletConstraints(bool value) {
    _physicsEngine->setShowBulletConstraints(value);
}

void Application::setShowBulletConstraintLimits(bool value) {
    _physicsEngine->setShowBulletConstraintLimits(value);
}

// The headset's sign-in dialog and the avatar-inputs bar (createLoginDialog, updateLoginDialogPosition, createAvatarInputsBar,
// destroyAvatarInputsBar) are in ApplicationUi.cpp.

// hasRiftControllers and hasViveControllers are in ApplicationDisplay.cpp.

// onDismissedLoginDialog is in ApplicationUi.cpp.

void Application::setShowTrackedObjects(bool value) {
    _showTrackedObjects = value;
}

// The HMD's session and queries (startHMDStandBySession, endHMDSession, getEyeProjection, getEyeOffset, getHMDSensorPose)
// are in ApplicationDisplay.cpp.

void Application::deadlockApplication() {
    qCDebug(interfaceapp) << "Intentionally deadlocked Interface";
    // Using a loop that will *technically* eventually exit (in ~600 billion years)
    // to avoid compiler warnings about a loop that will never exit
    for (uint64_t i = 1; i != 0; ++i) {
        QThread::sleep(1);
    }
}

// cause main thread to be unresponsive for 35 seconds
void Application::unresponsiveApplication() {
    // to avoid compiler warnings about a loop that will never exit
    uint64_t start = usecTimestampNow();
    uint64_t UNRESPONSIVE_FOR_SECONDS = 35;
    uint64_t UNRESPONSIVE_FOR_USECS = UNRESPONSIVE_FOR_SECONDS * USECS_PER_SECOND;
    qCDebug(interfaceapp) << "Intentionally cause Interface to be unresponsive for " << UNRESPONSIVE_FOR_SECONDS << " seconds";
    while (usecTimestampNow() - start < UNRESPONSIVE_FOR_USECS) {
        QThread::sleep(1);
    }
}

// setActiveDisplayPlugin is in ApplicationDisplay.cpp.

// raiseWindow is in ApplicationDisplay.cpp.

// goToHandedOffUrl (a later launch's URL, handed off by SingleInstance) is in ApplicationDomain.cpp.

CompositorHelper& Application::getApplicationCompositor() const {
    return *DependencyManager::get<CompositorHelper>();
}


// virtual functions required for PluginContainer
ui::Menu* Application::getPrimaryMenu() {
    auto appMenu = _window->menuBar();
    auto uiMenu = dynamic_cast<ui::Menu*>(appMenu);
    return uiMenu;
}

// The primary widget, window and context, the display plug-ins' tools and isForeground are in ApplicationDisplay.cpp.

// FIXME?  perhaps two, one for the main thread and one for the offscreen UI rendering thread?
static const int UI_RESERVED_THREADS = 1;
// Windows won't let you have all the cores
static const int OS_RESERVED_THREADS = 1;

void Application::updateThreadPoolCount() const {
    auto reservedThreads = UI_RESERVED_THREADS + OS_RESERVED_THREADS + _displayPlugin->getRequiredThreadCount();
    auto availableThreads = QThread::idealThreadCount() - reservedThreads;
    auto threadPoolSize = std::max(MIN_PROCESSING_THREAD_POOL_SIZE, availableThreads);
    qCDebug(interfaceapp) << "Ideal Thread Count " << QThread::idealThreadCount();
    qCDebug(interfaceapp) << "Reserved threads " << reservedThreads;
    qCDebug(interfaceapp) << "Setting thread pool size to " << threadPoolSize;
    QThreadPool::globalInstance()->setMaxThreadCount(threadPoolSize);
}

// The system tablet's mode and ids (updateSystemTabletMode, getTabletScreenID, getTabletHomeButtonID, getTabletFrameID,
// getTabletIDs) are in ApplicationUi.cpp.

// setAvatarOverrideUrl is in ApplicationAvatar.cpp.

void Application::saveNextPhysicsStats(QString filename) {
    _physicsEngine->saveNextPhysicsStats(filename);
}

void Application::copyToClipboard(const QString& text) {
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, "copyToClipboard", Q_ARG(QString, text));
        return;
    }

    // assume that the address is being copied because the user wants a shareable address
    QApplication::clipboard()->setText(text);
}

QString Application::getGraphicsCardType() {
    return GPUIdent::getInstance()->getName();
}

// openDirectory and showUrlHandler (the URL handler's "open this file?" ask) are in ApplicationDialogs.cpp.

// used to test "shutdown" crash annotation.
void Application::crashOnShutdown() {
    qDebug() << "crashOnShutdown(), ON PURPOSE!";
    _crashOnShutdown = true;
    quit();
}

void Application::overrideEntry(){
    _overrideEntry = true;
}
void Application::forceDisplayName(const QString& displayName) {
    getMyAvatar()->setDisplayName(displayName);
}
void Application::forceLoginWithTokens(const QString& tokens) {
    DependencyManager::get<AccountManager>()->setAccessTokens(tokens);
    Setting::Handle<bool>(KEEP_ME_LOGGED_IN_SETTING_NAME, true).set(true);
}

// The Android lifecycle (beforeEnterBackground, enterBackground, enterForeground, toggleAwayMode) is in ApplicationEvents.cpp.
