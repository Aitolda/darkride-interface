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


const QString SYSTEM_TABLET = "com.highfidelity.interface.tablet.system";   // declared in ApplicationConstants.h; ApplicationUi.cpp reads it too
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

// FIXME move to header, or better yet, design some kind of UI manager
// to take care of highlighting keyboard focused items, rather than
// continuing to overburden Application.cpp
QUuid _keyboardFocusHighlightID;   // declared in KeyboardFocusHighlight.h


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

// The name of the world being joined, for a popup, or "" when it has none to show (an address is not a name).
QString worldNameForPopup() {   // ApplicationDomain.cpp reads it too
    return DependencyManager::get<AddressManager>()->getDomainLabel().trimmed();
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

// The GL context and its share contexts, the display plug-ins and the render engine's start (initializeGL,
// initializeDisplayPlugins, initializeRenderEngine) are in ApplicationDisplay.cpp.

// The sign-in screen and the interface's bring-up (showLoginScreen, initializeUi) are in ApplicationUi.cpp.


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

// showHelp (the controls reference on the tablet) is in ApplicationUi.cpp.

// The window's resize (resizeEvent, resizeGL, with verticalFieldOfView) is in ApplicationDisplay.cpp.

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

// The serverless domains and the interstitial mode (isServerlessMode, setIsInterstitialMode, setIsServerlessMode,
// prepareServerlessDomainContents, loadServerlessDomain, loadErrorDomain) are in ApplicationDomain.cpp.

bool Application::importImage(const QString& urlString) {
    qCDebug(interfaceapp) << "An image file has been dropped in";
    QString filepath(urlString);
    filepath.remove("file:///");
    addAssetToWorld(filepath, "", false, false);
    return true;
}

// thread-safe
// onPresent (the display plug-in's present callback) is in ApplicationDisplay.cpp.

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

// The sign-in gate (pauseUntilLoginDetermined, resumeAfterLoginDialogActionTaken) is in ApplicationUi.cpp.

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

// The overlays (toggleOverlays, setOverlaysVisible, centerUI) are in ApplicationUi.cpp.

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

// requeryOctree and resetPhysicsReadyInformation are in ApplicationDomain.cpp.

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

// The per-frame queries to the servers (queryAvatars, sendNackPackets, queryOctree) are in ApplicationDomain.cpp.

// isHMDMode is in ApplicationDisplay.cpp.

float Application::getNumCollisionObjects() const {
    return _physicsEngine ? _physicsEngine->getNumCollisionObjects() : 0;
}

// getTargetRenderFrameRate and getDesirableApplicationGeometry are in ApplicationDisplay.cpp.

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

// The connection's events and the octree accounting (clearDomainOctreeDetails, domainURLChanged, goToErrorDomainURL,
// resettingDomain, nodeAdded, nodeActivated, nodeKilled, trackIncomingOctreePacket, gpuTextureMemSizeStable,
// processOctreeStats) are in ApplicationDomain.cpp.

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

// setSessionUUID is in ApplicationDomain.cpp.

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

// getRenderResolutionScale is in ApplicationDisplay.cpp.

// notifyPacketVersionMismatch is in ApplicationDomain.cpp.

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
