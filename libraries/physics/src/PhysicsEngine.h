//
//  PhysicsEngine.h
//  libraries/physics/src
//
//  Created by Andrew Meadows 2014.10.29
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_PhysicsEngine_h
#define hifi_PhysicsEngine_h

#include <stdint.h>

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include <QUuid>
#include <QHash>
#include <QSet>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include "JoltLayers.h"
#include "ContactInfo.h"
#include "ObjectMotionState.h"
#include "ObjectAction.h"
#include "ObjectConstraint.h"

namespace JPH {
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BodyFilter;
}

const float HALF_SIMULATION_EXTENT = 512.0f; // meters

class CharacterController;
class PhysicsDebugDraw;

// A contact between two simulated objects. Only MyAvatar's body has no ObjectMotionState, so a null _a
// means the avatar's character. Both null never happens.
class ContactKey {
public:
    ContactKey() = delete;
    ContactKey(void* a, void* b) : _a(a), _b(b) {}
    bool operator<(const ContactKey& other) const { return _a < other._a || (_a == other._a && _b < other._b); }
    bool operator==(const ContactKey& other) const { return _a == other._a && _b == other._b; }
    void* _a; // ObjectMotionState pointer
    void* _b; // ObjectMotionState pointer
};

struct ContactTestResult {
    ContactTestResult() = delete;

    ContactTestResult(const ContactTestResult& contactTestResult) :
        foundID(contactTestResult.foundID),
        testCollisionPoint(contactTestResult.testCollisionPoint),
        foundCollisionPoint(contactTestResult.foundCollisionPoint),
        collisionNormal(contactTestResult.collisionNormal) {
    }

    ContactTestResult(const QUuid& foundID, const glm::vec3& testCollisionPoint, const glm::vec3& otherCollisionPoint, const glm::vec3& collisionNormal) :
        foundID(foundID),
        testCollisionPoint(testCollisionPoint),
        foundCollisionPoint(otherCollisionPoint),
        collisionNormal(collisionNormal) {
    }

    QUuid foundID;
    // The deepest point of foundID's shape within the test shape, in world space.
    glm::vec3 testCollisionPoint;
    // The deepest point of the test shape within foundID's shape, in world space.
    glm::vec3 foundCollisionPoint;
    // The normal vector in world space of the test shape at foundCollisionPoint.
    glm::vec3 collisionNormal;
};

// What a ray cast hit, in the simulation frame.
struct RayHit {
    bool hit { false };
    float fraction { 1.0f };
    JPH::BodyID bodyID;
    ObjectMotionState* motionState { nullptr }; // null for MyAvatar's own body
    glm::vec3 point { 0.0f };
    glm::vec3 normal { 0.0f, 1.0f, 0.0f };
};

using ContactMap = std::map<ContactKey, ContactInfo>;
using CollisionEvents = std::vector<Collision>;

// One Jolt physics world and everything the client puts in it: bodies for entities and avatars, actions and
// constraints, MyAvatar's character, the contact bookkeeping that becomes script collision events and
// simulation ownership bids, and the debug drawing.
class PhysicsEngine {
public:
    class Transaction {
    public:
        void clear() {
            objectsToRemove.clear();
            objectsToAdd.clear();
            objectsToReinsert.clear();
            activeStaticObjects.clear();
        }
        std::vector<ObjectMotionState*> objectsToRemove;
        std::vector<ObjectMotionState*> objectsToAdd;
        std::vector<ObjectMotionState*> objectsToReinsert;
        std::vector<ObjectMotionState*> activeStaticObjects;
    };

    PhysicsEngine(const glm::vec3& offset);
    ~PhysicsEngine();
    void init();

    uint32_t getNumSubsteps() const { return _numSubsteps; }
    int32_t getNumCollisionObjects() const;

    void removeObjects(const VectorOfMotionStates& objects);
    void removeSetOfObjects(const SetOfMotionStates& objects); // only called during teardown

    void addObjects(const VectorOfMotionStates& objects);
    void reinsertObject(ObjectMotionState* object);

    void processTransaction(Transaction& transaction);

    void stepSimulation();
    void harvestPerformanceStats();
    void printPerformanceStatsToFile(const QString& filename);
    void updateContactMap();
    void doOwnershipInfectionForConstraints();

    bool hasOutgoingChanges() const { return _hasOutgoingChanges; }

    /// \return reference to list of changed MotionStates.  The list is only valid until beginning of next simulation loop.
    const VectorOfMotionStates& getChangedMotionStates();
    const VectorOfMotionStates& getDeactivatedMotionStates() const { return _deactivatedMotionStates; }

    /// \return reference to list of Collision events.  The list is only valid until beginning of next simulation loop.
    const CollisionEvents& getCollisionEvents();

    /// \brief prints timings for last simulation step
    void dumpStatsIfNecessary();
    /// \brief saves timings for last simulation step in filename
    void saveNextPhysicsStats(QString filename);

    /// \param offset position of simulation origin in domain-frame
    void setOriginOffset(const glm::vec3& offset) { _originOffset = offset; }

    /// \return position of simulation origin in domain-frame
    const glm::vec3& getOriginOffset() const { return _originOffset; }

    void setCharacterController(CharacterController* character);

    void dumpNextStats() { _dumpNextStats = true; }

    EntityDynamicPointer getDynamicByID(const QUuid& dynamicID) const;
    bool addDynamic(EntityDynamicPointer dynamic);
    void removeDynamic(const QUuid dynamicID);
    void forEachDynamic(std::function<void(EntityDynamicPointer)> actor);

    void setShowBulletWireframe(bool value);
    void setShowBulletAABBs(bool value);
    void setShowBulletContactPoints(bool value);
    void setShowBulletConstraints(bool value);
    void setShowBulletConstraintLimits(bool value);

    // Copies of these methods exist in PhysicsEntitySimulation.
    // There is a possible performance improvement in avoiding the duplication of the ShapeInfo calculation.
    std::vector<ContactTestResult> contactTest(uint16_t mask, const ShapeInfo& regionShapeInfo, const Transform& regionTransform, uint16_t group = USER_COLLISION_GROUP_DYNAMIC, float threshold = 0.0f) const;

    void removeContacts(ObjectMotionState* motionState);

    // Ray casts in the simulation frame, filtered as a body of the given group and mask would be.
    // A null body filter means "everything".
    bool castRayClosest(const glm::vec3& start, const glm::vec3& end, int32_t group, int32_t mask,
                        const JPH::BodyFilter* bodyFilter, RayHit& result) const;
    void castRayAll(const glm::vec3& start, const glm::vec3& end, int32_t group, int32_t mask,
                    std::vector<RayHit>& results) const;

    // A contact made by MyAvatar's character (which has no ObjectMotionState) against another body, recorded
    // by the CharacterController after its own update so it feeds collision events and ownership like any other.
    void recordCharacterContact(ObjectMotionState* other, const glm::vec3& positionOnOther, const glm::vec3& normalTowardCharacter, float distance);

    // Jolt, for the character controller and the constraints.
    JPH::PhysicsSystem* getPhysicsSystem() const { return _system.get(); }
    JPH::BodyInterface& getBodyInterface() const;
    JPH::TempAllocator* getTempAllocator() const;
    const JoltLayers::Tables& getLayers() const { return *_layers; }
    // the motion state behind a body, or null for MyAvatar's character and unknown bodies
    static ObjectMotionState* motionStateOf(const JPH::Body& body);

private:
    class ContactRecorder;
    class ActivationRecorder;

    QList<EntityDynamicPointer> removeDynamicsForBody(const JPH::BodyID& bodyID);
    void addObjectToDynamicsWorld(ObjectMotionState* motionState);
    void destroyBody(ObjectMotionState* motionState);
    void wakeBodiesTouching(ObjectMotionState* motionState);
    void wakeBodiesAround(const JPH::Body* body);

    void stepOnce(float dt);
    void applyGravityForces();
    void harvestContacts();
    void doOwnershipInfection(ObjectMotionState* motionStateA, bool aIsCharacter, bool aIsStatic,
                              ObjectMotionState* motionStateB, bool bIsStatic, bool bIsKinematic);

    // created in init(), after Jolt's allocator exists: a Jolt container before that is a crash
    std::unique_ptr<JoltLayers::Tables> _layers;
    std::unique_ptr<JPH::TempAllocatorImpl> _tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> _jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> _system;
    std::unique_ptr<ContactRecorder> _contactRecorder;
    std::unique_ptr<ActivationRecorder> _activationRecorder;
    std::unique_ptr<PhysicsDebugDraw> _physicsDebugDraw;

    ContactMap _contactMap;
    CollisionEvents _collisionEvents;
    QHash<QUuid, EntityDynamicPointer> _objectDynamics;
    QHash<uint32_t, QSet<QUuid>> _objectDynamicsByBody; // keyed by BodyID::GetIndexAndSequenceNumber()
    std::set<ObjectMotionState*> _motionStates; // every motion state with a body in this world
    std::set<ObjectMotionState*> _kinematicMotionStates;
    std::set<ObjectMotionState*> _activeStaticObjects;
    VectorOfMotionStates _changedMotionStates;
    VectorOfMotionStates _deactivatedMotionStates;
    QString _statsFilename;
    glm::vec3 _originOffset;
    CharacterController* _myAvatarController { nullptr };
    uint64_t _lastStepTime { 0 };
    float _accumulatedTime { 0.0f };
    float _lastStepDuration { 0.0f };
    uint32_t _numSubsteps { 0 };
    uint32_t _numContactFrames { 0 };
    uint32_t _debugDrawMode { 0 };
    bool _dumpNextStats { false };
    bool _saveNextStats { false };
    bool _hasOutgoingChanges { false };
};

typedef std::shared_ptr<PhysicsEngine> PhysicsEnginePointer;

#endif // hifi_PhysicsEngine_h
