//
//  ApplicationKeyboardFocus.cpp
//  interface/src
//
//  Application's keyboard focus: which entity has it (getKeyboardFocusEntity, setKeyboardFocusEntity: the focused
//  entity recorded, its renderable given the window, the keyboard-mouse device's focus-out, the sign-in dialog's
//  special case) and the highlight drawn around it, a local line-box entity made on demand and moved to the entity
//  (setKeyboardFocusHighlight), with the highlight's id and expansion factor that KeyboardFocusHighlight.h declares
//  (the frame loop follows the focused entity with it; the shutdown deletes it). The members as they stood in
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

// the highlight's id and expansion factor, declared in KeyboardFocusHighlight.h; ApplicationFrame.cpp and
// ApplicationShutdown.cpp read them
const float FOCUS_HIGHLIGHT_EXPANSION_FACTOR = 1.05f;   // declared in KeyboardFocusHighlight.h
// FIXME move to header, or better yet, design some kind of UI manager
// to take care of highlighting keyboard focused items, rather than
// continuing to overburden Application.cpp
QUuid _keyboardFocusHighlightID;   // declared in KeyboardFocusHighlight.h

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
