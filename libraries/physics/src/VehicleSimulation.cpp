//
//  VehicleSimulation.cpp
//  libraries/physics/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "VehicleSimulation.h"

#include <cmath>

#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <EntityItemProperties.h>
#include <NLPacket.h>
#include <NodeList.h>
#include <PhysicsHelpers.h>
#include <SharedUtil.h>

#include "EntityMotionState.h"
#include "JoltUtil.h"
#include "PhysicsLogging.h"
#include "PhysicsWorld.h"
#include "TrackSimulation.h"

namespace {
    const glm::vec3 UP(0.0f, 1.0f, 0.0f);
    const glm::vec3 FORWARD(0.0f, 0.0f, -1.0f);   // an entity's forward, as an avatar's
    const float GRAVITY = 9.8f;
    const float WHEEL_CAST_CONVEX_RADIUS = 0.05f;   // the cast cylinder's rounding, as a fraction of the radius
    const float ANTI_ROLL_BAR_STIFFNESS = 1000.0f;  // N/m, Jolt's default; keeps a tall car on its wheels in a bend

    float angleBetween(const glm::quat& a, const glm::quat& b) {
        float dot = glm::clamp(fabsf(glm::dot(glm::normalize(a), glm::normalize(b))), 0.0f, 1.0f);
        return 2.0f * acosf(dot);
    }

    JPH::WheeledVehicleController* controllerOf(JPH::VehicleConstraint* constraint) {
        return constraint ? static_cast<JPH::WheeledVehicleController*>(constraint->GetController()) : nullptr;
    }

    VehicleInput inputFromState(const VehiclePropertyGroup& group) {
        VehicleInput input;
        input.sequence = group.getInputSequence();
        input.throttle = group.getThrottle();
        input.steering = group.getSteering();
        input.brake = group.getBrake();
        input.handBrake = group.getHandBrake();
        return input;
    }
}

VehicleSimulation::VehicleSimulation() {
}

VehicleSimulation::~VehicleSimulation() {
    clear();
}

PhysicsWorld* VehicleSimulation::rootWorld() const {
    return _engine ? _engine->getRootWorld() : nullptr;
}

void VehicleSimulation::attach(EntityMotionState* state) {
    if (!state || !state->getEntity() || !state->getEntity()->isVehicle()) {
        return;
    }
    if (_vehicles.find(state) != _vehicles.end()) {
        return;
    }
    Vehicle vehicle;
    vehicle.state = state;
    vehicle.entity = state->getEntity();
    _byEntity[vehicle.entity->getID()] = state;
    // every peer simulates the vehicle itself; what the network says of its transform is a correction, not a placement
    vehicle.entity->setTransformDrivenLocally(true);
    _vehicles.emplace(state, std::move(vehicle));
    _stats.vehicles = (int)_vehicles.size();
}

void VehicleSimulation::detach(EntityMotionState* state) {
    auto it = _vehicles.find(state);
    if (it == _vehicles.end()) {
        return;
    }
    tearDown(it->second);
    if (it->second.entity) {
        it->second.entity->setTransformDrivenLocally(false);
        _byEntity.erase(it->second.entity->getID());
    }
    _vehicles.erase(it);
    _stats.vehicles = (int)_vehicles.size();
}

void VehicleSimulation::clear() {
    for (auto& entry : _vehicles) {
        tearDown(entry.second);
        if (entry.second.entity) {
            entry.second.entity->setTransformDrivenLocally(false);
        }
    }
    _vehicles.clear();
    _byEntity.clear();
    _snapshots.clear();
    _stats.vehicles = 0;
}

void VehicleSimulation::tearDown(Vehicle& vehicle) {
    PhysicsWorld* world = rootWorld();
    if (vehicle.constraint && world) {
        JPH::PhysicsSystem* system = world->getPhysicsSystem();
        system->RemoveStepListener(vehicle.constraint);
        system->RemoveConstraint(vehicle.constraint);
    }
    vehicle.constraint = nullptr;
    vehicle.tester = nullptr;
    vehicle.offsetShape = nullptr;
    vehicle.bodyID = JPH::BodyID();
    vehicle.visuals.clear();
}

// The Jolt vehicle from the entity's spec: the chassis body wears its shape inside an offset-centre-of-mass shape when
// the spec moves the centre, a WheelSettingsWV per wheel, the engine, an automatic gearbox, one differential per driven
// axle and an anti-roll bar per axle.
bool VehicleSimulation::rebuild(Vehicle& vehicle) {
    tearDown(vehicle);
    JPH::Body* body = vehicle.state->getBody();
    PhysicsWorld* world = vehicle.state->getWorld();
    if (!body || !world || !world->isRoot()) {
        return false;
    }
    if (!body->IsDynamic()) {
        if (!vehicle.logged) {
            qCWarning(physics) << "vehicle" << vehicle.entity->getName() << vehicle.entity->getID()
                << "is not a dynamic body: set dynamic true (and gravity) on the chassis";
            vehicle.logged = true;
        }
        return false;
    }

    const VehiclePropertyGroup& group = vehicle.entity->getVehicleProperties();
    WheeledVehicleSpec& spec = vehicle.spec;
    spec = WheeledVehicleSpec();
    QString error;
    if (!WheeledVehicleSpec::parseWheels(group.getWheels(), spec.wheels, &error) || spec.wheels.empty()) {
        if (!vehicle.logged) {
            qCWarning(physics) << "vehicle" << vehicle.entity->getName() << vehicle.entity->getID() << "has no usable wheels:" << error;
            vehicle.logged = true;
        }
        return false;
    }
    WheeledVehicleSpec::parseSeats(group.getSeats(), spec.seats, nullptr);
    WheeledVehicleSpec::parseGearRatios(group.getGearRatios(), spec.gearRatios, nullptr);
    spec.mass = group.getMass();
    spec.centerOfMass = group.getCenterOfMass();
    spec.maxSteerAngle = glm::clamp(group.getMaxSteerAngle(), 0.0f, 80.0f);
    spec.maxTorque = glm::max(0.0f, group.getMaxTorque());
    spec.minRPM = glm::max(1.0f, group.getMinRPM());
    spec.maxRPM = glm::max(spec.minRPM + 1.0f, group.getMaxRPM());
    spec.reverseGearRatio = glm::max(0.01f, group.getReverseGearRatio());
    spec.finalDriveRatio = glm::max(0.01f, group.getFinalDriveRatio());
    spec.maxBrakeTorque = glm::max(0.0f, group.getMaxBrakeTorque());
    spec.maxHandBrakeTorque = glm::max(0.0f, group.getMaxHandBrakeTorque());
    spec.driveType = WheeledVehicleSpec::parseDriveType(group.getDriveType());
    spec.suspensionFrequency = group.getSuspensionFrequency();
    spec.suspensionDamping = glm::max(0.0f, group.getSuspensionDamping());

    JPH::BodyInterface& bodies = world->getBodyInterface();
    const JPH::Shape* plainShape = vehicle.state->getShape();
    if (glm::length2(spec.centerOfMass) > 1.0e-8f && plainShape) {
        vehicle.offsetShape = new JPH::OffsetCenterOfMassShape(plainShape, glmToJolt(spec.centerOfMass));
        bodies.SetShape(body->GetID(), vehicle.offsetShape, false, JPH::EActivation::Activate);
        vehicle.state->updateBodyMassProperties();
    } else if (plainShape && body->GetShape() != plainShape) {
        bodies.SetShape(body->GetID(), plainShape, false, JPH::EActivation::Activate);
        vehicle.state->updateBodyMassProperties();
    }

    float gravity = glm::length(vehicle.state->getBodyGravity());
    if (gravity <= 0.0f) {
        gravity = GRAVITY;
    }

    JPH::VehicleConstraintSettings settings;
    settings.mUp = glmToJolt(UP);
    settings.mForward = glmToJolt(FORWARD);
    settings.mMaxPitchRollAngle = JPH::JPH_PI;
    for (const WheelSpec& wheel : spec.wheels) {
        JPH::WheelSettingsWV* ws = new JPH::WheelSettingsWV();
        ws->mPosition = glmToJolt(wheel.center);
        ws->mSuspensionDirection = glmToJolt(-UP);
        ws->mSteeringAxis = glmToJolt(UP);
        ws->mWheelUp = glmToJolt(UP);
        ws->mWheelForward = glmToJolt(FORWARD);
        ws->mSuspensionMinLength = 0.0f;
        ws->mSuspensionMaxLength = glm::max(0.01f, wheel.suspensionDistance);
        ws->mSuspensionPreloadLength = 0.0f;
        ws->mSuspensionForcePoint = glmToJolt(wheel.center - UP * (wheel.suspensionDistance * wheel.targetPosition));
        ws->mEnableSuspensionForcePoint = wheel.enableSuspensionForcePoint;
        if (wheel.spring > 0.0f) {
            // Unity's spring and damper, as they are
            ws->mSuspensionSpring = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, wheel.spring, wheel.damper);
        } else {
            // the vehicle's frequency; or, when it gives none, the one that rests the suspension at the wheel's targetPosition
            float frequency = spec.suspensionFrequency;
            if (frequency <= 0.0f) {
                float restCompression = glm::max(0.01f, wheel.suspensionDistance * wheel.targetPosition);
                frequency = sqrtf(gravity / restCompression) / (2.0f * (float)M_PI);
            }
            ws->mSuspensionSpring = JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, frequency, spec.suspensionDamping);
        }
        ws->mRadius = wheel.radius;
        ws->mWidth = wheel.width;
        ws->mInertia = 0.5f * wheel.mass * wheel.radius * wheel.radius;
        // a negative angle steers the other way (Jolt applies -input * angle), so a rear wheel can counter-steer
        ws->mMaxSteerAngle = glm::radians(spec.maxSteerAngle * wheel.steer);
        ws->mMaxBrakeTorque = spec.maxBrakeTorque * wheel.brake;
        ws->mMaxHandBrakeTorque = spec.maxHandBrakeTorque * wheel.handBrake;
        settings.mWheels.push_back(ws);
    }

    JPH::WheeledVehicleControllerSettings* controller = new JPH::WheeledVehicleControllerSettings();
    controller->mEngine.mMaxTorque = spec.maxTorque;
    controller->mEngine.mMinRPM = spec.minRPM;
    controller->mEngine.mMaxRPM = spec.maxRPM;
    controller->mTransmission.mMode = JPH::ETransmissionMode::Auto;
    if (!spec.gearRatios.empty()) {
        controller->mTransmission.mGearRatios.clear();
        for (float ratio : spec.gearRatios) {
            controller->mTransmission.mGearRatios.push_back(ratio);
        }
    }
    controller->mTransmission.mReverseGearRatios.clear();
    controller->mTransmission.mReverseGearRatios.push_back(-spec.reverseGearRatio);
    controller->mTransmission.mShiftUpRPM = 0.8f * spec.maxRPM;
    controller->mTransmission.mShiftDownRPM = glm::max(1.5f * spec.minRPM, 0.35f * spec.maxRPM);
    std::vector<std::pair<int, int>> axles = spec.axles();
    int drivenAxles = 0;
    for (const auto& axle : axles) {
        if (spec.isDriven(axle.first) || spec.isDriven(axle.second)) {
            ++drivenAxles;
        }
    }
    for (const auto& axle : axles) {
        bool left = spec.isDriven(axle.first);
        bool right = spec.isDriven(axle.second);
        if (left || right) {
            JPH::VehicleDifferentialSettings differential;
            differential.mLeftWheel = left ? axle.first : -1;
            differential.mRightWheel = right && axle.second != axle.first ? axle.second : (left ? -1 : axle.second);
            if (axle.first == axle.second) {
                differential.mLeftWheel = axle.first;
                differential.mRightWheel = -1;
            }
            differential.mDifferentialRatio = spec.finalDriveRatio;
            differential.mEngineTorqueRatio = 1.0f / (float)glm::max(1, drivenAxles);
            controller->mDifferentials.push_back(differential);
        }
        if (axle.first != axle.second) {
            JPH::VehicleAntiRollBar bar;
            bar.mLeftWheel = axle.first;
            bar.mRightWheel = axle.second;
            bar.mStiffness = ANTI_ROLL_BAR_STIFFNESS;
            settings.mAntiRollBars.push_back(bar);
        }
    }
    settings.mController = controller;

    vehicle.constraint = new JPH::VehicleConstraint(*body, settings);
    vehicle.tester = new JPH::VehicleCollisionTesterCastCylinder(body->GetObjectLayer(), WHEEL_CAST_CONVEX_RADIUS);
    vehicle.constraint->SetVehicleCollisionTester(vehicle.tester);
    JPH::PhysicsSystem* system = world->getPhysicsSystem();
    system->AddConstraint(vehicle.constraint);
    system->AddStepListener(vehicle.constraint);
    bodies.ActivateBody(body->GetID());

    vehicle.bodyID = body->GetID();
    vehicle.definitionVersion = vehicle.entity->getVehicleDefinitionVersion();
    vehicle.definitionHash = group.definitionHash();
    vehicle.history.clear();
    vehicle.visuals.clear();
    vehicle.childCount = -1;
    vehicle.logged = false;
    setInternalState(vehicle);

    qCDebug(physics) << "vehicle" << vehicle.entity->getName() << vehicle.entity->getID() << "built:" << spec.wheels.size()
        << "wheels," << drivenAxles << "driven axles, mass" << vehicle.state->getMass() << "kg, centre of mass"
        << spec.centerOfMass.x << spec.centerOfMass.y << spec.centerOfMass.z << (Physics::isServerAuthority() ? "(physics server)" : "");
    return true;
}

bool VehicleSimulation::isAuthority(const Vehicle& vehicle) const {
    return _serverless || vehicle.state->isLocallyOwned();
}

bool VehicleSimulation::localControls(const Vehicle& vehicle, VehicleInput& input) const {
    EntityItem::VehicleControls controls;
    if (!vehicle.entity->getLocalVehicleControls(controls)) {
        return false;
    }
    input.throttle = controls.throttle;
    input.steering = controls.steering;
    input.brake = controls.brake;
    input.handBrake = controls.handBrake;
    return true;
}

bool VehicleSimulation::isLocalDriver(EntityMotionState* state) const {
    auto it = _vehicles.find(state);
    if (it == _vehicles.end()) {
        return false;
    }
    const Vehicle& vehicle = it->second;
    VehicleInput input;
    if (!localControls(vehicle, input)) {
        return false;
    }
    return _serverless || vehicle.state->isLocallyOwned()
        || vehicle.entity->getVehicleProperties().getDriver() == Physics::getSessionUUID();
}

void VehicleSimulation::preSubstep(uint32_t step, float dt) {
    for (auto& entry : _vehicles) {
        Vehicle& vehicle = entry.second;
        JPH::Body* body = vehicle.state->getBody();
        bool stale = !body || body->GetID() != vehicle.bodyID || !vehicle.constraint
            || vehicle.entity->getVehicleDefinitionVersion() != vehicle.definitionVersion;
        if (stale && !rebuild(vehicle)) {
            continue;
        }
        bool authority = isAuthority(vehicle);
        if (authority != vehicle.wasAuthority) {
            vehicle.wasAuthority = authority;
            vehicle.history.clear();
            if (Physics::isServerAuthority() || authority) {
                qCDebug(physics) << "vehicle" << vehicle.entity->getName() << (authority ? "is simulated here" : "is simulated elsewhere")
                    << (Physics::isServerAuthority() ? "(physics server)" : "");
            }
        }
        checkCorrection(vehicle, step);
        chooseInput(vehicle, step);
        applyInput(vehicle);
    }
}

void VehicleSimulation::postSubstep(uint32_t step, float dt) {
    if (needsSnapshots()) {
        takeSnapshot(step);
    } else if (!_snapshots.empty()) {
        _snapshots.clear();
    }
    recordTelemetry();
    for (auto& entry : _vehicles) {
        Vehicle& vehicle = entry.second;
        if (vehicle.pendingRecord && !vehicle.history.empty()) {
            Record& record = vehicle.history.back();
            vehicle.state->getBodyTransform(record.position, record.rotation);
            record.velocity = vehicle.state->getBodyLinearVelocity();
            record.angularVelocity = vehicle.state->getBodyAngularVelocity();
            vehicle.pendingRecord = false;
        }
    }
}

void VehicleSimulation::recordTelemetry() {
    Telemetry t;
    for (auto& entry : _vehicles) {
        Vehicle& vehicle = entry.second;
        JPH::WheeledVehicleController* controller = controllerOf(vehicle.constraint);
        if (!controller) {
            continue;
        }
        t.valid = true;
        t.engineRPM = controller->GetEngine().GetCurrentRPM();
        t.gear = controller->GetTransmission().GetCurrentGear();
        t.clutch = controller->GetTransmission().GetClutchFriction();
        t.forward = controller->GetForwardInput();
        t.right = controller->GetRightInput();
        t.brake = controller->GetBrakeInput();
        t.handBrake = controller->GetHandBrakeInput();
        glm::quat rotation;
        glm::vec3 position;
        vehicle.state->getBodyTransform(position, rotation);
        t.speed = glm::dot(vehicle.state->getBodyLinearVelocity(), rotation * FORWARD);
        for (const JPH::Wheel* wheel : vehicle.constraint->GetWheels()) {
            t.wheelRPM.push_back(wheel->GetAngularVelocity() * 60.0f / (2.0f * (float)M_PI));
            t.wheelSlip.push_back(static_cast<const JPH::WheelWV*>(wheel)->mLongitudinalSlip);
            t.suspension.push_back(wheel->GetSuspensionLength());
            t.contact.push_back(wheel->HasContact());
        }
        break;
    }
    std::lock_guard<std::mutex> lock(_telemetryMutex);
    _telemetry = std::move(t);
}

bool VehicleSimulation::needsSnapshots() const {
    for (const auto& entry : _vehicles) {
        if (entry.second.constraint && !isAuthority(entry.second)) {
            return true;
        }
    }
    return false;
}

// Which input this substep applies: the authority takes its driver's (local controls here, or the queue of a remote
// driver's packets, one per substep); a driver who is not the authority predicts with its own and records it; everyone
// else runs the authority's last applied input on.
void VehicleSimulation::chooseInput(Vehicle& vehicle, uint32_t step) {
    quint64 now = usecTimestampNow();
    VehicleInput local;
    bool haveLocal = localControls(vehicle, local);
    const VehiclePropertyGroup& group = vehicle.entity->getVehicleProperties();

    if (isAuthority(vehicle)) {
        if (haveLocal) {
            local.sequence = vehicle.nextSequence++;
            vehicle.applied = local;
            vehicle.lastInputTime = now;
            return;
        }
        std::lock_guard<std::mutex> lock(_inputMutex);
        if (!vehicle.queue.empty()) {
            if ((int)vehicle.queue.size() > MAX_QUEUED_INPUTS) {
                // the driver's clock runs ahead of this one, or a burst arrived: catch up to the last two
                while (vehicle.queue.size() > 2) {
                    vehicle.queue.pop_front();
                    ++_stats.inputsDropped;
                }
            }
            vehicle.applied = vehicle.queue.front();
            vehicle.queue.pop_front();
            vehicle.lastInputTime = now;
            return;
        }
        if (vehicle.applied.sequence != 0 && !group.getDriver().isNull()) {
            ++_stats.inputsStarved;   // the driver's next input is late: hold the last
        }
        if (group.getDriver().isNull() || (vehicle.lastInputTime != 0 && now - vehicle.lastInputTime > DRIVER_SILENCE)) {
            // nobody at the wheel, or the driver has gone quiet: ease off and hold the brake
            vehicle.applied.throttle = 0.0f;
            vehicle.applied.steering = 0.0f;
            vehicle.applied.brake = glm::max(vehicle.applied.brake, 0.5f);
        }
        return;
    }

    if (haveLocal && (group.getDriver() == Physics::getSessionUUID())) {
        // the driver, predicting
        local.sequence = vehicle.nextSequence++;
        vehicle.applied = local;
        Record record;
        record.sequence = local.sequence;
        record.step = step;
        record.input = local;
        vehicle.history.push_back(record);
        while ((int)vehicle.history.size() > SNAPSHOT_RING) {
            vehicle.history.pop_front();
        }
        vehicle.pendingRecord = true;
        vehicle.unsent.push_back(local);
        return;
    }

    // a follower: the authority's last applied input; when the authority has gone quiet (its states are older than
    // the driver-silence limit) the vehicle eases off and holds the brake, as the authority would for a silent driver,
    // rather than driving on with the last input for ever
    vehicle.applied = inputFromState(group);
    quint64 stateLocalTime = group.getStateTime() ? TrackSimulation::localTime(group.getStateTime()) : 0;
    if (stateLocalTime == 0 || now > stateLocalTime + DRIVER_SILENCE) {
        vehicle.applied.throttle = 0.0f;
        vehicle.applied.steering = 0.0f;
        vehicle.applied.brake = glm::max(vehicle.applied.brake, 0.5f);
    }
}

void VehicleSimulation::applyInput(Vehicle& vehicle) {
    JPH::WheeledVehicleController* controller = controllerOf(vehicle.constraint);
    if (!controller) {
        return;
    }
    const VehicleInput& input = vehicle.applied;
    controller->SetDriverInput(input.throttle, input.steering, input.brake, input.handBrake);
    if (!input.isIdle()) {
        PhysicsWorld* world = vehicle.state->getWorld();
        if (world) {
            world->getBodyInterface().ActivateBody(vehicle.bodyID);
        }
    }
}

void VehicleSimulation::checkCorrection(Vehicle& vehicle, uint32_t step) {
    EntityItem::NetworkPose pose = vehicle.entity->getNetworkPose();
    if (pose.version == vehicle.networkPoseVersion) {
        return;
    }
    vehicle.networkPoseVersion = pose.version;
    if (isAuthority(vehicle)) {
        return;
    }
    const VehiclePropertyGroup& group = vehicle.entity->getVehicleProperties();
    quint64 stateTime = group.getStateTime();
    if (stateTime == 0 || stateTime <= vehicle.lastStateTime) {
        return;   // no state, or one already adopted
    }
    vehicle.lastStateTime = stateTime;
    if (isLocalDriver(vehicle.state)) {
        correctDriver(vehicle, pose, step);
    } else {
        correctFollower(vehicle, pose, step);
    }
}

void VehicleSimulation::setBodyState(Vehicle& vehicle, const glm::vec3& position, const glm::quat& rotation,
                                     const glm::vec3& velocity, const glm::vec3& angularVelocity) {
    PhysicsWorld* world = vehicle.state->getWorld();
    if (!world) {
        return;
    }
    JPH::BodyInterface& bodies = world->getBodyInterface();
    bodies.SetPositionAndRotation(vehicle.bodyID, glmToJoltR(position), glmToJolt(glm::normalize(rotation)), JPH::EActivation::Activate);
    bodies.SetLinearAndAngularVelocity(vehicle.bodyID, glmToJolt(velocity), glmToJolt(angularVelocity));
}

void VehicleSimulation::setInternalState(Vehicle& vehicle) {
    JPH::WheeledVehicleController* controller = controllerOf(vehicle.constraint);
    if (!controller) {
        return;
    }
    const VehiclePropertyGroup& group = vehicle.entity->getVehicleProperties();
    if (group.getStateTime() != 0) {
        controller->GetEngine().SetCurrentRPM(group.getEngineRPM());
        controller->GetTransmission().Set(group.getGear(), 1.0f);
    }
}

// The driver's reconciliation: the record for the sequence the authority reached is compared with its state; within the
// thresholds the prediction stands, else the world is rewound to that substep and the inputs since are replayed.
void VehicleSimulation::correctDriver(Vehicle& vehicle, const EntityItem::NetworkPose& pose, uint32_t step) {
    const VehiclePropertyGroup& group = vehicle.entity->getVehicleProperties();
    uint32_t ack = group.getInputSequence();
    const Record* acked = nullptr;
    for (auto it = vehicle.history.rbegin(); it != vehicle.history.rend(); ++it) {
        if (it->sequence == ack) {
            acked = &(*it);
            break;
        }
        if (it->sequence < ack) {
            break;
        }
    }
    if (!acked) {
        // before this client's first input, or older than the history: the state is adopted as a follower would
        correctFollower(vehicle, pose, step);
        return;
    }
    float error = glm::distance(acked->position, pose.position);
    float angle = angleBetween(acked->rotation, pose.rotation);
    if (error < REPLAY_POSITION_THRESHOLD && angle < REPLAY_ANGLE_THRESHOLD) {
        return;   // the prediction holds
    }
    ++_stats.corrections;
    _stats.lastError = error;
    _stats.maxError = glm::max(_stats.maxError, error);

    glm::vec3 shownPosition = vehicle.entity->getWorldPosition();
    glm::quat shownRotation = vehicle.entity->getWorldOrientation();
    uint32_t fromStep = acked->step;
    uint32_t toStep = step - 1;
    if (toStep < fromStep || toStep - fromStep > (uint32_t)MAX_REPLAY_STEPS) {
        // too far back to replay: take the state and run on from it
        setBodyState(vehicle, pose.position, pose.rotation, pose.velocity, pose.angularVelocity);
        setInternalState(vehicle);
    } else {
        std::map<uint32_t, VehicleInput> inputs;
        for (const Record& record : vehicle.history) {
            inputs[record.step] = record.input;
        }
        VehicleInput last = acked->input;
        replay(vehicle, pose, fromStep, toStep, [&](uint32_t s) {
            auto it = inputs.find(s);
            if (it != inputs.end()) {
                last = it->second;
            }
            return last;
        });
    }
    if (error > 0.25f || angle > 0.17f) {
        qCDebug(physics) << "vehicle" << vehicle.entity->getName() << "corrected by" << error << "m" << glm::degrees(angle)
            << "deg at input" << ack << "(" << (toStep - fromStep) << "substeps replayed)";
    }
    smooth(vehicle, shownPosition, shownRotation, DRIVER_BLEND_TIME);
}

// Everyone else: rewind to the state's time and run the same vehicle on from it with the authority's last inputs.
void VehicleSimulation::correctFollower(Vehicle& vehicle, const EntityItem::NetworkPose& pose, uint32_t step) {
    const VehiclePropertyGroup& group = vehicle.entity->getVehicleProperties();
    quint64 stateLocalTime = TrackSimulation::localTime(group.getStateTime());
    glm::vec3 shownPosition = vehicle.entity->getWorldPosition();
    glm::quat shownRotation = vehicle.entity->getWorldOrientation();
    glm::vec3 bodyPosition;
    glm::quat bodyRotation;
    vehicle.state->getBodyTransform(bodyPosition, bodyRotation);

    const Snapshot* snapshot = snapshotBefore(stateLocalTime);
    uint32_t toStep = step - 1;
    if (!snapshot || snapshot->step >= step || toStep - snapshot->step > (uint32_t)MAX_REPLAY_STEPS) {
        // nothing to rewind to: the state, read ahead by its age along its velocity
        quint64 now = usecTimestampNow();
        float age = now > stateLocalTime ? glm::min(0.5f, (float)(now - stateLocalTime) * 1.0e-6f) : 0.0f;
        glm::vec3 position = pose.position + pose.velocity * age;
        glm::quat rotation = pose.rotation;
        if (glm::length2(pose.angularVelocity) > 0.0f && age > 0.0f) {
            rotation = glm::normalize(computeBulletRotationStep(pose.angularVelocity, age) * rotation);
        }
        setBodyState(vehicle, position, rotation, pose.velocity, pose.angularVelocity);
        setInternalState(vehicle);
    } else {
        VehicleInput input = inputFromState(group);
        replay(vehicle, pose, snapshot->step, toStep, [&](uint32_t) { return input; });
    }
    float error = glm::distance(bodyPosition, shownPosition);
    glm::vec3 after;
    glm::quat afterRotation;
    vehicle.state->getBodyTransform(after, afterRotation);
    error = glm::distance(after, bodyPosition);
    if (error > REPLAY_POSITION_THRESHOLD) {
        ++_stats.corrections;
        _stats.lastError = error;
        _stats.maxError = glm::max(_stats.maxError, error);
    }
    smooth(vehicle, shownPosition, shownRotation, OTHERS_BLEND_TIME);
}

// Rewind: the other bodies back to the snapshot after fromStep, the vehicle to the authority's state; then every
// substep up to toStep again, with the inputs the caller names, re-recording the driver's history and the snapshots.
void VehicleSimulation::replay(Vehicle& vehicle, const EntityItem::NetworkPose& pose, uint32_t fromStep, uint32_t toStep,
                               const std::function<VehicleInput(uint32_t)>& inputAt) {
    PhysicsWorld* world = rootWorld();
    JPH::WheeledVehicleController* controller = controllerOf(vehicle.constraint);
    if (!world || !controller) {
        return;
    }
    const Snapshot* snapshot = snapshotAt(fromStep);
    if (snapshot) {
        world->restoreBodies(snapshot->bodies, JPH::BodyID());
    }
    setBodyState(vehicle, pose.position, pose.rotation, pose.velocity, pose.angularVelocity);
    setInternalState(vehicle);

    for (uint32_t s = fromStep + 1; s <= toStep; ++s) {
        VehicleInput input = inputAt(s);
        controller->SetDriverInput(input.throttle, input.steering, input.brake, input.handBrake);
        world->replayStep(PHYSICS_ENGINE_FIXED_SUBSTEP);
        ++_stats.replaySteps;
        for (Record& record : vehicle.history) {
            if (record.step == s) {
                vehicle.state->getBodyTransform(record.position, record.rotation);
                record.velocity = vehicle.state->getBodyLinearVelocity();
                record.angularVelocity = vehicle.state->getBodyAngularVelocity();
                break;
            }
        }
        for (Snapshot& old : _snapshots) {
            if (old.step == s) {
                old.bodies = world->saveBodies(JPH::BodyID());
                break;
            }
        }
    }
    // the input the coming substep would otherwise inherit
    controller->SetDriverInput(vehicle.applied.throttle, vehicle.applied.steering, vehicle.applied.brake, vehicle.applied.handBrake);
}

// The jump a correction makes is kept out of what is shown: the difference decays on the entity's shown pose.
void VehicleSimulation::smooth(Vehicle& vehicle, const glm::vec3& shownPosition, const glm::quat& shownRotation, float blendTime) {
    glm::vec3 position;
    glm::quat rotation;
    vehicle.state->getBodyTransform(position, rotation);
    glm::vec3 offset = shownPosition - position;
    if (glm::length2(offset) > SNAP_DISTANCE * SNAP_DISTANCE) {
        vehicle.state->setCorrection(glm::vec3(0.0f), glm::quat(), 0.0f);
        return;
    }
    glm::quat residual = glm::normalize(shownRotation * glm::inverse(rotation));
    vehicle.state->setCorrection(offset, residual, blendTime);
}

void VehicleSimulation::takeSnapshot(uint32_t step) {
    PhysicsWorld* world = rootWorld();
    if (!world) {
        return;
    }
    Snapshot snapshot;
    snapshot.step = step;
    snapshot.time = ObjectMotionState::getSubstepTime();
    snapshot.bodies = world->saveBodies(JPH::BodyID());
    _snapshots.push_back(std::move(snapshot));
    while ((int)_snapshots.size() > SNAPSHOT_RING) {
        _snapshots.pop_front();
    }
}

const VehicleSimulation::Snapshot* VehicleSimulation::snapshotAt(uint32_t step) const {
    for (auto it = _snapshots.rbegin(); it != _snapshots.rend(); ++it) {
        if (it->step == step) {
            return &(*it);
        }
        if (it->step < step) {
            break;
        }
    }
    return nullptr;
}

const VehicleSimulation::Snapshot* VehicleSimulation::snapshotBefore(quint64 time) const {
    for (auto it = _snapshots.rbegin(); it != _snapshots.rend(); ++it) {
        if (it->time <= time) {
            return &(*it);
        }
    }
    return nullptr;
}

void VehicleSimulation::scanVisuals(Vehicle& vehicle, uint32_t step) {
    vehicle.visuals.clear();
    vehicle.visualsScanStep = step;
    int count = 0;
    vehicle.entity->forEachChild([&](SpatiallyNestablePointer child) {
        ++count;
        if (child->getNestableType() != NestableType::Entity) {
            return;
        }
        EntityItemPointer childEntity = std::static_pointer_cast<EntityItem>(child);
        if (!childEntity->isWheel()) {
            return;
        }
        const VehiclePropertyGroup& group = childEntity->getVehicleProperties();
        for (size_t i = 0; i < vehicle.spec.wheels.size(); ++i) {
            if (vehicle.spec.wheels[i].name == group.getWheel()) {
                Visual visual;
                visual.entity = childEntity;
                visual.wheel = (int)i;
                float side = vehicle.spec.wheels[i].center.x >= 0.0f ? 1.0f : -1.0f;
                QString axle = group.getAxle().trimmed().toLower();
                if (axle == "y") {
                    visual.right = glm::vec3(0.0f, side, 0.0f);
                    visual.up = glm::vec3(0.0f, 0.0f, 1.0f);
                } else if (axle == "z") {
                    visual.right = glm::vec3(0.0f, 0.0f, side);
                    visual.up = glm::vec3(0.0f, 1.0f, 0.0f);
                } else {
                    visual.right = glm::vec3(side, 0.0f, 0.0f);
                    visual.up = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                vehicle.visuals.push_back(visual);
                break;
            }
        }
    });
    vehicle.childCount = count;
}

void VehicleSimulation::updateVisuals() {
    uint32_t step = ObjectMotionState::getWorldSimulationStep();
    for (auto& entry : _vehicles) {
        Vehicle& vehicle = entry.second;
        if (!vehicle.constraint) {
            continue;
        }
        int count = 0;
        vehicle.entity->forEachChild([&](SpatiallyNestablePointer) { ++count; });
        if (count != vehicle.childCount || step > vehicle.visualsScanStep + 90) {
            scanVisuals(vehicle, step);
        }
        for (const Visual& visual : vehicle.visuals) {
            EntityItemPointer child = visual.entity.lock();
            if (!child || visual.wheel < 0 || visual.wheel >= (int)vehicle.constraint->GetWheels().size()) {
                continue;
            }
            JPH::Mat44 matrix = vehicle.constraint->GetWheelLocalTransform((JPH::uint)visual.wheel, glmToJolt(visual.right), glmToJolt(visual.up));
            Transform local = child->getLocalTransform();
            local.setTranslation(joltToGLM(matrix.GetTranslation()));
            local.setRotation(glm::normalize(joltToGLM(matrix.GetQuaternion())));
            child->setLocalTransformAndVelocities(local, glm::vec3(0.0f), glm::vec3(0.0f));
        }
    }
}

void VehicleSimulation::sendInputs() {
    auto nodeList = DependencyManager::get<NodeList>();
    SharedNodePointer server;
    for (auto& entry : _vehicles) {
        Vehicle& vehicle = entry.second;
        if (vehicle.unsent.empty()) {
            continue;
        }
        if (isAuthority(vehicle) || !isLocalDriver(vehicle.state)) {
            vehicle.unsent.clear();
            continue;
        }
        for (const VehicleInput& input : vehicle.unsent) {
            vehicle.recent.push_back(input);
        }
        vehicle.unsent.clear();
        while ((int)vehicle.recent.size() > VehicleInputPacket::MAX_INPUTS) {
            vehicle.recent.erase(vehicle.recent.begin());
        }
        if (!server && nodeList) {
            server = nodeList->soloNodeOfType(NodeType::PhysicsServer);
        }
        if (!server) {
            continue;
        }
        QByteArray payload = VehicleInputPacket::pack(vehicle.entity->getID(), vehicle.recent);
        auto packet = NLPacket::create(PacketType::VehicleInput, payload.size());
        packet->write(payload);
        nodeList->sendUnreliablePacket(*packet, *server);
        ++_stats.inputsSent;
    }
}

void VehicleSimulation::receiveInputs(const QUuid& vehicleID, const QUuid& sender, const std::vector<VehicleInput>& inputs) {
    auto found = _byEntity.find(vehicleID);
    if (found == _byEntity.end() || inputs.empty()) {
        return;
    }
    auto it = _vehicles.find(found->second);
    if (it == _vehicles.end()) {
        return;
    }
    Vehicle& vehicle = it->second;
    if (vehicle.entity->getVehicleProperties().getDriver() != sender) {
        return;   // not the one in the driver's seat
    }
    std::lock_guard<std::mutex> lock(_inputMutex);
    if (vehicle.queueDriver != sender) {
        // a new driver counts from one again
        vehicle.queueDriver = sender;
        vehicle.queue.clear();
        vehicle.lastQueuedSequence = 0;
    }
    for (const VehicleInput& input : inputs) {
        if (input.sequence > vehicle.lastQueuedSequence) {
            vehicle.queue.push_back(input);
            vehicle.lastQueuedSequence = input.sequence;
            ++_stats.inputsReceived;
        }
    }
}

void VehicleSimulation::fillState(EntityMotionState* state, EntityItemProperties& properties) {
    auto it = _vehicles.find(state);
    if (it == _vehicles.end()) {
        return;
    }
    Vehicle& vehicle = it->second;
    JPH::WheeledVehicleController* controller = controllerOf(vehicle.constraint);
    VehiclePropertyGroup& group = properties.getVehicle();
    group.setInputSequence(vehicle.applied.sequence);
    group.setThrottle(vehicle.applied.throttle);
    group.setSteering(vehicle.applied.steering);
    group.setBrake(vehicle.applied.brake);
    group.setHandBrake(vehicle.applied.handBrake);
    if (controller) {
        group.setEngineRPM(controller->GetEngine().GetCurrentRPM());
        group.setGear(controller->GetTransmission().GetCurrentGear());
    }
    group.setStateTime(TrackSimulation::domainTime(usecTimestampNow()));
}
