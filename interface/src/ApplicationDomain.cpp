//
//  ApplicationDomain.cpp
//  interface/src
//
//  Application's side of the domain session: the connection's events (a node added, activated or killed; the domain
//  URL changed, the domain resetting, a connection refused, a packet version mismatch, the session id), the queries
//  the client sends the entity and avatar servers each frame (queryOctree with its NACKs, queryAvatars), the octree
//  stats and the packet accounting, the physics-ready bookkeeping (requeryOctree, resetPhysicsReadyInformation),
//  the serverless domains (a world from a file) and the interstitial mode, and the octree packet rate. The members
//  as they stood in Application.cpp, in their order; a home of their own until an owner takes them
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

QSharedPointer<OffscreenUi> getOffscreenUI();   // Application.cpp's
QString worldNameForPopup();                     // Application.cpp's: the world's name for a popup's text

// constants and the one setting only the domain session reads (the constructor's initializer list reads the setting too)
static const int ENTITY_SERVER_CONNECTION_TIMEOUT = 5000;
static const float INITIAL_QUERY_RADIUS = 10.0f;  // priority radius for entities before physics enabled
Setting::Handle<int> maxOctreePacketsPerSecond{"maxOctreePPS", DEFAULT_MAX_OCTREE_PPS};

void Application::setFailedToConnectToEntityServer() {
    _failedToConnectToEntityServer = true;
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

void Application::setSessionUUID(const QUuid& sessionUUID) const {
    Physics::setSessionUUID(sessionUUID);
}

void Application::notifyPacketVersionMismatch() {
    if (!_notifiedPacketVersionMismatchThisDomain && !isInterstitialMode()) {
        _notifiedPacketVersionMismatchThisDomain = true;

        const QString world = worldNameForPopup();
        OffscreenUi::asyncWarning((world.isEmpty() ? QString("This world") : world) + " runs a different version of Darkride",
                                  "Some things in it may not look right.");
    }
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
