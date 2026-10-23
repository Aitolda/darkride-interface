//
//  ApplicationUi.cpp
//  interface/src
//
//  Application's user-interface bring-up and sign-in gate. The bring-up: the QML desktop, the system tablet, the
//  menu and the preferences brought up in order (initializeUi); the tablet toggled, its toolbar mode and its entity
//  ids (toggleTabletUI, updateSystemTabletMode, getTabletScreenID, getTabletHomeButtonID, getTabletFrameID,
//  getTabletIDs); the overlays (toggleOverlays, setOverlaysVisible, centerUI); the controls reference (showHelp).
//  The gate: the sign-in screen (showLoginScreen); the pause that hides the avatar, stops the movement mappings,
//  disconnects the domain and hides the menus until an account is chosen (pauseUntilLoginDetermined); the resume
//  that undoes it, loads the scripts and goes to the starting location (resumeAfterLoginDialogActionTaken); the
//  headset's sign-in dialog as a web entity in front of the camera, kept there, and taken down
//  (createLoginDialog, updateLoginDialogPosition, onDismissedLoginDialog); the avatar-inputs bar (createAvatarInputsBar,
//  destroyAvatarInputsBar). The members as they stood in Application.cpp, in their order; a home of their own until
//  an owner takes them (ARCHITECTURE.md §5.4 step 4).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "Application.h"
#include "ApplicationConstants.h"
#include "ClientOptions.h"
#include "DeadlockWatchdog.h"
#include <thread>
#include <glm/glm.hpp>
#include <QtQml/QQmlContext>
#include <shared/QtHelpers.h>
#include <shared/GlobalAppProperties.h>
#include <StartupProfiler.h>
#include <AccountManager.h>
#include <AddressManager.h>
#include <EntityScriptingInterface.h>
#include <ErrorDialog.h>
#include <controllers/UserInputMapper.h>
#include <MainWindow.h>
#include <OffscreenUi.h>
#include <ui/OffscreenQmlSurfaceCache.h>
#include <PathUtils.h>
#include <plugins/PluginManager.h>
#include <plugins/PluginUtils.h>
#include <ScriptEngines.h>
#include <ui/TabletScriptingInterface.h>
#include <ui/ToolbarScriptingInterface.h>
#include <ui/types/ContextAwareProfile.h>
#include <Tooltip.h>
#include <Preferences.h>
#include <display-plugins/CompositorHelper.h>
#include <RenderableEntityItem.h>
#include <RenderableTextEntityItem.h>
#include <RenderableWebEntityItem.h>
#include "GLCanvas.h"
#include "scripting/Audio.h"
#include "scripting/PlatformInfoScriptingInterface.h"
#include "scripting/AccountServicesScriptingInterface.h"
#include "scripting/HMDScriptingInterface.h"
#include "scripting/TTSScriptingInterface.h"
#include "ui/ResourceImageItem.h"
#include "ui/AddressBarDialog.h"
#include "ui/DialogsManager.h"
#include "ui/LoginDialog.h"
#include "ui/Stats.h"
#include "ui/Keyboard.h"
#include "webbrowser/WebBrowserSuggestionsEngine.h"
#if defined(Q_OS_ANDROID)
#include <android/log.h>
#include "AndroidHelper.h"
#endif

using namespace std;

QSharedPointer<OffscreenUi> getOffscreenUI();   // Application.cpp's
QString labelledTitle(const QString& title);     // ApplicationDisplay.cpp's: a window title with the client label
extern void setupPreferences();                  // ui/PreferencesDialog.cpp's
void addDisplayPluginToMenu(const DisplayPluginPointer& displayPlugin, int index, bool active = false);   // ApplicationDisplay.cpp's; initializeUi reads it

// constants only this code reads; the sign-in dialog's setting (the shutdown clears it, ApplicationShutdown.cpp) and the
// avatar-inputs bar's QML (declared in ApplicationConstants.h; the constructor reads it)
static const QString INFO_HELP_PATH = "html/tabletHelp.html";
static const QString STANDARD_TO_ACTION_MAPPING_NAME = "Standard to Action";
static const QString NO_MOVEMENT_MAPPING_NAME = "Standard to Action (No Movement)";
static const QString NO_MOVEMENT_MAPPING_JSON = PathUtils::resourcesPath() + "/controllers/standard_nomovement.json";
Setting::Handle<bool> loginDialogPoppedUp{"loginDialogPoppedUp", false};
const QUrl AVATAR_INPUTS_BAR_QML = PathUtils::qmlUrl("AvatarInputsBar.qml");   // declared in ApplicationConstants.h

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
            _windowCursor.show(compositorHelper->getAllowMouseCapture() ?
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
    _keyboardFocus.setFocusedEntity(_loginDialogID);
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
