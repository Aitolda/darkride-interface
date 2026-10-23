//
//  ApplicationCamera.cpp
//  interface/src
//
//  Application's camera and view: the camera modes cycled and set from the menu (cycleCamera, cameraModeChanged,
//  cameraMenuChanged), the switch between first and third person as the boom length changes (changeViewAsNeeded),
//  the head pitch reset when the player is not centred in view (rotationModeChanged), the pick ray from a screen
//  point (computePickRay), the view frustums copied under their lock (copyViewFrustum, copyDisplayViewFrustum), the
//  sensors, avatar and audio reset (resetSensors) and the reticle's position (getMouse). The camera's per-frame
//  update stays with the frame loop (ApplicationFrame.cpp). The members as they stood in Application.cpp, in their
//  order; a home of their own until an owner takes them (ARCHITECTURE.md §5.4 step 4).
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

ivec2 Application::getMouse() const {
    return getApplicationCompositor().getReticlePosition();
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

void Application::rotationModeChanged() const {
    if (!Menu::getInstance()->isOptionChecked(MenuOption::CenterPlayerInView)) {
        getMyAvatar()->setHeadPitch(0);
    }
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
