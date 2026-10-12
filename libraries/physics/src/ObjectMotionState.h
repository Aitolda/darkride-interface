//
//  ObjectMotionState.h
//  libraries/physics/src
//
//  Created by Andrew Meadows 2014.11.05
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ObjectMotionState_h
#define hifi_ObjectMotionState_h

#include <glm/glm.hpp>
#include <QSet>
#include <QVector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>

#include <SimulationFlags.h>
#include <SpatiallyNestable.h>
#include <Transform.h>

#include "ContactInfo.h"
#include "ShapeManager.h"

namespace JPH {
    class BodyInterface;
}

class PhysicsWorld;

enum PhysicsMotionType {
    MOTION_TYPE_STATIC,     // no motion
    MOTION_TYPE_DYNAMIC,    // motion according to physical laws
    MOTION_TYPE_KINEMATIC   // keyframed motion
};

/**jsdoc
 * <p>An entity's physics motion type may be one of the following:</p>
 * <table>
 *   <thead>
 *     <tr><th>Value</th><th>Description</th></tr>
 *   </thead>
 *   <tbody>
 *     <tr><td><code>"static"</code></td><td>There is no motion because the entity is locked  &mdash; its <code>locked</code> 
 *         property is set to <code>true</code>.</td></tr>
 *     <tr><td><code>"kinematic"</code></td><td>Motion is applied without physical laws (e.g., damping) because the entity is 
 *         not locked and has its <code>dynamic</code> property set to <code>false</code>.</td></tr>
 *     <tr><td><code>"dynamic"</code></td><td>Motion is applied according to physical laws (e.g., damping) because the entity 
 *         is not locked and has its <code>dynamic</code> property set to <code>true</code>.</td></tr>
 *   </tbody>
 * </table>
 * @typedef {string} Entities.PhysicsMotionType
 */
inline QString motionTypeToString(PhysicsMotionType motionType) {
    switch(motionType) {
        case MOTION_TYPE_STATIC: return QString("static");
        case MOTION_TYPE_DYNAMIC: return QString("dynamic");
        case MOTION_TYPE_KINEMATIC: return QString("kinematic");
    }
    return QString("unknown");
}

enum MotionStateType {
    MOTIONSTATE_TYPE_INVALID,
    MOTIONSTATE_TYPE_ENTITY,
    MOTIONSTATE_TYPE_AVATAR,
    MOTIONSTATE_TYPE_DETAILED
};

// The update flags trigger two varieties of updates: "hard" which require the body to be pulled
// and re-added to the physics engine and "easy" which just updates the body properties.
const uint32_t HARD_DIRTY_PHYSICS_FLAGS = (uint32_t)(Simulation::DIRTY_MOTION_TYPE | Simulation::DIRTY_SHAPE |
                                                     Simulation::DIRTY_COLLISION_GROUP);
const uint32_t EASY_DIRTY_PHYSICS_FLAGS = (uint32_t)(Simulation::DIRTY_TRANSFORM | Simulation::DIRTY_VELOCITIES |
                                                     Simulation::DIRTY_MASS | Simulation::DIRTY_MATERIAL |
                                                     Simulation::DIRTY_SIMULATOR_ID | Simulation::DIRTY_SIMULATION_OWNERSHIP_PRIORITY |
                                                     Simulation::DIRTY_PHYSICS_ACTIVATION);

// These are the set of incoming flags that the PhysicsEngine needs to hear about:
const uint32_t DIRTY_PHYSICS_FLAGS = (uint32_t)(HARD_DIRTY_PHYSICS_FLAGS | EASY_DIRTY_PHYSICS_FLAGS);

// These are the outgoing flags that the PhysicsEngine can affect:
const uint32_t OUTGOING_DIRTY_PHYSICS_FLAGS = Simulation::DIRTY_TRANSFORM | Simulation::DIRTY_VELOCITIES;

class OctreeEditPacketSender;
class PhysicsEngine;

// An ObjectMotionState is the bridge between one simulated object (an entity, another avatar, a joint of an
// avatar) and its Jolt body. The object is the source of truth for what the body should be; the body is the
// source of truth for where a dynamic object ended up after a step.
//
// Jolt has no motion-state callbacks. Instead the engine calls pushTransformToBody() before each substep
// (kinematic bodies integrate their keyframed motion here) and pullTransformFromBody() after a step for
// every body that was active (dynamic bodies write their results back here).
class ObjectMotionState {
public:
    // The WorldOffset is used to keep the positions of objects in the simulation near the origin, to
    // reduce numerical error when computing vector differences.  In other words: The EntityMotionState
    // class translates between the simulation-frame and the world-frame as known by the render pipeline,
    // various object trees, etc.  The EntityMotionState class uses a static "worldOffset" to help in
    // the translations.
    static void setWorldOffset(const glm::vec3& offset);
    static const glm::vec3& getWorldOffset();

    // The WorldSimulationStep is a cached copy of number of SubSteps of the simulation, used for local time measurements.
    static void setWorldSimulationStep(uint32_t step);
    static uint32_t getWorldSimulationStep();

    // How far the present is past the last substep (less than one substep). The simulation runs in fixed
    // substeps and is read once per rendered frame, so the results are read this much ahead, along each
    // body's velocity; otherwise everything that moves would step by one or two substeps per frame.
    static void setLeftoverTime(float seconds);
    static float getLeftoverTime();

    // Moving reference frames (PHYSICS.md 4.1), for any object with a parent chain: entities and avatars.
    // The nearest ancestor that is a frame; null when there is none.
    static SpatiallyNestablePointer findFrameAncestor(const SpatiallyNestablePointer& object);
    // The transform and velocities of the object's parent in the coordinates of the object's frame: the chain
    // from the parent up to (not including) the frame, composed. False when the object has no frame.
    static bool getParentTransformInFrame(const SpatiallyNestablePointer& object, Transform& transform,
                                          glm::vec3& linearVelocity, glm::vec3& angularVelocity);
    // The world transform of the object's frame, with unit scale, for taking world-space poses into the frame.
    // False when the object has no frame.
    static bool getFrameTransform(const SpatiallyNestablePointer& object, Transform& frameTransform);

    static void setShapeManager(ShapeManager* manager);
    static ShapeManager* getShapeManager();

    ObjectMotionState(const JPH::Shape* shape);
    virtual ~ObjectMotionState();

    virtual void handleEasyChanges(uint32_t& flags);

    void updateBodyMaterialProperties();
    void updateBodyVelocities();
    void updateLastKinematicStep();
    virtual void updateBodyMassProperties();

    MotionStateType getType() const { return _type; }
    virtual PhysicsMotionType getMotionType() const { return _motionType; }

    virtual void setMass(float mass);
    virtual float getMass() const;

    void setBodyLinearVelocity(const glm::vec3& velocity) const;
    void setBodyAngularVelocity(const glm::vec3& velocity) const;
    void setBodyGravity(const glm::vec3& gravity);

    glm::vec3 getBodyLinearVelocity() const;
    glm::vec3 getBodyLinearVelocityGTSigma() const;
    glm::vec3 getBodyAngularVelocity() const;
    // The gravity this body wants; the engine applies it as a force every substep (the world's own gravity is zero,
    // so that every object can have its own, including none).
    const glm::vec3& getBodyGravity() const { return _gravity; }

    virtual glm::vec3 getObjectLinearVelocityChange() const;

    virtual uint32_t getIncomingDirtyFlags() const = 0;
    virtual void clearIncomingDirtyFlags(uint32_t mask = DIRTY_PHYSICS_FLAGS) = 0;

    virtual PhysicsMotionType computePhysicsMotionType() const = 0;

    virtual bool needsNewShape() const { return _shape == nullptr || getIncomingDirtyFlags() & Simulation::DIRTY_SHAPE; }
    const JPH::Shape* getShape() const { return _shape; }
    JPH::Body* getBody() const { return _body; }
    JPH::BodyID getBodyID() const;
    bool isInPhysicsSimulation() const { return _body != nullptr; }
    // the world the body lives in (null without a body)
    PhysicsWorld* getWorld() const { return _world; }

    // The moving reference frame this object belongs to: the frame entity's ID, or null for the domain itself.
    // The engine simulates the object in that frame's world, and the object's position, rotation and
    // velocities (getObject*) are then in that frame's coordinates.
    virtual QUuid getFrameID() const { return QUuid(); }
    // true when this object is itself a frame, whose own interior is a world of its own
    virtual bool isFrame() const { return false; }
    // For a frame: its transform and velocities in the world it moves in (its parent's, the domain for an
    // unnested frame), from the object's own record (the entity), which is what the renderer composes against too.
    bool getFrameTransformInDomain(glm::vec3& position, glm::quat& rotation, glm::vec3& linearVelocity, glm::vec3& angularVelocity) const;
    // For a frame: the box, in the frame's coordinates, inside which something belongs to the frame.
    virtual bool getFrameVolume(glm::vec3& minCorner, glm::vec3& maxCorner) const { return false; }

    virtual bool isMoving() const = 0;

    // These pure virtual methods must be implemented for each derived class
    virtual float getObjectRestitution() const = 0;
    virtual float getObjectFriction() const = 0;
    virtual float getObjectLinearDamping() const = 0;
    virtual float getObjectAngularDamping() const = 0;

    virtual glm::vec3 getObjectPosition() const = 0;
    virtual glm::quat getObjectRotation() const = 0;
    virtual glm::vec3 getObjectLinearVelocity() const = 0;
    virtual glm::vec3 getObjectAngularVelocity() const = 0;
    virtual glm::vec3 getObjectGravity() const = 0;

    virtual const QUuid getObjectID() const = 0;

    virtual uint8_t getSimulationPriority() const { return 0; }
    virtual QUuid getSimulatorID() const = 0;
    virtual void bump(uint8_t priority) {}

    virtual QString getName() const { return ""; }
    virtual ShapeType getShapeType() const = 0;

    virtual void computeCollisionGroupAndMask(int32_t& group, int32_t& mask) const = 0;

    // Which of the six degrees of freedom the body may use; avatars never tumble, for instance.
    virtual JPH::EAllowedDOFs getAllowedDOFs() const { return JPH::EAllowedDOFs::All; }

    // inline because the entity renderer reads it without linking the physics library
    bool isActive() const { return _body ? _body->IsActive() : false; }
    void activateBody();
    void deactivateBody();

    // The body's position and rotation in the simulation frame (world minus the world offset). Setting them
    // places the body outright; actions that hold an object use this every substep.
    void setBodyTransform(const glm::vec3& position, const glm::quat& rotation, bool activate);
    void getBodyTransform(glm::vec3& position, glm::quat& rotation) const;

    bool hasInternalKinematicChanges() const { return _hasInternalKinematicChanges; }

    void dirtyInternalKinematicChanges() const { _hasInternalKinematicChanges = true; }
    void clearInternalKinematicChanges() const { _hasInternalKinematicChanges = false; }

    virtual bool isLocallyOwned() const { return false; }
    virtual bool isLocallyOwnedOrShouldBe() const { return false; } // aka shouldEmitCollisionEvents()

    // Called by the engine before every substep. Kinematic objects integrate their own motion and move the body
    // toward it; dynamic objects have nothing to do here. Parents are pushed before their children (see
    // getNestingDepth), so a child of a moving thing follows this substep's position of its parent.
    virtual void pushTransformToBody(float dt) {}
    // how many parents the object has; the engine pushes shallower objects first
    virtual int getNestingDepth() const { return 0; }
    // Called by the engine after a step for every body that was active: copy the body's transform and velocities out.
    virtual void pullTransformFromBody() {}

    friend class PhysicsEngine;

    friend class PhysicsWorld;

protected:
    virtual void setMotionType(PhysicsMotionType motionType);
    void updateCCDConfiguration();

    // The body is created and destroyed by its PhysicsWorld; while it exists, the world's no-lock body
    // interface is used, between steps, on the simulation thread.
    virtual void setBody(JPH::Body* body, PhysicsWorld* world);
    JPH::BodyInterface* bodyInterface() const;
    virtual void setShape(const JPH::Shape* shape);


    MotionStateType _type { MOTIONSTATE_TYPE_INVALID }; // type of MotionState
    PhysicsMotionType _motionType { MOTION_TYPE_STATIC }; // type of motion: KINEMATIC, DYNAMIC, or STATIC

    const JPH::Shape* _shape { nullptr };
    JPH::Body* _body { nullptr };
    PhysicsWorld* _world { nullptr };
    glm::vec3 _gravity { 0.0f };
    float _density { 1.0f };

    mutable uint32_t _lastKinematicStep;
    // how far a keyframed object's own record has been read ahead of its last substep (see getLeftoverTime)
    mutable float _extrapolatedTime { 0.0f };
    mutable bool _hasInternalKinematicChanges { false };
};

using SetOfMotionStates = QSet<ObjectMotionState*>;
using VectorOfMotionStates = QVector<ObjectMotionState*>;

#endif // hifi_ObjectMotionState_h
