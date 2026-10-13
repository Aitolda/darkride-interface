//
//  VehicleSimulation.h
//  libraries/physics/src
//
//  The driven vehicles (PHYSICS.md 4.6, 4.8 and round 6). For every entity with a wheeled vehicle group that has a body in
//  the root world: Jolt's wheeled vehicle constraint built from the entity's spec (WheeledVehicleSpec), the driver's inputs
//  applied to it every substep, and the three roles of PHYSICS.md 4.6 in one class, the same code on the physics server
//  and on every client:
//
//  - the authority (the physics server when the domain has one, else the client that owns the vehicle) applies the
//    driver's inputs in sequence, one per substep, and publishes the vehicle's pose, velocities, the last input applied
//    and its sequence number in its ordinary ownership updates (EntityMotionState::sendUpdate asks fillState);
//  - the driver's client, when it is not the authority, applies its own inputs at once (client-side prediction), numbers
//    them, keeps a record of the pose after each, and sends the last few in every VehicleInput packet; when a state from
//    the authority names the last sequence it applied, the record for that sequence is compared with it, and if the error
//    is more than a few centimetres the world is rewound to that substep (the other dynamic bodies from a ring of
//    snapshots, the vehicle from the state) and the inputs since are replayed (reconciliation). The jump that leaves is
//    smoothed out of the shown pose over a tenth of a second;
//  - every other client runs the same vehicle from the last known inputs (extrapolation with the model) and, when a
//    state arrives, rewinds to its time and replays the same way, smoothing the difference over a quarter of a second.
//
//  Wheel entities (type "wheel") that are children of the chassis are placed from the local simulation every frame;
//  they cost no packets. A vehicle's transform is driven locally everywhere (EntityItem::setTransformDrivenLocally), so a
//  network update of it is a correction to adopt, never a placement.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_VehicleSimulation_h
#define hifi_VehicleSimulation_h

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QtCore/QUuid>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <EntityItem.h>
#include <VehicleInput.h>
#include <WheeledVehicleSpec.h>

#include "PhysicsEngine.h"

namespace JPH {
    class VehicleConstraint;
    class VehicleCollisionTester;
    class WheeledVehicleController;
    class Shape;
}

class EntityItemProperties;
class EntityMotionState;
class PhysicsWorld;

class VehicleSimulation : public PhysicsEngine::SubstepListener {
public:
    static constexpr float REPLAY_POSITION_THRESHOLD = 0.02f;          // m: a prediction this far off is rewound and replayed
    static constexpr float REPLAY_ANGLE_THRESHOLD = 0.0349f;           // rad (2 degrees)
    static constexpr float SNAP_DISTANCE = 3.0f;                       // m: a correction larger than this is shown at once
    static constexpr float DRIVER_BLEND_TIME = 0.1f;                   // s: the driver's residual decays with this time constant
    static constexpr float OTHERS_BLEND_TIME = 0.25f;                  // s: everyone else's
    static constexpr int MAX_REPLAY_STEPS = 45;                        // half a second of substeps
    static constexpr int SNAPSHOT_RING = 180;                          // two seconds of substeps
    static constexpr int MAX_QUEUED_INPUTS = 6;                        // the server keeps at most this many of a driver's inputs waiting
    static constexpr int UPDATE_STEPS = 9;                             // the authority publishes every 9 substeps (10 Hz) while awake
    static constexpr quint64 DRIVER_SILENCE = 2 * USECS_PER_SECOND;    // no input for this long: the authority eases off and brakes

    VehicleSimulation();
    ~VehicleSimulation();

    void setEngine(PhysicsEngine* engine) { _engine = engine; }
    void setServerless(bool serverless) { _serverless = serverless; }

    // the chassis's motion state has a body: build its vehicle (idempotent; the body is checked every substep)
    void attach(EntityMotionState* state);
    void detach(EntityMotionState* state);
    bool has(EntityMotionState* state) const { return _vehicles.find(state) != _vehicles.end(); }
    size_t getNumVehicles() const { return _vehicles.size(); }
    void clear();

    // PhysicsEngine::SubstepListener
    void preSubstep(uint32_t step, float dt) override;
    void postSubstep(uint32_t step, float dt) override;

    // once per frame on the simulation's thread with the tree locked: the wheel entities follow the wheels
    void updateVisuals();
    // once per frame: the driver's inputs since the last call go to the physics server
    void sendInputs();
    // the server's side: a driver's inputs arrived (any thread)
    void receiveInputs(const QUuid& vehicleID, const QUuid& sender, const std::vector<VehicleInput>& inputs);

    // the authority's state for an update: the applied input, its sequence, the engine, the gear, the domain time
    void fillState(EntityMotionState* state, EntityItemProperties& properties);
    // this process is driving the vehicle from local controls (the driver's seat here, or it owns the vehicle)
    bool isLocalDriver(EntityMotionState* state) const;

    struct Stats {
        int vehicles { 0 };
        int corrections { 0 };          // replays and snaps since the start
        int replaySteps { 0 };          // substeps replayed since the start
        float lastError { 0.0f };       // m: the error that caused the last correction
        float maxError { 0.0f };        // m: the largest ever
        int inputsSent { 0 };
        int inputsReceived { 0 };
        int inputsDropped { 0 };        // the server's queue overflowed
        int inputsStarved { 0 };        // the server had no input for a substep and held the last
    };
    const Stats& getStats() const { return _stats; }

    // what the local simulation's first vehicle is doing right now, for a test or an inspector (copied each substep)
    struct Telemetry {
        bool valid { false };
        float engineRPM { 0.0f };
        int gear { 0 };
        float clutch { 0.0f };          // the clutch's friction, 0 (open) to 1
        float forward { 0.0f };         // the inputs as applied
        float right { 0.0f };
        float brake { 0.0f };
        float handBrake { 0.0f };
        float speed { 0.0f };           // m/s along the vehicle's forward
        std::vector<float> wheelRPM;    // per wheel
        std::vector<float> wheelSlip;   // longitudinal slip per wheel
        std::vector<float> suspension;  // suspension length per wheel, m
        std::vector<bool> contact;      // per wheel
    };
    Telemetry getTelemetry() const { std::lock_guard<std::mutex> lock(_telemetryMutex); return _telemetry; }

private:
    struct Record {
        uint32_t sequence { 0 };
        uint32_t step { 0 };
        VehicleInput input;
        glm::vec3 position { 0.0f };
        glm::quat rotation;
        glm::vec3 velocity { 0.0f };
        glm::vec3 angularVelocity { 0.0f };
    };
    struct Visual {
        std::weak_ptr<EntityItem> entity;
        int wheel { -1 };
        glm::vec3 right { 1.0f, 0.0f, 0.0f };
        glm::vec3 up { 0.0f, 1.0f, 0.0f };
    };
    struct Vehicle {
        EntityMotionState* state { nullptr };
        EntityItemPointer entity;
        JPH::BodyID bodyID;
        JPH::Ref<JPH::VehicleConstraint> constraint;
        JPH::Ref<JPH::VehicleCollisionTester> tester;
        JPH::RefConst<JPH::Shape> offsetShape;
        uint32_t definitionVersion { 0 };
        uint64_t definitionHash { 0 };
        WheeledVehicleSpec spec;
        // inputs
        VehicleInput applied;                 // the last input applied, with its sequence
        uint32_t nextSequence { 1 };          // the driver's next
        std::deque<VehicleInput> queue;       // the authority's: a remote driver's inputs waiting, oldest first
        uint32_t lastQueuedSequence { 0 };
        QUuid queueDriver;                    // whose inputs the queue holds
        quint64 lastInputTime { 0 };          // when the authority last had a fresh input
        std::vector<VehicleInput> unsent;     // the driver's, since the last packet
        std::vector<VehicleInput> recent;     // the driver's last few, for redundancy
        // prediction
        std::deque<Record> history;
        bool pendingRecord { false };
        // the authority's state as received
        uint32_t networkPoseVersion { 0 };
        quint64 lastStateTime { 0 };
        // visuals
        std::vector<Visual> visuals;
        uint32_t visualsScanStep { 0 };
        int childCount { -1 };
        bool wasAuthority { false };
        bool logged { false };
    };
    struct Snapshot {
        uint32_t step { 0 };
        quint64 time { 0 };    // wall clock of the substep
        std::string bodies;    // the world's non-static bodies after the step (Jolt's state recorder)
    };

    PhysicsWorld* rootWorld() const;
    bool rebuild(Vehicle& vehicle);
    void tearDown(Vehicle& vehicle);
    bool isAuthority(const Vehicle& vehicle) const;
    bool localControls(const Vehicle& vehicle, VehicleInput& input) const;
    void chooseInput(Vehicle& vehicle, uint32_t step);
    void applyInput(Vehicle& vehicle);
    void checkCorrection(Vehicle& vehicle, uint32_t step);
    void correctDriver(Vehicle& vehicle, const EntityItem::NetworkPose& pose, uint32_t step);
    void correctFollower(Vehicle& vehicle, const EntityItem::NetworkPose& pose, uint32_t step);
    void setBodyState(Vehicle& vehicle, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& velocity,
                      const glm::vec3& angularVelocity);
    void setInternalState(Vehicle& vehicle);
    // rewind the other bodies to the snapshot after `step` and replay `steps` substeps with the given inputs
    void replay(Vehicle& vehicle, const EntityItem::NetworkPose& pose, uint32_t fromStep, uint32_t toStep,
                const std::function<VehicleInput(uint32_t)>& inputAt);
    void smooth(Vehicle& vehicle, const glm::vec3& shownPosition, const glm::quat& shownRotation, float blendTime);
    void takeSnapshot(uint32_t step);
    const Snapshot* snapshotAt(uint32_t step) const;
    const Snapshot* snapshotBefore(quint64 time) const;
    void scanVisuals(Vehicle& vehicle, uint32_t step);
    bool needsSnapshots() const;

    PhysicsEngine* _engine { nullptr };
    bool _serverless { false };
    std::map<EntityMotionState*, Vehicle> _vehicles;
    std::map<QUuid, EntityMotionState*> _byEntity;
    std::deque<Snapshot> _snapshots;
    std::mutex _inputMutex;
    Stats _stats;
    mutable std::mutex _telemetryMutex;
    Telemetry _telemetry;
    void recordTelemetry();
};

#endif // hifi_VehicleSimulation_h
