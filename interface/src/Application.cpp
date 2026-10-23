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

// The camera modes (cycleCamera, cameraModeChanged, changeViewAsNeeded, cameraMenuChanged) are in ApplicationCamera.cpp.

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

std::shared_ptr<MyAvatar> Application::getMyAvatar() const {
    return DependencyManager::get<AvatarManager>()->getMyAvatar();
}

glm::vec3 Application::getAvatarPosition() const {
    return getMyAvatar()->getWorldPosition();
}

// copyViewFrustum and copyDisplayViewFrustum are in ApplicationCamera.cpp.

// resetSensors is in ApplicationCamera.cpp.

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
