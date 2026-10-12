//
//  PhysicsEngine.cpp
//  libraries/physics/src
//
//  Created by Andrew Meadows 2014.10.29
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "PhysicsEngine.h"

#include <algorithm>
#include <functional>
#include <thread>

#include <QFile>
#include <QTextStream>

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Constraints/Constraint.h>

#include <PerfStat.h>
#include <PhysicsCollisionGroups.h>
#include <Profile.h>
#include <SharedUtil.h>

#include "CharacterController.h"
#include "JoltSupport.h"
#include "JoltUtil.h"
#include "ObjectMotionState.h"
#include "PhysicsHelpers.h"
#include "PhysicsDebugDraw.h"
#include "PhysicsLogging.h"

namespace {
    // Jolt's limits for one world. Bodies beyond this fail to add, so it is generous.
    const JPH::uint MAX_BODIES = 65536;
    const JPH::uint NUM_BODY_MUTEXES = 0; // 0 = Jolt's default
    const JPH::uint MAX_BODY_PAIRS = 65536;
    const JPH::uint MAX_CONTACT_CONSTRAINTS = 16384;
    const JPH::uint TEMP_ALLOCATOR_BYTES = 64 * 1024 * 1024;

    // a body sleeps after this long below the velocity threshold (Bullet's hard-coded value was 2 s)
    const float TIME_BEFORE_SLEEP = 1.0f;
}

// Jolt reports contacts from its worker threads while a step runs; this gathers them under a lock so the
// engine can fold them into its contact map afterwards, on the simulation thread.
class PhysicsEngine::ContactRecorder : public JPH::ContactListener {
public:
    struct Record {
        ObjectMotionState* a;
        ObjectMotionState* b;
        bool aIsStatic, bIsStatic, aIsKinematic, bIsKinematic;
        glm::vec3 positionOnB;
        glm::vec3 normalOnB; // points from B toward A
        float distance;      // negative when penetrating
    };

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override {
        record(body1, body2, manifold);
    }

    void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override {
        record(body1, body2, manifold);
    }

    void take(std::vector<Record>& out) {
        std::lock_guard<std::mutex> lock(_mutex);
        out.swap(_records);
        _records.clear();
    }

private:
    void record(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold) {
        ObjectMotionState* a = PhysicsEngine::motionStateOf(body1);
        ObjectMotionState* b = PhysicsEngine::motionStateOf(body2);
        if (!a && !b) {
            return;
        }
        // A body without a motion state is MyAvatar's inner body; it always takes the A side of a contact.
        bool swap = (b == nullptr);
        const JPH::Body& bodyA = swap ? body2 : body1;
        const JPH::Body& bodyB = swap ? body1 : body2;
        Record record;
        record.a = swap ? b : a;
        record.b = swap ? a : b;
        record.aIsStatic = bodyA.IsStatic();
        record.bIsStatic = bodyB.IsStatic();
        record.aIsKinematic = bodyA.IsKinematic();
        record.bIsKinematic = bodyB.IsKinematic();
        // Jolt's normal points from body 1 to body 2; the engine's contact normal points from B toward A.
        glm::vec3 normalFrom1To2 = joltToGLM(manifold.mWorldSpaceNormal);
        record.normalOnB = swap ? normalFrom1To2 : -normalFrom1To2;
        record.distance = -manifold.mPenetrationDepth;
        if (swap) {
            record.positionOnB = manifold.mRelativeContactPointsOn1.empty() ? joltToGLM(manifold.mBaseOffset)
                                                                            : joltToGLM(manifold.GetWorldSpaceContactPointOn1(0));
        } else {
            record.positionOnB = manifold.mRelativeContactPointsOn2.empty() ? joltToGLM(manifold.mBaseOffset)
                                                                            : joltToGLM(manifold.GetWorldSpaceContactPointOn2(0));
        }
        std::lock_guard<std::mutex> lock(_mutex);
        _records.push_back(record);
    }

    std::mutex _mutex;
    std::vector<Record> _records;
};

// Jolt tells us when bodies fall asleep or wake up, also from its worker threads.
class PhysicsEngine::ActivationRecorder : public JPH::BodyActivationListener {
public:
    void OnBodyActivated(const JPH::BodyID& bodyID, JPH::uint64 userData) override {
    }

    void OnBodyDeactivated(const JPH::BodyID& bodyID, JPH::uint64 userData) override {
        std::lock_guard<std::mutex> lock(_mutex);
        _deactivated.push_back((ObjectMotionState*)(uintptr_t)userData);
    }

    void takeDeactivated(std::vector<ObjectMotionState*>& out) {
        std::lock_guard<std::mutex> lock(_mutex);
        out.swap(_deactivated);
        _deactivated.clear();
    }

private:
    std::mutex _mutex;
    std::vector<ObjectMotionState*> _deactivated;
};

// static
ObjectMotionState* PhysicsEngine::motionStateOf(const JPH::Body& body) {
    return (ObjectMotionState*)(uintptr_t)body.GetUserData();
}

PhysicsEngine::PhysicsEngine(const glm::vec3& offset) :
        _originOffset(offset),
        _myAvatarController(nullptr) {
    // Jolt's allocator and types must exist before any Jolt object does, including the members of this class
    JoltSupport::initialize();
}

PhysicsEngine::~PhysicsEngine() {
    _myAvatarController = nullptr;
    // the system goes before the listeners and the job system it uses
    _system.reset();
    _contactRecorder.reset();
    _activationRecorder.reset();
    _jobSystem.reset();
    _tempAllocator.reset();
    _layers.reset();
}

void PhysicsEngine::init() {
    JoltSupport::initialize();
    if (!_system) {
        _tempAllocator.reset(new JPH::TempAllocatorImpl(TEMP_ALLOCATOR_BYTES));
        int numThreads = glm::max(1, (int)std::thread::hardware_concurrency() - 1);
        _jobSystem.reset(new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads));

        _layers.reset(new JoltLayers::Tables());
        _system.reset(new JPH::PhysicsSystem());
        _system->Init(MAX_BODIES, NUM_BODY_MUTEXES, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS,
                      _layers->broadPhaseLayers, _layers->objectVsBroadPhase, _layers->objectLayerPairs);

        // default gravity of the world is zero, so each object must specify its own gravity
        // TODO: set up gravity zones
        _system->SetGravity(JPH::Vec3::sZero());

        JPH::PhysicsSettings settings = _system->GetPhysicsSettings();
        settings.mTimeBeforeSleep = TIME_BEFORE_SLEEP;
        settings.mDeterministicSimulation = true;
        _system->SetPhysicsSettings(settings);

        _contactRecorder.reset(new ContactRecorder());
        _system->SetContactListener(_contactRecorder.get());
        _activationRecorder.reset(new ActivationRecorder());
        _system->SetBodyActivationListener(_activationRecorder.get());

        _physicsDebugDraw.reset(new PhysicsDebugDraw());
        _physicsDebugDraw->setWorldOffset(_originOffset);

        _lastStepTime = usecTimestampNow();
        qCDebug(physics) << "PhysicsEngine: Jolt world with" << numThreads << "worker threads";
    }
}

JPH::BodyInterface& PhysicsEngine::getBodyInterface() const {
    return _system->GetBodyInterfaceNoLock();
}

JPH::TempAllocator* PhysicsEngine::getTempAllocator() const {
    return _tempAllocator.get();
}

int32_t PhysicsEngine::getNumCollisionObjects() const {
    return _system ? (int32_t)_system->GetNumBodies() : 0;
}

// private
void PhysicsEngine::addObjectToDynamicsWorld(ObjectMotionState* motionState) {
    assert(motionState);
    if (motionState->getBody()) {
        // a reinsertion: the body is rebuilt from scratch so that its type, shape and layer are all fresh
        destroyBody(motionState);
    }
    const JPH::Shape* shape = motionState->getShape();
    assert(shape);
    if (!shape) {
        return;
    }

    PhysicsMotionType motionType = motionState->computePhysicsMotionType();
    motionState->setMotionType(motionType);

    int32_t group, mask;
    motionState->computeCollisionGroupAndMask(group, mask);

    JPH::BodyCreationSettings settings(shape, glmToJoltR(motionState->getObjectPosition()), glmToJolt(motionState->getObjectRotation()),
                                       JPH::EMotionType::Static, JoltLayers::layerFor(group, mask));
    settings.mUserData = (JPH::uint64)(uintptr_t)motionState;
    settings.mFriction = motionState->getObjectFriction();
    settings.mRestitution = motionState->getObjectRestitution();
    settings.mAllowedDOFs = motionState->getAllowedDOFs();
    settings.mAllowSleeping = true;

    bool activate = false;
    switch(motionType) {
        case MOTION_TYPE_KINEMATIC: {
            settings.mMotionType = JPH::EMotionType::Kinematic;
            activate = true;
            break;
        }
        case MOTION_TYPE_DYNAMIC: {
            settings.mMotionType = JPH::EMotionType::Dynamic;
            float mass = motionState->getMass();
            const float MIN_DYNAMIC_MASS = 0.01f;
            if (mass != mass || mass < MIN_DYNAMIC_MASS) {
                mass = MIN_DYNAMIC_MASS;
            }
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = mass;
            // an object that is not moving starts asleep, so a world full of resting things costs nothing
            activate = motionState->isMoving();
            break;
        }
        case MOTION_TYPE_STATIC:
        default: {
            settings.mMotionType = JPH::EMotionType::Static;
            break;
        }
    }

    JPH::BodyInterface& bodies = getBodyInterface();
    JPH::Body* body = bodies.CreateBody(settings);
    if (!body) {
        qCWarning(physics) << "PhysicsEngine: out of bodies, could not add" << motionState->getName();
        return;
    }
    bodies.AddBody(body->GetID(), activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    motionState->setBody(body, &bodies);
    _motionStates.insert(motionState);

    if (motionType == MOTION_TYPE_KINEMATIC) {
        motionState->updateBodyVelocities();
        motionState->updateLastKinematicStep();
        motionState->clearInternalKinematicChanges();
        _kinematicMotionStates.insert(motionState);
    } else if (motionType == MOTION_TYPE_DYNAMIC) {
        motionState->updateBodyVelocities();
        if (!activate) {
            bodies.DeactivateBody(body->GetID());
        }
    } else {
        // a new static body does not wake the sleeping things it may now be under or inside; we do
        wakeBodiesAround(body);
        if (motionState->isLocallyOwned()) {
            _activeStaticObjects.insert(motionState);
        }
    }
    motionState->updateBodyMaterialProperties();
}

// private
void PhysicsEngine::destroyBody(ObjectMotionState* motionState) {
    JPH::Body* body = motionState->getBody();
    if (!body) {
        return;
    }
    JPH::BodyID bodyID = body->GetID();
    removeDynamicsForBody(bodyID);
    JPH::BodyInterface& bodies = getBodyInterface();
    bodies.RemoveBody(bodyID);
    motionState->setBody(nullptr, nullptr);
    bodies.DestroyBody(bodyID);
    _motionStates.erase(motionState);
    _kinematicMotionStates.erase(motionState);
    _activeStaticObjects.erase(motionState);
}

QList<EntityDynamicPointer> PhysicsEngine::removeDynamicsForBody(const JPH::BodyID& bodyID) {
    // remove dynamics that are attached to this body
    QList<EntityDynamicPointer> removedDynamics;
    uint32_t key = bodyID.GetIndexAndSequenceNumber();
    if (!_objectDynamicsByBody.contains(key)) {
        return removedDynamics;
    }
    QMutableSetIterator<QUuid> i(_objectDynamicsByBody[key]);

    while (i.hasNext()) {
        QUuid dynamicID = i.next();
        if (dynamicID.isNull()) {
            continue;
        }
        EntityDynamicPointer dynamic = _objectDynamics[dynamicID];
        if (!dynamic) {
            continue;
        }
        removeDynamic(dynamicID);
        removedDynamics += dynamic;
    }
    return removedDynamics;
}

void PhysicsEngine::removeObjects(const VectorOfMotionStates& objects) {
    // wake whatever touched them and forget the contacts
    for (auto object : objects) {
        wakeBodiesTouching(object);
        removeContacts(object);
    }
    // remove bodies
    for (auto object : objects) {
        destroyBody(object);
    }
}

// Same as above, but takes a Set instead of a Vector.  Should only be called during teardown.
void PhysicsEngine::removeSetOfObjects(const SetOfMotionStates& objects) {
    _contactMap.clear();
    for (auto object : objects) {
        destroyBody(object);
        object->clearIncomingDirtyFlags();
    }
    _activeStaticObjects.clear();
}

void PhysicsEngine::addObjects(const VectorOfMotionStates& objects) {
    for (auto object : objects) {
        addObjectToDynamicsWorld(object);
    }
}

void PhysicsEngine::reinsertObject(ObjectMotionState* object) {
    wakeBodiesTouching(object);
    removeContacts(object);
    if (object->getBody()) {
        addObjectToDynamicsWorld(object);
    }
}

void PhysicsEngine::processTransaction(PhysicsEngine::Transaction& transaction) {
    // removes
    for (auto object : transaction.objectsToRemove) {
        wakeBodiesTouching(object);
        removeContacts(object);
        destroyBody(object);
    }

    // adds
    for (auto object : transaction.objectsToAdd) {
        addObjectToDynamicsWorld(object);
    }

    // reinserts
    for (auto object : transaction.objectsToReinsert) {
        reinsertObject(object);
    }

    // static bodies that were moved: wake whatever is now touching them, and report them as changed
    for (auto object : transaction.activeStaticObjects) {
        if (object->getBody()) {
            wakeBodiesAround(object->getBody());
            _activeStaticObjects.insert(object);
        }
    }
}

void PhysicsEngine::removeContacts(ObjectMotionState* motionState) {
    // trigger events for new/existing/old contacts
    ContactMap::iterator contactItr = _contactMap.begin();
    while (contactItr != _contactMap.end()) {
        if (contactItr->first._a == motionState || contactItr->first._b == motionState) {
            contactItr = _contactMap.erase(contactItr);
        } else {
            ++contactItr;
        }
    }
}

// private
void PhysicsEngine::wakeBodiesTouching(ObjectMotionState* motionState) {
    // Find all objects that touch the object corresponding to motionState and flag the other objects
    // for simulation ownership by the local simulation.
    assert(motionState);
    for (auto& contact : _contactMap) {
        ObjectMotionState* other = nullptr;
        if (contact.first._a == motionState) {
            other = static_cast<ObjectMotionState*>(contact.first._b);
        } else if (contact.first._b == motionState) {
            other = static_cast<ObjectMotionState*>(contact.first._a);
        }
        if (other && other->getBody() && other->getBody()->IsDynamic()) {
            other->bump(VOLUNTEER_SIMULATION_PRIORITY);
            other->activateBody();
        }
    }
}

// private
void PhysicsEngine::wakeBodiesAround(const JPH::Body* body) {
    if (!body) {
        return;
    }
    const float MARGIN = 0.1f; // meters: a resting body may sit just outside the bounds
    JPH::AABox bounds = body->GetWorldSpaceBounds();
    bounds.ExpandBy(JPH::Vec3::sReplicate(MARGIN));
    getBodyInterface().ActivateBodiesInAABox(bounds, JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter());
}

// private
void PhysicsEngine::applyGravityForces() {
    // The world has no gravity; every object carries its own, applied as a force to the active dynamic bodies.
    JPH::BodyIDVector activeBodies;
    _system->GetActiveBodies(JPH::EBodyType::RigidBody, activeBodies);
    const JPH::BodyLockInterfaceNoLock& lockInterface = _system->GetBodyLockInterfaceNoLock();
    for (const JPH::BodyID& bodyID : activeBodies) {
        JPH::BodyLockWrite lock(lockInterface, bodyID);
        if (!lock.Succeeded()) {
            continue;
        }
        JPH::Body& body = lock.GetBody();
        if (!body.IsDynamic()) {
            continue;
        }
        ObjectMotionState* motionState = motionStateOf(body);
        if (!motionState) {
            continue;
        }
        const glm::vec3& gravity = motionState->getBodyGravity();
        if (glm::length2(gravity) > 0.0f) {
            float mass = 1.0f / glm::max(body.GetMotionProperties()->GetInverseMass(), 1.0e-6f);
            body.AddForce(glmToJolt(gravity * mass));
        }
    }
}

// private
void PhysicsEngine::stepOnce(float dt) {
    ++_numSubsteps;
    ObjectMotionState::setWorldSimulationStep(_numSubsteps);

    // keyframed objects move toward where their owners say they are, parents before their children
    std::vector<ObjectMotionState*> kinematics(_kinematicMotionStates.begin(), _kinematicMotionStates.end());
    std::stable_sort(kinematics.begin(), kinematics.end(), [](ObjectMotionState* a, ObjectMotionState* b) {
        return a->getNestingDepth() < b->getNestingDepth();
    });
    for (ObjectMotionState* motionState : kinematics) {
        motionState->pushTransformToBody(dt);
    }

    // actions (grabs, tractors, offsets) push on their bodies
    for (auto& dynamic : _objectDynamics) {
        if (dynamic && dynamic->isAction()) {
            static_cast<ObjectAction*>(dynamic.get())->updateAction(dt);
        }
    }

    // MyAvatar's character moves itself through the world, before the world moves
    if (_myAvatarController) {
        _myAvatarController->substep(dt);
    }

    applyGravityForces();

    JPH::EPhysicsUpdateError error = _system->Update(dt, 1, _tempAllocator.get(), _jobSystem.get());
    if (error != JPH::EPhysicsUpdateError::None) {
        qCWarning(physics) << "PhysicsEngine: Jolt update error" << (int)error;
    }

    harvestContacts();
    updateContactMap();
    doOwnershipInfectionForConstraints();
}

void PhysicsEngine::stepSimulation() {
    PROFILE_RANGE(simulation_physics, "stepSimulation");
    // NOTE: the grand order of operations is:
    // (1) pull incoming changes
    // (2) step simulation
    // (3) synchronize outgoing motion states
    // (4) send outgoing packets

    const float MAX_TIMESTEP = (float)PHYSICS_ENGINE_MAX_NUM_SUBSTEPS * PHYSICS_ENGINE_FIXED_SUBSTEP;
    uint64_t now = usecTimestampNow();
    float dt = 1.0e-6f * (float)(now - _lastStepTime);
    _lastStepTime = now;
    _accumulatedTime += glm::min(dt, MAX_TIMESTEP);

    int numSubsteps = (int)(_accumulatedTime / PHYSICS_ENGINE_FIXED_SUBSTEP);
    if (numSubsteps > PHYSICS_ENGINE_MAX_NUM_SUBSTEPS) {
        // the simulation can't keep up; drop time rather than spiral
        numSubsteps = PHYSICS_ENGINE_MAX_NUM_SUBSTEPS;
        _accumulatedTime = 0.0f;
    } else {
        _accumulatedTime -= (float)numSubsteps * PHYSICS_ENGINE_FIXED_SUBSTEP;
    }

    uint64_t start = usecTimestampNow();
    for (int i = 0; i < numSubsteps; ++i) {
        stepOnce(PHYSICS_ENGINE_FIXED_SUBSTEP);
    }
    _lastStepDuration = 1.0e-3f * (float)(usecTimestampNow() - start);

    if (numSubsteps > 0) {
        _hasOutgoingChanges = true;
        if (_debugDrawMode != PhysicsDebugDraw::DRAW_NOTHING) {
            PROFILE_RANGE(simulation_physics, "debugDrawWorld");
            JPH::BodyManager::DrawSettings drawSettings;
            drawSettings.mDrawShape = (_debugDrawMode & PhysicsDebugDraw::DRAW_WIREFRAME) != 0;
            drawSettings.mDrawShapeWireframe = true;
            drawSettings.mDrawBoundingBox = (_debugDrawMode & PhysicsDebugDraw::DRAW_AABBS) != 0;
            if (drawSettings.mDrawShape || drawSettings.mDrawBoundingBox) {
                _system->DrawBodies(drawSettings, _physicsDebugDraw.get());
            }
            if (_debugDrawMode & PhysicsDebugDraw::DRAW_CONSTRAINTS) {
                _system->DrawConstraints(_physicsDebugDraw.get());
            }
            if (_debugDrawMode & PhysicsDebugDraw::DRAW_CONSTRAINT_LIMITS) {
                _system->DrawConstraintLimits(_physicsDebugDraw.get());
            }
            if (_debugDrawMode & PhysicsDebugDraw::DRAW_CONTACT_POINTS) {
                const glm::vec3 RED(1.0f, 0.0f, 0.0f);
                for (auto& contact : _contactMap) {
                    _physicsDebugDraw->drawContactPoint(contact.second.positionWorldOnB, contact.second.normalWorldOnB, contact.second.distance, RED);
                }
            }
        }
    }
}

void PhysicsEngine::harvestPerformanceStats() {
    PerformanceTimer::addTimerRecord("physics/...step", (uint64_t)(_lastStepDuration * 1000.0f));
}

void PhysicsEngine::printPerformanceStatsToFile(const QString& filename) {
    QFile file(filename);
    if (!file.open(QFile::WriteOnly)) {
        qCDebug(physics) << "unable to open file " << file.fileName() << " to save stepSimulation() stats";
        return;
    }
    QTextStream s(&file);
    s << "stepSimulation = " << _lastStepDuration << "\n";
    s << "bodies = " << getNumCollisionObjects() << "\n";
    s << "activeBodies = " << (_system ? _system->GetNumActiveBodies(JPH::EBodyType::RigidBody) : 0) << "\n";
    s << "contacts = " << (int)_contactMap.size() << "\n";
    file.close();
}

// private
void PhysicsEngine::doOwnershipInfection(ObjectMotionState* motionStateA, bool aIsCharacter, bool aIsStatic,
                                         ObjectMotionState* motionStateB, bool bIsStatic, bool bIsKinematic) {
    // "A owns itself" means: A is MyAvatar's character, or A is simulated by us and is not static.
    bool aIsOurs = aIsCharacter || (motionStateA && motionStateA->getSimulatorID() == Physics::getSessionUUID() && !aIsStatic);
    if (!aIsOurs || !motionStateB) {
        return;
    }
    // NOTE: we might own the simulation of a kinematic object (A)
    // but we don't claim ownership of kinematic objects (B) based on collisions here.
    if (!bIsStatic && !bIsKinematic && motionStateB->getSimulatorID() != Physics::getSessionUUID()) {
        uint8_t priorityA = motionStateA ? motionStateA->getSimulationPriority() : PERSONAL_SIMULATION_PRIORITY;
        motionStateB->bump(priorityA);
    }
}

// private
void PhysicsEngine::harvestContacts() {
    std::vector<ContactRecorder::Record> records;
    _contactRecorder->take(records);
    ++_numContactFrames;
    for (const auto& record : records) {
        // the manifold has up to 4 distinct points, but only extract info from the first
        _contactMap[ContactKey(record.a, record.b)].update(_numContactFrames, record.positionOnB, record.normalOnB, record.distance);

        if (!Physics::getSessionUUID().isNull()) {
            doOwnershipInfection(record.a, record.a == nullptr, record.aIsStatic, record.b, record.bIsStatic, record.bIsKinematic);
            if (record.a) {
                doOwnershipInfection(record.b, false, record.bIsStatic, record.a, record.aIsStatic, record.aIsKinematic);
            }
        }
    }
}

void PhysicsEngine::recordCharacterContact(ObjectMotionState* other, const glm::vec3& positionOnOther, const glm::vec3& normalTowardCharacter, float distance) {
    if (!other) {
        return;
    }
    _contactMap[ContactKey(nullptr, other)].update(_numContactFrames, positionOnOther, normalTowardCharacter, distance);
    if (!Physics::getSessionUUID().isNull()) {
        const JPH::Body* body = other->getBody();
        bool isStatic = body ? body->IsStatic() : true;
        bool isKinematic = body ? body->IsKinematic() : false;
        doOwnershipInfection(nullptr, true, false, other, isStatic, isKinematic);
    }
}

void PhysicsEngine::updateContactMap() {
    // contacts arrive through harvestContacts(); nothing more to do per step
}

void PhysicsEngine::doOwnershipInfectionForConstraints() {
    JPH::BodyID characterBodyID = _myAvatarController ? _myAvatarController->getInnerBodyID() : JPH::BodyID();
    foreach(const auto& dynamic, _objectDynamics) {
        if (!dynamic) {
            continue;
        }
        std::vector<JPH::BodyID> bodies = std::static_pointer_cast<ObjectDynamic>(dynamic)->getBodyIDs();
        if (bodies.size() > 1) {
            int32_t numOwned = 0;
            int32_t numStatic = 0;
            uint8_t priority = VOLUNTEER_SIMULATION_PRIORITY;
            std::vector<ObjectMotionState*> motionStates;
            for (const JPH::BodyID& bodyID : bodies) {
                JPH::BodyLockRead lock(_system->GetBodyLockInterfaceNoLock(), bodyID);
                if (!lock.Succeeded()) {
                    continue;
                }
                const JPH::Body& body = lock.GetBody();
                ObjectMotionState* motionState = motionStateOf(body);
                if (body.IsStatic()) {
                    ++numStatic;
                } else if (motionState && motionState->getType() == MOTIONSTATE_TYPE_AVATAR) {
                    // we can never take ownership of this constraint
                    numOwned = 0;
                    break;
                } else {
                    if (motionState && motionState->getSimulatorID() == Physics::getSessionUUID()) {
                        priority = glm::max(priority, motionState->getSimulationPriority());
                    } else if (bodyID == characterBodyID) {
                        priority = glm::max(priority, PERSONAL_SIMULATION_PRIORITY);
                    }
                    numOwned++;
                }
                if (motionState) {
                    motionStates.push_back(motionState);
                }
            }

            if (numOwned > 0) {
                if (numOwned + numStatic != (int32_t)bodies.size()) {
                    // we have partial ownership but it isn't complete so we walk each object
                    // and bump the simulation priority to the highest priority we encountered earlier
                    for (ObjectMotionState* motionState : motionStates) {
                        // NOTE: we submit priority+1 because the default behavior of bump() is to actually use priority - 1
                        // and we want all priorities of the objects to be at the SAME level
                        motionState->bump(priority + 1);
                    }
                }
            }
        }
    }
}

const CollisionEvents& PhysicsEngine::getCollisionEvents() {
    _collisionEvents.clear();

    // scan known contacts and trigger events
    ContactMap::iterator contactItr = _contactMap.begin();

    while (contactItr != _contactMap.end()) {
        ContactInfo& contact = contactItr->second;
        ContactEventType type = contact.computeType(_numContactFrames);
        const float SIGNIFICANT_DEPTH = -0.002f; // penetrations have negative distance
        if (type != CONTACT_EVENT_TYPE_CONTINUE ||
                (contact.distance < SIGNIFICANT_DEPTH &&
                 contact.readyForContinue(_numContactFrames))) {
            ObjectMotionState* motionStateA = static_cast<ObjectMotionState*>(contactItr->first._a);
            ObjectMotionState* motionStateB = static_cast<ObjectMotionState*>(contactItr->first._b);

            // NOTE: the MyAvatar character is the only object in the simulation that does NOT have a MotionState
            // which means should we ever want to report ALL collision events against the avatar we can
            // modify the logic below.
            //
            // We only create events when at least one of the objects is (or should be) owned in the local simulation.
            if (motionStateA && (motionStateA->isLocallyOwnedOrShouldBe())) {
                QUuid idA = motionStateA->getObjectID();
                QUuid idB;
                if (motionStateB) {
                    idB = motionStateB->getObjectID();
                }
                glm::vec3 position = contact.getPositionWorldOnB() + _originOffset;
                glm::vec3 velocityChange = motionStateA->getObjectLinearVelocityChange() +
                    (motionStateB ? motionStateB->getObjectLinearVelocityChange() : glm::vec3(0.0f));
                glm::vec3 penetration = contact.distance * contact.normalWorldOnB;
                _collisionEvents.push_back(Collision(type, idA, idB, position, penetration, velocityChange));
            } else if (motionStateB && (motionStateB->isLocallyOwnedOrShouldBe())) {
                QUuid idB = motionStateB->getObjectID();
                QUuid idA;
                if (motionStateA) {
                    idA = motionStateA->getObjectID();
                }
                glm::vec3 position = contact.getPositionWorldOnA() + _originOffset;
                glm::vec3 velocityChange = motionStateB->getObjectLinearVelocityChange() +
                    (motionStateA ? motionStateA->getObjectLinearVelocityChange() : glm::vec3(0.0f));
                // NOTE: we're flipping the order of A and B (so that the first objectID is never NULL)
                // hence we negate the penetration (because penetration always points from B to A).
                glm::vec3 penetration = - (contact.distance * contact.normalWorldOnB);
                _collisionEvents.push_back(Collision(type, idB, idA, position, penetration, velocityChange));
            }
        }

        if (type == CONTACT_EVENT_TYPE_END) {
            contactItr = _contactMap.erase(contactItr);
        } else {
            ++contactItr;
        }
    }
    return _collisionEvents;
}

const VectorOfMotionStates& PhysicsEngine::getChangedMotionStates() {
    PROFILE_RANGE(simulation_physics, "copyOutgoingChanges");
    _changedMotionStates.clear();
    _deactivatedMotionStates.clear();

    // every body that is awake has (possibly) moved: copy it out
    JPH::BodyIDVector activeBodies;
    _system->GetActiveBodies(JPH::EBodyType::RigidBody, activeBodies);
    const JPH::BodyLockInterfaceNoLock& lockInterface = _system->GetBodyLockInterfaceNoLock();
    for (const JPH::BodyID& bodyID : activeBodies) {
        JPH::BodyLockRead lock(lockInterface, bodyID);
        if (!lock.Succeeded()) {
            continue;
        }
        ObjectMotionState* motionState = motionStateOf(lock.GetBody());
        if (motionState) {
            motionState->pullTransformFromBody();
            _changedMotionStates.push_back(motionState);
        }
    }

    // bodies that fell asleep since the last harvest
    std::vector<ObjectMotionState*> deactivated;
    _activationRecorder->takeDeactivated(deactivated);
    for (ObjectMotionState* motionState : deactivated) {
        if (_motionStates.find(motionState) != _motionStates.end()) {
            motionState->pullTransformFromBody();
            _deactivatedMotionStates.push_back(motionState);
        }
    }

    // moved static bodies are reported once, then forgotten
    for (ObjectMotionState* motionState : _activeStaticObjects) {
        _changedMotionStates.push_back(motionState);
    }
    _activeStaticObjects.clear();

    _hasOutgoingChanges = false;
    return _changedMotionStates;
}

void PhysicsEngine::dumpStatsIfNecessary() {
    if (_dumpNextStats) {
        _dumpNextStats = false;
        if (_saveNextStats) {
            _saveNextStats = false;
            printPerformanceStatsToFile(_statsFilename);
        }
        qCDebug(physics) << "PhysicsEngine: last step" << _lastStepDuration << "ms," << getNumCollisionObjects() << "bodies,"
                         << (_system ? _system->GetNumActiveBodies(JPH::EBodyType::RigidBody) : 0) << "active,"
                         << (int)_contactMap.size() << "contacts";
    }
}

void PhysicsEngine::saveNextPhysicsStats(QString filename) {
    _saveNextStats = true;
    _dumpNextStats = true;
    _statsFilename = filename;
}

void PhysicsEngine::setCharacterController(CharacterController* character) {
    _myAvatarController = character;
}

EntityDynamicPointer PhysicsEngine::getDynamicByID(const QUuid& dynamicID) const {
    if (_objectDynamics.contains(dynamicID)) {
        return _objectDynamics[dynamicID];
    }
    return nullptr;
}

bool PhysicsEngine::addDynamic(EntityDynamicPointer dynamic) {
    assert(dynamic);

    if (!dynamic->isReadyForAdd()) {
        return false;
    }

    const QUuid& dynamicID = dynamic->getID();
    if (_objectDynamics.contains(dynamicID)) {
        if (_objectDynamics[dynamicID] == dynamic) {
            return true;
        }
        removeDynamic(dynamic->getID());
    }

    bool success { false };
    if (dynamic->isAction()) {
        success = true;
    } else if (dynamic->isConstraint()) {
        ObjectConstraint* objectConstraint = static_cast<ObjectConstraint*>(dynamic.get());
        JPH::Constraint* constraint = objectConstraint->getConstraint();
        if (constraint) {
            _system->AddConstraint(constraint);
            success = true;
        } // else perhaps not all the rigid bodies are available, yet
    }

    if (success) {
        _objectDynamics[dynamicID] = dynamic;
        for (const JPH::BodyID& bodyID : std::static_pointer_cast<ObjectDynamic>(dynamic)->getBodyIDs()) {
            _objectDynamicsByBody[bodyID.GetIndexAndSequenceNumber()] += dynamic->getID();
        }
    }
    return success;
}

void PhysicsEngine::removeDynamic(const QUuid dynamicID) {
    if (_objectDynamics.contains(dynamicID)) {
        ObjectDynamicPointer dynamic = std::static_pointer_cast<ObjectDynamic>(_objectDynamics[dynamicID]);
        if (!dynamic) {
            return;
        }
        std::vector<JPH::BodyID> bodies = dynamic->getBodyIDs();
        if (dynamic->isConstraint()) {
            ObjectConstraint* objectConstraint = static_cast<ObjectConstraint*>(dynamic.get());
            JPH::Constraint* constraint = objectConstraint->getConstraint();
            if (constraint) {
                _system->RemoveConstraint(constraint);
            } else {
                qCDebug(physics) << "PhysicsEngine::removeDynamic of constraint failed";
            }
        }
        _objectDynamics.remove(dynamicID);
        for (const JPH::BodyID& bodyID : bodies) {
            _objectDynamicsByBody[bodyID.GetIndexAndSequenceNumber()].remove(dynamic->getID());
        }
        dynamic->invalidate();
    }
}

void PhysicsEngine::forEachDynamic(std::function<void(EntityDynamicPointer)> actor) {
    QMutableHashIterator<QUuid, EntityDynamicPointer> iter(_objectDynamics);
    while (iter.hasNext()) {
        iter.next();
        if (iter.value()) {
            actor(iter.value());
        }
    }
}

static void setDebugBit(uint32_t& mode, uint32_t bit, bool value) {
    if (value) {
        mode |= bit;
    } else {
        mode &= ~bit;
    }
}

void PhysicsEngine::setShowBulletWireframe(bool value) {
    setDebugBit(_debugDrawMode, PhysicsDebugDraw::DRAW_WIREFRAME, value);
}

void PhysicsEngine::setShowBulletAABBs(bool value) {
    setDebugBit(_debugDrawMode, PhysicsDebugDraw::DRAW_AABBS, value);
}

void PhysicsEngine::setShowBulletContactPoints(bool value) {
    setDebugBit(_debugDrawMode, PhysicsDebugDraw::DRAW_CONTACT_POINTS, value);
}

void PhysicsEngine::setShowBulletConstraints(bool value) {
    setDebugBit(_debugDrawMode, PhysicsDebugDraw::DRAW_CONSTRAINTS, value);
}

void PhysicsEngine::setShowBulletConstraintLimits(bool value) {
    setDebugBit(_debugDrawMode, PhysicsDebugDraw::DRAW_CONSTRAINT_LIMITS, value);
}

// Everything within the given shape, as the zones use it to find what is inside them.
std::vector<ContactTestResult> PhysicsEngine::contactTest(uint16_t mask, const ShapeInfo& regionShapeInfo, const Transform& regionTransform, uint16_t group, float threshold) const {
    std::vector<ContactTestResult> contacts;
    if (!_system) {
        return contacts;
    }
    const JPH::Shape* shape = ObjectMotionState::getShapeManager()->getShape(regionShapeInfo);
    if (!shape) {
        return contacts;
    }

    JPH::BodyID characterBodyID = ((mask & USER_COLLISION_GROUP_MY_AVATAR) && _myAvatarController) ?
        _myAvatarController->getInnerBodyID() : JPH::BodyID();

    JPH::ObjectLayer layer = JoltLayers::layerFor((int32_t)group, (int32_t)mask);
    JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(_layers->objectVsBroadPhase, layer);
    JPH::DefaultObjectLayerFilter objectLayerFilter(_layers->objectLayerPairs, layer);

    JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(glmToJolt(regionTransform.getRotation()), glmToJoltR(regionTransform.getTranslation()));
    JPH::RMat44 centerOfMassTransform = transform.PreTranslated(shape->GetCenterOfMass());
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = 0.0f;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    _system->GetNarrowPhaseQueryNoLock().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), centerOfMassTransform, settings,
                                                       JPH::RVec3::sZero(), collector, broadPhaseFilter, objectLayerFilter);

    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        if (hit.mPenetrationDepth < threshold) {
            continue;
        }
        // the penetration axis points from the test shape (1) toward the found shape (2)
        glm::vec3 axis = joltToGLM(hit.mPenetrationAxis);
        float axisLength = glm::length(axis);
        glm::vec3 normal = axisLength > 0.0f ? -axis / axisLength : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 pointOnTest = joltToGLM(hit.mContactPointOn1);
        glm::vec3 pointOnFound = joltToGLM(hit.mContactPointOn2);

        if (!characterBodyID.IsInvalid() && hit.mBodyID2 == characterBodyID) {
            contacts.emplace_back(Physics::getSessionUUID(), pointOnTest, pointOnFound, normal);
            continue;
        }
        JPH::BodyLockRead lock(_system->GetBodyLockInterfaceNoLock(), hit.mBodyID2);
        if (!lock.Succeeded()) {
            continue;
        }
        ObjectMotionState* candidate = motionStateOf(lock.GetBody());
        if (!candidate) {
            continue;
        }
        contacts.emplace_back(candidate->getObjectID(), pointOnTest, pointOnFound, normal);
    }

    ObjectMotionState::getShapeManager()->releaseShape(shape);
    return contacts;
}

bool PhysicsEngine::castRayClosest(const glm::vec3& start, const glm::vec3& end, int32_t group, int32_t mask,
                                   const JPH::BodyFilter* bodyFilter, RayHit& result) const {
    result = RayHit();
    if (!_system) {
        return false;
    }
    JPH::ObjectLayer layer = JoltLayers::layerFor(group, mask);
    JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(_layers->objectVsBroadPhase, layer);
    JPH::DefaultObjectLayerFilter objectLayerFilter(_layers->objectLayerPairs, layer);
    JPH::BodyFilter noBodyFilter;

    JPH::RRayCast ray(glmToJoltR(start), glmToJolt(end - start));
    JPH::RayCastResult hit;
    bool found = _system->GetNarrowPhaseQueryNoLock().CastRay(ray, hit, broadPhaseFilter, objectLayerFilter,
                                                              bodyFilter ? *bodyFilter : noBodyFilter);
    if (!found) {
        return false;
    }
    result.hit = true;
    result.fraction = hit.mFraction;
    result.bodyID = hit.mBodyID;
    result.point = start + hit.mFraction * (end - start);
    JPH::BodyLockRead lock(_system->GetBodyLockInterfaceNoLock(), hit.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        result.motionState = motionStateOf(body);
        result.normal = joltToGLM(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, glmToJoltR(result.point)));
    }
    return true;
}

void PhysicsEngine::castRayAll(const glm::vec3& start, const glm::vec3& end, int32_t group, int32_t mask,
                               std::vector<RayHit>& results) const {
    results.clear();
    if (!_system) {
        return;
    }
    JPH::ObjectLayer layer = JoltLayers::layerFor(group, mask);
    JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(_layers->objectVsBroadPhase, layer);
    JPH::DefaultObjectLayerFilter objectLayerFilter(_layers->objectLayerPairs, layer);

    JPH::RRayCast ray(glmToJoltR(start), glmToJolt(end - start));
    JPH::RayCastSettings settings;
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    _system->GetNarrowPhaseQueryNoLock().CastRay(ray, settings, collector, broadPhaseFilter, objectLayerFilter);
    collector.Sort();
    results.reserve(collector.mHits.size());
    for (const JPH::RayCastResult& hit : collector.mHits) {
        RayHit result;
        result.hit = true;
        result.fraction = hit.mFraction;
        result.bodyID = hit.mBodyID;
        result.point = start + hit.mFraction * (end - start);
        JPH::BodyLockRead lock(_system->GetBodyLockInterfaceNoLock(), hit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            result.motionState = motionStateOf(body);
            result.normal = joltToGLM(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, glmToJoltR(result.point)));
        }
        results.push_back(result);
    }
}
