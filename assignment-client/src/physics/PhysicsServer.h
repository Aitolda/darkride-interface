//
//  PhysicsServer.h
//  assignment-client/src/physics
//
//  The physics server (PHYSICS.md 4.5, round 5): the domain's server-authoritative simulation. An assignment client with a
//  headless entity tree of the domain's Track entities and everything that rides one, the physics library's engine and
//  simulation (one Jolt world for the domain and one per frame, as a client has) stepped at 90 Hz on a timer, and no
//  renderer. It owns every car on a track at SERVER priority, drives the trains with the same follower every client
//  runs, and publishes each train's state on its Track entity through the entity server, as ordinary edits, when
//  something happens and once a second besides; clients run the state on in the same substeps and blend corrections.
//  If it dies, ownership of the cars expires as any owner's does and the clients drive them as in round 4.
//
//  Not in this round: bodies for avatars, entities marked for the server beyond track cars, vehicle models with a
//  driver, a direct state stream to clients.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_PhysicsServer_h
#define hifi_PhysicsServer_h

#include <QtCore/QObject>
#include <QtCore/QTimer>

#include <EntityEditPacketSender.h>
#include <PhysicalEntitySimulation.h>
#include <PhysicsEngine.h>
#include <ShapeManager.h>
#include <ThreadedAssignment.h>

#include "../entities/EntityTreeHeadlessViewer.h"

class PhysicsServer : public ThreadedAssignment {
    Q_OBJECT

public:
    PhysicsServer(ReceivedMessage& message);
    ~PhysicsServer();

    void aboutToFinish() override;

public slots:
    void run() override;
    void nodeActivated(SharedNodePointer activatedNode);
    void nodeKilled(SharedNodePointer killedNode);
    void sendStatsPacket() override;

private slots:
    void handleOctreePacket(QSharedPointer<ReceivedMessage> message, SharedNodePointer senderNode);
    void step();

private:
    void publishTrainStates();

    EntityEditPacketSender _entityEditSender;
    EntityTreeHeadlessViewer _entityViewer;
    ShapeManager _shapeManager;
    PhysicsEnginePointer _physicsEngine;
    PhysicalEntitySimulationPointer _entitySimulation;
    QTimer* _stepTimer { nullptr };
    quint64 _lastQueryTime { 0 };
    bool _running { false };

    // stats
    quint64 _stepsTaken { 0 };
    quint64 _statesPublished { 0 };
    quint64 _lastStatsTime { 0 };
    quint64 _lastLogTime { 0 };
    quint64 _stepsAtLastLog { 0 };
    quint64 _stepsAtLastStats { 0 };
    float _stepsPerSecond { 0.0f };
};

#endif // hifi_PhysicsServer_h
