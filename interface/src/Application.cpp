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
#include "ImportExtensions.h"
#include "ApplicationConstants.h"
#include "LambdaEvent.h"
#include "DeadlockWatchdog.h"

#include <thread>

#include <QtCore/QThreadPool>

#include <QtGui/QClipboard>

#include <shared/QtHelpers.h>
#include <AccountManager.h>
#include <BuildInfo.h>
#include <MainWindow.h>
#include <OffscreenUi.h>
#include <plugins/CodecPlugin.h>
#include <plugins/PluginManager.h>
#include <display-plugins/CompositorHelper.h>
#include "AudioClient.h"

#include "InterfaceLogging.h"

#include "Util.h"

#include <GPUIdent.h>

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

// The window's cursor is a WindowCursor (WindowCursor.h), the member _windowCursor, reached by getWindowCursor().

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

// The entities' clipboard (export, import, paste) is an EntityClipboard (EntityClipboard.h), the member _entityClipboard,
// reached by getEntityClipboard().

// loadSettings and saveSettings are in ApplicationSettings.cpp.

// init (the avatar manager, the entity tree renderer, the physics engine, the entity simulation and the game workload
// brought up; the interface stage calls it) is in ApplicationStartup.cpp.

// The sign-in gate (pauseUntilLoginDetermined, resumeAfterLoginDialogActionTaken) is in ApplicationUi.cpp.

// The avatar's scripts (loadAvatarScripts, unloadAvatarScripts) are in ApplicationAvatar.cpp.

// The per-frame helpers (updateLOD, pushPostUpdateLambda, updateMyAvatarLookAtPosition, updateThreads) are in ApplicationFrame.cpp.

// The overlays (toggleOverlays, setOverlaysVisible, centerUI) are in ApplicationUi.cpp.

// The camera and the view frustums are a ViewState (ViewState.h), the member _viewState, reached by getViewState();
// getCamera(), the view-state overrides and the two View menu slots forward to it.

// requeryOctree and resetPhysicsReadyInformation are in ApplicationDomain.cpp.

// reloadResourceCaches (the domain's content reloaded) is in ApplicationDomain.cpp.

// The keyboard focus and its highlight are a KeyboardFocus (KeyboardFocus.h), the member _keyboardFocus, reached by
// getKeyboardFocus().

// The simulation step and what frames it (updateDialogs, the physics gate setArrivalWaitsForNearby/tryToEnablePhysics,
// update, updateRenderArgs) is in ApplicationFrame.cpp.

// The per-frame queries to the servers (queryAvatars, sendNackPackets, queryOctree) are in ApplicationDomain.cpp.

// isHMDMode is in ApplicationDisplay.cpp.

float Application::getNumCollisionObjects() const {
    return _physicsEngine ? _physicsEngine->getNumCollisionObjects() : 0;
}

// getTargetRenderFrameRate and getDesirableApplicationGeometry are in ApplicationDisplay.cpp.

// getMyAvatar and getAvatarPosition are in ApplicationAvatar.cpp.

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

// The snapshots asked of the next frame are a SnapshotRequests (SnapshotRequests.h), the member _snapshotRequests, reached
// by getSnapshotRequests().

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
