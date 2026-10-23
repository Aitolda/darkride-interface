//
//  ApplicationStartup.cpp
//  interface/src
//
//  Application's start-up: setupEssentials (what runs before the members exist: the version switches, the crash
//  check, the resources, the plug-in manager and the registrations) and the constructor, which brings the client up
//  in ten named stages (the process, the services and their connections, the window and the renderer, the
//  interface, the entity processing, input, settings, the operators, then the sign-in screen), each a function
//  here whose body is the constructor's former text between two of the start-up profile's phase marks. With the
//  pieces only the start-up uses: the Qt message handler, the Windows native event filter, the settings' defaults
//  (ARCHITECTURE.md §5.4 step 4).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
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

QSharedPointer<OffscreenUi> getOffscreenUI();            // Application.cpp's
QString labelledTitle(const QString& title);             // Application.cpp's: a window title with the client label
extern Setting::Handle<int> maxOctreePacketsPerSecond;   // Application.cpp's

QTimer locationUpdateTimer;   // started by the constructor, stopped in ApplicationShutdown.cpp

// constants only the start-up reads
#if !defined(Q_OS_ANDROID)
static const uint32_t MAX_CONCURRENT_RESOURCE_DOWNLOADS = 16;
#else
static const uint32_t MAX_CONCURRENT_RESOURCE_DOWNLOADS = 4;
#endif
static const int ENTITY_SERVER_ADDED_TIMEOUT = 5000;
static const QString DESKTOP_LOCATION = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
static const QUrl BUBBLE_ICON_QML = PathUtils::qmlUrl("BubbleIcon.qml");

#ifdef Q_OS_WIN
static const UINT UWM_IDENTIFY_INSTANCES =
    RegisterWindowMessage("UWM_IDENTIFY_INSTANCES_{8AB82783-B74A-4258-955B-8188C22AA0D6}_" + qgetenv("USERNAME"));
static const UINT UWM_SHOW_APPLICATION =
    RegisterWindowMessage("UWM_SHOW_APPLICATION_{71123FD6-3DA8-4DC1-9C27-8A12A6250CBA}_" + qgetenv("USERNAME"));

class MyNativeEventFilter : public QAbstractNativeEventFilter {
public:
    static MyNativeEventFilter& getInstance() {
        static MyNativeEventFilter staticInstance;
        return staticInstance;
    }

    bool nativeEventFilter(const QByteArray &eventType, void* msg, long* result) Q_DECL_OVERRIDE {
        if (eventType == "windows_generic_MSG") {
            MSG* message = (MSG*)msg;

            if (message->message == UWM_IDENTIFY_INSTANCES) {
                *result = UWM_IDENTIFY_INSTANCES;
                return true;
            }

            if (message->message == UWM_SHOW_APPLICATION) {
                MainWindow* applicationWindow = qApp->getWindow();
                if (applicationWindow->isMinimized()) {
                    applicationWindow->showNormal();  // Restores to windowed or maximized state appropriately.
                }
                qApp->setActiveWindow(applicationWindow);  // Flashes the taskbar icon if not focus.
                return true;
            }
        }
        return false;
    }
};
#endif

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    QString logMessage = LogHandler::getInstance().printMessage((LogMsgType) type, context, message);

    if (!logMessage.isEmpty()) {
#ifdef Q_OS_ANDROID
        const char * local=logMessage.toStdString().c_str();
        switch (type) {
            case QtDebugMsg:
                __android_log_write(ANDROID_LOG_DEBUG,"Interface",local);
                break;
            case QtInfoMsg:
                __android_log_write(ANDROID_LOG_INFO,"Interface",local);
                break;
            case QtWarningMsg:
                __android_log_write(ANDROID_LOG_WARN,"Interface",local);
                break;
            case QtCriticalMsg:
                __android_log_write(ANDROID_LOG_ERROR,"Interface",local);
                break;
            case QtFatalMsg:
            default:
                __android_log_write(ANDROID_LOG_FATAL,"Interface",local);
                abort();
        }
#else
        qApp->getLogger()->addMessage(qPrintable(logMessage));
#endif
    }
}

/**jsdoc
 * <p>The <code>Controller.Hardware.Application</code> object has properties representing Interface's state. The property
 * values are integer IDs, uniquely identifying each output. <em>Read-only.</em></p>
 * <p>These states can be mapped to actions or functions or <code>Controller.Standard</code> items in a {@link RouteObject} 
 * mapping (e.g., using the {@link RouteObject#when} method). Each data value is either <code>1.0</code> for "true" or 
 * <code>0.0</code> for "false".</p>
 * <table>
 *   <thead>
 *     <tr><th>Property</th><th>Type</th><th>Data</th><th>Description</th></tr>
 *   </thead>
 *   <tbody>
 *     <tr><td><code>CameraFirstPerson</code></td><td>number</td><td>number</td><td>The camera is in first-person mode.
 *       <em>Legacy first person camera mode.</em></td></tr>
 *     <tr><td><code>CameraFirstPersonLookAt</code></td><td>number</td><td>number</td><td>The camera is in first-person mode.
 *       <em>Default first person camera mode.</em></td></tr>
 *     <tr><td><code>CameraThirdPerson</code></td><td>number</td><td>number</td><td>The camera is in third-person mode.
 *       <em>Legacy third person camera mode.</em></td></tr>
 *     <tr><td><code>CameraLookAt</code></td><td>number</td><td>number</td><td>The camera is in third-person mode.
 *       <em>Default third person camera mode.</em></td></tr>
 *     <tr><td><code>CameraFSM</code></td><td>number</td><td>number</td><td>The camera is in full screen mirror mode.
 *       <em>Legacy "look at myself" behavior.</em></td></tr>
 *     <tr><td><code>CameraSelfie</code></td><td>number</td><td>number</td><td>The camera is in selfie mode.
 *       <em>Default "look at myself" camera mode.</em></td></tr>
 *     <tr><td><code>CameraIndependent</code></td><td>number</td><td>number</td><td>The camera is in independent mode.</td></tr>
 *     <tr><td><code>CameraEntity</code></td><td>number</td><td>number</td><td>The camera is in entity mode.</td></tr>
 *     <tr><td><code>InHMD</code></td><td>number</td><td>number</td><td>The user is in HMD mode.</td></tr>
 *     <tr><td><code>AdvancedMovement</code></td><td>number</td><td>number</td><td>Advanced movement (walking) controls are 
 *       enabled.</td></tr>
 *     <tr><td><code>StrafeEnabled</code></td><td>number</td><td>number</td><td>Strafing is enabled</td></tr>
 *     <tr><td><code>LeftHandDominant</code></td><td>number</td><td>number</td><td>Dominant hand set to left.</td></tr>
 *     <tr><td><code>RightHandDominant</code></td><td>number</td><td>number</td><td>Dominant hand set to right.</td></tr>
 *     <tr><td><code>SnapTurn</code></td><td>number</td><td>number</td><td>Snap turn is enabled.</td></tr>
 *     <tr><td><code>Grounded</code></td><td>number</td><td>number</td><td>The user's avatar is on the ground.</td></tr>
 *     <tr><td><code>NavigationFocused</code></td><td>number</td><td>number</td><td><em>Not used.</em></td></tr>
 *     <tr><td><code>PlatformWindows</code></td><td>number</td><td>number</td><td>The operating system is Windows.</td></tr>
 *     <tr><td><code>PlatformMac</code></td><td>number</td><td>number</td><td>The operating system is Mac.</td></tr>
 *     <tr><td><code>PlatformAndroid</code></td><td>number</td><td>number</td><td>The operating system is Android.</td></tr>
 *   </tbody>
 * </table>
 * @typedef {object} Controller.Hardware-Application
 */


// Statically provided display and input plugins
extern DisplayPluginList getDisplayPlugins();
extern InputPluginList getInputPlugins();
extern void saveInputPluginSettings(const InputPluginList& plugins);

bool setupEssentials(bool runningMarkerExisted) {
    const ClientOptions& options = ClientOptions::get();

    qInstallMessageHandler(messageHandler);

    StartupProfiler::phase("setupEssentials: version switches, crash check, resources");

    // --protocolVersion <file>: the protocol signature into that file, then out, before anything else is set up.
    // It reports to a file because stdout is captured for the log. (A launcher asking this should also pass
    // --allowMultipleInstances, since this runs after the single-instance check.)
    if (!options.protocolVersionFile.isEmpty()) {
        FILE* fp = fopen(options.protocolVersionFile.toLocal8Bit().constData(), "w");
        if (fp) {
            fputs(protocolVersionsSignatureBase64().toLatin1().data(), fp);
            fclose(fp);
            if (!runningMarkerExisted) { // don't leave ours around
                RunningMarker runingMarker(RUNNING_MARKER_FILENAME);
                runingMarker.deleteRunningMarkerFile(); // happens in deleter, but making the side-effect explicit.
            }
            _exit(0);
        }
    }

    const int listenPort = options.listenPort >= 0 ? options.listenPort : INVALID_PORT;

    // emulate standalone device
    bool isStandalone = options.standalone;
    qApp->setProperty(hifi::properties::STANDALONE, isStandalone);

    // Ignore any previous crashes if running from command line with a test script.
    bool inTestMode = !options.testScript.isEmpty();

    bool previousSessionCrashed { false };
    if (!inTestMode) {
        // previousSessionCrashed = CrashRecoveryHandler::checkForResetSettings(runningMarkerExisted, suppressPrompt);
        previousSessionCrashed = CrashRecoveryHandler::checkForResetSettings(runningMarkerExisted, true);
    }

    // get dir to use for cache
    QString cacheDir = options.cacheDirectory;
    if (!cacheDir.isEmpty()) {
        qApp->setProperty(hifi::properties::APP_LOCAL_DATA_PATH, cacheDir);
    }

    {
        const QString resourcesBinaryFile = PathUtils::getRccPath();
        if (!QFile::exists(resourcesBinaryFile)) {
            throw std::runtime_error("Unable to find primary resources");
        }
        if (!QResource::registerResource(resourcesBinaryFile)) {
            throw std::runtime_error("Unable to load primary resources");
        }
    }

    StartupProfiler::phase("setupEssentials: plug-in manager, runtime plug-ins loaded");
    // Tell the plugin manager about our statically linked plugins
    DependencyManager::set<ScriptInitializers>();
    DependencyManager::set<PluginManager>();
    auto pluginManager = PluginManager::getInstance();
    pluginManager->setInputPluginProvider([] { return getInputPlugins(); });
    pluginManager->setDisplayPluginProvider([] { return getDisplayPlugins(); });
    pluginManager->setInputPluginSettingsPersister([](const InputPluginList& plugins) { saveInputPluginSettings(plugins); });
    // The runtime plug-ins load here, on the main thread, before the audio client exists: left to the first lookup,
    // the audio client's codec search would load them from its own thread and the plug-ins' objects (the MIDI
    // plug-in's among them) would live there. The Steam and Oculus init that stood here did this load as a side effect.
    pluginManager->loadPlugins();

    PROFILE_SET_THREAD_NAME("Main Thread");

#if defined(Q_OS_WIN)
    // Select appropriate audio DLL
    QString audioDLLPath = QCoreApplication::applicationDirPath();
    if (IsWindows8OrGreater()) {
        audioDLLPath += "/audioWin8";
    } else {
        audioDLLPath += "/audioWin7";
    }
    QCoreApplication::addLibraryPath(audioDLLPath);
#endif 

    QString defaultScriptsOverrideOption = options.defaultScriptsOverride;

    StartupProfiler::phase("setupEssentials: the registrations (each row a service's constructor)");
    registerApplicationServices(listenPort, defaultScriptsOverrideOption);
    StartupProfiler::phase("setupEssentials: platform helper, bookmarks; then the members (physics engine, clipboard tree, settings)");
    PlatformHelper::setup();
    
    QObject::connect(PlatformHelper::instance(), &PlatformHelper::systemWillWake, [] {
        QMetaObject::invokeMethod(DependencyManager::get<NodeList>().data(), "noteAwakening", Qt::QueuedConnection);
        QMetaObject::invokeMethod(DependencyManager::get<AudioClient>().data(), "noteAwakening", Qt::QueuedConnection);
    });


    QString setBookmarkValue = options.setBookmark;
    if (!setBookmarkValue.isEmpty()) {
        // Bookmarks are expected to be in a name=url form.
        // An `=` character in the name or url is unsupported.
        auto parts = setBookmarkValue.split("=");
        if (parts.length() != 2) {
            qWarning() << "Malformed setBookmark argument: " << setBookmarkValue;
        } else {
            qDebug() << "Setting bookmark" << parts[0] << "to" << parts[1];
            DependencyManager::get<LocationBookmarks>()->insert(parts[0], parts[1]);
        }
    }

    return previousSessionCrashed;
}

const float DEFAULT_HMD_TABLET_SCALE_PERCENT = 100.0f;
const float DEFAULT_DESKTOP_TABLET_SCALE_PERCENT = 75.0f;
const bool DEFAULT_DESKTOP_TABLET_BECOMES_TOOLBAR = true;
const bool DEFAULT_HMD_TABLET_BECOMES_TOOLBAR = false;
const bool DEFAULT_PREFER_STYLUS_OVER_LASER = false;
const bool DEFAULT_PREFER_AVATAR_FINGER_OVER_STYLUS = false;
const QString DEFAULT_CURSOR_NAME = "DEFAULT";   // declared in ApplicationConstants.h
const bool DEFAULT_MINI_TABLET_ENABLED = false; 
const bool DEFAULT_AWAY_STATE_WHEN_FOCUS_LOST_IN_VR_ENABLED = true;
const bool DEFAULT_LOAD_COMPLETE_ENTITY_TREE = true; 
const bool DEFAULT_BYPASS_PRIORITY_SORTING = false; 
const bool DEFAULT_BYPASS_SCRIPT_THROTTLING = false;

Application::Application(int& argc, char** argv, QElapsedTimer& startupTimer, bool runningMarkerExisted) :
    QApplication(argc, argv),
    _window(new MainWindow(desktop())),
    _sessionRunTimer(startupTimer),
#ifndef Q_OS_ANDROID
    _logger(new FileLogger(this)),
#endif
    _previousSessionCrashed(setupEssentials(runningMarkerExisted)),
    _entitySimulation(new PhysicalEntitySimulation()),
    _physicsEngine(new PhysicsEngine(Vectors::ZERO)),
    _entityClipboard(new EntityTree()),
    _previousScriptLocation("LastScriptLocation", DESKTOP_LOCATION),
    _fieldOfView("fieldOfView", DEFAULT_FIELD_OF_VIEW_DEGREES),
    _farClip("farClip", DEFAULT_FAR_CLIP),
    _nearClip("nearClip", DEFAULT_NEAR_CLIP),
    _hmdTabletScale("hmdTabletScale", DEFAULT_HMD_TABLET_SCALE_PERCENT),
    _desktopTabletScale("desktopTabletScale", DEFAULT_DESKTOP_TABLET_SCALE_PERCENT),
    _firstRun(Settings::firstRun, true),
    _desktopTabletBecomesToolbarSetting("desktopTabletBecomesToolbar", DEFAULT_DESKTOP_TABLET_BECOMES_TOOLBAR),
    _hmdTabletBecomesToolbarSetting("hmdTabletBecomesToolbar", DEFAULT_HMD_TABLET_BECOMES_TOOLBAR),
    _preferStylusOverLaserSetting("preferStylusOverLaser", DEFAULT_PREFER_STYLUS_OVER_LASER),
    _preferAvatarFingerOverStylusSetting("preferAvatarFingerOverStylus", DEFAULT_PREFER_AVATAR_FINGER_OVER_STYLUS),
    _constrainToolbarPosition("toolbar/constrainToolbarToCenterX", true),
    _awayStateWhenFocusLostInVREnabled("awayStateWhenFocusLostInVREnabled", DEFAULT_AWAY_STATE_WHEN_FOCUS_LOST_IN_VR_ENABLED),
    _preferredCursor("preferredCursor", DEFAULT_CURSOR_NAME),
    _miniTabletEnabledSetting("miniTabletEnabled", DEFAULT_MINI_TABLET_ENABLED),
    _loadCompleteEntityTreeSetting("loadCompleteEntityTree", DEFAULT_LOAD_COMPLETE_ENTITY_TREE),
    _bypassPrioritySortingSetting("bypassPrioritySorting", DEFAULT_BYPASS_PRIORITY_SORTING), 
    _bypassScriptThrottling("bypassScriptThrottling", DEFAULT_BYPASS_SCRIPT_THROTTLING), 
    _scaleMirror(1.0f),
    _mirrorYawOffset(0.0f),
    _raiseMirror(0.0f),
    _enableProcessOctreeThread(true),
    _lastNackTime(usecTimestampNow()),
    _lastSendDownstreamAudioStats(usecTimestampNow()),
    _notifiedPacketVersionMismatchThisDomain(false),
    _maxOctreePPS(maxOctreePacketsPerSecond.get()),
    _snapshotSound(nullptr),
    _sampleSound(nullptr)
{
    setProperty(hifi::properties::CRASHED, _previousSessionCrashed);

    // The client comes up in ten stages, in this order; each is a function below and a group of rows in the start-up
    // profile (the phase marks inside it). A stage reads what it needs from the DependencyManager rather than from a
    // local of an earlier stage, so each one says what it depends on.
    setupProcess();           // the log handler, test arguments, the GPU's identity, the plug-in container, the thread pool, fonts
    startServices();          // the node list and address manager threads, the watchdog, the account manager, the domain check-in, the audio client
    connectServices();        // the messages client, the domain handler's and node list's signals, discoverability, the script engine initializer
    openWindow();             // the GL canvas and the window, the GL context, the display plug-ins, the audio scripting interface, the render engine
    setupInterface();         // the overlays, the sign-in connections, initializeUi, init
    startEntityProcessing();  // the octree processor and edit sender threads, the first update
    setupInput();             // controller actions and the input devices
    setupSettings();          // loadSettings, the settings thread, the audio interfaces, menu options
    installOperators();       // the callbacks the entity, render and pick libraries make into the application
    finishStartup();          // tablet sounds, the keyboard, URL handlers, the graphics engine, then the sign-in screen
}

// The log handler, test arguments, the GPU's identity, the plug-in container, the thread pool, fonts.
void Application::setupProcess() {
    const ClientOptions& options = ClientOptions::get();

    StartupProfiler::phase("constructor: log handler, test arguments");

    LogHandler::getInstance().moveToThread(thread());
    LogHandler::getInstance().setupRepeatedMessageFlusher();

    if (!options.testScript.isEmpty()) {
        const QString& testScriptPath = options.testScript;
        // If the URL scheme is http(s) or ftp, then use as is, else - treat it as a local file
        // This is done so as not break previous command line scripts
        if (testScriptPath.left(HIFI_URL_SCHEME_HTTP.length()) == HIFI_URL_SCHEME_HTTP ||
            testScriptPath.left(HIFI_URL_SCHEME_FTP.length()) == HIFI_URL_SCHEME_FTP) {
            setProperty(hifi::properties::TEST, QUrl::fromUserInput(testScriptPath));
        } else if (QFileInfo(testScriptPath).exists()) {
            setProperty(hifi::properties::TEST, QUrl::fromLocalFile(testScriptPath));
        }
        quitWhenFinished = options.testQuitWhenFinished;
    }
    if (!options.testResultsLocation.isEmpty()) {
        // Set test snapshot location only if it is a writeable directory
        QFileInfo fileInfo(options.testResultsLocation);
        if (fileInfo.isDir() && fileInfo.isWritable()) {
            TestScriptingInterface::getInstance()->setTestResultsLocation(options.testResultsLocation);
        }
    }

    StartupProfiler::phase("constructor: GPU identification (GPUIdent)");
    {
        // identify gpu as early as possible to help identify OpenGL initialization errors.
        auto gpuIdent = GPUIdent::getInstance();
        // setCrashAnnotation("sentry[contexts][gpu][name]", gpuIdent->getName().toStdString());
        // setCrashAnnotation("sentry[contexts][gpu][version]", gpuIdent->getDriver().toStdString());
        // setCrashAnnotation("sentry[contexts][gpu][memory_size]", std::to_string(gpuIdent->getMemory()));
        setCrashAnnotation("gpu_name", gpuIdent->getName().toStdString());
        setCrashAnnotation("gpu_driver", gpuIdent->getDriver().toStdString());
        setCrashAnnotation("gpu_memory", std::to_string(gpuIdent->getMemory()));
    }

    StartupProfiler::phase("constructor: debug draw, plug-in container, thread pool, controller interface, clipboard");
    // make sure the debug draw singleton is initialized on the main thread.
    DebugDraw::getInstance().removeMarker("");

    PluginContainer* pluginContainer = dynamic_cast<PluginContainer*>(this); // set the container for any plugins that care
    PluginManager::getInstance()->setContainer(pluginContainer);

    QThreadPool::globalInstance()->setMaxThreadCount(MIN_PROCESSING_THREAD_POOL_SIZE);
    thread()->setPriority(QThread::HighPriority);
    thread()->setObjectName("Main Thread");

    setInstance(this);

    auto controllerScriptingInterface = DependencyManager::get<controller::ScriptingInterface>().data();
    _controllerScriptingInterface = dynamic_cast<ControllerScriptingInterface*>(controllerScriptingInterface);
    connect(PluginManager::getInstance().data(), &PluginManager::inputDeviceRunningChanged,
        controllerScriptingInterface, &controller::ScriptingInterface::updateRunningInputDevices);

    EntityTree::setEntityClicksCapturedOperator([this] {
        return _controllerScriptingInterface->areEntityClicksCaptured();
    });

    _entityClipboard->createRootElement();

#ifdef Q_OS_WIN
    installNativeEventFilter(&MyNativeEventFilter::getInstance());
#endif

    StartupProfiler::phase("constructor: 16 fonts");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/Cairo-SemiBold.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/fontawesome-webfont.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/Graphik-Medium.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/Graphik-Regular.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/Graphik-SemiBold.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/hifi-glyphs.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/rawline-500.ttf");

    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/Roboto-Bold.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/Roboto-Light.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/Roboto-Medium.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/Roboto-Regular.ttf");

    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/RobotoCondensed-Bold.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/RobotoCondensed-Light.ttf");
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/RobotoCondensed-Regular.ttf");
    
    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "fonts/RobotoMono-Regular.ttf");

    QFontDatabase::addApplicationFont(PathUtils::resourcesPath() + "styles/Inconsolata.otf");
}

// The node list and address manager threads, the watchdog, the account manager, the domain check-in, the audio client.
void Application::startServices() {
    const ClientOptions& options = ClientOptions::get();

    StartupProfiler::phase("constructor: node list thread, address manager");
    _window->setWindowTitle(labelledTitle("Darkride"));

    Model::setAbstractViewStateInterface(this); // The model class will sometimes need to know view state details from us

    auto nodeList = DependencyManager::get<NodeList>();
    nodeList->startThread();
    nodeList->setFlagTimeForConnectionStep(true);

    // move the AddressManager to the NodeList thread so that domain resets due to domain changes always occur
    // before we tell MyAvatar to go to a new location in the new domain
    auto addressManager = DependencyManager::get<AddressManager>();
    addressManager->moveToThread(nodeList->thread());

    StartupProfiler::phase("constructor: deadlock watchdog");
    if (!options.disableWatchdog && !DeadlockWatchdogThread::disabledByEnvironment()) {
        DeadlockWatchdogThread::startWatching(this);
    }

    StartupProfiler::phase("constructor: avatar manager space, account manager and keypair, version log");
    int avatarSpaceStep = StartupProfiler::begin("avatar manager's workload space");
    // Set File Logger Session UUID
    auto avatarManager = DependencyManager::get<AvatarManager>();
    auto myAvatar = avatarManager ? avatarManager->getMyAvatar() : nullptr;
    if (avatarManager) {
        workload::SpacePointer space = getEntities()->getWorkloadSpace();
        avatarManager->setSpace(space);
    }
    StartupProfiler::end(avatarSpaceStep);
    auto accountManager = DependencyManager::get<AccountManager>();

    // set the account manager's root URL and trigger a login request if we don't have the access token
    int authStep = StartupProfiler::begin("account manager: auth URL, saved account, profile request");
    accountManager->setIsAgent(true);
    accountManager->setAuthURL(NetworkingConstants::METAVERSE_SERVER_URL());
    StartupProfiler::end(authStep);
    int keypairStep = StartupProfiler::begin("keypair check (a new pair is generated on the pool)");
    if (!accountManager->hasKeyPair()) {
        accountManager->generateNewUserKeypair();
    }
    StartupProfiler::end(keypairStep);
    int sessionStep = StartupProfiler::begin("session id, crash annotations, version log");

#ifndef Q_OS_ANDROID
    _logger->setSessionID(accountManager->getSessionID());
#endif

    setCrashAnnotation("metaverse_session_id", accountManager->getSessionID().toString().toStdString());
    setCrashAnnotation("main_thread_id", std::to_string((size_t)QThread::currentThreadId()));

    qCDebug(interfaceapp) << "[VERSION] Build sequence:" << qPrintable(applicationVersion());
    qCDebug(interfaceapp) << "[VERSION] MODIFIED_ORGANIZATION:" << BuildInfo::MODIFIED_ORGANIZATION;
    qCDebug(interfaceapp) << "[VERSION] VERSION:" << BuildInfo::VERSION;
    qCDebug(interfaceapp) << "[VERSION] BUILD_TYPE_STRING:" << BuildInfo::BUILD_TYPE_STRING;
    qCDebug(interfaceapp) << "[VERSION] BUILD_GLOBAL_SERVICES:" << BuildInfo::BUILD_GLOBAL_SERVICES;
#if USE_STABLE_GLOBAL_SERVICES
    qCDebug(interfaceapp) << "[VERSION] We will use STABLE global services.";
#else
    qCDebug(interfaceapp) << "[VERSION] We will use DEVELOPMENT global services.";
#endif

    updateHeartbeat();

    StartupProfiler::end(sessionStep);
    StartupProfiler::phase("constructor: domain check-in timer, audio client thread");
    // setup a timer for domain-server check ins
    QTimer* domainCheckInTimer = new QTimer(this);
    connect(domainCheckInTimer, &QTimer::timeout, nodeList.data(), &NodeList::sendDomainServerCheckIn);
    domainCheckInTimer->start(DOMAIN_SERVER_CHECK_IN_MSECS);
    connect(this, &QCoreApplication::aboutToQuit, [domainCheckInTimer] {
        domainCheckInTimer->stop();
        domainCheckInTimer->deleteLater();
    });

    {
        auto audioIO = DependencyManager::get<AudioClient>().data();
        audioIO->setPositionGetter([] {
            auto avatarManager = DependencyManager::get<AvatarManager>();
            auto myAvatar = avatarManager ? avatarManager->getMyAvatar() : nullptr;

            return myAvatar ? myAvatar->getPositionForAudio() : Vectors::ZERO;
        });
        audioIO->setOrientationGetter([] {
            auto avatarManager = DependencyManager::get<AvatarManager>();
            auto myAvatar = avatarManager ? avatarManager->getMyAvatar() : nullptr;

            return myAvatar ? myAvatar->getOrientationForAudio() : Quaternions::IDENTITY;
        });

        recording::Frame::registerFrameHandler(AudioConstants::getAudioFrameName(), [&audioIO](recording::Frame::ConstPointer frame) {
            audioIO->handleRecordedAudioInput(frame->data);
        });

        connect(audioIO, &AudioClient::inputReceived, [](const QByteArray& audio) {
            static auto recorder = DependencyManager::get<recording::Recorder>();
            if (recorder->isRecording()) {
                static const recording::FrameType AUDIO_FRAME_TYPE = recording::Frame::registerFrameType(AudioConstants::getAudioFrameName());
                recorder->recordFrame(AUDIO_FRAME_TYPE, audio);
            }
        });
        audioIO->startThread();
    }

    // Make sure we don't time out during slow operations at startup
    updateHeartbeat();
}

// The messages client, the domain handler's and node list's signals, discoverability, the script engine initializer.
void Application::connectServices() {
    const ClientOptions& options = ClientOptions::get();
    auto nodeList = DependencyManager::get<NodeList>();
    auto addressManager = DependencyManager::get<AddressManager>();
    auto accountManager = DependencyManager::get<AccountManager>();
    auto myAvatar = getMyAvatar();

    StartupProfiler::phase("constructor: messages client thread, domain handler connections");
    // Setup MessagesClient
    DependencyManager::get<MessagesClient>()->startThread();

    const DomainHandler& domainHandler = nodeList->getDomainHandler();

    connect(&domainHandler, &DomainHandler::domainURLChanged, this, &Application::domainURLChanged);
    connect(&domainHandler, &DomainHandler::redirectToErrorDomainURL, this, &Application::goToErrorDomainURL);
    connect(&domainHandler, &DomainHandler::domainURLChanged, [](QUrl domainURL){
        setCrashAnnotation("domain", domainURL.toString().toStdString());
    });
    connect(&domainHandler, &DomainHandler::resetting, this, &Application::resettingDomain);
    connect(&domainHandler, &DomainHandler::connectedToDomain, this, &Application::updateWindowTitle);
    connect(&domainHandler, &DomainHandler::connectedToDomain, this, [] { StartupProfiler::instant("connected to a domain"); });
    connect(&domainHandler, &DomainHandler::disconnectedFromDomain, this, &Application::updateWindowTitle);
    connect(&domainHandler, &DomainHandler::disconnectedFromDomain, this, [this]() {
        auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
        entityScriptingInterface->deleteEntity(getTabletScreenID());
        entityScriptingInterface->deleteEntity(getTabletHomeButtonID());
        entityScriptingInterface->deleteEntity(getTabletFrameID());
        _failedToConnectToEntityServer = false;
    });

    _entityServerConnectionTimer.setSingleShot(true);
    connect(&_entityServerConnectionTimer, &QTimer::timeout, this, &Application::setFailedToConnectToEntityServer);

    connect(&domainHandler, &DomainHandler::connectedToDomain, this, [this]() {
        if (!isServerlessMode()) {
            _entityServerConnectionTimer.setInterval(ENTITY_SERVER_ADDED_TIMEOUT);
            _entityServerConnectionTimer.start();
            _failedToConnectToEntityServer = false;
        }
    });
    connect(&domainHandler, &DomainHandler::domainConnectionRefused, this, &Application::domainConnectionRefused);

    nodeList->getDomainHandler().setErrorDomainURL(QUrl(REDIRECT_HIFI_ADDRESS));

    // We could clear ATP assets only when changing domains, but it's possible that the domain you are connected
    // to has gone down and switched to a new content set, so when you reconnect the cached ATP assets will no longer be valid.
    connect(&domainHandler, &DomainHandler::disconnectedFromDomain, DependencyManager::get<ScriptCache>().data(), &ScriptCache::clearATPScriptsFromCache);

    StartupProfiler::phase("constructor: discoverability, node list and account connections");
    // update our location every 5 seconds in the metaverse server, assuming that we are authenticated with one
    const qint64 DATA_SERVER_LOCATION_CHANGE_UPDATE_MSECS = 5 * MSECS_PER_SECOND;

    auto discoverabilityManager = DependencyManager::get<DiscoverabilityManager>();
    connect(&locationUpdateTimer, &QTimer::timeout, discoverabilityManager.data(), &DiscoverabilityManager::updateLocation);
    connect(&locationUpdateTimer, &QTimer::timeout, this, [this] {
        // nothing to remember while the sign-in screen is up, since no world is joined until someone signs in
        // (pauseUntilLoginDetermined); the last place stays saved for after
        if (!getLoginDialogPoppedUp()) {
            DependencyManager::get<AddressManager>()->storeCurrentAddress();
        }
    });
    locationUpdateTimer.start(DATA_SERVER_LOCATION_CHANGE_UPDATE_MSECS);

    // if we get a domain change, immediately attempt update location in metaverse server
    connect(&nodeList->getDomainHandler(), &DomainHandler::connectedToDomain,
        discoverabilityManager.data(), &DiscoverabilityManager::updateLocation);

    // send a location update immediately
    discoverabilityManager->updateLocation();

    connect(nodeList.data(), &NodeList::nodeAdded, this, &Application::nodeAdded);
    connect(nodeList.data(), &NodeList::nodeKilled, this, &Application::nodeKilled);
    connect(nodeList.data(), &NodeList::nodeActivated, this, &Application::nodeActivated);
    connect(nodeList.data(), &NodeList::uuidChanged, myAvatar.get(), &MyAvatar::setSessionUUID);
    connect(nodeList.data(), &NodeList::uuidChanged, this, &Application::setSessionUUID);
    connect(nodeList.data(), &NodeList::packetVersionMismatch, this, &Application::notifyPacketVersionMismatch);

    // you might think we could just do this in NodeList but we only want this connection for Interface
    connect(&nodeList->getDomainHandler(), &DomainHandler::limitOfSilentDomainCheckInsReached,
        nodeList.data(), [nodeList]() {nodeList->reset("Domain checkin limit"); });

    auto dialogsManager = DependencyManager::get<DialogsManager>();
#if defined(Q_OS_ANDROID)
    connect(accountManager.data(), &AccountManager::authRequired, this, []() {
        auto addressManager = DependencyManager::get<AddressManager>();
        AndroidHelper::instance().showLoginDialog(addressManager->currentAddress());
    });
#else
    connect(accountManager.data(), &AccountManager::authRequired, dialogsManager.data(), &DialogsManager::showLoginDialog);
#endif
    connect(accountManager.data(), &AccountManager::usernameChanged, this, &Application::updateWindowTitle);
    connect(accountManager.data(), &AccountManager::usernameChanged, this, [&](const QString username){
        auto avatarManager = DependencyManager::get<AvatarManager>();
        if (avatarManager) avatarManager->getMyAvatar()->setDisplayName(username);
    });

    // use our MyAvatar position and quat for address manager path
    addressManager->setPositionGetter([] {
        auto avatarManager = DependencyManager::get<AvatarManager>();
        auto myAvatar = avatarManager ? avatarManager->getMyAvatar() : nullptr;
        return myAvatar ? myAvatar->getWorldFeetPosition() : Vectors::ZERO;
    });
    addressManager->setOrientationGetter([] {
        auto avatarManager = DependencyManager::get<AvatarManager>();
        auto myAvatar = avatarManager ? avatarManager->getMyAvatar() : nullptr;
        return myAvatar ? myAvatar->getWorldOrientation() : glm::quat();
    });

    connect(addressManager.data(), &AddressManager::hostChanged, this, &Application::updateWindowTitle);
    connect(this, &QCoreApplication::aboutToQuit, addressManager.data(), &AddressManager::storeCurrentAddress);

    connect(this, &Application::activeDisplayPluginChanged, this, &Application::updateThreadPoolCount);
    connect(this, &Application::activeDisplayPluginChanged, this, [=](){
        qApp->setProperty(hifi::properties::HMD, qApp->isHMDMode());
        auto displayPlugin = qApp->getActiveDisplayPlugin();

        if (displayPlugin->isHmd()) {
            if (_preferredCursor.get() == Cursor::Manager::getIconName(Cursor::Icon::RETICLE)) {
                setPreferredCursor(Cursor::Manager::getIconName(Cursor::Icon::RETICLE));
            }
            else {
                setPreferredCursor(Cursor::Manager::getIconName(Cursor::Icon::ARROW));
            }
        }
        else {
            setPreferredCursor(Cursor::Manager::getIconName(Cursor::Icon::SYSTEM));
        }

        setCrashAnnotation("display_plugin", displayPlugin->getName().toStdString());
        setCrashAnnotation("hmd", displayPlugin->isHmd() ? "1" : "0");
    });
    connect(this, &Application::activeDisplayPluginChanged, this, &Application::updateSystemTabletMode);
    connect(this, &Application::activeDisplayPluginChanged, this, [&](){
        if (getLoginDialogPoppedUp()) {
            auto dialogsManager = DependencyManager::get<DialogsManager>();
            auto keyboard = DependencyManager::get<Keyboard>();
            if (_firstRun.get()) {
                // display mode changed.  Don't allow auto-switch to work after this session.
                _firstRun.set(false);
            }
            if (isHMDMode()) {
                emit loginDialogFocusDisabled();
                dialogsManager->hideLoginDialog();
                createLoginDialog();
            } else {
                DependencyManager::get<EntityScriptingInterface>()->deleteEntity(_loginDialogID);
                _loginDialogID = QUuid();
                _loginStateManager.tearDown();
                dialogsManager->showLoginDialog();
                emit loginDialogFocusEnabled();
            }
        }
    });

    // Save avatar location immediately after a teleport.
    connect(myAvatar.get(), &MyAvatar::positionGoneTo,
        DependencyManager::get<AddressManager>().data(), &AddressManager::storeCurrentAddress);

    connect(myAvatar.get(), &MyAvatar::positionGoneTo, this, [this] {
        if (!_physicsEnabled) {
            // when we arrive somewhere without physics enabled --> startSafeLanding
            _octreeProcessor.startSafeLanding();
            // and query the entity server at once: the restarted landing completes on the server's next report of the
            // initial scene, which it makes on the next query that asks for it; left to the periodic query that was
            // up to 3 s of waiting for nothing (WORLD-LOADING.md, the arrival gate)
            _queryExpiry = SteadyClock::now();
        }
    }, Qt::QueuedConnection);

    connect(myAvatar.get(), &MyAvatar::skeletonModelURLChanged, [](){
        QUrl avatarURL = qApp->getMyAvatar()->getSkeletonModelURL();
        setCrashAnnotation("avatar", avatarURL.toString().toStdString());
    });

    StartupProfiler::phase("constructor: sample sound, script engine initializer, node interest set, options");
    // Inititalize sample before registering
    _sampleSound = DependencyManager::get<SoundCache>()->getSound(PathUtils::resourcesUrl("sounds/sample.wav"));

    {
        auto scriptEngines = DependencyManager::get<ScriptEngines>().data();
        scriptEngines->registerScriptInitializer([this](ScriptEnginePointer engine) {
            registerScriptEngineWithApplicationServices(engine);
        });

        connect(scriptEngines, &ScriptEngines::scriptCountChanged, this, [this] {
            auto scriptEngines = DependencyManager::get<ScriptEngines>();
            if (scriptEngines->getRunningScripts().isEmpty()) {
                getMyAvatar()->clearScriptableSettings();
            }
        }, Qt::QueuedConnection);

        connect(scriptEngines, &ScriptEngines::scriptsReloading, this, [this] {
            getEntities()->reloadEntityScripts();
            loadAvatarScripts(getMyAvatar()->getScriptUrls());
        }, Qt::QueuedConnection);
    }

#ifdef _WIN32
    WSADATA WsaData;
    int wsaresult = WSAStartup(MAKEWORD(2, 2), &WsaData);
#endif

    // tell the NodeList instance who to tell the domain server we care about
    nodeList->addSetOfNodeTypesToNodeInterestSet(NodeSet() << NodeType::AudioMixer << NodeType::AvatarMixer
        << NodeType::EntityServer << NodeType::AssetServer << NodeType::MessagesMixer << NodeType::EntityScriptServer
        << NodeType::PhysicsServer);


    ResourceCache::setRequestLimit(options.concurrentDownloads >= 0 ? (uint32_t)options.concurrentDownloads
                                                                   : MAX_CONCURRENT_RESOURCE_DOWNLOADS);

    // perhaps override the avatar url.  Since we will test later for validity
    // we don't need to do so here.
    QString avatarURL = options.avatarUrl;
    _avatarOverrideUrl = QUrl::fromUserInput(avatarURL);

    // If someone specifies both --avatarURL and --replaceAvatarURL,
    // the replaceAvatarURL wins.  So only set the _overrideUrl if this
    // does have a non-empty string.
    QString replaceURL = options.replaceAvatarUrl;
    if (!replaceURL.isEmpty()) {
        _avatarOverrideUrl = QUrl::fromUserInput(replaceURL);
        _saveAvatarOverrideUrl = true;
    }
}

// The GL canvas and the window, the GL context, the display plug-ins, the audio scripting interface, the render engine.
void Application::openWindow() {
    const ClientOptions& options = ClientOptions::get();

    StartupProfiler::phase("constructor: GL canvas, window restored and shown");
    // setDefaultFormat has no effect after the platform window has been created, so call it here.
    QSurfaceFormat::setDefaultFormat(getDefaultOpenGLSurfaceFormat());

    int canvasStep = StartupProfiler::begin("GL canvas and viewport container");
    _glWidget = new GLCanvas();
    getApplicationCompositor().setRenderingWidget(_glWidget);
    // the GL view sits in a container that can hold its size while build mode's panels are dragged
    _window->setCentralWidget(new ViewportContainer(_glWidget, _window));
    StartupProfiler::end(canvasStep);
    int shownStep = StartupProfiler::begin("window geometry restored and shown");

    _window->restoreGeometry();
    _window->setVisible(true);
    StartupProfiler::end(shownStep);
    // the window has been seen to end up at Qt's bare 640 x 480 after the start on macOS (2026-10-18), cause unknown:
    // where it is is logged once it has been shown and a little later, and a shrunk window is put back
    QTimer::singleShot(0, _window, [this] { _window->checkStartGeometry(); });
    QTimer::singleShot(3000, _window, [this] { _window->checkStartGeometry(); });

    _glWidget->setFocusPolicy(Qt::StrongFocus);
    _glWidget->setFocus();

    if (options.systemCursor) {
        _preferredCursor.set(Cursor::Manager::getIconName(Cursor::Icon::SYSTEM));
    }
    showCursor(Cursor::Manager::lookupIcon(_preferredCursor.get()));

    // enable mouse tracking; otherwise, we only get drag events
    _glWidget->setMouseTracking(true);
    // Make sure the window is set to the correct size by processing the pending events
    {
        StartupProfiler::Scope step("pending events processed (the window's first layout)");
        QCoreApplication::processEvents();
    }

    StartupProfiler::phase("constructor: initializeGL");
    // Create the main thread context, the GPU backend
    initializeGL();
    qCDebug(interfaceapp, "Initialized GL");

    StartupProfiler::phase("constructor: initializeDisplayPlugins");
    // Initialize the display plugin architecture
    initializeDisplayPlugins();
    qCDebug(interfaceapp, "Initialized Display");

    if (_displayPlugin && !_displayPlugin->isHmd()) {
        _preferredCursor.set(Cursor::Manager::getIconName(Cursor::Icon::SYSTEM));
        showCursor(Cursor::Manager::lookupIcon(_preferredCursor.get()));
    }
    StartupProfiler::phase("constructor: audio scripting interface");
    // An audio device changed signal received before the display plugins are set up will cause a crash,
    // so we defer the setup of the `scripting::Audio` class until this point
    {
        auto audioScriptingInterface = DependencyManager::set<AudioScriptingInterface, scripting::Audio>();
        auto audioIO = DependencyManager::get<AudioClient>().data();
        connect(audioIO, &AudioClient::mutedByMixer, audioScriptingInterface.data(), &AudioScriptingInterface::mutedByMixer);
        connect(audioIO, &AudioClient::receivedFirstPacket, audioScriptingInterface.data(), &AudioScriptingInterface::receivedFirstPacket);
        connect(audioIO, &AudioClient::disconnected, audioScriptingInterface.data(), &AudioScriptingInterface::disconnected);
        connect(audioIO, &AudioClient::muteEnvironmentRequested, [](glm::vec3 position, float radius) {
            auto audioClient = DependencyManager::get<AudioClient>();
            auto audioScriptingInterface = DependencyManager::get<AudioScriptingInterface>();
            auto myAvatarPosition = DependencyManager::get<AvatarManager>()->getMyAvatar()->getWorldPosition();
            float distance = glm::distance(myAvatarPosition, position);

            if (distance < radius) {
                audioClient->setMuted(true);
                audioScriptingInterface->environmentMuted();
            }
        });
        QSharedPointer<scripting::Audio> scriptingAudioSharedPointer = qSharedPointerDynamicCast<scripting::Audio>(DependencyManager::get<AudioScriptingInterface>());
        if (scriptingAudioSharedPointer) {
            connect(this, &Application::activeDisplayPluginChanged,
                scriptingAudioSharedPointer.data(), &scripting::Audio::onContextChanged);
        }
    }

    StartupProfiler::phase("constructor: initializeRenderEngine");
    // Create the rendering engine.  This can be slow on some machines due to lots of
    // GPU pipeline creation.
    initializeRenderEngine();
    qCDebug(interfaceapp, "Initialized Render Engine.");
}

// The overlays, the sign-in connections, initializeUi, init.
void Application::setupInterface() {
    const ClientOptions& options = ClientOptions::get();

    StartupProfiler::phase("constructor: overlays, sign-in connections");
    _overlays.init(); // do this before scripts load

    auto offscreenUi = getOffscreenUI();
    connect(offscreenUi.data(), &OffscreenUi::desktopReady, []() {
        // Now that we've loaded the menu and thus switched to the previous display plugin
        // we can unlock the desktop repositioning code, since all the positions will be
        // relative to the desktop size for this plugin
        auto offscreenUi = getOffscreenUI();
        auto desktop = offscreenUi->getDesktop();
        if (desktop) {
            desktop->setProperty("repositionLocked", false);
        }
    });

    connect(offscreenUi.data(), &OffscreenUi::keyboardFocusActive, [this]() {
#if !defined(Q_OS_ANDROID)
        // Darkride starts on the sign-in screen: until someone signs in, or continues as the remembered account, the
        // avatar is hidden, no world is joined and no scripts run (pauseUntilLoginDetermined), and the screen covers
        // the window. A launcher passing --tokens has signed in for us.
        if (ClientOptions::get().hasTokens) {
            resumeAfterLoginDialogActionTaken();
            return;
        }

        showLoginScreen();
#else
        resumeAfterLoginDialogActionTaken();
#endif
    });

#if !defined(Q_OS_ANDROID)
    // Signing out of a running viewer goes back to the sign-in screen, as at the start: out of the world, the avatar
    // hidden, until someone signs in, and then on as at the start (resumeAfterLoginDialogActionTaken).
    if (!options.hasTokens) {
        connect(DependencyManager::get<AccountManager>().data(), &AccountManager::logoutComplete, this, [this]() {
            if (getLoginDialogPoppedUp()) {
                return;
            }
            pauseUntilLoginDetermined();
            showLoginScreen();
        }, Qt::QueuedConnection);
    }
#endif

    StartupProfiler::phase("constructor: initializeUi");
    // Initialize the user interface and menu system
    // Needs to happen AFTER the render engine initialization to access its configuration
    initializeUi();

    StartupProfiler::phase("constructor: init");
    init();
    qCDebug(interfaceapp, "init() complete.");
}

// The octree processor and edit sender threads, the first update.
void Application::startEntityProcessing() {
    auto myAvatar = getMyAvatar();

    StartupProfiler::phase("constructor: octree processor and edit sender threads, first update");
    // create thread for parsing of octree data independent of the main network and rendering threads
    _octreeProcessor.initialize(_enableProcessOctreeThread);
    connect(&_octreeProcessor, &OctreePacketProcessor::packetVersionMismatch, this, &Application::notifyPacketVersionMismatch);
    _entityEditSender.initialize(_enableProcessOctreeThread);

    _idleLoopStdev.reset();

    // update before the first render
    update(0);

    // Make sure we don't time out during slow operations at startup
    updateHeartbeat();

    _entityEditSender.setMyAvatar(myAvatar.get());

    // The entity octree will have to know about MyAvatar for the parentJointName import
    getEntities()->getTree()->setMyAvatar(myAvatar);
    _entityClipboard->setMyAvatar(myAvatar);

    // For now we're going to set the PPS for outbound packets to be super high, this is
    // probably not the right long term solution. But for now, we're going to do this to
    // allow you to move an entity around in your hand
    _entityEditSender.setPacketsPerSecond(3000); // super high!!

    // Make sure we don't time out during slow operations at startup
    updateHeartbeat();

    connect(this, SIGNAL(aboutToQuit()), this, SLOT(onAboutToQuit()));

    // FIXME -- I'm a little concerned about this.
    connect(myAvatar->getSkeletonModel().get(), &SkeletonModel::skeletonLoaded,
        this, &Application::checkSkeleton, Qt::QueuedConnection);
}

// Controller actions and the input devices.
void Application::setupInput() {
    const ClientOptions& options = ClientOptions::get();

    StartupProfiler::phase("constructor: controller actions, input devices");
    // Setup the userInputMapper with the actions
    auto userInputMapper = DependencyManager::get<UserInputMapper>();
    connect(userInputMapper.data(), &UserInputMapper::actionEvent, [this](int action, float state) {
        using namespace controller;
        auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
        QSharedPointer<scripting::Audio> audioScriptingInterface = qSharedPointerDynamicCast<scripting::Audio>(DependencyManager::get<AudioScriptingInterface>());
        {
            auto actionEnum = static_cast<Action>(action);
            int key = Qt::Key_unknown;
            static int lastKey = Qt::Key_unknown;
            bool navAxis = false;
            switch (actionEnum) {
                case Action::TOGGLE_PUSHTOTALK:
                    if (audioScriptingInterface) {
                        if (state > 0.0f) {
                            audioScriptingInterface->setPushingToTalk(true);
                        } else if (state <= 0.0f) {
                            audioScriptingInterface->setPushingToTalk(false);
                        }
                    }
                    break;

                case Action::UI_NAV_VERTICAL:
                    navAxis = true;
                    if (state > 0.0f) {
                        key = Qt::Key_Up;
                    } else if (state < 0.0f) {
                        key = Qt::Key_Down;
                    }
                    break;

                case Action::UI_NAV_LATERAL:
                    navAxis = true;
                    if (state > 0.0f) {
                        key = Qt::Key_Right;
                    } else if (state < 0.0f) {
                        key = Qt::Key_Left;
                    }
                    break;

                case Action::UI_NAV_GROUP:
                    navAxis = true;
                    if (state > 0.0f) {
                        key = Qt::Key_Tab;
                    } else if (state < 0.0f) {
                        key = Qt::Key_Backtab;
                    }
                    break;

                case Action::UI_NAV_BACK:
                    key = Qt::Key_Escape;
                    break;

                case Action::UI_NAV_SELECT:
                    key = Qt::Key_Return;
                    break;
                default:
                    break;
            }

            auto window = tabletScriptingInterface->getTabletWindow();
            if (navAxis && window) {
                if (lastKey != Qt::Key_unknown) {
                    QKeyEvent event(QEvent::KeyRelease, lastKey, Qt::NoModifier);
                    sendEvent(window, &event);
                    lastKey = Qt::Key_unknown;
                }

                if (key != Qt::Key_unknown) {
                    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
                    sendEvent(window, &event);
                    tabletScriptingInterface->processEvent(&event);
                    lastKey = key;
                }
            } else if (key != Qt::Key_unknown && window) {
                if (state) {
                    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
                    sendEvent(window, &event);
                    tabletScriptingInterface->processEvent(&event);
                } else {
                    QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier);
                    sendEvent(window, &event);
                }
                return;
            }
        }

        if (action == controller::toInt(controller::Action::RETICLE_CLICK)) {
            auto reticlePos = getApplicationCompositor().getReticlePosition();
            QPoint localPos(reticlePos.x, reticlePos.y); // both hmd and desktop already handle this in our coordinates.
            if (state) {
                QMouseEvent mousePress(QEvent::MouseButtonPress, localPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                sendEvent(_glWidget, &mousePress);
                _reticleClickPressed = true;
            } else {
                QMouseEvent mouseRelease(QEvent::MouseButtonRelease, localPos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                sendEvent(_glWidget, &mouseRelease);
                _reticleClickPressed = false;
            }
            return; // nothing else to do
        }

        if (state) {
            if (action == controller::toInt(controller::Action::TOGGLE_MUTE)) {
                auto audioClient = DependencyManager::get<AudioClient>();
                audioClient->setMuted(!audioClient->isMuted());
            } else if (action == controller::toInt(controller::Action::CYCLE_CAMERA)) {
                cycleCamera();
            } else if (action == controller::toInt(controller::Action::CONTEXT_MENU) && !isInterstitialMode()) {
                toggleTabletUI();
            } else if (action == controller::toInt(controller::Action::RETICLE_X)) {
                auto oldPos = getApplicationCompositor().getReticlePosition();
                getApplicationCompositor().setReticlePosition({ oldPos.x + state, oldPos.y });
            } else if (action == controller::toInt(controller::Action::RETICLE_Y)) {
                auto oldPos = getApplicationCompositor().getReticlePosition();
                getApplicationCompositor().setReticlePosition({ oldPos.x, oldPos.y + state });
            } else if (action == controller::toInt(controller::Action::TOGGLE_OVERLAY)) {
                toggleOverlays();
            }
        }
    });

    _applicationStateDevice = userInputMapper->getStateDevice();

    _applicationStateDevice->setInputVariant(STATE_IN_HMD, []() -> float {
        return qApp->isHMDMode() ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_CAMERA_FULL_SCREEN_MIRROR, []() -> float {
        return qApp->getCamera().getMode() == CAMERA_MODE_MIRROR ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_CAMERA_FIRST_PERSON, []() -> float {
        return qApp->getCamera().getMode() == CAMERA_MODE_FIRST_PERSON ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_CAMERA_FIRST_PERSON_LOOK_AT, []() -> float {
        return qApp->getCamera().getMode() == CAMERA_MODE_FIRST_PERSON_LOOK_AT ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_CAMERA_THIRD_PERSON, []() -> float {
        return qApp->getCamera().getMode() == CAMERA_MODE_THIRD_PERSON ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_CAMERA_LOOK_AT, []() -> float {
        return qApp->getCamera().getMode() == CAMERA_MODE_LOOK_AT ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_CAMERA_SELFIE, []() -> float {
        return qApp->getCamera().getMode() == CAMERA_MODE_SELFIE ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_CAMERA_ENTITY, []() -> float {
        return qApp->getCamera().getMode() == CAMERA_MODE_ENTITY ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_CAMERA_INDEPENDENT, []() -> float {
        return qApp->getCamera().getMode() == CAMERA_MODE_INDEPENDENT ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_SNAP_TURN, []() -> float {
        return qApp->getMyAvatar()->getSnapTurn() ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_ADVANCED_MOVEMENT_CONTROLS, []() -> float {
        return qApp->getMyAvatar()->useAdvancedMovementControls() ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_LEFT_HAND_DOMINANT, []() -> float {
        return qApp->getMyAvatar()->getDominantHand() == "left" ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_RIGHT_HAND_DOMINANT, []() -> float {
        return qApp->getMyAvatar()->getDominantHand() == "right" ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_STRAFE_ENABLED, []() -> float {
        return qApp->getMyAvatar()->getStrafeEnabled() ? 1 : 0;
    });

    _applicationStateDevice->setInputVariant(STATE_GROUNDED, []() -> float {
        return qApp->getMyAvatar()->getCharacterController()->onGround() ? 1 : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_NAV_FOCUSED, []() -> float {
        auto offscreenUi = getOffscreenUI();
        return offscreenUi ? (offscreenUi->navigationFocused() ? 1 : 0) : 0;
    });
    _applicationStateDevice->setInputVariant(STATE_PLATFORM_WINDOWS, []() -> float {
#if defined(Q_OS_WIN)
        return 1;
#else
        return 0;
#endif
    });
    _applicationStateDevice->setInputVariant(STATE_PLATFORM_MAC, []() -> float {
#if defined(Q_OS_MACOS)
        return 1;
#else
        return 0;
#endif
    });
    _applicationStateDevice->setInputVariant(STATE_PLATFORM_ANDROID, []() -> float {
#if defined(Q_OS_ANDROID)
        return 1 ;
#else
        return 0;
#endif
    });


    getRefreshRateManager().setRefreshRateRegime(RefreshRateManager::RefreshRateRegime::STARTUP);

    // Setup the _keyboardMouseDevice, _touchscreenDevice, _touchscreenVirtualPadDevice and the user input mapper with the default bindings
    userInputMapper->registerDevice(_keyboardMouseDevice->getInputDevice());
    // if the _touchscreenDevice is not supported it will not be registered
    if (_touchscreenDevice) {
        userInputMapper->registerDevice(_touchscreenDevice->getInputDevice());
    }
    if (_touchscreenVirtualPadDevice) {
        userInputMapper->registerDevice(_touchscreenVirtualPadDevice->getInputDevice());
    }

    _defaultScriptsLocation.setPath(options.scriptsPath);

    // Make sure we don't time out during slow operations at startup
    updateHeartbeat();
}

// loadSettings, the settings thread, the audio interfaces, menu options.
void Application::setupSettings() {
    StartupProfiler::phase("constructor: loadSettings");
    loadSettings();
    // the menu has just replayed its saved state (which build mode ignored); from here on it may be entered
    DependencyManager::get<Editor>()->setReady();

    updateVerboseLogging();

    // Make sure we don't time out during slow operations at startup
    updateHeartbeat();
    StartupProfiler::phase("constructor: settings thread, audio interfaces, menu options");
    QTimer* settingsTimer = new QTimer();
    moveToNewNamedThread(settingsTimer, "Settings Thread", [this, settingsTimer]{
        // This needs to run on the settings thread, so we need to pass the `settingsTimer` as the
        // receiver object, otherwise it will run on the application thread and trigger a warning
        // about trying to kill the timer on the main thread.
        connect(qApp, &Application::beforeAboutToQuit, settingsTimer, [this, settingsTimer]{
            // Disconnect the signal from the save settings
            QObject::disconnect(settingsTimer, &QTimer::timeout, this, &Application::saveSettings);
            // Stop the settings timer
            settingsTimer->stop();
            // Delete it (this will trigger the thread destruction
            settingsTimer->deleteLater();
            // Mark the settings thread as finished, so we know we can safely save in the main application
            // shutdown code
            _settingsGuard.trigger();
        });

        int SAVE_SETTINGS_INTERVAL = 10 * MSECS_PER_SECOND; // Let's save every seconds for now
        settingsTimer->setSingleShot(false);
        settingsTimer->setInterval(SAVE_SETTINGS_INTERVAL); // 10s, Qt::CoarseTimer acceptable
        QObject::connect(settingsTimer, &QTimer::timeout, this, &Application::saveSettings);
        settingsTimer->start();
    }, QThread::LowestPriority);

    if (Menu::getInstance()->isOptionChecked(MenuOption::FirstPersonLookAt)) {
        getMyAvatar()->setBoomLength(MyAvatar::ZOOM_MIN);  // So that camera doesn't auto-switch to third person.
    }

    {
        auto audioIO = DependencyManager::get<AudioClient>().data();
        // set the local loopback interface for local sounds
        AudioInjector::setLocalAudioInterface(audioIO);
        auto audioScriptingInterface = DependencyManager::get<AudioScriptingInterface>();
        audioScriptingInterface->setLocalAudioInterface(audioIO);
        connect(audioIO, &AudioClient::noiseGateOpened, audioScriptingInterface.data(), &AudioScriptingInterface::noiseGateOpened);
        connect(audioIO, &AudioClient::noiseGateClosed, audioScriptingInterface.data(), &AudioScriptingInterface::noiseGateClosed);
        connect(audioIO, &AudioClient::inputReceived, audioScriptingInterface.data(), &AudioScriptingInterface::inputReceived);
    }

    this->installEventFilter(this);


    Menu::getInstance()->setIsOptionChecked(MenuOption::ActionMotorControl, true);
    Menu::getInstance()->setIsOptionChecked(
        MenuOption::MaterialProceduralShaders,
        MeshPartPayload::DEFAULT_ENABLE_MATERIAL_PROCEDURAL_SHADERS
    );
}

// The callbacks the entity, render and pick libraries make into the application.
void Application::installOperators() {
    auto myAvatar = getMyAvatar();
    const DomainHandler& domainHandler = DependencyManager::get<NodeList>()->getDomainHandler();

    StartupProfiler::phase("constructor: pointer, entity and material operators");
    // If the user clicks on an object, we will check that it's a web surface, and if so, set the focus to it
    auto pointerManager = DependencyManager::get<PointerManager>();
    auto keyboardFocusOperator = [this](const QUuid& id, const PointerEvent& event) {
        if (event.shouldFocus()) {
            auto keyboard = DependencyManager::get<Keyboard>();
            if (getEntities()->wantsKeyboardFocus(id)) {
                setKeyboardFocusEntity(id);
            } else if (!keyboard->containsID(id)) { // FIXME: this is a hack to make the keyboard work for now, since the keys would otherwise steal focus
                setKeyboardFocusEntity(UNKNOWN_ENTITY_ID);
            }
        }
    };
    connect(pointerManager.data(), &PointerManager::triggerBeginEntity, keyboardFocusOperator);
    connect(pointerManager.data(), &PointerManager::triggerBeginOverlay, keyboardFocusOperator);

    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
    connect(entityScriptingInterface.data(), &EntityScriptingInterface::deletingEntity, this, [this](const EntityItemID& entityItemID) {
        if (entityItemID == _keyboardFocusedEntity.get()) {
            setKeyboardFocusEntity(UNKNOWN_ENTITY_ID);
        }
    }, Qt::QueuedConnection);

    EntityTreeRenderer::setAddMaterialToEntityOperator([this](const QUuid& entityID, graphics::MaterialLayer material, const std::string& parentMaterialName) {
        if (_aboutToQuit) {
            return false;
        }

        auto renderable = getEntities()->renderableForEntityId(entityID);
        if (renderable) {
            renderable->addMaterial(material, parentMaterialName);
            return true;
        }

        return false;
    });
    EntityTreeRenderer::setRemoveMaterialFromEntityOperator([this](const QUuid& entityID, graphics::MaterialPointer material, const std::string& parentMaterialName) {
        if (_aboutToQuit) {
            return false;
        }

        auto renderable = getEntities()->renderableForEntityId(entityID);
        if (renderable) {
            renderable->removeMaterial(material, parentMaterialName);
            return true;
        }

        return false;
    });

    EntityTreeRenderer::setAddMaterialToAvatarOperator([](const QUuid& avatarID, graphics::MaterialLayer material, const std::string& parentMaterialName) {
        auto avatarManager = DependencyManager::get<AvatarManager>();
        auto avatar = static_pointer_cast<Avatar>(avatarManager->getAvatarBySessionID(avatarID));
        if (avatar) {
            avatar->addMaterial(material, parentMaterialName);
            return true;
        }
        return false;
    });
    EntityTreeRenderer::setRemoveMaterialFromAvatarOperator([](const QUuid& avatarID, graphics::MaterialPointer material, const std::string& parentMaterialName) {
        auto avatarManager = DependencyManager::get<AvatarManager>();
        auto avatar = static_pointer_cast<Avatar>(avatarManager->getAvatarBySessionID(avatarID));
        if (avatar) {
            avatar->removeMaterial(material, parentMaterialName);
            return true;
        }
        return false;
    });
    EntityTree::setGetUnscaledDimensionsForIDOperator([this](const QUuid& id) {
        if (_aboutToQuit) {
            return glm::vec3(1.0f);
        }

        auto entity = getEntities()->getEntity(id);
        if (entity) {
            return entity->getUnscaledDimensions();
        }

        auto avatarManager = DependencyManager::get<AvatarManager>();
        auto avatar = static_pointer_cast<Avatar>(avatarManager->getAvatarBySessionID(id));
        if (avatar) {
            return avatar->getSNScale();
        }
        return glm::vec3(1.0f);
    });
    Procedural::opaqueStencil = [](gpu::StatePointer state) { PrepareStencil::testMaskDrawShape(*state); };
    Procedural::transparentStencil = [](gpu::StatePointer state) { PrepareStencil::testMask(*state); };

    EntityTree::setGetEntityObjectOperator([this](const QUuid& id) -> QObject* {
        auto entities = getEntities();
        if (auto entity = entities->renderableForEntityId(id)) {
            return qobject_cast<QObject*>(entity.get());
        }
        return nullptr;
    });

    EntityTree::setEmitScriptEventOperator([this](const QUuid& id, const QVariant& message) {
        auto entities = getEntities();
        if (auto entity = entities->renderableForEntityId(id)) {
            entity->emitScriptEvent(message);
        }
    });

    EntityTree::setSendToQmlOperator([this](const QUuid& id, const QVariant& message) {
        auto entities = getEntities();
        if (auto entity = entities->renderableForEntityId(id)) {
            entity->sendToQml(message);
        }
    });

    EntityTree::setTextSizeOperator([this](const QUuid& id, const QString& text) {
        auto entities = getEntities();
        if (auto entity = entities->renderableForEntityId(id)) {
            if (auto renderable = std::dynamic_pointer_cast<render::entities::TextEntityRenderer>(entity)) {
                return renderable->textSize(text);
            }
        }
        return QSizeF(0.0f, 0.0f);
    });

    connect(this, &Application::aboutToQuit, [this]() {
        setKeyboardFocusEntity(UNKNOWN_ENTITY_ID);
    });

    // Make sure we don't time out during slow operations at startup
    updateHeartbeat();

    OctreeEditPacketSender* packetSender = entityScriptingInterface->getPacketSender();
    EntityEditPacketSender* entityPacketSender = static_cast<EntityEditPacketSender*>(packetSender);
    entityPacketSender->setMyAvatar(myAvatar.get());

    connect(this, &Application::applicationStateChanged, this, &Application::activeChanged);
    connect(_window, SIGNAL(windowMinimizedChanged(bool)), this, SLOT(windowMinimizedChanged(bool)));
    qCDebug(interfaceapp, "Startup time: %4.2f seconds.", (double)_sessionRunTimer.elapsed() / 1000.0);
    StartupProfiler::phase("constructor: fade function, snapshot sound, asset timers, picks, web-surface operators");

    EntityTreeRenderer::setEntitiesShouldFadeFunction([this]() {
        SharedNodePointer entityServerNode = DependencyManager::get<NodeList>()->soloNodeOfType(NodeType::EntityServer);
        return entityServerNode && !isPhysicsEnabled();
    });

    _snapshotSound = DependencyManager::get<SoundCache>()->getSound(PathUtils::resourcesUrl("sounds/snapshot/snap.wav"));

    // Monitor model assets (e.g., from Clara.io) added to the world that may need resizing.
    static const int ADD_ASSET_TO_WORLD_TIMER_INTERVAL_MS = 1000;
    _addAssetToWorldResizeTimer.setInterval(ADD_ASSET_TO_WORLD_TIMER_INTERVAL_MS); // 1s, Qt::CoarseTimer acceptable
    connect(&_addAssetToWorldResizeTimer, &QTimer::timeout, this, &Application::addAssetToWorldCheckModelSize);

    // Auto-update and close adding asset to world info message box.
    static const int ADD_ASSET_TO_WORLD_INFO_TIMEOUT_MS = 5000;
    _addAssetToWorldInfoTimer.setInterval(ADD_ASSET_TO_WORLD_INFO_TIMEOUT_MS); // 5s, Qt::CoarseTimer acceptable
    _addAssetToWorldInfoTimer.setSingleShot(true);
    connect(&_addAssetToWorldInfoTimer, &QTimer::timeout, this, &Application::addAssetToWorldInfoTimeout);
    static const int ADD_ASSET_TO_WORLD_ERROR_TIMEOUT_MS = 8000;
    _addAssetToWorldErrorTimer.setInterval(ADD_ASSET_TO_WORLD_ERROR_TIMEOUT_MS); // 8s, Qt::CoarseTimer acceptable
    _addAssetToWorldErrorTimer.setSingleShot(true);
    connect(&_addAssetToWorldErrorTimer, &QTimer::timeout, this, &Application::addAssetToWorldErrorTimeout);

    connect(this, &QCoreApplication::aboutToQuit, this, &Application::addAssetToWorldMessageClose);
    connect(&domainHandler, &DomainHandler::domainURLChanged, this, &Application::addAssetToWorldMessageClose);
    connect(&domainHandler, &DomainHandler::redirectToErrorDomainURL, this, &Application::addAssetToWorldMessageClose);

    updateSystemTabletMode();

    connect(&_myCamera, &Camera::modeUpdated, this, &Application::cameraModeChanged);

    DependencyManager::get<PickManager>()->setShouldPickHUDOperator([]() { return DependencyManager::get<HMDScriptingInterface>()->isHMDMode(); });
    DependencyManager::get<PickManager>()->setCalculatePos2DFromHUDOperator([this](const glm::vec3& intersection) {
        const glm::vec2 MARGIN(25.0f);
        glm::vec2 maxPos = _controllerScriptingInterface->getViewportDimensions() - MARGIN;
        glm::vec2 pos2D = DependencyManager::get<HMDScriptingInterface>()->overlayFromWorldPoint(intersection);
        return glm::max(MARGIN, glm::min(pos2D, maxPos));
    });

    // Setup the mouse ray pick and related operators
    {
        auto mouseRayPick = std::make_shared<RayPick>(Vectors::ZERO, Vectors::UP, PickFilter(PickScriptingInterface::PICK_ENTITIES() | PickScriptingInterface::PICK_LOCAL_ENTITIES()), 0.0f, true);
        mouseRayPick->parentTransform = std::make_shared<MouseTransformNode>();
        mouseRayPick->setJointState(PickQuery::JOINT_STATE_MOUSE);
        auto mouseRayPickID = DependencyManager::get<PickManager>()->addPick(PickQuery::Ray, mouseRayPick);
        DependencyManager::get<EntityTreeRenderer>()->setMouseRayPickID(mouseRayPickID);
    }
    DependencyManager::get<EntityTreeRenderer>()->setMouseRayPickResultOperator([](unsigned int rayPickID) {
        RayToEntityIntersectionResult entityResult;
        entityResult.intersects = false;
        auto pickResult = DependencyManager::get<PickManager>()->getPrevPickResultTyped<RayPickResult>(rayPickID);
        if (pickResult) {
            entityResult.intersects = pickResult->type != IntersectionType::NONE;
            if (entityResult.intersects) {
                entityResult.intersection = pickResult->intersection;
                entityResult.distance = pickResult->distance;
                entityResult.surfaceNormal = pickResult->surfaceNormal;
                entityResult.entityID = pickResult->objectID;
                entityResult.extraInfo = pickResult->extraInfo;
            }
        }
        return entityResult;
    });
    DependencyManager::get<EntityTreeRenderer>()->setSetPrecisionPickingOperator([](unsigned int rayPickID, bool value) {
        DependencyManager::get<PickManager>()->setPrecisionPicking(rayPickID, value);
    });

    EntityItem::setBillboardRotationOperator([](const glm::vec3& position, const glm::quat& rotation, BillboardMode billboardMode, const glm::vec3& frustumPos) {
        if (billboardMode == BillboardMode::YAW) {
            //rotate about vertical to face the camera
            glm::vec3 dPosition = frustumPos - position;
            // If x and z are 0, atan(x, z) is undefined, so default to 0 degrees
            float yawRotation = dPosition.x == 0.0f && dPosition.z == 0.0f ? 0.0f : glm::atan(dPosition.x, dPosition.z);
            return glm::quat(glm::vec3(0.0f, yawRotation, 0.0f));
        } else if (billboardMode == BillboardMode::FULL) {
            // use the referencial from the avatar, y isn't always up
            glm::vec3 avatarUP = DependencyManager::get<AvatarManager>()->getMyAvatar()->getWorldOrientation() * Vectors::UP;
            // check to see if glm::lookAt will work / using glm::lookAt variable name
            glm::highp_vec3 s(glm::cross(position - frustumPos, avatarUP));

            // make sure s is not NaN for any component
            if (glm::length2(s) > 0.0f) {
                return glm::conjugate(glm::toQuat(glm::lookAt(frustumPos, position, avatarUP)));
            }
        }
        return rotation;
    });
    EntityItem::setPrimaryViewFrustumPositionOperator([this]() {
        ViewFrustum viewFrustum;
        copyViewFrustum(viewFrustum);
        return viewFrustum.getPosition();
    });

    DependencyManager::get<UsersScriptingInterface>()->setKickConfirmationOperator([this] (const QUuid& nodeID) { userKickConfirmation(nodeID); });

    render::entities::WebEntityRenderer::setAcquireWebSurfaceOperator([=](const QString& url, bool htmlContent, QSharedPointer<OffscreenQmlSurface>& webSurface, bool& cachedWebSurface) {
        bool isTablet = url == TabletScriptingInterface::QML;
        if (htmlContent) {
            webSurface = DependencyManager::get<OffscreenQmlSurfaceCache>()->acquire(render::entities::WebEntityRenderer::QML);
            cachedWebSurface = true;
            auto rootItemLoadedFunctor = [url, webSurface] {
                webSurface->getRootItem()->setProperty(render::entities::WebEntityRenderer::URL_PROPERTY, url);
            };
            if (webSurface->getRootItem()) {
                rootItemLoadedFunctor();
            } else {
                QObject::connect(webSurface.data(), &hifi::qml::OffscreenSurface::rootContextCreated, rootItemLoadedFunctor);
            }
            auto surfaceContext = webSurface->getSurfaceContext();
            surfaceContext->setContextProperty("KeyboardScriptingInterface", DependencyManager::get<KeyboardScriptingInterface>().data());
        } else {
            // FIXME: the tablet should use the OffscreenQmlSurfaceCache
            webSurface = QSharedPointer<OffscreenQmlSurface>(new OffscreenQmlSurface(), [](OffscreenQmlSurface* webSurface) {
                AbstractViewStateInterface::instance()->sendLambdaEvent([webSurface] {
                    // WebEngineView may run other threads (wasapi), so they must be deleted for a clean shutdown
                    // if the application has already stopped its event loop, delete must be explicit
                    delete webSurface;
                });
            });
            auto rootItemLoadedFunctor = [webSurface, url, isTablet] {
                Application::setupQmlSurface(webSurface->getSurfaceContext(), isTablet || url == LOGIN_DIALOG.toString() || url == AVATAR_INPUTS_BAR_QML.toString() ||
                   url == BUBBLE_ICON_QML.toString());
            };
            if (webSurface->getRootItem()) {
                rootItemLoadedFunctor();
            } else {
                QObject::connect(webSurface.data(), &hifi::qml::OffscreenSurface::rootContextCreated, rootItemLoadedFunctor);
            }
            webSurface->load(url);
            cachedWebSurface = false;
        }
        
        // average target framerate for a 60 hz monitor 
        const uint8_t DEFAULT_MAX_FPS = 120;
        const uint8_t TABLET_FPS = 120;
        webSurface->setMaxFps(isTablet ? TABLET_FPS : DEFAULT_MAX_FPS);
    });
    render::entities::WebEntityRenderer::setReleaseWebSurfaceOperator([=](QSharedPointer<OffscreenQmlSurface>& webSurface, bool& cachedWebSurface, std::vector<QMetaObject::Connection>& connections) {
        QQuickItem* rootItem = webSurface->getRootItem();

        // Fix for crash in QtWebEngineCore when rapidly switching domains
        // Call stop on the QWebEngineView before destroying OffscreenQMLSurface.
        if (rootItem && !cachedWebSurface) {
            // stop loading
            QMetaObject::invokeMethod(rootItem, "stop");
        }

        webSurface->pause();

        for (auto& connection : connections) {
            QObject::disconnect(connection);
        }
        connections.clear();

        // If the web surface was fetched out of the cache, release it back into the cache
        if (cachedWebSurface) {
            // If it's going back into the cache make sure to explicitly set the URL to a blank page
            // in order to stop any resource consumption or audio related to the page.
            if (rootItem) {
                rootItem->setProperty("url", "about:blank");
            }
            auto offscreenCache = DependencyManager::get<OffscreenQmlSurfaceCache>();
            if (offscreenCache) {
                offscreenCache->release(render::entities::WebEntityRenderer::QML, webSurface);
            }
            cachedWebSurface = false;
        }
        webSurface.reset();
    });
}

// Tablet sounds, the keyboard, URL handlers, the graphics engine, then the sign-in screen.
void Application::finishStartup() {
    auto accountManager = DependencyManager::get<AccountManager>();

    StartupProfiler::phase("constructor: tablet sounds, keyboard, URL handlers, graphics engine start");
    // Preload Tablet sounds
    DependencyManager::get<EntityScriptingInterface>()->setEntityTree(qApp->getEntities()->getTree());
    DependencyManager::get<TabletScriptingInterface>()->preloadSounds();
    DependencyManager::get<Keyboard>()->createKeyboard();

    FileDialogHelper::setOpenDirectoryOperator([this](const QString& path) { openDirectory(path); });
    QDesktopServices::setUrlHandler("file", this, "showUrlHandler");
    QDesktopServices::setUrlHandler("", this, "showUrlHandler");
    auto drives = QDir::drives();
    for (auto drive : drives) {
        QDesktopServices::setUrlHandler(QUrl(drive.absolutePath()).scheme(), this, "showUrlHandler");
    }

    _pendingIdleEvent = false;
    _graphicsEngine.startup();

    qCDebug(interfaceapp) << "Metaverse session ID is" << uuidStringWithoutCurlyBraces(accountManager->getSessionID());

#if defined(Q_OS_ANDROID)
    connect(&AndroidHelper::instance(), &AndroidHelper::beforeEnterBackground, this, &Application::beforeEnterBackground);
    connect(&AndroidHelper::instance(), &AndroidHelper::enterBackground, this, &Application::enterBackground);
    connect(&AndroidHelper::instance(), &AndroidHelper::enterForeground, this, &Application::enterForeground);
    connect(&AndroidHelper::instance(), &AndroidHelper::toggleAwayMode, this, &Application::toggleAwayMode);
    AndroidHelper::instance().notifyLoadComplete();
#endif
    StartupProfiler::phase("constructor: pauseUntilLoginDetermined");
    pauseUntilLoginDetermined();
}
