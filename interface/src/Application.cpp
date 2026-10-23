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


// The window title's client label, first in the title when there is one: --clientLabel "<text>" on the command line,
// else the DARKRIDE_CLIENT_LABEL environment variable, else the build's (cmake -DCLIENT_LABEL=...). With several
// sessions' clients on one machine, a label set per launch names the session and the run ("Darkride - Physics - Card
// Run Test") without a rebuild, release builds included, so Caity doesn't close one for her own (2026-10-22).
static const QString& clientLabel() {
    static const QString label = []() -> QString {
        const QString& fromCommandLine = ClientOptions::get().clientLabel;
        if (!fromCommandLine.isEmpty()) {
            return fromCommandLine;
        }
        const QString fromEnvironment = QProcessEnvironment::systemEnvironment().value("DARKRIDE_CLIENT_LABEL").trimmed();
        return fromEnvironment.isEmpty() ? BuildInfo::CLIENT_LABEL : fromEnvironment;
    }();
    return label;
}

QString labelledTitle(const QString& title) {   // ApplicationStartup.cpp reads it too
    return clientLabel().isEmpty() ? title : clientLabel() + " | " + title;
}


// For processing on QThreadPool, we target a number of threads after reserving some
// based on how many are being consumed by the application and the display plugin.  However,
// we will never drop below the 'min' value
const int MIN_PROCESSING_THREAD_POOL_SIZE = 2;   // declared in ApplicationConstants.h

const QString SNAPSHOT_EXTENSION = ".jpg";
const QString JPG_EXTENSION = ".jpg";
const QString PNG_EXTENSION = ".png";
const QString SVO_EXTENSION = ".svo";
const QString SVO_JSON_EXTENSION = ".svo.json";
const QString JSON_EXTENSION = ".json";
const QString JS_EXTENSION = ".js";
const QString FST_EXTENSION = ".fst";
const QString FBX_EXTENSION = ".fbx";
const QString OBJ_EXTENSION = ".obj";
const QString AVA_JSON_EXTENSION = ".ava.json";
static const QString WEB_VIEW_TAG = "noDownload=true";
const QString ZIP_EXTENSION = ".zip";


static const QString INFO_EDIT_ENTITIES_PATH = "html/edit-commands.html";
static const QString INFO_HELP_PATH = "html/tabletHelp.html";

static const unsigned int THROTTLED_SIM_FRAMERATE = 15;
static const int THROTTLED_SIM_FRAME_PERIOD_MS = MSECS_PER_SECOND / THROTTLED_SIM_FRAMERATE;
static const int ENTITY_SERVER_CONNECTION_TIMEOUT = 5000;

static const float INITIAL_QUERY_RADIUS = 10.0f;  // priority radius for entities before physics enabled


Setting::Handle<int> maxOctreePacketsPerSecond{"maxOctreePPS", DEFAULT_MAX_OCTREE_PPS};

Setting::Handle<bool> loginDialogPoppedUp{"loginDialogPoppedUp", false};

const QUrl AVATAR_INPUTS_BAR_QML = PathUtils::qmlUrl("AvatarInputsBar.qml");   // declared in ApplicationConstants.h

static const QString STANDARD_TO_ACTION_MAPPING_NAME = "Standard to Action";
static const QString NO_MOVEMENT_MAPPING_NAME = "Standard to Action (No Movement)";
static const QString NO_MOVEMENT_MAPPING_JSON = PathUtils::resourcesPath() + "/controllers/standard_nomovement.json";

static const int INTERVAL_TO_CHECK_HMD_WORN_STATUS = 500; // milliseconds
static const QString DESKTOP_DISPLAY_PLUGIN_NAME = "Desktop";
const QString ACTIVE_DISPLAY_PLUGIN_SETTING_NAME = "activeDisplayPlugin";   // declared in ApplicationConstants.h
static const QString SYSTEM_TABLET = "com.highfidelity.interface.tablet.system";
const QString KEEP_ME_LOGGED_IN_SETTING_NAME = "keepMeLoggedIn";   // declared in ApplicationConstants.h

const float FOCUS_HIGHLIGHT_EXPANSION_FACTOR = 1.05f;   // declared in KeyboardFocusHighlight.h

#if defined(Q_OS_ANDROID)
#endif
const std::vector<std::pair<QString, Application::AcceptURLMethod>> Application::_acceptedExtensions {
    { SVO_EXTENSION, &Application::importSVOFromURL },
    { SVO_JSON_EXTENSION, &Application::importSVOFromURL },
    { AVA_JSON_EXTENSION, &Application::askToWearAvatarAttachmentUrl },
    { JSON_EXTENSION, &Application::importJSONFromURL },
    { JS_EXTENSION, &Application::askToLoadScript },
    { FST_EXTENSION, &Application::askToSetAvatarUrl },
    { ZIP_EXTENSION, &Application::importFromZIP },
    { JPG_EXTENSION, &Application::importImage },
    { PNG_EXTENSION, &Application::importImage }
};

// MyNativeEventFilter (the Windows messages that identify and raise the running instance) is in ApplicationStartup.cpp.


// messageHandler (Qt's messages into the log) is in ApplicationStartup.cpp.



// setupEssentials (what runs before the members exist: the version switches, the crash check, the resources, the
// plug-ins, the registrations) is in ApplicationStartup.cpp, with the statically provided plug-in lists it needs.
extern DisplayPluginList getDisplayPlugins();   // the statically provided display plug-ins; updateDisplayMode reads them too

// FIXME move to header, or better yet, design some kind of UI manager
// to take care of highlighting keyboard focused items, rather than
// continuing to overburden Application.cpp
QUuid _keyboardFocusHighlightID;   // declared in KeyboardFocusHighlight.h

OffscreenGLCanvas* _qmlShareContext { nullptr };

// FIXME hack access to the internal share context for the Chromium helper
// Normally we'd want to use QWebEngine::initialize(), but we can't because
// our primary context is a QGLWidget, which can't easily be initialized to share
// from a QOpenGLContext.
//
// So instead we create a new offscreen context to share with the QGLWidget,
// and manually set THAT to be the shared context for the Chromium helper
OffscreenGLCanvas* _chromiumShareContext { nullptr };

Q_GUI_EXPORT void qt_gl_set_global_share_context(QOpenGLContext *context);
Q_GUI_EXPORT QOpenGLContext *qt_gl_global_share_context();


// The settings' defaults are in ApplicationStartup.cpp, beside the constructor that reads them.

QSharedPointer<OffscreenUi> getOffscreenUI() {
    return DependencyManager::get<OffscreenUi>();
}

// The constructor is in ApplicationStartup.cpp.

void Application::setFailedToConnectToEntityServer() {
    _failedToConnectToEntityServer = true;
}

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

// The name of the world being joined, for a popup, or "" when it has none to show (an address is not a name).
static QString worldNameForPopup() {
    return DependencyManager::get<AddressManager>()->getDomainLabel().trimmed();
}

void Application::domainConnectionRefused(const QString& reasonMessage, int reasonCodeInt, const QString& extraInfo) {
    DomainHandler::ConnectionRefusedReason reasonCode = static_cast<DomainHandler::ConnectionRefusedReason>(reasonCodeInt);

    if (reasonCode == DomainHandler::ConnectionRefusedReason::TooManyUsers && !extraInfo.isEmpty()) {
        DependencyManager::get<AddressManager>()->handleLookupString(extraInfo);
        return;
    }

    switch (reasonCode) {
        case DomainHandler::ConnectionRefusedReason::ProtocolMismatch:
        case DomainHandler::ConnectionRefusedReason::TooManyUsers:
        case DomainHandler::ConnectionRefusedReason::Unknown: {
            const QString world = worldNameForPopup();
            QString why = reasonMessage.trimmed();
            if (reasonCode == DomainHandler::ConnectionRefusedReason::TooManyUsers) {
                why = "It's full. Try again later.";
            } else if (reasonCode == DomainHandler::ConnectionRefusedReason::ProtocolMismatch) {
                why = "It runs a different version of Darkride.";
            }
            OffscreenUi::asyncWarning("Couldn't join " + (world.isEmpty() ? QString("this world") : world), why);
            getMyAvatar()->setWorldVelocity(glm::vec3(0.0f));
            break;
        }
        default:
            // nothing to do.
            break;
    }
}

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

void Application::toggleTabletUI(bool shouldOpen) const {
    auto hmd = DependencyManager::get<HMDScriptingInterface>();
    if (!(shouldOpen && hmd->getShouldShowTablet())) {
        auto HMD = DependencyManager::get<HMDScriptingInterface>();
        HMD->toggleShouldShowTablet();

        if (!HMD->getShouldShowTablet()) {
            DependencyManager::get<Keyboard>()->setRaised(false);
            _window->activateWindow();
            auto tablet = DependencyManager::get<TabletScriptingInterface>()->getTablet(SYSTEM_TABLET);
            tablet->unfocus();
        }
    }
}

void Application::checkChangeCursor() {
    QMutexLocker locker(&_changeCursorLock);
    if (_cursorNeedsChanging) {
#ifdef Q_OS_MACOS
        auto cursorTarget = _window; // macOS doesn't seem to provide for hiding the cursor only on the GL widget
#else
        // On windows and linux, hiding the top level cursor also means it's invisible when hovering over the
        // window menu, which is a pain, so only hide it for the GL surface
        auto cursorTarget = _glWidget;
#endif
        cursorTarget->setCursor(_desiredCursor);

        _cursorNeedsChanging = false;
    }
}

void Application::showCursor(const Cursor::Icon& cursor) {
    QMutexLocker locker(&_changeCursorLock);

    auto managedCursor = Cursor::Manager::instance().getCursor();
    auto curIcon = managedCursor->getIcon();
    if (curIcon != cursor) {
        managedCursor->setIcon(cursor);
        curIcon = cursor;
    }
    _desiredCursor = cursor == Cursor::Icon::SYSTEM ? Qt::ArrowCursor : Qt::BlankCursor;
    _cursorNeedsChanging = true;
}

void Application::updateSystemCursor(const Qt::CursorShape cursor) {
    QMutexLocker locker(&_changeCursorLock);

    if (_desiredCursor != Qt::BlankCursor && _desiredCursor != cursor) {
        _desiredCursor = cursor;
        _cursorNeedsChanging = true;
    }
}

void Application::updateHeartbeat() const {
    DeadlockWatchdogThread::updateHeartbeat();
}

// The shutdown sequence (onAboutToQuit, cleanupBeforeQuit, ~Application) is in ApplicationShutdown.cpp.

void Application::initializeGL() {
    qCDebug(interfaceapp) << "Created Display Window.";


    // initialize glut for shape drawing; Qt apparently initializes it on OS X
    if (_isGLInitialized) {
        return;
    } else {
        _isGLInitialized = true;
    }

    _glWidget->windowHandle()->setFormat(getDefaultOpenGLSurfaceFormat());

    // When loading QtWebEngineWidgets, it creates a global share context on startup.
    // We have to account for this possibility by checking here for an existing
    // global share context
    auto globalShareContext = qt_gl_global_share_context();

    // Build a shared canvas / context for the Chromium processes
    if (!globalShareContext) {
        StartupProfiler::Scope step("initializeGL: Chromium share context");
        // Chromium rendering uses some GL functions that prevent nSight from capturing
        // frames, so we only create the shared context if nsight is NOT active.
        if (!nsightActive()) {
            _chromiumShareContext = new OffscreenGLCanvas();
            _chromiumShareContext->setObjectName("ChromiumShareContext");
            auto format =QSurfaceFormat::defaultFormat();
#ifdef Q_OS_MACOS
            // On mac, the primary shared OpenGL context must be a 3.2 core context,
            // or chromium flips out and spews error spam (but renders fine)
            format.setMajorVersion(3);
            format.setMinorVersion(2);
#endif
            _chromiumShareContext->setFormat(format);
            _chromiumShareContext->create();
            if (!_chromiumShareContext->makeCurrent()) {
                qCWarning(interfaceapp, "Unable to make chromium shared context current");
            }
            globalShareContext = _chromiumShareContext->getContext();
            qt_gl_set_global_share_context(globalShareContext);
            _chromiumShareContext->doneCurrent();
        }
    }


    {
        StartupProfiler::Scope step("initializeGL: window context");
        _glWidget->createContext(globalShareContext);

        if (!_glWidget->makeCurrent()) {
            qCWarning(interfaceapp, "Unable to make window context current");
        }

        // Populate the global OpenGL context based on the information for the primary window GL context
        gl::ContextInfo::get(true);
    }

    QStringList chromiumFlags;
    // HACK: re-expose mic and camera to prevent crash on domain-change in chromium's media::FakeAudioInputStream::ReadAudioFromSource()
    // This isn't necessary because there's a permission popup when the webcam is requested 
    // chromiumFlags << "--use-fake-device-for-media-stream";

    // Disable signed distance field font rendering on ATI/AMD GPUs, due to
    // a High Fidelity bug report: text drawn white in the web views on those GPUs
    std::string vendor{ (const char*)glGetString(GL_VENDOR) };
    if ((vendor.find("AMD") != std::string::npos) || (vendor.find("ATI") != std::string::npos)) {
        chromiumFlags << "--disable-distance-field-text";
    }

    // Enable autoplay
    chromiumFlags << "--autoplay-policy=no-user-gesture-required";

    // Enable this for debugging chromium
    // chromiumFlags << "--enable-logging" << "--log-level=0" << "--v=1";

    // Ensure all Qt webengine processes launched from us have the appropriate command line flags
    if (!chromiumFlags.empty()) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags.join(' ').toLocal8Bit());
    }

    if (!globalShareContext) {
        globalShareContext = _glWidget->qglContext();
        qt_gl_set_global_share_context(globalShareContext);
    }

    // Build a shared canvas / context for the QML rendering
    {
        StartupProfiler::Scope step("initializeGL: QML share context");
        _qmlShareContext = new OffscreenGLCanvas();
        _qmlShareContext->setObjectName("QmlShareContext");
        _qmlShareContext->create(globalShareContext);
        if (!_qmlShareContext->makeCurrent()) {
            qCWarning(interfaceapp, "Unable to make QML shared context current");
        }
        OffscreenQmlSurface::setSharedContext(_qmlShareContext->getContext());
        _qmlShareContext->doneCurrent();
        if (!_glWidget->makeCurrent()) {
            qCWarning(interfaceapp, "Unable to make window context current");
        }
    }


    // Build an offscreen GL context for the main thread.
    _glWidget->makeCurrent();
    glClearColor(0.2f, 0.2f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    _glWidget->swapBuffers();

    {
        StartupProfiler::Scope step("initializeGL: GPU context and render thread (GraphicsEngine::initializeGPU)");
        _graphicsEngine.initializeGPU(_glWidget);
    }
}

void Application::initializeDisplayPlugins() {
    auto displayPlugins = PluginManager::getInstance()->getDisplayPlugins();
    Setting::Handle<QString> activeDisplayPluginSetting{ ACTIVE_DISPLAY_PLUGIN_SETTING_NAME, displayPlugins.at(0)->getName() };
    auto lastActiveDisplayPluginName = activeDisplayPluginSetting.get();

    auto defaultDisplayPlugin = displayPlugins.at(0);
    // Once time initialization code
    DisplayPluginPointer targetDisplayPlugin;
    foreach(auto displayPlugin, displayPlugins) {
        displayPlugin->setContext(_graphicsEngine.getGPUContext());
        if (displayPlugin->getName() == lastActiveDisplayPluginName) {
            targetDisplayPlugin = displayPlugin;
        }

        if (!_autoSwitchDisplayModeSupportedHMDPlugin) {
            if (displayPlugin->isHmd() && displayPlugin->getSupportsAutoSwitch()) {
                _autoSwitchDisplayModeSupportedHMDPlugin = displayPlugin;
                _autoSwitchDisplayModeSupportedHMDPluginName = _autoSwitchDisplayModeSupportedHMDPlugin->getName();
                _previousHMDWornStatus = _autoSwitchDisplayModeSupportedHMDPlugin->isDisplayVisible() && _autoSwitchDisplayModeSupportedHMDPlugin->isActive();
            }
        }

        QObject::connect(displayPlugin.get(), &DisplayPlugin::recommendedFramebufferSizeChanged,
            [this](const QSize& size) { resizeGL(); });
        QObject::connect(displayPlugin.get(), &DisplayPlugin::resetSensorsRequested, this, &Application::requestReset);

        if (displayPlugin->isHmd()) {
            auto hmdDisplayPlugin = dynamic_cast<HmdDisplayPlugin*>(displayPlugin.get());
            QObject::connect(hmdDisplayPlugin, &HmdDisplayPlugin::hmdMountedChanged,
                DependencyManager::get<HMDScriptingInterface>().data(), &HMDScriptingInterface::mountedChanged);
        }
    }

    // The default display plugin needs to be activated first, otherwise the display plugin thread
    // may be launched by an external plugin, which is bad
    {
        StartupProfiler::Scope step("initializeDisplayPlugins: default display plugin activated (the present thread)");
        setDisplayPlugin(defaultDisplayPlugin);
    }

    // Now set the desired plugin if it's not the same as the default plugin
    if (targetDisplayPlugin && (targetDisplayPlugin != defaultDisplayPlugin)) {
        StartupProfiler::Scope step("initializeDisplayPlugins: the saved display plugin activated");
        setDisplayPlugin(targetDisplayPlugin);
    }

    if (_autoSwitchDisplayModeSupportedHMDPlugin) {
        if (getActiveDisplayPlugin() != _autoSwitchDisplayModeSupportedHMDPlugin && !_autoSwitchDisplayModeSupportedHMDPlugin->isSessionActive()) {
            startHMDStandBySession();
        }
        // Poll periodically to check whether the user has worn HMD or not. Switch Display mode accordingly.
        // If the user wears HMD then switch to VR mode. If the user removes HMD then switch to Desktop mode.
        QTimer* autoSwitchDisplayModeTimer = new QTimer(this);
        connect(autoSwitchDisplayModeTimer, SIGNAL(timeout()), this, SLOT(switchDisplayMode()));
        autoSwitchDisplayModeTimer->start(INTERVAL_TO_CHECK_HMD_WORN_STATUS);
    }

    // Submit a default frame to render until the engine starts up
    updateRenderArgs(0.0f);
}

void Application::initializeRenderEngine() {
    // FIXME: on low end systems os the shaders take up to 1 minute to compile, so we pause the deadlock watchdog thread.
    DeadlockWatchdogThread::withPause([&] {
        {
            StartupProfiler::Scope step("initializeRenderEngine: the render engine and its pipelines (GraphicsEngine::initializeRender)");
            _graphicsEngine.initializeRender();
        }
        DependencyManager::get<Keyboard>()->registerKeyboardHighlighting();
    });
}

extern void setupPreferences();
static void addDisplayPluginToMenu(const DisplayPluginPointer& displayPlugin, int index, bool active = false);

void Application::showLoginScreen() {
    // Signed in or not, the sign-in screen comes first: a remembered account ("Remember me") gets "Continue as <name>"
    // there (LoginDialog/ContinueBody.qml), and goes in from it.
    auto dialogsManager = DependencyManager::get<DialogsManager>();
    if (!isHMDMode()) {
        auto toolbar =  DependencyManager::get<ToolbarScriptingInterface>()->getToolbar("com.highfidelity.interface.toolbar.system");
        toolbar->writeProperty("visible", false);
    }
    _loginDialogPoppedUp = true;
    loginDialogPoppedUp.set(_loginDialogPoppedUp);
    dialogsManager->showLoginDialog();
    StartupProfiler::instant("sign-in dialog opened");
    emit loginDialogFocusEnabled();
    _window->setWindowTitle(labelledTitle("Darkride"));
}

void Application::initializeUi() {

    // Allow remote QML content from trusted sources ONLY
    {
        auto newValidator = [=](const QUrl& url)->bool {
            auto pathStr = url.toString().toLower();
            if (pathStr.contains(":/") && !(pathStr.startsWith("file:/") || pathStr.startsWith("qrc:/"))) {
                return false;
            }
            return true;
        };
        OffscreenQmlSurface::setUrlValidator(newValidator);
    }

    AddressBarDialog::registerType();
    ErrorDialog::registerType();
    LoginDialog::registerType();
    Tooltip::registerType();
    QmlContextCallback platformInfoCallback = [](QQmlContext* context) {
        context->setContextProperty("PlatformInfo", new PlatformInfoScriptingInterface());
    };
    OffscreenQmlSurface::addWhitelistContextHandler({
        QUrl{ "hifi/tablet/TabletAddressDialog.qml" },
        QUrl{ "hifi/Card.qml" },
        QUrl{ "hifi/Pal.qml" },
        QUrl{ "hifi/NameCard.qml" },
        }, platformInfoCallback);

    QmlContextCallback ttsCallback = [](QQmlContext* context) {
        context->setContextProperty("TextToSpeech", DependencyManager::get<TTSScriptingInterface>().data());
    };
    OffscreenQmlSurface::addWhitelistContextHandler({
        QUrl{ "hifi/tts/TTS.qml" }
    }, ttsCallback);
    qmlRegisterType<ResourceImageItem>("Hifi", 1, 0, "ResourceImageItem");
    qmlRegisterType<Preference>("Hifi", 1, 0, "Preference");
    qmlRegisterType<WebBrowserSuggestionsEngine>("HifiWeb", 1, 0, "WebBrowserSuggestionsEngine");

    {
        StartupProfiler::Scope step("initializeUi: the system tablet");
        auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
        tabletScriptingInterface->getTablet(SYSTEM_TABLET);
    }

    auto offscreenUi = getOffscreenUI();
    connect(offscreenUi.data(), &hifi::qml::OffscreenSurface::rootContextCreated,
        this, &Application::onDesktopRootContextCreated);
    connect(offscreenUi.data(), &hifi::qml::OffscreenSurface::rootItemCreated,
        this, &Application::onDesktopRootItemCreated);

    offscreenUi->setProxyWindow(_window->windowHandle());
    // OffscreenUi is a subclass of OffscreenQmlSurface specifically designed to
    // support the window management and scripting proxies for VR use
    DeadlockWatchdogThread::withPause([&] {
        StartupProfiler::Scope step("initializeUi: Desktop.qml (OffscreenUi::createDesktop)");
        offscreenUi->createDesktop(PathUtils::qmlUrl("hifi/Desktop.qml"));
    });
    // FIXME either expose so that dialogs can set this themselves or
    // do better detection in the offscreen UI of what has focus
    offscreenUi->setNavigationFocused(false);

    {
        StartupProfiler::Scope step("initializeUi: preferences");
        setupPreferences();
    }

    _glWidget->installEventFilter(offscreenUi.data());
    offscreenUi->setMouseTranslator([=](const QPointF& pt) {
        QPointF result = pt;
        auto displayPlugin = getActiveDisplayPlugin();
        if (displayPlugin->isHmd()) {
            getApplicationCompositor().handleRealMouseMoveEvent(false);
            auto resultVec = getApplicationCompositor().getReticlePosition();
            result = QPointF(resultVec.x, resultVec.y);
        }
        return result.toPoint();
    });

    // BUGZ-1365 - the root context should explicitly default to being unable to load local HTML content
    ContextAwareProfile::restrictContext(offscreenUi->getSurfaceContext(), true);
    offscreenUi->resume();
    connect(_window, &MainWindow::windowGeometryChanged, [this](const QRect& r){
        resizeGL();
        if (_touchscreenVirtualPadDevice) {
            _touchscreenVirtualPadDevice->resize();
        }
    });

    // This will set up the input plugins UI
    foreach(auto inputPlugin, PluginManager::getInstance()->getInputPlugins()) {
        if (KeyboardMouseDevice::NAME == inputPlugin->getName()) {
            _keyboardMouseDevice = std::dynamic_pointer_cast<KeyboardMouseDevice>(inputPlugin);
        }
        if (TouchscreenDevice::NAME == inputPlugin->getName()) {
            _touchscreenDevice = std::dynamic_pointer_cast<TouchscreenDevice>(inputPlugin);
        }
        if (TouchscreenVirtualPadDevice::NAME == inputPlugin->getName()) {
            _touchscreenVirtualPadDevice = std::dynamic_pointer_cast<TouchscreenVirtualPadDevice>(inputPlugin);
#if defined(ANDROID_APP_INTERFACE)
            auto& virtualPadManager = VirtualPad::Manager::instance();
            connect(&virtualPadManager, &VirtualPad::Manager::hapticFeedbackRequested,
                    this, [](int duration) {
                        AndroidHelper::instance().performHapticFeedback(duration);
                    });
#endif
        }
    }

    auto compositorHelper = DependencyManager::get<CompositorHelper>();
    connect(compositorHelper.data(), &CompositorHelper::allowMouseCaptureChanged, this, [=] {
        if (isHMDMode()) {
            auto compositorHelper = DependencyManager::get<CompositorHelper>(); // don't capture outer smartpointer
            showCursor(compositorHelper->getAllowMouseCapture() ?
                       Cursor::Manager::lookupIcon(_preferredCursor.get()) :
                       Cursor::Icon::SYSTEM);
        }
    });

    // Pre-create a couple of offscreen surfaces to speed up tablet UI
    auto offscreenSurfaceCache = DependencyManager::get<OffscreenQmlSurfaceCache>();
    offscreenSurfaceCache->setOnRootContextCreated([&](const QString& rootObject, QQmlContext* surfaceContext) {
        if (rootObject == TabletScriptingInterface::QML) {
            // in Qt 5.10.0 there is already an "Audio" object in the QML context
            // though I failed to find it (from QtMultimedia??). So..  let it be "AudioScriptingInterface"
            surfaceContext->setContextProperty("AudioScriptingInterface", DependencyManager::get<AudioScriptingInterface>().data());
            surfaceContext->setContextProperty("Account", AccountServicesScriptingInterface::getInstance()); // DEPRECATED - TO BE REMOVED
        }
    });

    {
        StartupProfiler::Scope step("initializeUi: offscreen surfaces reserved (one tablet, two web)");
        offscreenSurfaceCache->reserve(TabletScriptingInterface::QML, 1);
        offscreenSurfaceCache->reserve(render::entities::WebEntityRenderer::QML, 2);
    }

    {
        StartupProfiler::Scope step("initializeUi: the menu (flushMenuUpdates)");
        flushMenuUpdates();
    }

    // Now that the menu is instantiated, ensure the display plugin menu is properly updated
    {
        auto displayPlugins = PluginManager::getInstance()->getDisplayPlugins();
        // first sort the plugins into groupings: standard, advanced, developer
        std::stable_sort(displayPlugins.begin(), displayPlugins.end(),
            [](const DisplayPluginPointer& a, const DisplayPluginPointer& b) -> bool { return a->getGrouping() < b->getGrouping(); });
        int dpIndex = 1;
        // concatenate the groupings into a single list in the order: standard, advanced, developer
        for(const auto& displayPlugin : displayPlugins) {
            addDisplayPluginToMenu(displayPlugin, dpIndex, _displayPlugin == displayPlugin);
            dpIndex++;
        }

        // after all plugins have been added to the menu, add a separator to the menu
        auto parent = getPrimaryMenu()->getMenu(MenuOption::OutputMenu);
        parent->addSeparator();
    }

    // The display plugins are created before the menu now, so we need to do this here to hide the menu bar
    // now that it exists
    if (_window && _window->isFullScreen()) {
        setFullscreen(nullptr, true);
    }

    setIsInterstitialMode(true);
}


// The desktop's QML context (onDesktopRootContextCreated, onDesktopRootItemCreated) is in ApplicationScripting.cpp.

void Application::userKickConfirmation(const QUuid& nodeID) {
    auto avatarHashMap = DependencyManager::get<AvatarHashMap>();
    auto avatar = avatarHashMap->getAvatarBySessionID(nodeID);

    QString userName = avatar ? avatar->getSessionDisplayName().trimmed() : QString();
    const QString world = worldNameForPopup();

    // A ban keeps them out until it is lifted, which is what the People app's Ban button asks for.
    ModalDialogListener* dlg = OffscreenUi::asyncQuestion(
        "Ban " + (userName.isEmpty() ? QString("this person") : userName) + " from " +
            (world.isEmpty() ? QString("this world") : world) + "?",
        (userName.isEmpty() ? QString("They") : userName) + " can't come back until you lift the ban in the world settings.",
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

    if (QQuickItem* box = dlg->getDialogItem()) {
        OffscreenUi::setButtonText(box, QMessageBox::Yes, "Ban");
        box->setProperty("destructiveButton", (int)QMessageBox::Yes);

        QObject::connect(dlg, &ModalDialogListener::response, this, [=] (QVariant answer) {
            QObject::disconnect(dlg, &ModalDialogListener::response, this, nullptr);

            bool yes = (static_cast<QMessageBox::StandardButton>(answer.toInt()) == QMessageBox::Yes);
            // ask the NodeList to kick the user with the given session ID

            if (yes) {
                DependencyManager::get<NodeList>()->kickNodeBySessionID(nodeID);
            }

            DependencyManager::get<UsersScriptingInterface>()->setWaitForKickResponse(false);
        });
        DependencyManager::get<UsersScriptingInterface>()->setWaitForKickResponse(true);
    }
}

// The context properties of every other QML surface (setupQmlSurface) are in ApplicationScripting.cpp.

// The camera's per-frame update (updateCamera) is in ApplicationFrame.cpp.

void Application::runTests() {
    runTimingTests();
    runUnitTests();
}

void Application::setFieldOfView(float fov) {
    if (fov != _fieldOfView.get()) {
        _fieldOfView.set(fov);
        resizeGL();
    }
}

void Application::setFarClip(float farClip) {
    if (farClip != _farClip.get()) {
        _farClip.set(farClip);
    }
}

void Application::setNearClip(float nearClip) {
    if (nearClip != _nearClip.get()) {
        _nearClip.set(nearClip);
        resizeGL();   // the projection is rebuilt there
    }
}

void Application::setHMDTabletScale(float hmdTabletScale) {
    _hmdTabletScale.set(hmdTabletScale);
}

void Application::setDesktopTabletScale(float desktopTabletScale) {
    _desktopTabletScale.set(desktopTabletScale);
}

void Application::setDesktopTabletBecomesToolbarSetting(bool value) {
    _desktopTabletBecomesToolbarSetting.set(value);
    updateSystemTabletMode();
}

void Application::setHmdTabletBecomesToolbarSetting(bool value) {
    _hmdTabletBecomesToolbarSetting.set(value);
    updateSystemTabletMode();
}

void Application::setPreferStylusOverLaser(bool value) {
    _preferStylusOverLaserSetting.set(value);
}

void Application::setPreferAvatarFingerOverStylus(bool value) {
    _preferAvatarFingerOverStylusSetting.set(value);
}

void Application::setPreferredCursor(const QString& cursorName) {
    qCDebug(interfaceapp) << "setPreferredCursor" << cursorName;

    if (_displayPlugin && _displayPlugin->isHmd()) {
        _preferredCursor.set(cursorName.isEmpty() ? DEFAULT_CURSOR_NAME : cursorName);
    }
    else { 
        _preferredCursor.set(cursorName.isEmpty() ? Cursor::Manager::getIconName(Cursor::Icon::SYSTEM) : cursorName); 
    }

    showCursor(Cursor::Manager::lookupIcon(_preferredCursor.get()));
}

void Application::setSettingConstrainToolbarPosition(bool setting) {
    _constrainToolbarPosition.set(setting);
    getOffscreenUI()->setConstrainToolbarToCenterX(setting);
}

void Application::setAwayStateWhenFocusLostInVREnabled(bool enabled) {
    _awayStateWhenFocusLostInVREnabled.set(enabled);
    emit awayStateWhenFocusLostInVRChanged(enabled);
}

void Application::setMiniTabletEnabled(bool enabled) {
    _miniTabletEnabledSetting.set(enabled);
    emit miniTabletEnabledChanged(enabled);
}

void Application::showHelp() {
    static const QString HAND_CONTROLLER_NAME_VIVE = "vive";
    static const QString HAND_CONTROLLER_NAME_OCULUS_TOUCH = "oculus";
    static const QString HAND_CONTROLLER_NAME_WINDOWS_MR = "windowsMR";

    static const QString VIVE_PLUGIN_NAME = "HTC Vive";
    static const QString OCULUS_RIFT_PLUGIN_NAME = "Oculus Rift";
    static const QString WINDOWS_MR_PLUGIN_NAME = "WindowsMR";

    static const QString TAB_KEYBOARD_MOUSE = "kbm";
    static const QString TAB_GAMEPAD = "gamepad";
    static const QString TAB_HAND_CONTROLLERS = "handControllers";

    QString handControllerName;
    QString defaultTab = TAB_KEYBOARD_MOUSE;

    if (PluginUtils::isHMDAvailable(WINDOWS_MR_PLUGIN_NAME)) {
        defaultTab = TAB_HAND_CONTROLLERS;
        handControllerName = HAND_CONTROLLER_NAME_WINDOWS_MR;
    } else if (PluginUtils::isHMDAvailable(VIVE_PLUGIN_NAME)) {
        defaultTab = TAB_HAND_CONTROLLERS;
        handControllerName = HAND_CONTROLLER_NAME_VIVE;
    } else if (PluginUtils::isHMDAvailable(OCULUS_RIFT_PLUGIN_NAME)) {
        if (PluginUtils::isOculusTouchControllerAvailable()) {
            defaultTab = TAB_HAND_CONTROLLERS;
            handControllerName = HAND_CONTROLLER_NAME_OCULUS_TOUCH;
        } else if (PluginUtils::isXboxControllerAvailable()) {
            defaultTab = TAB_GAMEPAD;
        } else {
            defaultTab = TAB_KEYBOARD_MOUSE;
        }
    } else if (PluginUtils::isXboxControllerAvailable()) {
        defaultTab = TAB_GAMEPAD;
    } else {
        defaultTab = TAB_KEYBOARD_MOUSE;
    }

    QUrlQuery queryString;
    queryString.addQueryItem("handControllerName", handControllerName);
    queryString.addQueryItem("defaultTab", defaultTab);
    TabletProxy* tablet = dynamic_cast<TabletProxy*>(DependencyManager::get<TabletScriptingInterface>()->getTablet(SYSTEM_TABLET));
    tablet->gotoWebScreen(PathUtils::resourcesUrl() + INFO_HELP_PATH + "?" + queryString.toString());
    DependencyManager::get<HMDScriptingInterface>()->openTablet();
}

void Application::resizeEvent(QResizeEvent* event) {
    // in build mode the ViewportContainer holds the GL view's size while a panel splitter is
    // dragged and resizes it once at the end, so by the time this runs the size is final
    resizeGL();
}

// The field-of-view setting is a horizontal angle and the vertical one is derived from the aspect
// ratio, which runs away on a tall, narrow viewport (build mode's panels) and flips past 180
// degrees. Build mode keeps the vertical angle at its 16:9 value, so a narrower viewport crops
// the sides instead, as Unity's scene view does.
float verticalFieldOfView(float horizontalFov, float aspectRatio) {   // ApplicationFrame.cpp reads it too
    auto editor = DependencyManager::get<Editor>();
    if (editor && editor->isActive()) {
        return horizontalFov / (16.0f / 9.0f);
    }
    return horizontalFov / aspectRatio;
}

void Application::resizeGL() {
    PROFILE_RANGE(render, __FUNCTION__);
    if (nullptr == _displayPlugin) {
        return;
    }

    auto displayPlugin = getActiveDisplayPlugin();
    // Set the desired FBO texture size. If it hasn't changed, this does nothing.
    // Otherwise, it must rebuild the FBOs
    uvec2 framebufferSize = displayPlugin->getRecommendedRenderSize();
    uvec2 renderSize = uvec2(framebufferSize);
    if (_renderResolution != renderSize) {
        _renderResolution = renderSize;
        DependencyManager::get<FramebufferCache>()->setFrameBufferSize(fromGlm(renderSize));
    }

    // FIXME the aspect ratio for stereo displays is incorrect based on this.
    float aspectRatio = displayPlugin->getRecommendedAspectRatio();
    float fovY = verticalFieldOfView(_fieldOfView.get(), aspectRatio);
    _myCamera.setProjection(
        glm::perspective(
            glm::radians(fovY), aspectRatio, getNearClip(), getFarClip()
        )
    );
    // Possible change in aspect ratio
    {
        QMutexLocker viewLocker(&_viewMutex);
        _myCamera.loadViewFrustum(_viewFrustum);
    }

    getOffscreenUI()->resize(fromGlm(displayPlugin->getRecommendedUiSize()));
}

void Application::goToStartingLocation() {
    PROFILE_RANGE(render, __FUNCTION__);

    // Get controller availability
#ifdef ANDROID_APP_QUEST_INTERFACE
    bool hasHandControllers = true;
#else
    bool hasHandControllers = false;
    if (PluginUtils::isViveControllerAvailable() || PluginUtils::isOculusTouchControllerAvailable()) {
        hasHandControllers = true;
    }
#endif

    // Check HMD use (may be technically available without being in use)
    bool hasHMD = PluginUtils::isHMDAvailable();
    bool isUsingHMD = _displayPlugin->isHmd();
    bool isUsingHMDAndHandControllers = hasHMD && hasHandControllers && isUsingHMD;

    qCDebug(interfaceapp) << "HMD:" << hasHMD << ", Hand Controllers: " << hasHandControllers << ", Using HMD: " << isUsingHMDAndHandControllers;

    QString addressLookupString;

    // when --url in command line, teleport to location
    QCommandLineParser parser;
    QCommandLineOption urlOption("url", "", "value");
    parser.addOption(urlOption);
    parser.parse(arguments());
    if (parser.isSet(urlOption)) {
        QUrl url = QUrl(parser.value(urlOption));
        if (url.scheme() == URL_SCHEME_HIFIAPP) {
            Setting::Handle<QVariant>("startUpApp").set(url.path());
        } else {
            addressLookupString = url.toString();
        }
    }

    // If this is a first run we short-circuit the address passed in
    if (_firstRun.get()) {
        if (!_overrideEntry) {
            DependencyManager::get<AddressManager>()->goToEntry();
        } else {
            DependencyManager::get<AddressManager>()->loadSettings(addressLookupString);
        }
       _firstRun.set(false);
    } else {
        QString goingTo = "";
        if (addressLookupString.isEmpty()) {
            if (Menu::getInstance()->isOptionChecked(MenuOption::HomeLocation)) {
                auto locationBookmarks = DependencyManager::get<LocationBookmarks>();
                addressLookupString = locationBookmarks->addressForBookmark(LocationBookmarks::HOME_BOOKMARK);
                goingTo = "home location";
            } else {
                goingTo = "previous location";
            }
        }
        qCDebug(interfaceapp) << "Not first run... going to" << qPrintable(!goingTo.isEmpty() ? goingTo : addressLookupString);
        DependencyManager::get<AddressManager>()->loadSettings(addressLookupString);
    }
    
    _connectionMonitor.init();
}

bool Application::importJSONFromURL(const QString& urlString) {
    // we only load files that terminate in just .json (not .svo.json and not .ava.json)
    QUrl jsonURL { urlString };

    emit svoImportRequested(urlString);
    return true;
}

bool Application::importSVOFromURL(const QString& urlString) {
    emit svoImportRequested(urlString);
    return true;
}

bool Application::importFromZIP(const QString& filePath) {
    qDebug() << "A zip file has been dropped in: " << filePath;
    QUrl empty;
    qApp->getFileDownloadInterface()->runUnzip(filePath, empty, true, true, false);
    return true;
}

bool Application::isServerlessMode() const {
    auto tree = getEntities()->getTree();
    if (tree) {
        return tree->isServerlessMode();
    }
    return false;
}

void Application::setIsInterstitialMode(bool interstitialMode) {
    bool enableInterstitial = DependencyManager::get<NodeList>()->getDomainHandler().getInterstitialModeEnabled();
    if (enableInterstitial) {
        if (_interstitialMode != interstitialMode) {
            _interstitialMode = interstitialMode;
            emit interstitialModeChanged(_interstitialMode);

            DependencyManager::get<AudioClient>()->setAudioPaused(_interstitialMode);
            DependencyManager::get<AvatarManager>()->setMyAvatarDataPacketsPaused(_interstitialMode);
        }
    }
}

void Application::setIsServerlessMode(bool serverlessDomain) {
    DependencyManager::get<NodeList>()->setSendDomainServerCheckInEnabled(!serverlessDomain);
    auto tree = getEntities()->getTree();
    if (tree) {
        tree->setIsServerlessMode(serverlessDomain);
    }
}

std::map<QString, QString> Application::prepareServerlessDomainContents(QUrl domainURL, QByteArray data) {
    QUuid serverlessSessionID = QUuid::createUuid();
    getMyAvatar()->setSessionUUID(serverlessSessionID);
    auto nodeList = DependencyManager::get<NodeList>();
    nodeList->setSessionUUID(serverlessSessionID);

    // there is no domain-server to tell us our permissions, so enable all
    NodePermissions permissions;
    permissions.setAll(true);
    nodeList->setPermissions(permissions);

    // FIXME: Lock the main tree and import directly into it.
    EntityTreePointer tmpTree(new EntityTree());
    tmpTree->setIsServerlessMode(true);
    tmpTree->createRootElement();
    auto myAvatar = getMyAvatar();
    tmpTree->setMyAvatar(myAvatar);
    bool success = tmpTree->readFromByteArray(domainURL.toString(), data);
    if (success) {
        tmpTree->reaverageOctreeElements();
        tmpTree->sendEntities(&_entityEditSender, getEntities()->getTree(), "domain", 0, 0, 0);
    }
    std::map<QString, QString> namedPaths = tmpTree->getNamedPaths();

    // we must manually eraseAllOctreeElements(false) else the tmpTree will mem-leak
    tmpTree->eraseAllOctreeElements(false);

    return namedPaths;
}

void Application::loadServerlessDomain(QUrl domainURL) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "loadServerlessDomain", Q_ARG(QUrl, domainURL));
        return;
    }

    if (domainURL.isEmpty()) {
        return;
    }

    QString trimmedUrl = domainURL.toString().trimmed();
    bool DEFAULT_IS_OBSERVABLE = true;
    const qint64 DEFAULT_CALLER_ID = -1;
    auto request = DependencyManager::get<ResourceManager>()->createResourceRequest(
        this, trimmedUrl, DEFAULT_IS_OBSERVABLE, DEFAULT_CALLER_ID, "Application::loadServerlessDomain");

    if (!request) {
        return;
    }

    connect(request, &ResourceRequest::finished, this, [=]() {
        if (request->getResult() == ResourceRequest::Success) {
            auto namedPaths = prepareServerlessDomainContents(domainURL, request->getData());
            auto nodeList = DependencyManager::get<NodeList>();
            nodeList->getDomainHandler().connectedToServerless(namedPaths);
            _fullSceneReceivedCounter++;
        }
        request->deleteLater();
    });
    request->send();
}

void Application::loadErrorDomain(QUrl domainURL) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "loadErrorDomain", Q_ARG(QUrl, domainURL));
        return;
    }

    loadServerlessDomain(domainURL);
}

bool Application::importImage(const QString& urlString) {
    qCDebug(interfaceapp) << "An image file has been dropped in";
    QString filepath(urlString);
    filepath.remove("file:///");
    addAssetToWorld(filepath, "", false, false);
    return true;
}

// thread-safe
void Application::onPresent(quint32 frameCount) {
    bool expected = false;
    if (_pendingIdleEvent.compare_exchange_strong(expected, true)) {
        postEvent(this, new QEvent((QEvent::Type)ApplicationEvent::Idle), Qt::HighEventPriority);
    }
    expected = false;
    if (_graphicsEngine.checkPendingRenderEvent() && !isAboutToQuit()) {
        postEvent(_graphicsEngine._renderEventHandler, new QEvent((QEvent::Type)ApplicationEvent::Render));
    }
}

// The event routing (notify, event, eventFilter, the key, mouse, touch, wheel and drop handlers) is in
// ApplicationEvents.cpp.

// This is currently not used, but could be invoked if the user wants to go to the place embedded in an
// Interface-taken snapshot. (It was developed for drag and drop, before we had asset-server loading or in-world browsers.)
bool Application::acceptSnapshot(const QString& urlString) {
    QUrl url(urlString);
    QString snapshotPath = url.toLocalFile();

    SnapshotMetaData* snapshotData = DependencyManager::get<Snapshot>()->parseSnapshotData(snapshotPath);
    if (snapshotData) {
        if (!snapshotData->getURL().toString().isEmpty()) {
            DependencyManager::get<AddressManager>()->handleLookupString(snapshotData->getURL().toString());
        }
    } else {
        OffscreenUi::asyncWarning("", "No location details were found in the file\n" +
                             snapshotPath + "\nTry dragging in a Darkride Snap saved on this computer.");
    }
    return true;
}

// The frame loop's tick (idle, with the Windows CPU monitor it starts) is in ApplicationFrame.cpp.

ivec2 Application::getMouse() const {
    return getApplicationCompositor().getReticlePosition();
}

bool Application::exportEntities(const QString& filename,
                                 const QVector<QUuid>& entityIDs,
                                 const glm::vec3* givenOffset) {
    QHash<EntityItemID, EntityItemPointer> entities;

    auto nodeList = DependencyManager::get<NodeList>();
    const QUuid myAvatarID = nodeList->getSessionUUID();

    auto entityTree = getEntities()->getTree();
    auto exportTree = std::make_shared<EntityTree>();
    exportTree->setMyAvatar(getMyAvatar());
    exportTree->createRootElement();
    glm::vec3 root(TREE_SCALE, TREE_SCALE, TREE_SCALE);
    bool success = true;
    entityTree->withReadLock([entityIDs, entityTree, givenOffset, myAvatarID, &root, &entities, &success, &exportTree] {
        for (auto entityID : entityIDs) { // Gather entities and properties.
            auto entityItem = entityTree->findEntityByEntityItemID(entityID);
            if (!entityItem) {
                qCWarning(interfaceapp) << "Skipping export of" << entityID << "that is not in scene.";
                continue;
            }

            if (!givenOffset) {
                EntityItemID parentID = entityItem->getParentID();
                bool parentIsAvatar = (parentID == AVATAR_SELF_ID || parentID == myAvatarID);
                if (!parentIsAvatar && (parentID.isInvalidID() ||
                                        !entityIDs.contains(parentID) ||
                                        !entityTree->findEntityByEntityItemID(parentID))) {
                    // If parent wasn't selected, we want absolute position, which isn't in properties.
                    auto position = entityItem->getWorldPosition();
                    root.x = glm::min(root.x, position.x);
                    root.y = glm::min(root.y, position.y);
                    root.z = glm::min(root.z, position.z);
                }
            }
            entities[entityID] = entityItem;
        }

        if (entities.size() == 0) {
            success = false;
            return;
        }

        if (givenOffset) {
            root = *givenOffset;
        }
        for (EntityItemPointer& entityDatum : entities) {
            auto properties = entityDatum->getProperties();
            EntityItemID parentID = properties.getParentID();
            bool parentIsAvatar = (parentID == AVATAR_SELF_ID || parentID == myAvatarID);
            if (parentIsAvatar) {
                properties.setParentID(AVATAR_SELF_ID);
            } else {
                if (parentID.isInvalidID()) {
                    properties.setPosition(properties.getPosition() - root);
                } else if (!entities.contains(parentID)) {
                    entityDatum->globalizeProperties(properties, "Parent %3 of %2 %1 is not selected for export.", -root);
                } // else valid parent -- don't offset
            }
            exportTree->addEntity(entityDatum->getEntityItemID(), properties);
        }
    });
    if (success) {
        success = exportTree->writeToJSONFile(filename.toLocal8Bit().constData());

        // restore the main window's active state
        _window->activateWindow();
    }
    return success;
}

bool Application::exportEntities(const QString& filename, float x, float y, float z, float scale) {
    glm::vec3 center(x, y, z);
    glm::vec3 minCorner = center - vec3(scale);
    float cubeSize = scale * 2;
    AACube boundingCube(minCorner, cubeSize);
    QVector<QUuid> entities;
    auto entityTree = getEntities()->getTree();
    entityTree->withReadLock([&] {
        entityTree->evalEntitiesInCube(boundingCube, PickFilter(), entities);
    });
    return exportEntities(filename, entities, &center);
}

void Application::loadSettings() {

    {
        StartupProfiler::Scope step("loadSettings: audio and LOD");
        DependencyManager::get<AudioClient>()->loadSettings();
        DependencyManager::get<LODManager>()->loadSettings();
    }

    auto menu = Menu::getInstance();
    {
        StartupProfiler::Scope step("loadSettings: menu");
        menu->loadSettings();
    }

    // override the menu option show overlays to always be true on startup
    menu->setIsOptionChecked(MenuOption::Overlays, true);

    int displayPluginStep = StartupProfiler::begin("loadSettings: display plugin from the menu");
    // If there is a preferred plugin, we probably messed it up with the menu settings, so fix it.
    auto pluginManager = PluginManager::getInstance();
    auto plugins = pluginManager->getPreferredDisplayPlugins();
    if (plugins.size() > 0) {
        for (auto plugin : plugins) {
            if (auto action = menu->getActionForOption(plugin->getName())) {
                action->setChecked(true);
                action->trigger();
                // Find and activated highest priority plugin, bail for the rest
                break;
            }
        }
    }

    bool isFirstPerson = false;
    auto displayPlugins = pluginManager->getDisplayPlugins();
    for (auto& plugin : displayPlugins) {
        if (!plugin->isHmd()) {
            if (auto action = menu->getActionForOption(plugin->getName())) {
                action->setChecked(true);
                action->trigger();
                break;
            }
        }
    }
    isFirstPerson = (qApp->isHMDMode());
    StartupProfiler::end(displayPluginStep);

    // Load settings of the RenderScritpingInterface
    // Do that explicitely before being used
    {
        StartupProfiler::Scope step("loadSettings: render settings");
        RenderScriptingInterface::getInstance()->loadSettings();
    }

    // Setup the PerformanceManager which will enforce the several settings to match the Preset
    // On the first run, the Preset is evaluated from the 
    {
        StartupProfiler::Scope step("loadSettings: performance preset");
        getPerformanceManager().setupPerformancePresetSettings(_firstRun.get());
    }

    // finish initializing the camera, based on everything we checked above. Third person camera will be used if no settings
    // dictated that we should be in first person
    Menu::getInstance()->setIsOptionChecked(MenuOption::FirstPersonLookAt, isFirstPerson);
    Menu::getInstance()->setIsOptionChecked(MenuOption::ThirdPerson, !isFirstPerson);
    _myCamera.setMode((isFirstPerson) ? CAMERA_MODE_FIRST_PERSON_LOOK_AT : CAMERA_MODE_LOOK_AT);
    cameraMenuChanged();

    {
        StartupProfiler::Scope step("loadSettings: input plugins activated");
        auto inputs = pluginManager->getInputPlugins();
        for (auto plugin : inputs) {
            if (!plugin->isActive()) {
                plugin->activate();
            }
        }
    }

    QSharedPointer<scripting::Audio> audioScriptingInterface = qSharedPointerDynamicCast<scripting::Audio>(DependencyManager::get<AudioScriptingInterface>());
    if (audioScriptingInterface) {
        audioScriptingInterface->loadData();
    }

    {
        StartupProfiler::Scope step("loadSettings: avatar");
        getMyAvatar()->loadData();
    }
    _settingsLoaded = true;
}

void Application::saveSettings() const {
    DependencyManager::get<AudioClient>()->saveSettings();
    DependencyManager::get<LODManager>()->saveSettings();

    QSharedPointer<scripting::Audio> audioScriptingInterface = qSharedPointerDynamicCast<scripting::Audio>(DependencyManager::get<AudioScriptingInterface>());
    if (audioScriptingInterface) {
        audioScriptingInterface->saveData();
    }

    Menu::getInstance()->saveSettings();
    getMyAvatar()->saveData();
    PluginManager::getInstance()->saveSettings();
}

bool Application::importEntities(const QString& urlOrFilename, const bool isObservable, const qint64 callerId) {
    bool success = false;
    _entityClipboard->withWriteLock([&] {
        _entityClipboard->eraseAllOctreeElements();

        // FIXME: readFromURL() can take over the main event loop which may cause problems, especially if downloading the JSON 
        // from the Web.
        success = _entityClipboard->readFromURL(urlOrFilename, isObservable, callerId, true);
        if (success) {
            _entityClipboard->reaverageOctreeElements();
        }
    });
    return success;
}

QVector<EntityItemID> Application::pasteEntities(const QString& entityHostType, float x, float y, float z) {
    return _entityClipboard->sendEntities(&_entityEditSender, getEntities()->getTree(), entityHostType, x, y, z);
}

void Application::init() {
    // Make sure Login state is up to date
    DependencyManager::get<DialogsManager>()->toggleLoginDialog();
    {
        StartupProfiler::Scope step("init: avatar manager");
        DependencyManager::get<AvatarManager>()->init();
    }

    _lastTimeUpdated.start();

    qCDebug(interfaceapp) << "Loaded settings";

    // fire off an immediate domain-server check in now that settings are loaded
    QMetaObject::invokeMethod(DependencyManager::get<NodeList>().data(), "sendDomainServerCheckIn");

    // This allows collision to be set up properly for shape entities supported by GeometryCache.
    // This is before entity setup to ensure that it's ready for whenever instance collision is initialized.
    ShapeEntityItem::setShapeInfoCalulator(ShapeEntityItem::ShapeInfoCalculator(&shapeInfoCalculator));

    {
        StartupProfiler::Scope step("init: entity tree renderer");
        getEntities()->init();
    }
    getEntities()->setEntityLoadingPriorityFunction([this, editor = DependencyManager::get<Editor>()](const EntityItem& item) {
        auto dims = item.getScaledDimensions();
        auto maxSize = glm::compMax(dims);

        if (maxSize <= 0.0f) {
            return 0.0f;
        }

        auto distance = glm::distance(getMyAvatar()->getWorldPosition(), item.getWorldPosition());
        // in build mode the camera roams far from the avatar: what it looks at loads first
        if (editor && editor->isActive()) {
            distance = std::min(distance, glm::distance(_myCamera.getPosition(), item.getWorldPosition()));
        }
        return atan2(maxSize, distance);
    });

    ObjectMotionState::setShapeManager(&_shapeManager);
    {
        StartupProfiler::Scope step("init: physics engine");
        _physicsEngine->init();
    }

    EntityTreePointer tree = getEntities()->getTree();
    {
        StartupProfiler::Scope step("init: entity simulation");
        _entitySimulation->init(tree, _physicsEngine, &_entityEditSender);
    }
    tree->setSimulation(_entitySimulation);

    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();

    // connect the _entityCollisionSystem to our EntityTreeRenderer since that's what handles running entity scripts
    connect(_entitySimulation.get(), &PhysicalEntitySimulation::entityCollisionWithEntity,
            getEntities().data(), &EntityTreeRenderer::entityCollisionWithEntity);

     // connect the _entities (EntityTreeRenderer) to our script engine's EntityScriptingInterface for firing
    // of events related clicking, hovering over, and entering entities
    getEntities()->connectSignalsToSlots(entityScriptingInterface.data());

    // Make sure any new sounds are loaded as soon as know about them.
    connect(tree.get(), &EntityTree::newCollisionSoundURL, this, [this](QUrl newURL, EntityItemID id) {
        getEntities()->setCollisionSound(id, DependencyManager::get<SoundCache>()->getSound(newURL));
    }, Qt::QueuedConnection);
    connect(getMyAvatar().get(), &MyAvatar::newCollisionSoundURL, this, [this](QUrl newURL) {
        if (auto avatar = getMyAvatar()) {
            auto sound = DependencyManager::get<SoundCache>()->getSound(newURL);
            avatar->setCollisionSound(sound);
        }
    }, Qt::QueuedConnection);

    {
        StartupProfiler::Scope step("init: game workload");
        _gameWorkload.startup(getEntities()->getWorkloadSpace(), _graphicsEngine.getRenderScene(), _entitySimulation);
    }
    _entitySimulation->setWorkloadSpace(getEntities()->getWorkloadSpace());
}

void Application::pauseUntilLoginDetermined() {
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, "pauseUntilLoginDetermined");
        return;
    }

    _signInPending = true;

    auto myAvatar = getMyAvatar();
    _previousAvatarTargetScale = myAvatar->getTargetScale();
    _previousAvatarSkeletonModel = myAvatar->getSkeletonModelURL().toString();
    myAvatar->setTargetScale(1.0f);
    myAvatar->setSkeletonModelURLFromScript(myAvatar->defaultFullAvatarModelUrl().toString());
    myAvatar->setEnableMeshVisible(false);

    _controllerScriptingInterface->disableMapping(STANDARD_TO_ACTION_MAPPING_NAME);

    {
        auto userInputMapper = DependencyManager::get<UserInputMapper>();
        if (userInputMapper->loadMapping(NO_MOVEMENT_MAPPING_JSON)) {
            _controllerScriptingInterface->enableMapping(NO_MOVEMENT_MAPPING_NAME);
        }
    }

    const auto& nodeList = DependencyManager::get<NodeList>();
    // save interstitial mode setting until resuming.
    _interstitialModeEnabled = nodeList->getDomainHandler().getInterstitialModeEnabled();
    nodeList->getDomainHandler().setInterstitialModeEnabled(false);

    auto menu = Menu::getInstance();
    menu->getMenu("Edit")->setVisible(false);
    menu->getMenu("View")->setVisible(false);
    menu->getMenu("Navigate")->setVisible(false);
    menu->getMenu("Settings")->setVisible(false);
    _developerMenuVisible = menu->getMenu("Developer")->isVisible();
    menu->setIsOptionChecked(MenuOption::Stats, false);
    if (_developerMenuVisible) {
        menu->getMenu("Developer")->setVisible(false);
    }
    _previousCameraMode = _myCamera.getMode();
    _myCamera.setMode(CAMERA_MODE_FIRST_PERSON_LOOK_AT);
    cameraModeChanged();

    // disconnect domain handler.
    nodeList->getDomainHandler().disconnect("Pause until login determined");

    // From now on, it's permissible to call resumeAfterLoginDialogActionTaken()
    _resumeAfterLoginDialogActionTaken_SafeToRun = true;

    if (_resumeAfterLoginDialogActionTaken_WasPostponed) {
        // resumeAfterLoginDialogActionTaken() was already called, but it aborted. Now it's safe to call it again.
        resumeAfterLoginDialogActionTaken();
    }
}

void Application::resumeAfterLoginDialogActionTaken() {
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, "resumeAfterLoginDialogActionTaken");
        return;
    }

    if (!_resumeAfterLoginDialogActionTaken_SafeToRun) {
        _resumeAfterLoginDialogActionTaken_WasPostponed = true;
        return;
    }

    if (!isHMDMode() && getDesktopTabletBecomesToolbarSetting()) {
        auto toolbar = DependencyManager::get<ToolbarScriptingInterface>()->getToolbar("com.highfidelity.interface.toolbar.system");
        toolbar->writeProperty("visible", true);
    } else {
        getApplicationCompositor().getReticleInterface()->setAllowMouseCapture(true);
        getApplicationCompositor().getReticleInterface()->setVisible(true);
    }

    updateSystemTabletMode();

    {
        auto userInputMapper = DependencyManager::get<UserInputMapper>();
        userInputMapper->unloadMapping(NO_MOVEMENT_MAPPING_JSON);
        _controllerScriptingInterface->disableMapping(NO_MOVEMENT_MAPPING_NAME);
    }

    auto myAvatar = getMyAvatar();
    myAvatar->setTargetScale(_previousAvatarTargetScale);
    myAvatar->setSkeletonModelURLFromScript(_previousAvatarSkeletonModel);
    myAvatar->setEnableMeshVisible(true);

    _controllerScriptingInterface->enableMapping(STANDARD_TO_ACTION_MAPPING_NAME);

    const auto& nodeList = DependencyManager::get<NodeList>();
    nodeList->getDomainHandler().setInterstitialModeEnabled(_interstitialModeEnabled);
    {
        auto scriptEngines = DependencyManager::get<ScriptEngines>().data();
        // this will force the model the look at the correct directory (weird order of operations issue)
        scriptEngines->reloadLocalFiles();

        // if the --scripts command-line argument was used.
        if (_defaultScriptsLocation.exists() && !ClientOptions::get().scriptsPath.isEmpty()) {
            scriptEngines->loadDefaultScripts();
            scriptEngines->defaultScriptsLocationOverridden(true);
        } else {
            scriptEngines->loadScripts();
        }
    }

    auto accountManager = DependencyManager::get<AccountManager>();
    auto addressManager = DependencyManager::get<AddressManager>();

    // restart domain handler.
    nodeList->getDomainHandler().resetting();

    QVariant testProperty = property(hifi::properties::TEST);
    if (testProperty.isValid()) {
        const auto testScript = property(hifi::properties::TEST).toUrl();
        // Set last parameter to exit interface when the test script finishes, if so requested
        DependencyManager::get<ScriptEngines>()->loadScript(testScript, false, false, false, false, quitWhenFinished);
        // This is done so we don't get a "connection time-out" message when we haven't passed in a URL.
        if (ClientOptions::get().hasUrl) {
            QMetaObject::invokeMethod(this, "goToStartingLocation", Qt::QueuedConnection);
        }
    } else {
        QMetaObject::invokeMethod(this, "goToStartingLocation", Qt::QueuedConnection);
    }

    auto menu = Menu::getInstance();
    menu->getMenu("Edit")->setVisible(true);
    menu->getMenu("View")->setVisible(true);
    menu->getMenu("Navigate")->setVisible(true);
    menu->getMenu("Settings")->setVisible(true);
    menu->getMenu("Developer")->setVisible(_developerMenuVisible);
    _myCamera.setMode(_previousCameraMode);
    cameraModeChanged();
    _startUpFinished = true;
    StartupProfiler::instant("signed in: scripts loading, going to the starting location");
    getRefreshRateManager().setRefreshRateRegime(RefreshRateManager::RefreshRateRegime::FOCUS_ACTIVE);
    _signInPending = false;
}

void Application::loadAvatarScripts(const QVector<QString>& urls) {
    auto scriptEngines = DependencyManager::get<ScriptEngines>();
    auto runningScripts = scriptEngines->getRunningScripts();
    for (auto url : urls) {
        int index = runningScripts.indexOf(url);
        if (index < 0) {
            auto scriptEnginePointer = scriptEngines->loadScript(url, false);
            if (scriptEnginePointer) {
                scriptEnginePointer->setType(ScriptEngine::Type::AVATAR);
            }
        }
    }
}

void Application::unloadAvatarScripts() {
    auto scriptEngines = DependencyManager::get<ScriptEngines>();
    auto urls = scriptEngines->getRunningScripts();
    for (auto url : urls) {
        auto scriptEngine = scriptEngines->getScriptEngine(url);
        if (scriptEngine->getType() == ScriptEngine::Type::AVATAR) {
            scriptEngines->stopScript(url, false);
        }
    }
}

// The per-frame helpers (updateLOD, pushPostUpdateLambda, updateMyAvatarLookAtPosition, updateThreads) are in ApplicationFrame.cpp.

void Application::toggleOverlays() {
    auto menu = Menu::getInstance();
    menu->setIsOptionChecked(MenuOption::Overlays, !menu->isOptionChecked(MenuOption::Overlays));
}

void Application::setOverlaysVisible(bool visible) {
    auto menu = Menu::getInstance();
    menu->setIsOptionChecked(MenuOption::Overlays, visible);
}

void Application::centerUI() {
    _overlayConductor.centerUI();
}

void Application::cycleCamera() {
    auto menu = Menu::getInstance();
    if (menu->isOptionChecked(MenuOption::FirstPersonLookAt)) {

        menu->setIsOptionChecked(MenuOption::FirstPersonLookAt, false);
        menu->setIsOptionChecked(MenuOption::LookAtCamera, true);

    } else if (menu->isOptionChecked(MenuOption::LookAtCamera)) {

        menu->setIsOptionChecked(MenuOption::LookAtCamera, false);
        if (menu->getActionForOption(MenuOption::SelfieCamera)->isVisible()) {
            menu->setIsOptionChecked(MenuOption::SelfieCamera, true);
        } else {
            menu->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);
        }

    } else if (menu->isOptionChecked(MenuOption::SelfieCamera)) {

        menu->setIsOptionChecked(MenuOption::SelfieCamera, false);
        menu->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);

    }
    cameraMenuChanged(); // handle the menu change
}

void Application::cameraModeChanged() {
    switch (_myCamera.getMode()) {
        case CAMERA_MODE_FIRST_PERSON_LOOK_AT:
            Menu::getInstance()->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);
            break;
        case CAMERA_MODE_LOOK_AT:
            Menu::getInstance()->setIsOptionChecked(MenuOption::LookAtCamera, true);
            break;
        case CAMERA_MODE_SELFIE:
            Menu::getInstance()->setIsOptionChecked(MenuOption::SelfieCamera, true);
            break;
        default:
            // we don't have menu items for the others, so just leave it alone.
            return;
    }
    cameraMenuChanged();
}

void Application::changeViewAsNeeded(float boomLength) {
    // Switch between first and third person views as needed
    // This is called when the boom length has changed
    bool boomLengthGreaterThanMinimum = (boomLength > MyAvatar::ZOOM_MIN);

    if (
        (
            _myCamera.getMode() == CAMERA_MODE_FIRST_PERSON_LOOK_AT ||
            _myCamera.getMode() == CAMERA_MODE_FIRST_PERSON
        ) && boomLengthGreaterThanMinimum
    ) {
        Menu::getInstance()->setIsOptionChecked(MenuOption::FirstPersonLookAt, false);
        Menu::getInstance()->setIsOptionChecked(MenuOption::LookAtCamera, true);
        cameraMenuChanged();
    } else if (
        (
            _myCamera.getMode() == CAMERA_MODE_LOOK_AT ||
            _myCamera.getMode() == CAMERA_MODE_THIRD_PERSON
        ) && !boomLengthGreaterThanMinimum
    ) {
        Menu::getInstance()->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);
        Menu::getInstance()->setIsOptionChecked(MenuOption::LookAtCamera, false);
        cameraMenuChanged();
    }
}

void Application::cameraMenuChanged() {
    auto menu = Menu::getInstance();
    if (menu->isOptionChecked(MenuOption::FirstPersonLookAt)) {
        if (_myCamera.getMode() != CAMERA_MODE_FIRST_PERSON_LOOK_AT) {
            _myCamera.setMode(CAMERA_MODE_FIRST_PERSON_LOOK_AT);
            getMyAvatar()->setBoomLength(MyAvatar::ZOOM_MIN);
        }
    } else if (menu->isOptionChecked(MenuOption::LookAtCamera)) {
        if (_myCamera.getMode() != CAMERA_MODE_LOOK_AT) {
            _myCamera.setMode(CAMERA_MODE_LOOK_AT);
            if (getMyAvatar()->getBoomLength() == MyAvatar::ZOOM_MIN) {
                getMyAvatar()->setBoomLength(MyAvatar::ZOOM_DEFAULT);
            }
        }
    } else if (menu->isOptionChecked(MenuOption::SelfieCamera)) {
        if (_myCamera.getMode() != CAMERA_MODE_SELFIE) {
            _myCamera.setMode(CAMERA_MODE_SELFIE);
            if (getMyAvatar()->getBoomLength() == MyAvatar::ZOOM_MIN) {
                getMyAvatar()->setBoomLength(MyAvatar::ZOOM_DEFAULT);
            }
        }
    }
}

void Application::requeryOctree() {      
    DependencyManager::get<EntityTreeRenderer>()->forceRecheckEntities();
    auto now = SteadyClock::now();
    static const std::chrono::seconds MIN_PERIOD_BETWEEN_QUERIES { 3 };
    _queryExpiry = SteadyClock::now();
    _octreeQuery.incrementConnectionID();
    QJsonObject queryJSONParameters;
    QJsonObject queryFlags;
    queryFlags["includeDescendants"] = true;
    queryFlags["includeAncestors"] = true;
    queryJSONParameters["flags"] = queryFlags;
    queryOctree(
        NodeType::EntityServer,
        PacketType::EntityQuery,
        queryJSONParameters
    );    
    _queryExpiry = now + MIN_PERIOD_BETWEEN_QUERIES;
}

void Application::resetPhysicsReadyInformation() {
    // we've changed domains or cleared out caches or something.  we no longer know enough about the
    // collision information of nearby entities to make running bullet be safe.
    _fullSceneReceivedCounter = 0;
    _fullSceneCounterAtLastPhysicsCheck = 0;
    _gpuTextureMemSizeStabilityCount = 0;
    _gpuTextureMemSizeAtLastCheck = 0;
    _physicsEnabled = false;
    _octreeProcessor.stopSafeLanding();
}

void Application::reloadResourceCaches() {
    resetPhysicsReadyInformation();

    // Query the octree to refresh everything in view
    _queryExpiry = SteadyClock::now();
    _octreeQuery.incrementConnectionID();

    QJsonObject queryJSONParameters;
    QJsonObject queryFlags;
    queryFlags["includeDescendants"] = true;
    queryFlags["includeAncestors"] = true;
    queryJSONParameters["flags"] = queryFlags;
    queryOctree(
        NodeType::EntityServer,
        PacketType::EntityQuery,
        queryJSONParameters
    );

    getMyAvatar()->prepareAvatarEntityDataForReload();
    // Clear the entities and their renderables
    getEntities()->clear();

    DependencyManager::get<AssetClient>()->clearCache();
    DependencyManager::get<ScriptCache>()->clearCache();

    // Clear all the resource caches
    DependencyManager::get<ResourceCacheSharedItems>()->clear();
    DependencyManager::get<AnimationCache>()->refreshAll();
    DependencyManager::get<SoundCache>()->refreshAll();
    DependencyManager::get<MaterialCache>()->refreshAll();
    DependencyManager::get<ModelCache>()->refreshAll();
    ShaderCache::instance().refreshAll();
    DependencyManager::get<TextureCache>()->refreshAll();
    DependencyManager::get<recording::ClipCache>()->refreshAll();

    DependencyManager::get<NodeList>()->reset("Reloading resources");  // Force redownload of .fst models

    DependencyManager::get<ScriptEngines>()->reloadAllScripts();
    getOffscreenUI()->clearCache();

    DependencyManager::get<Keyboard>()->createKeyboard();

    getMyAvatar()->resetFullAvatarURL();
}

void Application::rotationModeChanged() const {
    if (!Menu::getInstance()->isOptionChecked(MenuOption::CenterPlayerInView)) {
        getMyAvatar()->setHeadPitch(0);
    }
}

void Application::setKeyboardFocusHighlight(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& dimensions) {
    if (qApp->getLoginDialogPoppedUp()) {
        return;
    }

    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
    if (_keyboardFocusHighlightID == UNKNOWN_ENTITY_ID || !entityScriptingInterface->isAddedEntity(_keyboardFocusHighlightID)) {
        EntityItemProperties properties;
        properties.setType(EntityTypes::Box);
        properties.setAlpha(1.0f);
        properties.setColor({ 0xFF, 0xEF, 0x00 });
        properties.setPrimitiveMode(PrimitiveMode::LINES);
        properties.getPulse().setMin(0.5);
        properties.getPulse().setMax(1.0f);
        properties.getPulse().setColorMode(PulseMode::IN_PHASE);
        properties.setIgnorePickIntersection(true);
        _keyboardFocusHighlightID = entityScriptingInterface->addEntityInternal(properties, entity::HostType::LOCAL);
    }

    // Position focus
    EntityItemProperties properties;
    properties.setPosition(position);
    properties.setRotation(rotation);
    properties.setDimensions(dimensions);
    properties.setVisible(true);
    entityScriptingInterface->editEntity(_keyboardFocusHighlightID, properties);
}

QUuid Application::getKeyboardFocusEntity() const {
    return _keyboardFocusedEntity.get();
}

void Application::setKeyboardFocusEntity(const QUuid& id) {
    if (_keyboardFocusedEntity.get() != id) {
        if (qApp->getLoginDialogPoppedUp() && !_loginDialogID.isNull()) {
            if (id == _loginDialogID) {
                emit loginDialogFocusEnabled();
            } else if (!_keyboardFocusWaitingOnRenderable) {
                // that's the only entity we want in focus;
                return;
            }
        }

        _keyboardFocusedEntity.set(id);

        auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
        if (id != UNKNOWN_ENTITY_ID) {
            EntityPropertyFlags desiredProperties;
            desiredProperties += PROP_VISIBLE;
            desiredProperties += PROP_LOCALLY_VISIBLE; // TIVOLI new
            desiredProperties += PROP_SHOW_KEYBOARD_FOCUS_HIGHLIGHT;
            auto properties = entityScriptingInterface->getEntityProperties(id);
            if (properties.getVisible()) {
                auto entities = getEntities();
                auto entityId = _keyboardFocusedEntity.get();
                auto entityItemRenderable = entities->renderableForEntityId(entityId);
                if (!entityItemRenderable) {
                    _keyboardFocusWaitingOnRenderable = true;
                } else if (entityItemRenderable->wantsKeyboardFocus()) {
                    entities->setProxyWindow(entityId, _window->windowHandle());
                    if (_keyboardMouseDevice->isActive()) {
                        _keyboardMouseDevice->pluginFocusOutEvent();
                    }
                    _lastAcceptedKeyPress = usecTimestampNow();

                    if (properties.getShowKeyboardFocusHighlight()) {
                        if (auto entity = entities->getEntity(entityId)) {
                            setKeyboardFocusHighlight(entity->getWorldPosition(), entity->getWorldOrientation(),
                                entity->getScaledDimensions() * FOCUS_HIGHLIGHT_EXPANSION_FACTOR);
                            return;
                        }
                    }
                }
            }
        }

        EntityItemProperties properties;
        properties.setVisible(false);
        entityScriptingInterface->editEntity(_keyboardFocusHighlightID, properties);
    }
}

// The simulation step and what frames it (updateDialogs, updateSecondaryCameraViewFrustum, the physics gate
// setArrivalWaitsForNearby/tryToEnablePhysics, update, updateRenderArgs) is in ApplicationFrame.cpp.

void Application::queryAvatars() {
    if (!isInterstitialMode()) {
        auto avatarPacket = NLPacket::create(PacketType::AvatarQuery);
        auto destinationBuffer = reinterpret_cast<unsigned char*>(avatarPacket->getPayload());
        unsigned char* bufferStart = destinationBuffer;

        uint8_t numFrustums = (uint8_t)_conicalViews.size();
        memcpy(destinationBuffer, &numFrustums, sizeof(numFrustums));
        destinationBuffer += sizeof(numFrustums);

        for (const auto& view : _conicalViews) {
            destinationBuffer += view.serialize(destinationBuffer);
        }

        avatarPacket->setPayloadSize(destinationBuffer - bufferStart);

        DependencyManager::get<NodeList>()->broadcastToNodes(std::move(avatarPacket), NodeSet() << NodeType::AvatarMixer);
    }
}


int Application::sendNackPackets() {

    // iterates through all nodes in NodeList
    auto nodeList = DependencyManager::get<NodeList>();

    int packetsSent = 0;

    nodeList->eachNode([&](const SharedNodePointer& node){

        if (node->getActiveSocket() && node->getType() == NodeType::EntityServer) {

            auto nackPacketList = NLPacketList::create(PacketType::OctreeDataNack);

            QUuid nodeUUID = node->getUUID();

            // if there are octree packets from this node that are waiting to be processed,
            // don't send a NACK since the missing packets may be among those waiting packets.
            if (_octreeProcessor.hasPacketsToProcessFrom(nodeUUID)) {
                return;
            }

            QSet<OCTREE_PACKET_SEQUENCE> missingSequenceNumbers;
            _octreeServerSceneStats.withReadLock([&] {
                // retrieve octree scene stats of this node
                if (_octreeServerSceneStats.find(nodeUUID) == _octreeServerSceneStats.end()) {
                    return;
                }
                // get sequence number stats of node, prune its missing set, and make a copy of the missing set
                SequenceNumberStats& sequenceNumberStats = _octreeServerSceneStats[nodeUUID].getIncomingOctreeSequenceNumberStats();
                sequenceNumberStats.pruneMissingSet();
                missingSequenceNumbers = sequenceNumberStats.getMissingSet();
            });

            _isMissingSequenceNumbers = (missingSequenceNumbers.size() != 0);

            // construct nack packet(s) for this node
            foreach(const OCTREE_PACKET_SEQUENCE& missingNumber, missingSequenceNumbers) {
                nackPacketList->writePrimitive(missingNumber);
            }

            if (nackPacketList->getNumPackets()) {
                packetsSent += (int)nackPacketList->getNumPackets();

                // send the packet list
                nodeList->sendPacketList(std::move(nackPacketList), *node);
            }
        }
    });

    return packetsSent;
}


void Application::queryOctree(
    NodeType_t serverType,
    PacketType packetType,
    QJsonObject queryJSONParameters
) {

    if (!_settingsLoaded) {
        return; // bail early if settings are not loaded
    }

    const bool isModifiedQuery = !_physicsEnabled;
    
    if (isModifiedQuery) {   

        if (!_octreeProcessor.safeLandingIsActive()) {
            // don't send the octreeQuery until SafeLanding knows it has started
            return;
        }
        // Create modified view that is a simple sphere.

        ConicalViewFrustum sphericalView;
        // the sphere is centred on where the avatar is now, not on the global bounding box: that box follows
        // _globalPosition, refreshed in simulateAttachments, so the query sent in the frame of a teleport (the
        // arrival's own path lookup) still asked around the old place, and the restarted landing had nothing of the
        // new place to wait for (WORLD-LOADING.md, the arrival gate)
        float radius = INITIAL_QUERY_RADIUS; //glm::max(INITIAL_QUERY_RADIUS, 350);//0.5 * glm::length(box.getDimensions())); 
        sphericalView.setPositionAndSimpleRadius(getMyAvatar()->getWorldPosition(), radius);

        auto menu = Menu::getInstance();
        if (menu && menu->isOptionChecked(MenuOption::LoadCompleteEntityTree)) {            // if Dev > Tivoli Options >>> LoadCompleteEntityTree truee

            ConicalViewFrustum farView;
            farView.set(_viewFrustum);

            QJsonObject queryFlags;
            queryFlags["includeDescendants"] = true; // forces entire tree
            queryFlags["includeAncestors"] = true; // forces entire tree
            queryFlags["serverScripts"] = true;

            queryJSONParameters["flags"] = queryFlags;
           // _octreeQuery->static_cast<EntityNodeData*>(node->getLinkedData()); //setShouldForceFullScene(true);
            _octreeQuery.clearConicalViews();                     // TIVOLI go frustumless
            _octreeQuery.setJSONParameters(queryJSONParameters);  // TIVOLI force ancestors and descendents
            _octreeQuery.setConicalViews({ sphericalView, farView });
        } else {
            _octreeQuery.setConicalViews({ sphericalView });
        }

        _octreeQuery.setOctreeSizeScale(DEFAULT_OCTREE_SIZE_SCALE);
        static constexpr float MIN_LOD_ADJUST = -20.0f;
        _octreeQuery.setBoundaryLevelAdjust(MIN_LOD_ADJUST);
    } else {
        _octreeQuery.setConicalViews(_conicalViews);
        auto lodManager = DependencyManager::get<LODManager>();
        _octreeQuery.setOctreeSizeScale(lodManager->getOctreeSizeScale());
        _octreeQuery.setBoundaryLevelAdjust(lodManager->getBoundaryLevelAdjust());
    }
    // ENTITY-STREAMING.md 3.3: this client draws impostor cards, so the entity server sizes a carded entity, and the
    // models under it, by its card and sends them out to where the card is drawn. One flag among the query's JSON
    // flags, kept whatever the paths above set (only the whole-tree option writes the JSON); a server that does not
    // know it sends as before. TIVOLI_NO_IMPOSTOR_CARDS in the environment leaves the flag out, to measure an older
    // client's stream against the same server (launch-editor.sh EDITOR_NO_IMPOSTOR_CARDS=1).
    static const bool SEND_IMPOSTOR_CARDS = !QProcessEnvironment::systemEnvironment().contains("TIVOLI_NO_IMPOSTOR_CARDS");
    if (SEND_IMPOSTOR_CARDS) {
        QJsonObject parameters = _octreeQuery.getJSONParameters();
        QJsonObject flags = parameters[EntityJSONQueryProperties::FLAGS_PROPERTY].toObject();
        if (!flags[EntityJSONQueryProperties::IMPOSTOR_CARDS_PROPERTY].toBool()) {
            flags[EntityJSONQueryProperties::IMPOSTOR_CARDS_PROPERTY] = true;
            parameters[EntityJSONQueryProperties::FLAGS_PROPERTY] = flags;
            _octreeQuery.setJSONParameters(parameters);
        }
    }
    _octreeQuery.setReportInitialCompletion(isModifiedQuery);

    auto treeRenderer = DependencyManager::get<EntityTreeRenderer>();

    auto nodeList = DependencyManager::get<NodeList>();
    auto node = nodeList->soloNodeOfType(serverType);

    if (node && node->getActiveSocket()) {
        _octreeQuery.setMaxQueryPacketsPerSecond(getMaxOctreePacketsPerSecond());

        auto queryPacket = NLPacket::create(packetType);

        // encode the query data
        auto packetData = reinterpret_cast<unsigned char*>(queryPacket->getPayload());
        int packetSize = _octreeQuery.getBroadcastData(packetData);
        queryPacket->setPayloadSize(packetSize);

        // TIVOLI_QUERY_DIAG in the environment: a log line per entity query sent, with the views it carries (the hunt
        // for entities that never reach a client, KNOWN-ISSUES.md). Off by default: nothing is formatted.
        static const bool QUERY_DIAG = QProcessEnvironment::systemEnvironment().contains("TIVOLI_QUERY_DIAG");
        if (QUERY_DIAG && serverType == NodeType::EntityServer) {
            QString views;
            for (const auto& view : _conicalViews) {
                const auto& p = view.getPosition();
                const auto& d = view.getDirection();
                views += QString(" [pos %1 %2 %3 dir %4 %5 %6 angle %7 far %8 radius %9]")
                    .arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2).arg(p.z, 0, 'f', 2)
                    .arg(d.x, 0, 'f', 3).arg(d.y, 0, 'f', 3).arg(d.z, 0, 'f', 3)
                    .arg(glm::degrees(view.getAngle()), 0, 'f', 1).arg(view.getFarClip(), 0, 'f', 0).arg(view.getRadius(), 0, 'f', 2);
            }
            qCDebug(interfaceapp).noquote() << "entity query:" << (isModifiedQuery ? "initial-sphere" : "views")
                << "views" << _conicalViews.size() << views
                << "lodAdjust" << _octreeQuery.getBoundaryLevelAdjust() << "sizeScale" << _octreeQuery.getOctreeSizeScale()
                << "pps" << _octreeQuery.getMaxQueryPacketsPerSecond() << "bytes" << packetSize;
        }

        // make sure we still have an active socket
        nodeList->sendUnreliablePacket(*queryPacket, *node);
    }
}

bool Application::isHMDMode() const {
    return getActiveDisplayPlugin()->isHmd();
}

float Application::getNumCollisionObjects() const {
    return _physicsEngine ? _physicsEngine->getNumCollisionObjects() : 0;
}

float Application::getTargetRenderFrameRate() const { return getActiveDisplayPlugin()->getTargetFrameRate(); }

QRect Application::getDesirableApplicationGeometry() const {
    QRect applicationGeometry = getWindow()->geometry();

    // If our parent window is on the HMD, then don't use its geometry, instead use
    // the "main screen" geometry.
    HMDToolsDialog* hmdTools = DependencyManager::get<DialogsManager>()->getHMDToolsDialog();
    if (hmdTools && hmdTools->hasHMDScreen()) {
        QScreen* hmdScreen = hmdTools->getHMDScreen();
        QWindow* appWindow = getWindow()->windowHandle();
        QScreen* appScreen = appWindow->screen();

        // if our app's screen is the hmd screen, we don't want to place the
        // running scripts widget on it. So we need to pick a better screen.
        // we will use the screen for the HMDTools since it's a guaranteed
        // better screen.
        if (appScreen == hmdScreen) {
            QScreen* betterScreen = hmdTools->windowHandle()->screen();
            applicationGeometry = betterScreen->geometry();
        }
    }
    return applicationGeometry;
}

PickRay Application::computePickRay(float x, float y) const {
    vec2 pickPoint { x, y };
    PickRay result;
    if (isHMDMode()) {
        getApplicationCompositor().computeHmdPickRay(pickPoint, result.origin, result.direction);
    } else {
        pickPoint /= getCanvasSize();
        if (_myCamera.getMode() == CameraMode::CAMERA_MODE_MIRROR) {
            pickPoint.x = 1.0f - pickPoint.x;
        }
        QMutexLocker viewLocker(&_viewMutex);
        _viewFrustum.computePickRay(pickPoint.x, pickPoint.y, result.origin, result.direction);
    }
    return result;
}

std::shared_ptr<MyAvatar> Application::getMyAvatar() const {
    return DependencyManager::get<AvatarManager>()->getMyAvatar();
}

glm::vec3 Application::getAvatarPosition() const {
    return getMyAvatar()->getWorldPosition();
}

void Application::copyViewFrustum(ViewFrustum& viewOut) const {
    QMutexLocker viewLocker(&_viewMutex);
    viewOut = _viewFrustum;
}

void Application::copyDisplayViewFrustum(ViewFrustum& viewOut) const {
    QMutexLocker viewLocker(&_viewMutex);
    viewOut = _displayViewFrustum;
}

// resentSensors() is a bit of vestigial feature. It used to be used for Oculus DK2 to recenter the view around
// the current head orientation.  With the introduction of "room scale" tracking we no longer need that particular
// feature.  However, we still use this to reset face trackers, eye trackers, audio and to optionally re-load the avatar
// rig and animations from scratch.
void Application::resetSensors(bool andReload) {
    _overlayConductor.centerUI();
    getActiveDisplayPlugin()->resetSensors();
    getMyAvatar()->reset(true, andReload);
    QMetaObject::invokeMethod(DependencyManager::get<AudioClient>().data(), "reset", Qt::QueuedConnection);
}

void Application::updateWindowTitle() const {

    const auto nodeList = DependencyManager::get<NodeList>();
    const auto accountManager = DependencyManager::get<AccountManager>();
    const auto addressManager = DependencyManager::get<AddressManager>();

    setCrashAnnotation("sentry[user][username]", accountManager->getAccountInfo().getUsername().toStdString());

    // Like a Mac document window, the title names what you're looking at: the world you're in, by its name, or
    // Darkride when you're in none or it has no name to show (an address is not a name). The account is in its
    // menu, the build in About and in the log line below, and a connection problem is shown in the window itself.
    QString title = "Darkride";
    auto& domainHandler = nodeList->getDomainHandler();
    if (!isServerlessMode() && domainHandler.isConnected() && !domainHandler.isInErrorState()) {
        const QString worldName = addressManager->getDomainLabel().trimmed();
        if (!worldName.isEmpty()) {
            title = worldName;
        }
    }
    // the client label first, when there is one (see clientLabel): with several sessions' clients on one machine, it
    // tells them apart at a glance (Caity, 2026-10-19)
    title = labelledTitle(title);
    // With several branches built on this machine, one per session, the title says which build this client is
    // ("Darkride - physics-net"), so the right window gets closed. Dev builds only; a release shows no branch.
    if (BuildInfo::BUILD_TYPE == BuildInfo::Dev && !BuildInfo::BUILD_BRANCH.isEmpty() && BuildInfo::BUILD_BRANCH != "HEAD") {
        title += " - " + BuildInfo::BUILD_BRANCH;
    }

#ifndef WIN32
    // crashes with vs2013/win32
    qCDebug(interfaceapp, "Application title set to: %s (build %s %s %s)", title.toStdString().c_str(),
            applicationVersion().toStdString().c_str(), BuildInfo::BUILD_BRANCH.toStdString().c_str(),
            BuildInfo::BUILD_COMMIT.toStdString().c_str());
#endif
    _window->setWindowTitle(title);

    // updateTitleWindow gets called whenever there's a change regarding the domain, so rather
    // than placing this within domainURLChanged, it's placed here to cover the other potential cases.
    DependencyManager::get< MessagesClient >()->sendLocalMessage("Toolbar-DomainChanged", "");


}

void Application::clearDomainOctreeDetails(bool clearAll) {
    // if we're about to quit, we really don't need to do the rest of these things...
    if (_aboutToQuit) {
        return;
    }

    qCDebug(interfaceapp) << "Clearing domain octree details...";

    resetPhysicsReadyInformation();
    setIsInterstitialMode(true);

    _octreeServerSceneStats.withWriteLock([&] {
        _octreeServerSceneStats.clear();
    });

    // reset the model renderer
    clearAll ? getEntities()->clear() : getEntities()->clearDomainAndNonOwnedEntities();

    DependencyManager::get<AnimationCache>()->clearUnusedResources();
    DependencyManager::get<SoundCache>()->clearUnusedResources();
    DependencyManager::get<MaterialCache>()->clearUnusedResources();
    DependencyManager::get<ModelCache>()->clearUnusedResources();
    ShaderCache::instance().clearUnusedResources();
    DependencyManager::get<TextureCache>()->clearUnusedResources();
    DependencyManager::get<recording::ClipCache>()->clearUnusedResources();
}

void Application::domainURLChanged(QUrl domainURL) {
    // disable physics until we have enough information about our new location to not cause craziness.
    setIsServerlessMode(domainURL.scheme() != URL_SCHEME_HIFI);
    if (isServerlessMode()) {
        loadServerlessDomain(domainURL);
    }
    updateWindowTitle();
}

void Application::goToErrorDomainURL(QUrl errorDomainURL) {
    // disable physics until we have enough information about our new location to not cause craziness.
    setIsServerlessMode(errorDomainURL.scheme() != URL_SCHEME_HIFI);
    if (isServerlessMode()) {
        loadErrorDomain(errorDomainURL);
    }
    updateWindowTitle();
}

void Application::resettingDomain() {
    _notifiedPacketVersionMismatchThisDomain = false;

    clearDomainOctreeDetails(false);
}

void Application::nodeAdded(SharedNodePointer node) {
    if (node->getType() == NodeType::EntityServer) {
        if (_failedToConnectToEntityServer && !_entityServerConnectionTimer.isActive()) {
            _octreeProcessor.stopSafeLanding();
            _failedToConnectToEntityServer = false;
        } else if (_entityServerConnectionTimer.isActive()) {
            _entityServerConnectionTimer.stop();
        }
        _octreeProcessor.startSafeLanding();
        _entityServerConnectionTimer.setInterval(ENTITY_SERVER_CONNECTION_TIMEOUT);
        _entityServerConnectionTimer.start();
    }
}

void Application::nodeActivated(SharedNodePointer node) {
    if (node->getType() == NodeType::AssetServer) {
        // asset server just connected - check if we have the asset browser showing

        auto offscreenUi = getOffscreenUI();
        if (offscreenUi) {
            auto nodeList = DependencyManager::get<NodeList>();

            if (nodeList->getThisNodeCanWriteAssets()) {
                // call reload on the shown asset browser dialog to get the mappings (if permissions allow)
                auto assetDialog = offscreenUi ? offscreenUi->getRootItem()->findChild<QQuickItem*>("AssetServer") : nullptr;
                if (assetDialog) {
                    QMetaObject::invokeMethod(assetDialog, "reload");
                }
            } else {
                // we switched to an Asset Server that we can't modify, hide the Asset Browser
                offscreenUi->hide("AssetServer");
            }
        }
    }

    // If we get a new EntityServer activated, reset lastQueried time
    // so we will do a proper query during update
    if (node->getType() == NodeType::EntityServer) {
        _queryExpiry = SteadyClock::now();
        _octreeQuery.incrementConnectionID();

        if  (!_failedToConnectToEntityServer) {
            _entityServerConnectionTimer.stop();
        }
    }

    if (node->getType() == NodeType::AudioMixer && !isInterstitialMode()) {
        DependencyManager::get<AudioClient>()->negotiateAudioFormat();
    }

    if (node->getType() == NodeType::AvatarMixer) {
        _queryExpiry = SteadyClock::now();

        // new avatar mixer, send off our identity packet on next update loop
        // Reset skeletonModelUrl if the last server modified our choice.
        // Override the avatar url (but not model name) here too.
        if (_avatarOverrideUrl.isValid()) {
            getMyAvatar()->useFullAvatarURL(_avatarOverrideUrl);
        }

        if (getMyAvatar()->getFullAvatarURLFromPreferences() != getMyAvatar()->getSkeletonModelURL()) {
            getMyAvatar()->resetFullAvatarURL();
        }
        getMyAvatar()->markIdentityDataChanged();
        getMyAvatar()->resetLastSent();

        if (!isInterstitialMode()) {
            // transmit a "sendAll" packet to the AvatarMixer we just connected to.
            getMyAvatar()->sendAvatarDataPacket(true);
        }
    }
}

void Application::nodeKilled(SharedNodePointer node) {
    // These are here because connecting NodeList::nodeKilled to OctreePacketProcessor::nodeKilled doesn't work:
    // OctreePacketProcessor::nodeKilled is not being called when NodeList::nodeKilled is emitted.
    // This may have to do with GenericThread::threadRoutine() blocking the QThread event loop

    _octreeProcessor.nodeKilled(node);

    _entityEditSender.nodeKilled(node);

    // Let's somehow compute a network reliability value here depending on 
    // the frequency of things getting lost and use that to determine when
    // to show the user a message that they need a better network connection

    if (node->getType() == NodeType::DomainServer) qDebug() <<"*** DOMAIN SERVER LOST ***" << usecTimestampNow();
    if (node->getType() == NodeType::AvatarMixer) qDebug() << "*** AVATAR MIXER LOST ***" << usecTimestampNow();
    if (node->getType() == NodeType::MessagesMixer) qDebug() << "*** MESSAGES MIXER LOST ***" << usecTimestampNow();
    if (node->getType() == NodeType::EntityScriptServer) qDebug() << "*** ENTITY SCRIPT SERVER LOST ***" << usecTimestampNow();
    if (node->getType() == NodeType::AudioMixer) {
        qDebug() << "*** AUDIO MIXER LOST ***" << usecTimestampNow();
        QMetaObject::invokeMethod(DependencyManager::get<AudioClient>().data(), "audioMixerKilled");
    } else if (node->getType() == NodeType::EntityServer) {
        clearDomainOctreeDetails(false); // CPM No. Terrible user experience.
        qDebug() << "*** ENTITY SERVER LOST *** " << usecTimestampNow();
    } else if (node->getType() == NodeType::AssetServer) {
        // asset server going away - check if we have the asset browser showing
        qDebug() << "*** ASSET SERVER LOST ***" << usecTimestampNow();

        auto offscreenUi = getOffscreenUI();
        auto assetDialog = offscreenUi ? offscreenUi->getRootItem()->findChild<QQuickItem*>("AssetServer") : nullptr;

        if (assetDialog) {
            // call reload on the shown asset browser dialog
            QMetaObject::invokeMethod(assetDialog, "clear");
        }
    }
}

void Application::trackIncomingOctreePacket(ReceivedMessage& message, SharedNodePointer sendingNode, bool wasStatsPacket) {
    // Attempt to identify the sender from its address.
    if (sendingNode) {
        const QUuid& nodeUUID = sendingNode->getUUID();

        // now that we know the node ID, let's add these stats to the stats for that node...
        _octreeServerSceneStats.withWriteLock([&] {
            if (_octreeServerSceneStats.find(nodeUUID) != _octreeServerSceneStats.end()) {
                OctreeSceneStats& stats = _octreeServerSceneStats[nodeUUID];
                stats.trackIncomingOctreePacket(message, wasStatsPacket, sendingNode->getClockSkewUsec());
            }
        });
    }
}

bool Application::gpuTextureMemSizeStable() {
    auto renderConfig = qApp->getRenderEngine()->getConfiguration();
    auto renderStats = renderConfig->getConfig<render::EngineStats>("Stats");

    qint64 textureResourceGPUMemSize = renderStats->textureResourceGPUMemSize;
    qint64 texturePopulatedGPUMemSize = renderStats->textureResourcePopulatedGPUMemSize;
    qint64 textureTransferSize = renderStats->texturePendingGPUTransferSize;

    if (_gpuTextureMemSizeAtLastCheck == textureResourceGPUMemSize) {
        _gpuTextureMemSizeStabilityCount++;
    } else {
        _gpuTextureMemSizeStabilityCount = 0;
    }
    _gpuTextureMemSizeAtLastCheck = textureResourceGPUMemSize;

    if (_gpuTextureMemSizeStabilityCount >= _minimumGPUTextureMemSizeStabilityCount) {
        return (textureResourceGPUMemSize == texturePopulatedGPUMemSize) && (textureTransferSize == 0);
    }
    return false;
}

int Application::processOctreeStats(ReceivedMessage& message, SharedNodePointer sendingNode) {
    // parse the incoming stats datas stick it in a temporary object for now, while we
    // determine which server it belongs to
    int statsMessageLength = 0;

    const QUuid& nodeUUID = sendingNode->getUUID();

    // now that we know the node ID, let's add these stats to the stats for that node...
    _octreeServerSceneStats.withWriteLock([&] {
        OctreeSceneStats& octreeStats = _octreeServerSceneStats[nodeUUID];
        statsMessageLength = octreeStats.unpackFromPacket(message);

        if (octreeStats.isFullScene()) {
            _fullSceneReceivedCounter++;
        }
    });

    return statsMessageLength;
}

// The script API's globals (registerScriptEngineWithApplicationServices) are in ApplicationScripting.cpp.

bool Application::canAcceptURL(const QString& urlString) const {
    QUrl url(urlString);
    if (url.query().contains(WEB_VIEW_TAG)) {
        return false;
    } else if (urlString.startsWith(URL_SCHEME_HIFI)) {
        return true;
    }
    QString lowerPath = url.path().toLower();
    for (auto& pair : _acceptedExtensions) {
        if (lowerPath.endsWith(pair.first, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool Application::acceptURL(const QString& urlString, bool defaultUpload) {
    QUrl url(urlString);

    if (url.scheme() == URL_SCHEME_HIFI) {
        // this is a hifi URL - have the AddressManager handle it
        QMetaObject::invokeMethod(DependencyManager::get<AddressManager>().data(), "handleLookupString",
                                  Qt::AutoConnection, Q_ARG(const QString&, urlString));
        return true;
    }

    QString lowerPath = url.path().toLower();
    for (auto& pair : _acceptedExtensions) {
        if (lowerPath.endsWith(pair.first, Qt::CaseInsensitive)) {
            AcceptURLMethod method = pair.second;
            return (this->*method)(urlString);
        }
    }

    
    return defaultUpload;
}

void Application::setSessionUUID(const QUuid& sessionUUID) const {
    Physics::setSessionUUID(sessionUUID);
}

bool Application::askToSetAvatarUrl(const QString& url) {
    QUrl realUrl(url);
    if (realUrl.isLocalFile()) {
        // an avatar on this computer goes through the importer, as with File > Import Avatar
        AvatarImporter::importFile(realUrl.toLocalFile());
        return true;
    }

    // Download the FST file, to attempt to determine its model type
    QVariantHash fstMapping = FSTReader::downloadMapping(url);

    FSTReader::ModelType modelType = FSTReader::predictModelType(fstMapping);

    QString modelName = fstMapping["name"].toString();
    QString modelLicense = fstMapping["license"].toString();
    // the avatar's own name in the popups, else its file's
    const QString shownName = modelName.trimmed().isEmpty() ? QFileInfo(realUrl.path()).completeBaseName() : modelName.trimmed();

    auto wear = [=] () {
        getMyAvatar()->useFullAvatarURL(url, modelName);
        emit fullAvatarURLChanged(url, modelName);
    };

    if (!modelLicense.isEmpty()) {
        ModalDialogListener* dlg = OffscreenUi::asyncQuestion(shownName + " comes with a license",
                                                              "Read it before you wear this avatar.",
                                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (QQuickItem* box = dlg->getDialogItem()) {
            OffscreenUi::setButtonText(box, QMessageBox::Yes, "Agree and Wear");
            OffscreenUi::setButtonText(box, QMessageBox::No, "Don't Wear");
            box->setProperty("detailedText", modelLicense);
        }
        QObject::connect(dlg, &ModalDialogListener::response, this, [=] (QVariant answer) {
            QObject::disconnect(dlg, &ModalDialogListener::response, this, nullptr);

            if (static_cast<QMessageBox::StandardButton>(answer.toInt()) == QMessageBox::Yes) {
                if (modelType == FSTReader::HEAD_AND_BODY_MODEL) {
                    wear();
                } else {
                    OffscreenUi::asyncWarning(shownName + " can't be worn", "It needs a head and a body.");
                }
            } else {
                qCDebug(interfaceapp) << "Declined to agree to avatar license";
            }
        });
    } else {
        ModalDialogListener* dlg = OffscreenUi::asyncQuestion("Wear " + shownName + "?",
                                                              realUrl.host().isEmpty() ? QString() : "From " + realUrl.host() + ".",
                                                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
        OffscreenUi::setButtonText(dlg->getDialogItem(), QMessageBox::Ok, "Wear");
        QObject::connect(dlg, &ModalDialogListener::response, this, [=] (QVariant answer) {
            QObject::disconnect(dlg, &ModalDialogListener::response, this, nullptr);

            if (static_cast<QMessageBox::StandardButton>(answer.toInt()) == QMessageBox::Ok) {
                wear();
            } else {
                qCDebug(interfaceapp) << "Declined to use the avatar";
            }
        });
    }

    return true;
}


bool Application::askToLoadScript(const QString& scriptFilenameOrURL) {
    QUrl scriptURL { scriptFilenameOrURL };

    QString name = QFileInfo(scriptURL.path()).fileName();
    if (name.isEmpty()) {
        name = "this script";
    }
    QString message = "Only run scripts from people you trust.";
    if (!scriptURL.isLocalFile() && !scriptURL.host().isEmpty()) {
        message = "From " + scriptURL.host() + ". " + message;
    }
    ModalDialogListener* dlg = OffscreenUi::asyncQuestion(getWindow(), "Run " + name + "?", message,
                                                           QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    OffscreenUi::setButtonText(dlg->getDialogItem(), QMessageBox::Yes, "Run");

    QObject::connect(dlg, &ModalDialogListener::response, this, [=] (QVariant answer) {
        const QString& fileName = scriptFilenameOrURL;
        if (static_cast<QMessageBox::StandardButton>(answer.toInt()) == QMessageBox::Yes) {
            qCDebug(interfaceapp) << "Chose to run the script: " << fileName;
            DependencyManager::get<ScriptEngines>()->loadScript(fileName);
        } else {
            qCDebug(interfaceapp) << "Declined to run the script";
        }
        QObject::disconnect(dlg, &ModalDialogListener::response, this, nullptr);
    });
    return true;
}

bool Application::askToWearAvatarAttachmentUrl(const QString& url) {
    QNetworkAccessManager& networkAccessManager = NetworkAccessManager::getInstance();
    QNetworkRequest networkRequest = QNetworkRequest(url);
    networkRequest.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, TIVOLI_CLOUD_VR_USER_AGENT);
    QNetworkReply* reply = networkAccessManager.get(networkRequest);
    int requestNumber = ++_avatarAttachmentRequest;
    connect(reply, &QNetworkReply::finished, [this, reply, url, requestNumber]() {

        if (requestNumber != _avatarAttachmentRequest) {
            // this request has been superseded by another more recent request
            reply->deleteLater();
            return;
        }

        QNetworkReply::NetworkError networkError = reply->error();
        if (networkError == QNetworkReply::NoError) {
            // download success
            QByteArray contents = reply->readAll();

            QJsonParseError jsonError;
            auto doc = QJsonDocument::fromJson(contents, &jsonError);
            if (jsonError.error == QJsonParseError::NoError) {

                auto jsonObject = doc.object();

                // retrieve optional name field from JSON
                QString name;
                auto nameValue = jsonObject.value("name");
                if (nameValue.isString()) {
                    name = nameValue.toString().trimmed();
                }

                ModalDialogListener* dlg = OffscreenUi::asyncQuestion(
                    tr("Wear %1 on your avatar?").arg(name.isEmpty() ? tr("this attachment") : name), QString(),
                    QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
                OffscreenUi::setButtonText(dlg->getDialogItem(), QMessageBox::Ok, tr("Wear"));
                QObject::connect(dlg, &ModalDialogListener::response, this, [=] (QVariant answer) {
                    QObject::disconnect(dlg, &ModalDialogListener::response, this, nullptr);
                    if (static_cast<QMessageBox::StandardButton>(answer.toInt()) == QMessageBox::Ok) {
                        // add attachment to avatar
                        auto myAvatar = getMyAvatar();
                        assert(myAvatar);
                        auto attachmentDataVec = myAvatar->getAttachmentData();
                        AttachmentData attachmentData;
                        attachmentData.fromJson(jsonObject);
                        attachmentDataVec.push_back(attachmentData);
                        myAvatar->setAttachmentData(attachmentDataVec);
                    } else {
                        qCDebug(interfaceapp) << "User declined to wear the avatar attachment";
                    }
                });
            } else {
                // json parse error
                qCWarning(interfaceapp) << "Attachment JSON doesn't parse:" << url;
                displayAvatarAttachmentWarning(tr("The attachment file is damaged."));
            }
        } else {
            // download failure
            qCWarning(interfaceapp) << "Attachment JSON didn't download:" << url << reply->errorString();
            displayAvatarAttachmentWarning(tr("The attachment file couldn't be downloaded."));
        }
        reply->deleteLater();
    });
    return true;
}

void Application::displayAvatarAttachmentWarning(const QString& message) const {
    OffscreenUi::asyncWarning(tr("Couldn't load the attachment"), message);
}

void Application::showDialog(const QUrl& widgetUrl, const QUrl& tabletUrl, const QString& name) const {
    auto tablet = DependencyManager::get<TabletScriptingInterface>()->getTablet(SYSTEM_TABLET);
    auto hmd = DependencyManager::get<HMDScriptingInterface>();
    bool onTablet = false;

    if (!tablet->getToolbarMode()) {
        onTablet = tablet->pushOntoStack(tabletUrl);
        if (onTablet) {
            toggleTabletUI(true);
        }
    } else {
        getOffscreenUI()->show(widgetUrl, name);
    }
}

void Application::showScriptLogs() {
    QUrl defaultScriptsLoc = PathUtils::defaultScriptsLocation();
    defaultScriptsLoc.setPath(defaultScriptsLoc.path() + "developer/debugging/debugWindow.js");
    DependencyManager::get<ScriptEngines>()->loadScript(defaultScriptsLoc.toString());
}

void Application::showAssetServerWidget(QString filePath) {
    if (!DependencyManager::get<NodeList>()->getThisNodeCanWriteAssets() || getLoginDialogPoppedUp()) {
        return;
    }
    static const QUrl url { "hifi/AssetServer.qml" };

    auto startUpload = [=](QQmlContext* context, QObject* newObject){
        if (!filePath.isEmpty()) {
            emit uploadRequest(filePath);
        }
    };
    auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
    auto tablet = dynamic_cast<TabletProxy*>(tabletScriptingInterface->getTablet(SYSTEM_TABLET));
    auto hmd = DependencyManager::get<HMDScriptingInterface>();
    if (tablet->getToolbarMode()) {
        getOffscreenUI()->show(url, "AssetServer", startUpload);
    } else {
        if (!hmd->getShouldShowTablet() && !isHMDMode()) {
            getOffscreenUI()->show(url, "AssetServer", startUpload);
        } else {
            static const QUrl url("qrc:///qml/hifi/dialogs/TabletAssetServer.qml");
            if (!tablet->isPathLoaded(url)) {
                tablet->pushOntoStack(url);
            }
        }
    }

    startUpload(nullptr, nullptr);
}

// The import of content into the world (addAssetToWorld and its steps, handleUnzip) is in ApplicationImport.cpp.

void Application::packageModel() {
    ModelPackager::package();
}

void Application::loadDialog() {
    ModalDialogListener* dlg = OffscreenUi::getOpenFileNameAsync(_glWidget, tr("Open script"),
                                                                 getPreviousScriptLocation(),
                                                                 tr("JavaScript files (*.js)"));
    connect(dlg, &ModalDialogListener::response, this, [=] (QVariant answer) {
        disconnect(dlg, &ModalDialogListener::response, this, nullptr);
        const QString& response = answer.toString();
        if (!response.isEmpty() && QFile(response).exists()) {
            setPreviousScriptLocation(QFileInfo(response).absolutePath());
            DependencyManager::get<ScriptEngines>()->loadScript(response, true, false, false, true);  // Don't load from cache
        }
    });
}

void Application::importAvatarDialog() {
    static Setting::Handle<QString> previousLocation("previousAvatarImportLocation",
                                                     QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    // Qt's standard file dialog, as ModelSelector uses: the macOS panel that OffscreenUi's modeless dialog opens
    // showed every file greyed out (10-12), whatever the filter
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("Import Avatar"), previousLocation.get(),
                                                      tr("Characters and avatars (*)"));
    qCDebug(interfaceapp) << "Import Avatar: picked" << (path.isEmpty() ? QString("nothing") : path);
    if (!path.isEmpty() && QFile(path).exists()) {
        previousLocation.set(QFileInfo(path).absolutePath());
        AvatarImporter::importFile(path);
    }
}

void Application::putAvatarInMyFiles() {
    AvatarImporter::putInMyFiles(getMyAvatar()->getFullAvatarURLFromPreferences());
}

QString Application::getPreviousScriptLocation() {
    QString result = _previousScriptLocation.get();
    return result;
}

void Application::setPreviousScriptLocation(const QString& location) {
    _previousScriptLocation.set(location);
}

void Application::loadScriptURLDialog() const {
    ModalDialogListener* dlg = OffscreenUi::getTextAsync(OffscreenUi::ICON_NONE, "Run a Script from the Web", QString());
    if (QQuickItem* box = dlg->getDialogItem()) {
        OffscreenUi::setButtonText(box, QMessageBox::Ok, "Run");
        box->setProperty("placeholderText", QString::fromUtf8("https://…/script.js"));
    }
    connect(dlg, &ModalDialogListener::response, this, [=] (QVariant response) {
        disconnect(dlg, &ModalDialogListener::response, this, nullptr);
        const QString& newScript = response.toString();
        if (QUrl(newScript).scheme() == "atp") {
            OffscreenUi::asyncWarning("Can't run a script from an atp: address", "Use an https: address.");
        } else if (!newScript.isEmpty()) {
            DependencyManager::get<ScriptEngines>()->loadScript(newScript.trimmed());
        }
    });
}

SharedSoundPointer Application::getSampleSound() const {
    return _sampleSound;
}

void Application::loadLODToolsDialog() {
    auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
    auto tablet = dynamic_cast<TabletProxy*>(tabletScriptingInterface->getTablet(SYSTEM_TABLET));
    if (tablet->getToolbarMode() || (!tablet->getTabletRoot() && !isHMDMode())) {
        auto dialogsManager = DependencyManager::get<DialogsManager>();
        dialogsManager->lodTools();
    } else {
        tablet->pushOntoStack("hifi/dialogs/TabletLODTools.qml");
    }
}

void Application::loadEntityStatisticsDialog() {
    auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
    auto tablet = dynamic_cast<TabletProxy*>(tabletScriptingInterface->getTablet(SYSTEM_TABLET));
    if (tablet->getToolbarMode() || (!tablet->getTabletRoot() && !isHMDMode())) {
        auto dialogsManager = DependencyManager::get<DialogsManager>();
        dialogsManager->octreeStatsDetails();
    } else {
        tablet->pushOntoStack("hifi/dialogs/TabletEntityStatistics.qml");
    }
}

void Application::loadDomainConnectionDialog() {
    auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
    auto tablet = dynamic_cast<TabletProxy*>(tabletScriptingInterface->getTablet(SYSTEM_TABLET));
    if (tablet->getToolbarMode() || (!tablet->getTabletRoot() && !isHMDMode())) {
        auto dialogsManager = DependencyManager::get<DialogsManager>();
        dialogsManager->showDomainConnectionDialog();
    } else {
        tablet->pushOntoStack("hifi/dialogs/TabletDCDialog.qml");
    }
}

void Application::toggleLogDialog() {
#ifndef ANDROID_APP_QUEST_INTERFACE
    if (getLoginDialogPoppedUp()) {
        return;
    }
    if (! _logDialog) {

        bool keepOnTop =_keepLogWindowOnTop.get();
#ifdef Q_OS_WIN
        _logDialog = new LogDialog(keepOnTop ? qApp->getWindow() : nullptr, getLogger());
#elif !defined(Q_OS_ANDROID)
        _logDialog = new LogDialog(nullptr, getLogger());

        if (keepOnTop) {
            Qt::WindowFlags flags = _logDialog->windowFlags() | Qt::Tool;
            _logDialog->setWindowFlags(flags);
        }
#else
        Q_UNUSED(keepOnTop)
#endif
    }

    if (_logDialog->isVisible()) {
        _logDialog->hide();
    } else {
        _logDialog->show();
    }
#endif
}

 void Application::recreateLogWindow(int keepOnTop) {
     _keepLogWindowOnTop.set(keepOnTop != 0);
     if (_logDialog) {
         bool toggle = _logDialog->isVisible();
         _logDialog->close();
         _logDialog = nullptr;

         if (toggle) {
             toggleLogDialog();
         }
     }
 }

void Application::toggleEntityScriptServerLogDialog() {
    if (! _entityScriptServerLogDialog) {
        _entityScriptServerLogDialog = new EntityScriptServerLogDialog(nullptr);
    }

    if (_entityScriptServerLogDialog->isVisible()) {
        _entityScriptServerLogDialog->hide();
    } else {
        _entityScriptServerLogDialog->show();
    }
}

void Application::addSnapshotOperator(const SnapshotOperator& snapshotOperator) {
    std::lock_guard<std::mutex> lock(_snapshotMutex);
    _snapshotOperators.push(snapshotOperator);
    _hasPrimarySnapshot = _hasPrimarySnapshot || std::get<2>(snapshotOperator);
}

bool Application::takeSnapshotOperators(std::queue<SnapshotOperator>& snapshotOperators) {
    std::lock_guard<std::mutex> lock(_snapshotMutex);
    bool hasPrimarySnapshot = _hasPrimarySnapshot;
    _hasPrimarySnapshot = false;
    _snapshotOperators.swap(snapshotOperators);
    return hasPrimarySnapshot;
}

void Application::takeSnapshot(bool notify, bool includeAnimated, float aspectRatio, const QString& filename) {
    addSnapshotOperator(std::make_tuple([notify, includeAnimated, aspectRatio, filename](const QImage& snapshot) {
        qApp->postLambdaEvent([snapshot, notify, includeAnimated, aspectRatio, filename] {
            QString path = DependencyManager::get<Snapshot>()->saveSnapshot(snapshot, filename, TestScriptingInterface::getInstance()->getTestResultsLocation());

            // If we're not doing an animated snapshot as well...
            if (!includeAnimated) {
                if (!path.isEmpty()) {
                    // Tell the dependency manager that the capture of the still snapshot has taken place.
                    emit DependencyManager::get<WindowScriptingInterface>()->stillSnapshotTaken(path, notify);
                }
            } else if (!SnapshotAnimated::isAlreadyTakingSnapshotAnimated()) {
                // Get an animated GIF snapshot and save it
                SnapshotAnimated::saveSnapshotAnimated(path, aspectRatio, DependencyManager::get<WindowScriptingInterface>());
            }
        });
    }, aspectRatio, true));
}

void Application::takeSecondaryCameraSnapshot(const bool& notify, const QString& filename) {
    addSnapshotOperator(std::make_tuple([notify, filename](const QImage& snapshot) {
        qApp->postLambdaEvent([snapshot, notify, filename] {
            QString snapshotPath = DependencyManager::get<Snapshot>()->saveSnapshot(snapshot, filename, TestScriptingInterface::getInstance()->getTestResultsLocation());

            emit DependencyManager::get<WindowScriptingInterface>()->stillSnapshotTaken(snapshotPath, notify);
        });
    }, 0.0f, false));
}

void Application::takeSecondaryCamera360Snapshot(const glm::vec3& cameraPosition, const bool& cubemapOutputFormat, const bool& notify, const QString& filename) {
    postLambdaEvent([notify, filename, cubemapOutputFormat, cameraPosition] {
        DependencyManager::get<Snapshot>()->save360Snapshot(cameraPosition, cubemapOutputFormat, notify, filename);
    });
}

void Application::shareSnapshot(const QString& path, const QUrl& href) {
    postLambdaEvent([path, href] {
        // not much to do here, everything is done in snapshot code...
        DependencyManager::get<Snapshot>()->uploadSnapshot(path, href);
    });
}

float Application::getRenderResolutionScale() const {
    return RenderScriptingInterface::getInstance()->getViewportResolutionScale();
}

void Application::notifyPacketVersionMismatch() {
    if (!_notifiedPacketVersionMismatchThisDomain && !isInterstitialMode()) {
        _notifiedPacketVersionMismatchThisDomain = true;

        const QString world = worldNameForPopup();
        OffscreenUi::asyncWarning((world.isEmpty() ? QString("This world") : world) + " runs a different version of Darkride",
                                  "Some things in it may not look right.");
    }
}

void Application::checkSkeleton() const {
    if (getMyAvatar()->getSkeletonModel()->isActive() && !getMyAvatar()->getSkeletonModel()->hasSkeleton()) {
        qCDebug(interfaceapp) << "MyAvatar model has no skeleton";

        const QUrl url = getMyAvatar()->getFullAvatarURLFromPreferences();
        QString name = getMyAvatar()->getFullAvatarModelName().trimmed();
        if (name.isEmpty()) {
            name = QFileInfo(url.path()).completeBaseName();
        }
        // A model on this computer can be rigged by the importer on the spot; .fst files name a model of their own.
        const bool canRig = url.isLocalFile() && QFileInfo(url.path()).suffix().toLower() != "fst";
        ModalDialogListener* dlg = OffscreenUi::asyncWarning((name.isEmpty() ? QString("This avatar") : name) + " has no skeleton",
                                                             "You're wearing the default avatar for now.",
                                                             canRig ? QMessageBox::Ok | QMessageBox::Cancel : QMessageBox::Ok,
                                                             QMessageBox::Ok);
        if (canRig) {
            OffscreenUi::setButtonText(dlg->getDialogItem(), QMessageBox::Ok, "Auto Rig");
            OffscreenUi::setButtonText(dlg->getDialogItem(), QMessageBox::Cancel, "OK");
            QObject::connect(dlg, &ModalDialogListener::response, qApp, [dlg, url] (QVariant answer) {
                QObject::disconnect(dlg, &ModalDialogListener::response, qApp, nullptr);
                if (static_cast<QMessageBox::StandardButton>(answer.toInt()) == QMessageBox::Ok) {
                    AvatarImporter::importFile(url.toLocalFile());
                }
            });
        }

        getMyAvatar()->useFullAvatarURL(AvatarData::defaultFullAvatarModelUrl(), DEFAULT_FULL_AVATAR_MODEL_NAME);
    } else {
        _physicsEngine->setCharacterController(getMyAvatar()->getCharacterController());
    }
}

void Application::activeChanged(Qt::ApplicationState state) {
    switch (state) {
        case Qt::ApplicationActive:
            _isForeground = true;
            if (!_aboutToQuit && _startUpFinished) {
                getRefreshRateManager().setRefreshRateRegime(RefreshRateManager::RefreshRateRegime::FOCUS_ACTIVE);
            }
            break;

        case Qt::ApplicationSuspended:
            break;
        case Qt::ApplicationHidden:
            break;
        case Qt::ApplicationInactive:
            if (!_aboutToQuit && _startUpFinished) {
                getRefreshRateManager().setRefreshRateRegime(RefreshRateManager::RefreshRateRegime::UNFOCUS);
            }
            break;
        default:
            _isForeground = false;
            break;
    }
}

void Application::windowMinimizedChanged(bool minimized) {
    // initialize the _minimizedWindowTimer
    static std::once_flag once;
    std::call_once(once, [&] {
        connect(&_minimizedWindowTimer, &QTimer::timeout, this, [] {
            QCoreApplication::postEvent(QCoreApplication::instance(), new QEvent(static_cast<QEvent::Type>(Idle)), Qt::HighEventPriority);
        });
    });

    // avoid rendering to the display plugin but continue posting Idle events,
    // so that physics continues to simulate and the deadlock watchdog knows we're alive
    if (!minimized && !getActiveDisplayPlugin()->isActive()) {
        _minimizedWindowTimer.stop();
        getActiveDisplayPlugin()->activate();
    } else if (minimized && getActiveDisplayPlugin()->isActive()) {
        getActiveDisplayPlugin()->deactivate();
        _minimizedWindowTimer.start(THROTTLED_SIM_FRAME_PERIOD_MS);
    }
}

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

void Application::initPlugins(const ClientOptions& options) {
    if (!options.displays.isEmpty()) {
        qInfo() << "Setting prefered display plugins:" << options.displays;
        PluginManager::getInstance()->setPreferredDisplayPlugins(options.displays);
    }
    if (!options.disabledDisplays.isEmpty()) {
        qInfo() << "Disabling following display plugins:"  << options.disabledDisplays;
        PluginManager::getInstance()->disableDisplays(options.disabledDisplays);
    }
    if (!options.disabledInputs.isEmpty()) {
        qInfo() << "Disabling following input plugins:" << options.disabledInputs;
        PluginManager::getInstance()->disableInputs(options.disabledInputs);
    }
}

glm::uvec2 Application::getCanvasSize() const {
    return glm::uvec2(_glWidget->width(), _glWidget->height());
}

QRect Application::getRenderingGeometry() const {
    auto geometry = _glWidget->geometry();
    auto topLeft = geometry.topLeft();
    auto topLeftScreen = _glWidget->mapToGlobal(topLeft);
    geometry.moveTopLeft(topLeftScreen);
    return geometry;
}

glm::uvec2 Application::getUiSize() const {
    static const uint MIN_SIZE = 1;
    glm::uvec2 result(MIN_SIZE);
    if (_displayPlugin) {
        result = getActiveDisplayPlugin()->getRecommendedUiSize();
    }
    return result;
}

QRect Application::getRecommendedHUDRect() const {
    auto uiSize = getUiSize();
    QRect result(0, 0, uiSize.x, uiSize.y);
    if (_displayPlugin) {
        result = getActiveDisplayPlugin()->getRecommendedHUDRect();
    }
    return result;
}

glm::vec2 Application::getDeviceSize() const {
    static const int MIN_SIZE = 1;
    glm::vec2 result(MIN_SIZE);
    if (_displayPlugin) {
        result = getActiveDisplayPlugin()->getRecommendedRenderSize();
    }
    return result;
}

bool Application::isThrottleRendering() const {
    if (_displayPlugin) {
        return getActiveDisplayPlugin()->isThrottled();
    }
    return false;
}

bool Application::hasFocus() const {
    bool result = (QApplication::activeWindow() != nullptr);

    
#if defined(Q_OS_WIN)
    // On Windows, QWidget::activateWindow() - as called in setFocus() - makes the application's taskbar icon flash but doesn't
    // take user focus away from their current window. So also check whether the application is the user's current foreground
    // window.
    result = result && (HWND)QApplication::activeWindow()->winId() == GetForegroundWindow();
#endif
    return result;
}

void Application::setFocus() {
    // Note: Windows doesn't allow a user focus to be taken away from another application. Instead, it changes the color of and
    // flashes the taskbar icon.
    auto window = qApp->getWindow();
    window->activateWindow();
}

void Application::raise() {
    auto windowState = qApp->getWindow()->windowState();
    if (windowState & Qt::WindowMinimized) {
        if (windowState & Qt::WindowMaximized) {
            qApp->getWindow()->showMaximized();
        } else if (windowState & Qt::WindowFullScreen) {
            qApp->getWindow()->showFullScreen();
        } else {
            qApp->getWindow()->showNormal();
        }
    }
    qApp->getWindow()->raise();
}

void Application::setMaxOctreePacketsPerSecond(int maxOctreePPS) {
    if (maxOctreePPS != _maxOctreePPS) {
        _maxOctreePPS = maxOctreePPS;
        maxOctreePacketsPerSecond.set(_maxOctreePPS);
    }
}

int Application::getMaxOctreePacketsPerSecond() const {
    return _maxOctreePPS;
}

qreal Application::getDevicePixelRatio() {
    return (_window && _window->windowHandle()) ? _window->windowHandle()->devicePixelRatio() : 1.0;
}

DisplayPluginPointer Application::getActiveDisplayPlugin() const {
    if (QThread::currentThread() != thread()) {
        std::unique_lock<std::mutex> lock(_displayPluginLock);
        return _displayPlugin;
    }

    if (!_aboutToQuit && !_displayPlugin) {
        const_cast<Application*>(this)->updateDisplayMode();
        Q_ASSERT(_displayPlugin);
    }
    return _displayPlugin;
}


static const char* EXCLUSION_GROUP_KEY = "exclusionGroup";

static void addDisplayPluginToMenu(const DisplayPluginPointer& displayPlugin, int index, bool active) {
    auto menu = Menu::getInstance();
    QString name = displayPlugin->getName();
    auto grouping = displayPlugin->getGrouping();
    QString groupingMenu { "" };
    Q_ASSERT(!menu->menuItemExists(MenuOption::OutputMenu, name));

    // assign the meny grouping based on plugin grouping
    switch (grouping) {
        case Plugin::ADVANCED:
            groupingMenu = "Advanced";
            break;
        case Plugin::DEVELOPER:
            groupingMenu = "Developer";
            break;
        default:
            groupingMenu = "Standard";
            break;
    }

    static QActionGroup* displayPluginGroup = nullptr;
    if (!displayPluginGroup) {
        displayPluginGroup = new QActionGroup(menu);
        displayPluginGroup->setExclusive(true);
    }
    auto parent = menu->getMenu(MenuOption::OutputMenu);
    auto action = menu->addActionToQMenuAndActionHash(parent,
        name, QKeySequence(Qt::CTRL + (Qt::Key_0 + index)), qApp,
        SLOT(updateDisplayMode()),
        QAction::NoRole, Menu::UNSPECIFIED_POSITION, groupingMenu);

    action->setCheckable(true);
    action->setChecked(active);
    displayPluginGroup->addAction(action);

    action->setProperty(EXCLUSION_GROUP_KEY, QVariant::fromValue(displayPluginGroup));
    Q_ASSERT(menu->menuItemExists(MenuOption::OutputMenu, name));
}

void Application::updateDisplayMode() {
    // Unsafe to call this method from anything but the main thread
    if (QThread::currentThread() != thread()) {
        qFatal("Attempted to switch display plugins from a non-main thread");
    }

    // Once time initialization code that depends on the UI being available
    auto displayPlugins = getDisplayPlugins();

    // Default to the first item on the list, in case none of the menu items match

    DisplayPluginPointer newDisplayPlugin = displayPlugins.at(0);
    auto menu = getPrimaryMenu();
    if (menu) {
        foreach(DisplayPluginPointer displayPlugin, PluginManager::getInstance()->getDisplayPlugins()) {
            QString name = displayPlugin->getName();
            QAction* action = menu->getActionForOption(name);
            // Menu might have been removed if the display plugin lost
            if (!action) {
                continue;
            }
            if (action->isChecked()) {
                newDisplayPlugin = displayPlugin;
                break;
            }
        }
    }

    if (newDisplayPlugin == _displayPlugin) {
        return;
    }

    setDisplayPlugin(newDisplayPlugin);
}

void Application::setDisplayPlugin(DisplayPluginPointer newDisplayPlugin) {
    if (newDisplayPlugin == _displayPlugin) {
        return;
    }

    // FIXME don't have the application directly set the state of the UI,
    // instead emit a signal that the display plugin is changing and let
    // the desktop lock itself.  Reduces coupling between the UI and display
    // plugins
    auto offscreenUi = getOffscreenUI();
    auto desktop = offscreenUi ? offscreenUi->getDesktop() : nullptr;
    auto menu = Menu::getInstance();

    // Make the switch atomic from the perspective of other threads
    {
        std::unique_lock<std::mutex> lock(_displayPluginLock);
        bool wasRepositionLocked = false;
        if (desktop) {
            // Tell the desktop to no reposition (which requires plugin info), until we have set the new plugin, below.
            wasRepositionLocked = desktop->property("repositionLocked").toBool();
            desktop->setProperty("repositionLocked", true);
        }

        if (_displayPlugin) {
            disconnect(_displayPlugin.get(), &DisplayPlugin::presented, this, &Application::onPresent);
            _displayPlugin->deactivate();
        }

        auto oldDisplayPlugin = _displayPlugin;
        bool active = newDisplayPlugin->activate();

        if (!active) {
            auto displayPlugins = PluginManager::getInstance()->getDisplayPlugins();

            // If the new plugin fails to activate, fallback to last display
            qWarning() << "Failed to activate display: " << newDisplayPlugin->getName();
            newDisplayPlugin = oldDisplayPlugin;

            if (newDisplayPlugin) {
                qWarning() << "Falling back to last display: " << newDisplayPlugin->getName();
                active = newDisplayPlugin->activate();
            }

            // If there is no last display, or
            // If the last display fails to activate, fallback to desktop
            if (!active) {
                newDisplayPlugin = displayPlugins.at(0);
                qWarning() << "Falling back to display: " << newDisplayPlugin->getName();
                active = newDisplayPlugin->activate();
            }

            if (!active) {
                qFatal("Failed to activate fallback plugin");
            }
        }

        if (offscreenUi) {
            offscreenUi->resize(fromGlm(newDisplayPlugin->getRecommendedUiSize()));
        }
        getApplicationCompositor().setDisplayPlugin(newDisplayPlugin);
        _displayPlugin = newDisplayPlugin;
        connect(_displayPlugin.get(), &DisplayPlugin::presented, this, &Application::onPresent, Qt::DirectConnection);
        if (desktop) {
            desktop->setProperty("repositionLocked", wasRepositionLocked);
        }

        RefreshRateManager& refreshRateManager = getRefreshRateManager();
        refreshRateManager.setRefreshRateOperator(OpenGLDisplayPlugin::getRefreshRateOperator());
        bool isHmd = newDisplayPlugin->isHmd();
        RefreshRateManager::UXMode uxMode = isHmd ? RefreshRateManager::UXMode::VR :
            RefreshRateManager::UXMode::DESKTOP;

        refreshRateManager.setUXMode(uxMode);
    }

    bool isHmd = _displayPlugin->isHmd();
    qCDebug(interfaceapp) << "Entering into" << (isHmd ? "HMD" : "Desktop") << "Mode";

    // Only emit after a successful change
    emit activeDisplayPluginChanged();

    // reset the avatar, to set head and hand palms back to a reasonable default pose.
    getMyAvatar()->reset(false);

    // switch to first person if entering hmd and setting is checked
    if (menu) {
        QAction* action = menu->getActionForOption(newDisplayPlugin->getName());
        if (action) {
            action->setChecked(true);
        }

        if (isHmd && menu->isOptionChecked(MenuOption::FirstPersonHMD)) {
            menu->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);
            cameraMenuChanged();
        }

        // Remove the selfie camera options from menu if in HMD mode
        auto selfieAction = menu->getActionForOption(MenuOption::SelfieCamera);
        selfieAction->setVisible(!isHmd);
    }

    Q_ASSERT_X(_displayPlugin, "Application::updateDisplayMode", "could not find an activated display plugin");
}

void Application::switchDisplayMode() {
    if (!_autoSwitchDisplayModeSupportedHMDPlugin) {
        return;
    }

    bool currentHMDWornStatus = _autoSwitchDisplayModeSupportedHMDPlugin->isDisplayVisible();
    if (currentHMDWornStatus != _previousHMDWornStatus) {
        // Switch to respective mode as soon as currentHMDWornStatus changes
        if (currentHMDWornStatus) {
            qCDebug(interfaceapp) << "Switching from Desktop to HMD mode";
            endHMDSession();
            setActiveDisplayPlugin(_autoSwitchDisplayModeSupportedHMDPluginName);
        } else {
            qCDebug(interfaceapp) << "Switching from HMD to desktop mode";
            setActiveDisplayPlugin(DESKTOP_DISPLAY_PLUGIN_NAME);
            startHMDStandBySession();
        }
    }
    _previousHMDWornStatus = currentHMDWornStatus;
}

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

void Application::createLoginDialog() {
    const glm::vec3 LOGIN_DIMENSIONS { 0.89f, 0.5f, 0.01f };
    const auto OFFSET = glm::vec2(0.7f, -0.1f);
    auto cameraPosition = _myCamera.getPosition();
    auto cameraOrientation = _myCamera.getOrientation();
    auto upVec = getMyAvatar()->getWorldOrientation() * Vectors::UNIT_Y;
    auto headLookVec = (cameraOrientation * Vectors::FRONT);
    // DEFAULT_DPI / tablet scale percentage
    const float DPI = 31.0f / (75.0f / 100.0f);
    auto offset = headLookVec * OFFSET.x;
    auto position = (cameraPosition + offset) + (upVec * OFFSET.y);

    EntityItemProperties properties;
    properties.setType(EntityTypes::Web);
    properties.setName("LoginDialogEntity");
    properties.setSourceUrl(LOGIN_DIALOG.toString());
    properties.setPosition(position);
    properties.setRotation(cameraOrientation);
    properties.setDimensions(LOGIN_DIMENSIONS);
    properties.setPrimitiveMode(PrimitiveMode::SOLID);
    properties.getGrab().setGrabbable(false);
    properties.setIgnorePickIntersection(false);
    properties.setAlpha(1.0f);
    properties.setDPI(DPI);
    properties.setVisible(true);

    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
    _loginDialogID = entityScriptingInterface->addEntityInternal(properties, entity::HostType::LOCAL);

    auto keyboard = DependencyManager::get<Keyboard>().data();
    if (!keyboard->getAnchorID().isNull() && !_loginDialogID.isNull()) {
        auto keyboardLocalOffset = cameraOrientation * glm::vec3(-0.4f * getMyAvatar()->getSensorToWorldScale(), -0.3f, 0.2f);

        EntityItemProperties properties;
        properties.setPosition(position + keyboardLocalOffset);
        properties.setRotation(cameraOrientation * Quaternions::Y_180);

        entityScriptingInterface->editEntity(keyboard->getAnchorID(), properties);
        keyboard->setResetKeyboardPositionOnRaise(false);
    }
    setKeyboardFocusEntity(_loginDialogID);
    emit loginDialogFocusEnabled();
    getApplicationCompositor().getReticleInterface()->setAllowMouseCapture(false);
    getApplicationCompositor().getReticleInterface()->setVisible(false);
    if (!_loginStateManager.isSetUp()) {
        _loginStateManager.setUp();
    }
}

void Application::updateLoginDialogPosition() {
    const float LOOK_AWAY_THRESHOLD_ANGLE = 70.0f;
    const auto OFFSET = glm::vec2(0.7f, -0.1f);

    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
    EntityPropertyFlags desiredProperties;
    desiredProperties += PROP_POSITION;
    auto properties = entityScriptingInterface->getEntityProperties(_loginDialogID, desiredProperties);
    auto positionVec = properties.getPosition();
    auto cameraPositionVec = _myCamera.getPosition();
    auto cameraOrientation = cancelOutRollAndPitch(_myCamera.getOrientation());
    auto headLookVec = (cameraOrientation * Vectors::FRONT);
    auto entityToHeadVec = positionVec - cameraPositionVec;
    auto pointAngle = (glm::acos(glm::dot(glm::normalize(entityToHeadVec), glm::normalize(headLookVec))) * 180.0f / PI);
    auto upVec = getMyAvatar()->getWorldOrientation() * Vectors::UNIT_Y;
    auto offset = headLookVec * OFFSET.x;
    auto newPositionVec = (cameraPositionVec + offset) + (upVec * OFFSET.y);

    bool outOfBounds = glm::distance(positionVec, cameraPositionVec) > 1.0f;

    if (pointAngle > LOOK_AWAY_THRESHOLD_ANGLE || outOfBounds) {
        {
            EntityItemProperties properties;
            properties.setPosition(newPositionVec);
            properties.setRotation(cameraOrientation);
            entityScriptingInterface->editEntity(_loginDialogID, properties);
        }

        {
            glm::vec3 keyboardLocalOffset = cameraOrientation * glm::vec3(-0.4f * getMyAvatar()->getSensorToWorldScale(), -0.3f, 0.2f);
            glm::quat keyboardOrientation = cameraOrientation * glm::quat(glm::radians(glm::vec3(-30.0f, 180.0f, 0.0f)));

            EntityItemProperties properties;
            properties.setPosition(newPositionVec + keyboardLocalOffset);
            properties.setRotation(keyboardOrientation);
            entityScriptingInterface->editEntity(DependencyManager::get<Keyboard>()->getAnchorID(), properties);
        }
    }
}

void Application::createAvatarInputsBar() {
    const glm::vec3 LOCAL_POSITION { 0.0, 0.0, -1.0 };
    // DEFAULT_DPI / tablet scale percentage
    const float DPI = 31.0f / (75.0f / 100.0f);

    EntityItemProperties properties;
    properties.setType(EntityTypes::Web);
    properties.setName("AvatarInputsBarEntity");
    properties.setSourceUrl(AVATAR_INPUTS_BAR_QML.toString());
    properties.setParentID(getMyAvatar()->getSelfID());
    properties.setParentJointIndex(getMyAvatar()->getJointIndex("_CAMERA_MATRIX"));
    properties.setPosition(LOCAL_POSITION);
    properties.setLocalRotation(Quaternions::IDENTITY);
    //properties.setDimensions(LOGIN_DIMENSIONS);
    properties.setPrimitiveMode(PrimitiveMode::SOLID);
    properties.getGrab().setGrabbable(false);
    properties.setIgnorePickIntersection(false);
    properties.setAlpha(1.0f);
    properties.setDPI(DPI);
    properties.setVisible(true);

    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
    _avatarInputsBarID = entityScriptingInterface->addEntityInternal(properties, entity::HostType::LOCAL);
}

void Application::destroyAvatarInputsBar() {
    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
    if (!_avatarInputsBarID.isNull()) {
        entityScriptingInterface->deleteEntity(_avatarInputsBarID);
    }
}

bool Application::hasRiftControllers() {
    return PluginUtils::isOculusTouchControllerAvailable();
}

bool Application::hasViveControllers() {
    return PluginUtils::isViveControllerAvailable();
}

void Application::onDismissedLoginDialog() {
    _loginDialogPoppedUp = false;
    loginDialogPoppedUp.set(false);
    auto keyboard = DependencyManager::get<Keyboard>().data();
    keyboard->setResetKeyboardPositionOnRaise(true);
    if (!_loginDialogID.isNull()) {
        DependencyManager::get<EntityScriptingInterface>()->deleteEntity(_loginDialogID);
        _loginDialogID = QUuid();
        _loginStateManager.tearDown();
    }
    resumeAfterLoginDialogActionTaken();
}

void Application::setShowTrackedObjects(bool value) {
    _showTrackedObjects = value;
}

void Application::startHMDStandBySession() {
    _autoSwitchDisplayModeSupportedHMDPlugin->startStandBySession();
}

void Application::endHMDSession() {
    _autoSwitchDisplayModeSupportedHMDPlugin->endSession();
}

mat4 Application::getEyeProjection(int eye) const {
    QMutexLocker viewLocker(&_viewMutex);
    if (isHMDMode()) {
        return getActiveDisplayPlugin()->getEyeProjection((Eye)eye, _viewFrustum.getProjection());
    }
    return _viewFrustum.getProjection();
}

mat4 Application::getEyeOffset(int eye) const {
    // FIXME invert?
    return getActiveDisplayPlugin()->getEyeToHeadTransform((Eye)eye);
}

mat4 Application::getHMDSensorPose() const {
    if (isHMDMode()) {
        return getActiveDisplayPlugin()->getHeadPose();
    }
    return mat4();
}

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

void Application::setActiveDisplayPlugin(const QString& pluginName) {
    DisplayPluginPointer newDisplayPlugin;
    for (DisplayPluginPointer displayPlugin : PluginManager::getInstance()->getDisplayPlugins()) {
        QString name = displayPlugin->getName();
        if (pluginName == name) {
            newDisplayPlugin = displayPlugin;
            break;
        }
    }

    if (newDisplayPlugin) {
        setDisplayPlugin(newDisplayPlugin);
    }
}

void Application::raiseWindow() const {
    qApp->getWindow()->raise();
    qApp->getWindow()->activateWindow();
}

void Application::goToHandedOffUrl(const QString& url) const {
    // a later launch's --url, handed over by SingleInstance: go there, as that launch would have
    if (DependencyManager::get<AddressManager>()->handleLookupString(url)) {
        raiseWindow();
    }
}

CompositorHelper& Application::getApplicationCompositor() const {
    return *DependencyManager::get<CompositorHelper>();
}


// virtual functions required for PluginContainer
ui::Menu* Application::getPrimaryMenu() {
    auto appMenu = _window->menuBar();
    auto uiMenu = dynamic_cast<ui::Menu*>(appMenu);
    return uiMenu;
}

void Application::showDisplayPluginsTools(bool show) {
    DependencyManager::get<DialogsManager>()->hmdTools(show);
}

GLWidget* Application::getPrimaryWidget() {
    return _glWidget;
}

MainWindow* Application::getPrimaryWindow() {
    return getWindow();
}

QOpenGLContext* Application::getPrimaryContext() {
    return _glWidget->qglContext();
}

bool Application::makeRenderingContextCurrent() {
    return true;
}

bool Application::isForeground() const {
    return _isForeground && !_window->isMinimized();
}

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

void Application::updateSystemTabletMode() {
    if (_settingsLoaded && !getLoginDialogPoppedUp()) {
        qApp->setProperty(hifi::properties::HMD, isHMDMode());
        if (isHMDMode()) {
            DependencyManager::get<TabletScriptingInterface>()->setToolbarMode(getHmdTabletBecomesToolbarSetting());
        } else {
            DependencyManager::get<TabletScriptingInterface>()->setToolbarMode(getDesktopTabletBecomesToolbarSetting());
        }
    }
}

QUuid Application::getTabletScreenID() const {
    auto HMD = DependencyManager::get<HMDScriptingInterface>();
    return HMD->getCurrentTabletScreenID();
}

QUuid Application::getTabletHomeButtonID() const {
    auto HMD = DependencyManager::get<HMDScriptingInterface>();
    return HMD->getCurrentHomeButtonID();
}

QUuid Application::getTabletFrameID() const {
    auto HMD = DependencyManager::get<HMDScriptingInterface>();
    return HMD->getCurrentTabletFrameID();
}

QVector<QUuid> Application::getTabletIDs() const {
    // Most important first.
    QVector<QUuid> result;
    auto HMD = DependencyManager::get<HMDScriptingInterface>();
    result << HMD->getCurrentTabletScreenID();
    result << HMD->getCurrentHomeButtonID();
    result << HMD->getCurrentTabletFrameID();
    return result;
}

void Application::setAvatarOverrideUrl(const QUrl& url, bool save) {
    _avatarOverrideUrl = url;
    _saveAvatarOverrideUrl = save;
}

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

void Application::openDirectory(const QString& path) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "openDirectory", Q_ARG(const QString&, path));
        return;
    }

    QString dirPath = path;
    const QString FILE_SCHEME = "file:///";
    if (dirPath.startsWith(FILE_SCHEME)) {
        dirPath.remove(0, FILE_SCHEME.length());
    }
    QFileInfo fileInfo(dirPath);
    if (fileInfo.isDir()) {
        auto scheme = QUrl(path).scheme();
        QDesktopServices::unsetUrlHandler(scheme);
        QDesktopServices::openUrl(path);
        QDesktopServices::setUrlHandler(scheme, this, "showUrlHandler");
    }
}

void Application::showUrlHandler(const QUrl& url) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "showUrlHandler", Q_ARG(const QUrl&, url));
        return;
    }

    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toDisplayString();
    const QString name = QFileInfo(path).fileName();
    ModalDialogListener* dlg = OffscreenUi::asyncQuestion(
        name.isEmpty() ? QString("Open this file?") : QString::fromUtf8("Open “%1”?").arg(name),
        "A script wants to open this file on your computer.", QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel);
    if (QQuickItem* box = dlg->getDialogItem()) {
        box->setProperty("detailedText", path);
    }
    QObject::connect(dlg, &ModalDialogListener::response, this, [=](QVariant answer) {
        QObject::disconnect(dlg, &ModalDialogListener::response, this, nullptr);
        if (QMessageBox::Open == static_cast<QMessageBox::StandardButton>(answer.toInt())) {
            // Unset the handler, open the URL, and the reset the handler
            QDesktopServices::unsetUrlHandler(url.scheme());
            QDesktopServices::openUrl(url);
            QDesktopServices::setUrlHandler(url.scheme(), this, "showUrlHandler");
        }
    });
}

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

#if defined(Q_OS_ANDROID)
void Application::beforeEnterBackground() {
    auto nodeList = DependencyManager::get<NodeList>();
    nodeList->setSendDomainServerCheckInEnabled(false);
    nodeList->reset("Entering background", true);
    clearDomainOctreeDetails();
}



void Application::enterBackground() {
    QMetaObject::invokeMethod(DependencyManager::get<AudioClient>().data(),
                              "stop", Qt::BlockingQueuedConnection);
// Quest only supports one plugin which can't be deactivated currently
#if !defined(ANDROID_APP_QUEST_INTERFACE)
    if (getActiveDisplayPlugin()->isActive()) {
        getActiveDisplayPlugin()->deactivate();
    }
#endif
}

void Application::enterForeground() {
    QMetaObject::invokeMethod(DependencyManager::get<AudioClient>().data(),
                                  "start", Qt::BlockingQueuedConnection);
// Quest only supports one plugin which can't be deactivated currently
#if !defined(ANDROID_APP_QUEST_INTERFACE)
    if (!getActiveDisplayPlugin() || getActiveDisplayPlugin()->isActive() || !getActiveDisplayPlugin()->activate()) {
        qWarning() << "Could not re-activate display plugin";
    }
#endif
    auto nodeList = DependencyManager::get<NodeList>();
    nodeList->setSendDomainServerCheckInEnabled(true);
}


void Application::toggleAwayMode(){
    QKeyEvent event = QKeyEvent (QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent (this, &event);
}


#endif
