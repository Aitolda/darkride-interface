//
//  ApplicationDisplay.cpp
//  interface/src
//
//  Application's GL context, display plug-ins and window: the main context and the share contexts for QML and
//  Chromium (initializeGL), the display plug-ins found, chosen, switched and listed in the menu
//  (initializeDisplayPlugins, setDisplayPlugin, updateDisplayMode, switchDisplayMode, setActiveDisplayPlugin,
//  getActiveDisplayPlugin; initPlugins reads the enable lists), the render engine's start (initializeRenderEngine),
//  the window and its geometry (resizeEvent, resizeGL, getDesirableApplicationGeometry, the canvas, UI and device
//  sizes, focus, raise, foreground, active and minimized), the HMD's session and queries (the eye projections and
//  offsets, the sensor pose, the controllers present) and the display's present callback and frame-rate regime
//  (onPresent, getTargetRenderFrameRate, isThrottleRendering, getRenderResolutionScale). The members as they stood in
//  Application.cpp, in their order; a home of their own until an owner takes them (ARCHITECTURE.md §5.4 step 4).
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

QSharedPointer<OffscreenUi> getOffscreenUI();   // Application.cpp's
extern DisplayPluginList getDisplayPlugins();   // the statically provided display plug-ins (plugins/src); updateDisplayMode reads them

// constants and the one setting key only the display code reads (the shutdown reads the key too, through ApplicationConstants.h)
static const unsigned int THROTTLED_SIM_FRAMERATE = 15;
static const int THROTTLED_SIM_FRAME_PERIOD_MS = MSECS_PER_SECOND / THROTTLED_SIM_FRAMERATE;
static const int INTERVAL_TO_CHECK_HMD_WORN_STATUS = 500; // milliseconds
static const QString DESKTOP_DISPLAY_PLUGIN_NAME = "Desktop";
const QString ACTIVE_DISPLAY_PLUGIN_SETTING_NAME = "activeDisplayPlugin";   // declared in ApplicationConstants.h

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

bool Application::isHMDMode() const {
    return getActiveDisplayPlugin()->isHmd();
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

float Application::getRenderResolutionScale() const {
    return RenderScriptingInterface::getInstance()->getViewportResolutionScale();
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

// The octree packet rate (setMaxOctreePacketsPerSecond, getMaxOctreePacketsPerSecond) is in ApplicationDomain.cpp.

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

void addDisplayPluginToMenu(const DisplayPluginPointer& displayPlugin, int index, bool active) {   // initializeUi (ApplicationUi.cpp) reads it too
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

bool Application::hasRiftControllers() {
    return PluginUtils::isOculusTouchControllerAvailable();
}

bool Application::hasViveControllers() {
    return PluginUtils::isViveControllerAvailable();
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
