//
//  PhysicsServer.cpp
//  assignment-client/src/physics
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "PhysicsServer.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

#include <EntityItemProperties.h>
#include <EntityNodeData.h>
#include <EntityScriptingInterface.h>
#include <EntityTree.h>
#include <PhysicsHelpers.h>
#include <PickFilter.h>
#include <PhysicsLogging.h>
#include <ResourceManager.h>
#include <TrackEntityItem.h>
#include <TrackSimulation.h>

#include "../AssignmentDynamicFactory.h"
#include "../entities/AssignmentParentFinder.h"

namespace {
    const QString PHYSICS_SERVER_LOGGING_NAME = "physics-server";
    const int STEP_INTERVAL_MSECS = 11;   // a little faster than the 90 Hz substep; the engine's paced clock does the rest
    const quint64 QUERY_INTERVAL = USECS_PER_SECOND;   // the entity server is asked again this often
    const int EDIT_PACKETS_PER_SECOND = 300;
}

PhysicsServer::PhysicsServer(ReceivedMessage& message) : ThreadedAssignment(message) {
    DependencyManager::registerInheritance<EntityDynamicFactoryInterface, AssignmentDynamicFactory>();
    DependencyManager::set<AssignmentDynamicFactory>();
    DependencyManager::registerInheritance<SpatialParentFinder, AssignmentParentFinder>();
    DependencyManager::set<EntityScriptingInterface>(false)->setPacketSender(&_entityEditSender);
    DependencyManager::set<ResourceManager>();

    auto& packetReceiver = DependencyManager::get<NodeList>()->getPacketReceiver();
    packetReceiver.registerListenerForTypes({ PacketType::OctreeStats, PacketType::EntityData, PacketType::EntityErase },
                                            this, "handleOctreePacket");
}

PhysicsServer::~PhysicsServer() {
}

void PhysicsServer::run() {
    ThreadedAssignment::commonInit(PHYSICS_SERVER_LOGGING_NAME, NodeType::PhysicsServer);

    auto nodeList = DependencyManager::get<NodeList>();
    nodeList->addSetOfNodeTypesToNodeInterestSet({ NodeType::EntityServer });
    connect(nodeList.data(), &LimitedNodeList::nodeActivated, this, &PhysicsServer::nodeActivated);
    connect(nodeList.data(), &LimitedNodeList::nodeKilled, this, &PhysicsServer::nodeKilled);
    connect(nodeList.data(), &LimitedNodeList::uuidChanged, this, [](const QUuid& sessionUUID, const QUuid&) {
        Physics::setSessionUUID(sessionUUID);
    });
    Physics::setSessionUUID(nodeList->getSessionUUID());
    Physics::setServerAuthority(true);

    _entityEditSender.setPacketsPerSecond(EDIT_PACKETS_PER_SECOND);

    _entityViewer.init();
    EntityTreePointer tree = _entityViewer.getTree();

    // what this server sees of the domain: every Track, everything that rides one, and their ancestors (a car's
    // parent places it); nothing else, so a big world costs it nothing
    QJsonObject queryJSONParameters;
    queryJSONParameters[EntityJSONQueryProperties::TRACK_ID_PROPERTY] = EntityQueryFilterSymbol::NonDefault;
    queryJSONParameters[EntityJSONQueryProperties::ENTITY_TYPE_PROPERTY] = EntityTypes::getEntityTypeName(EntityTypes::Track);
    QJsonObject queryFlags;
    queryFlags[EntityJSONQueryProperties::INCLUDE_ANCESTORS_PROPERTY] = true;
    queryFlags[EntityJSONQueryProperties::INCLUDE_DESCENDANTS_PROPERTY] = false;
    queryJSONParameters[EntityJSONQueryProperties::FLAGS_PROPERTY] = queryFlags;
    _entityViewer.getOctreeQuery().setJSONParameters(queryJSONParameters);

    DependencyManager::set<AssignmentParentFinder>(tree);
    DependencyManager::get<EntityScriptingInterface>()->setEntityTree(tree);

    // the physics: the same engine and simulation a client runs, with no character and no workload regions
    ObjectMotionState::setShapeManager(&_shapeManager);
    _physicsEngine = std::make_shared<PhysicsEngine>(Vectors::ZERO);
    _physicsEngine->init();
    _entitySimulation = std::make_shared<PhysicalEntitySimulation>();
    _entitySimulation->init(tree, _physicsEngine, &_entityEditSender);
    tree->setSimulation(_entitySimulation);

    _stepTimer = new QTimer(this);
    _stepTimer->setTimerType(Qt::PreciseTimer);
    connect(_stepTimer, &QTimer::timeout, this, &PhysicsServer::step);
    _stepTimer->start(STEP_INTERVAL_MSECS);
    _running = true;
    _lastStatsTime = usecTimestampNow();
    _lastLogTime = _lastStatsTime;

    qCInfo(physics) << "physics-server running: the trains on the tracks are driven from here (PHYSICS.md round 5)";
}

void PhysicsServer::step() {
    if (!_running) {
        return;
    }
    EntityTreePointer tree = _entityViewer.getTree();
    if (!tree) {
        return;
    }

    quint64 now = usecTimestampNow();
    if (now - _lastQueryTime > QUERY_INTERVAL) {
        _entityViewer.queryOctree();
        _lastQueryTime = now;
    }

    // the same order as the client's update: the tree's bookkeeping, the physics transaction, the step, the results
    _entityViewer.update();
    _entitySimulation->removeDeadEntities();
    {
        PhysicsEngine::Transaction transaction;
        _entitySimulation->buildPhysicsTransaction(transaction);
        _physicsEngine->processTransaction(transaction);
        _entitySimulation->handleProcessedPhysicsTransaction(transaction);
    }
    _entitySimulation->applyDynamicChanges();
    _physicsEngine->forEachDynamic([&](EntityDynamicPointer dynamic) {
        dynamic->prepareForPhysicsSimulation();
    });
    tree->withWriteLock([&] {
        _physicsEngine->stepSimulation();
    });
    if (_physicsEngine->hasOutgoingChanges()) {
        auto& collisionEvents = _physicsEngine->getCollisionEvents();
        tree->withWriteLock([&] {
            const VectorOfMotionStates& outgoingChanges = _physicsEngine->getChangedMotionStates();
            _entitySimulation->handleChangedMotionStates(outgoingChanges);
            const VectorOfMotionStates& deactivations = _physicsEngine->getDeactivatedMotionStates();
            _entitySimulation->handleDeactivatedMotionStates(deactivations);
        });
        _entitySimulation->handleCollisionEvents(collisionEvents);
        _physicsEngine->updateContactMap();
        _physicsEngine->doOwnershipInfectionForConstraints();
    }
    _stepsTaken++;

    publishTrainStates();

    // a line in the log every ten seconds: the loop's rate, what it holds, what it has sent
    const quint64 LOG_INTERVAL = 10 * USECS_PER_SECOND;
    if (now - _lastLogTime > LOG_INTERVAL) {
        float seconds = (float)(now - _lastLogTime) / (float)USECS_PER_SECOND;
        quint64 steps = _stepsTaken - _stepsAtLastLog;
        qCInfo(physics) << "physics-server:" << (seconds > 0.0f ? (float)steps / seconds : 0.0f) << "steps/s,"
            << _physicsEngine->getNumSubsteps() << "substeps," << _physicsEngine->getNumCollisionObjects() << "bodies,"
            << _physicsEngine->getNumFrames() << "frames," << tree->getOctreeElementsCount() << "tree elements,"
            << _statesPublished << "train states published," << _entityEditSender.getPacketsPerSecond() << "pps";
        _lastLogTime = now;
        _stepsAtLastLog = _stepsTaken;
    }

    // the bids, the cars' updates and the train states go out from here: an assignment client's edit sender is
    // released and pumped by its script engine every frame, and this one has none. Without the release a lone edit (a
    // train state on its own) waits in a half-filled packet until a burst fills it.
    if (_entityEditSender.serversExist()) {
        _entityEditSender.releaseQueuedMessages();
        _entityEditSender.process();
    }
}

// The trains' states go out as ordinary edits of their Track entities, stamped with the domain time of the substep
// they describe; the entity server relays them to every client, which runs the follower on from there.
void PhysicsServer::publishTrainStates() {
    std::vector<TrackSimulation::Publication> publications;
    _entitySimulation->getTrackSimulation()->takePublications(publications);
    if (publications.empty()) {
        return;
    }
    EntityTreePointer tree = _entityViewer.getTree();
    quint64 now = usecTimestampNow();
    for (const auto& publication : publications) {
        EntityItemProperties properties;
        properties.setType(EntityTypes::Track);
        properties.setTrainDistance(publication.state.s);
        properties.setTrainSpeed(publication.state.speed);
        properties.setTrainReversed(publication.state.direction < 0);
        properties.setTrainDwell(publication.state.dwellRemaining);
        properties.setTrainStation(publication.state.station < 0 ? TrackEntityItem::NO_STATION : (quint16)publication.state.station);
        properties.setTrainTime(publication.domainTime);
        properties.setLastEdited(now);
        _entityEditSender.queueEditEntityMessage(PacketType::EntityEdit, tree, publication.trackID, properties);
        if (_statesPublished == 0) {
            qCInfo(physics) << "physics-server: first train state published for track" << publication.trackID << "at" << publication.state.s
                << "m, domain time" << publication.domainTime;
        }
        _statesPublished++;
    }
}

void PhysicsServer::nodeActivated(SharedNodePointer activatedNode) {
    if (activatedNode->getType() == NodeType::EntityServer) {
        _lastQueryTime = 0;   // ask the new entity server at once
    }
}

void PhysicsServer::nodeKilled(SharedNodePointer killedNode) {
    if (killedNode->getType() == NodeType::EntityServer) {
        bool hasAnotherEntityServer = false;
        auto nodeList = DependencyManager::get<NodeList>();
        nodeList->eachNodeBreakable([&hasAnotherEntityServer, &killedNode](const SharedNodePointer& node) {
            if (node->getType() == NodeType::EntityServer && node->getUUID() != killedNode->getUUID()) {
                hasAnotherEntityServer = true;
                return false;
            }
            return true;
        });
        if (!hasAnotherEntityServer) {
            // the world is gone until an entity server comes back; the trains start again from their cars then
            _entityViewer.clear();
        }
    }
}

void PhysicsServer::sendStatsPacket() {
    QJsonObject statsObject;

    quint64 now = usecTimestampNow();
    float seconds = (float)(now - _lastStatsTime) / (float)USECS_PER_SECOND;
    if (seconds > 0.0f) {
        _stepsPerSecond = (float)(_stepsTaken - _stepsAtLastStats) / seconds;
    }
    _lastStatsTime = now;
    _stepsAtLastStats = _stepsTaken;

    QJsonObject physicsStats;
    physicsStats["steps_per_second"] = _stepsPerSecond;
    physicsStats["substeps"] = (double)(_physicsEngine ? _physicsEngine->getNumSubsteps() : 0);
    physicsStats["bodies"] = _physicsEngine ? _physicsEngine->getNumCollisionObjects() : 0;
    physicsStats["frames"] = (double)(_physicsEngine ? _physicsEngine->getNumFrames() : 0);
    physicsStats["train_states_published"] = (double)_statesPublished;

    QJsonArray trains;
    EntityTreePointer tree = _entityViewer.getTree();
    if (tree) {
        QJsonObject octreeStats;
        octreeStats["elementCount"] = (double)OctreeElement::getNodeCount();
        statsObject["octree_stats"] = octreeStats;
        QVector<QUuid> trackIDs;
        unsigned int searchFilter = PickFilter::getBitMask(PickFilter::FlagBit::DOMAIN_ENTITIES) |
                                    PickFilter::getBitMask(PickFilter::FlagBit::AVATAR_ENTITIES);
        tree->withReadLock([&] {
            tree->evalEntitiesInSphereWithType(Vectors::ZERO, (float)TREE_SCALE, EntityTypes::Track, PickFilter(searchFilter), trackIDs);
            for (const QUuid& trackID : trackIDs) {
                auto track = std::dynamic_pointer_cast<TrackEntityItem>(tree->findEntityByID(trackID));
                if (!track) {
                    continue;
                }
                QJsonObject train;
                train["track"] = uuidStringWithoutCurlyBraces(track->getID());
                train["name"] = track->getName();
                train["distance_m"] = track->getTrainDistance();
                train["speed_m_s"] = track->getTrainSpeed();
                train["reversed"] = track->getTrainReversed();
                train["dwell_s"] = track->getTrainDwell();
                train["station"] = track->getTrainStation() == TrackEntityItem::NO_STATION ? -1 : (int)track->getTrainStation();
                train["time"] = (double)track->getTrainTime();
                trains.append(train);
            }
        });
    }
    physicsStats["trains"] = trains;
    statsObject["physics_stats"] = physicsStats;

    addPacketStatsAndSendStatsPacket(statsObject);
}

void PhysicsServer::handleOctreePacket(QSharedPointer<ReceivedMessage> message, SharedNodePointer senderNode) {
    auto packetType = message->getType();

    if (packetType == PacketType::OctreeStats) {
        int statsMessageLength = OctreeHeadlessViewer::parseOctreeStats(message, senderNode);
        if (message->getSize() > statsMessageLength) {
            // pull out the piggybacked packet and create a new QSharedPointer<NLPacket> for it
            int piggyBackedSizeWithHeader = message->getSize() - statsMessageLength;
            auto buffer = std::unique_ptr<char[]>(new char[piggyBackedSizeWithHeader]);
            memcpy(buffer.get(), message->getRawMessage() + statsMessageLength, piggyBackedSizeWithHeader);
            auto newPacket = NLPacket::fromReceivedPacket(std::move(buffer), piggyBackedSizeWithHeader, message->getSenderSockAddr());
            message = QSharedPointer<ReceivedMessage>::create(*newPacket);
        } else {
            return;   // no piggyback data
        }
        packetType = message->getType();
    }

    if (packetType == PacketType::EntityData) {
        _entityViewer.processDatagram(*message, senderNode);
    } else if (packetType == PacketType::EntityErase) {
        _entityViewer.processEraseMessage(*message, senderNode);
    }
}

void PhysicsServer::aboutToFinish() {
    _running = false;
    if (_stepTimer) {
        _stepTimer->stop();
    }
    if (_entitySimulation) {
        _entitySimulation->clearEntities();
    }
    _entityViewer.clear();
    DependencyManager::get<EntityScriptingInterface>()->setEntityTree(nullptr);
    DependencyManager::get<ResourceManager>()->cleanup();
    DependencyManager::destroy<EntityScriptingInterface>();
    DependencyManager::destroy<ResourceManager>();
    DependencyManager::destroy<AssignmentDynamicFactory>();
    DependencyManager::destroy<AssignmentParentFinder>();
    Physics::setServerAuthority(false);
}
