//
//  ObjectMotionState.cpp
//  libraries/physics/src
//
//  Created by Andrew Meadows 2014.11.05
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ObjectMotionState.h"

#include <math.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "JoltUtil.h"
#include "PhysicsEngine.h"
#include "PhysicsHelpers.h"
#include "PhysicsLogging.h"

// these thresholds determine what updates (object-->body) will activate the physical object
const float ACTIVATION_POSITION_DELTA = 0.005f;
const float ACTIVATION_ALIGNMENT_DOT = 0.99990f;
const float ACTIVATION_LINEAR_VELOCITY_DELTA = 0.01f;
const float ACTIVATION_GRAVITY_DELTA = 0.1f;
const float ACTIVATION_ANGULAR_VELOCITY_DELTA = 0.03f;

// origin of physics simulation in world-frame
glm::vec3 _worldOffset(0.0f);

// static
void ObjectMotionState::setWorldOffset(const glm::vec3& offset) {
    _worldOffset = offset;
}

// static
const glm::vec3& ObjectMotionState::getWorldOffset() {
    return _worldOffset;
}

// static
uint32_t worldSimulationStep = 0;
void ObjectMotionState::setWorldSimulationStep(uint32_t step) {
    assert(step > worldSimulationStep);
    worldSimulationStep = step;
}

// static
uint32_t ObjectMotionState::getWorldSimulationStep() {
    return worldSimulationStep;
}

// static
ShapeManager* shapeManager = nullptr;
void ObjectMotionState::setShapeManager(ShapeManager* manager) {
    assert(manager);
    shapeManager = manager;
}

ShapeManager* ObjectMotionState::getShapeManager() {
    assert(shapeManager); // you must properly set shapeManager before calling getShapeManager()
    return shapeManager;
}

ObjectMotionState::ObjectMotionState(const JPH::Shape* shape) :
    _lastKinematicStep(worldSimulationStep)
{
    setShape(shape);
}

ObjectMotionState::~ObjectMotionState() {
    assert(!_body);
    setShape(nullptr);
}

JPH::BodyID ObjectMotionState::getBodyID() const {
    return _body ? _body->GetID() : JPH::BodyID();
}

// the mass is the density of the shape's bounding box, so that a changed shape keeps its feel
static float boundsVolume(const JPH::Shape* shape) {
    JPH::AABox bounds = shape->GetLocalBounds();
    JPH::Vec3 diagonal = bounds.GetSize();
    return diagonal.GetX() * diagonal.GetY() * diagonal.GetZ();
}

void ObjectMotionState::setMass(float mass) {
    _density = 1.0f;
    if (_shape) {
        // we compute the density for the current shape's Aabb volume
        // and save that instead of the total mass
        float volume = boundsVolume(_shape);
        if (volume > EPSILON) {
            _density = fabsf(mass) / volume;
        }
    }
}

float ObjectMotionState::getMass() const {
    if (_shape && _shape->GetType() != JPH::EShapeType::Mesh) {
        // scale the density by the current Aabb volume to get mass
        float volume = boundsVolume(_shape);

        // cap the max mass for numerical stability
        const float MIN_OBJECT_MASS = 0.0f;
        const float MAX_OBJECT_DENSITY = 20000.0f; // kg/m^3 density of Tungsten
        const float MAX_OBJECT_VOLUME = 1.0e6f;
        const float MAX_OBJECT_MASS = MAX_OBJECT_DENSITY * MAX_OBJECT_VOLUME;
        return glm::clamp(_density * volume, MIN_OBJECT_MASS, MAX_OBJECT_MASS);
    }
    return 0.0f;
}

void ObjectMotionState::setBodyLinearVelocity(const glm::vec3& velocity) const {
    if (_body && _bodyInterface && !_body->IsStatic()) {
        _bodyInterface->SetLinearVelocity(_body->GetID(), glmToJolt(velocity));
    }
}

void ObjectMotionState::setBodyAngularVelocity(const glm::vec3& velocity) const {
    if (_body && _bodyInterface && !_body->IsStatic()) {
        _bodyInterface->SetAngularVelocity(_body->GetID(), glmToJolt(velocity));
    }
}

void ObjectMotionState::setBodyGravity(const glm::vec3& gravity) {
    // the world has no gravity of its own; the engine applies this one as a force each substep
    _gravity = gravity;
}

glm::vec3 ObjectMotionState::getBodyLinearVelocity() const {
    return (_body && !_body->IsStatic()) ? joltToGLM(_body->GetLinearVelocity()) : glm::vec3(0.0f);
}

glm::vec3 ObjectMotionState::getBodyLinearVelocityGTSigma() const {
    // NOTE: the threshold to use here relates to the linear displacement threshold (dX) for sending updates
    // to objects that are tracked server-side (e.g. entities which use dX = 2mm).  Hence an object moving
    // just under this velocity threshold would trigger an update about V/dX times per second.
    const float MIN_LINEAR_SPEED_SQUARED = 0.0036f; // 6 mm/sec

    glm::vec3 velocity = getBodyLinearVelocity();
    if (glm::length2(velocity) < MIN_LINEAR_SPEED_SQUARED) {
        velocity *= 0.0f;
    }
    return velocity;
}

glm::vec3 ObjectMotionState::getObjectLinearVelocityChange() const {
    return glm::vec3(0.0f);  // Subclasses override where meaningful.
}

glm::vec3 ObjectMotionState::getBodyAngularVelocity() const {
    return (_body && !_body->IsStatic()) ? joltToGLM(_body->GetAngularVelocity()) : glm::vec3(0.0f);
}

void ObjectMotionState::setMotionType(PhysicsMotionType motionType) {
    _motionType = motionType;
}

// Continuous collision detection (a linear cast per step) for the small, fast things that would otherwise
// pass through walls; big and mesh shapes don't need it.
void ObjectMotionState::updateCCDConfiguration() {
    assert(_body);
    if (!_bodyInterface || _body->IsStatic()) {
        return;
    }
    const float MAX_RADIUS_FOR_CCD = 0.5f; // meters
    bool wantsCCD = _shape && _shape->GetType() != JPH::EShapeType::Mesh && _shape->GetInnerRadius() < MAX_RADIUS_FOR_CCD;
    _bodyInterface->SetMotionQuality(_body->GetID(), wantsCCD ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete);
}

void ObjectMotionState::setBody(JPH::Body* body, JPH::BodyInterface* bodyInterface) {
    // give the body a back-pointer to this ObjectMotionState
    if (_body != body) {
        if (_body) {
            _body->SetUserData(0);
        }
        _body = body;
        _bodyInterface = body ? bodyInterface : nullptr;
        if (_body) {
            _body->SetUserData((JPH::uint64)(uintptr_t)this);
            assert(_body->GetShape() == _shape);
            updateCCDConfiguration();
        }
    }
}

void ObjectMotionState::setShape(const JPH::Shape* shape) {
    if (_shape != shape) {
        if (_shape) {
            getShapeManager()->releaseShape(_shape);
        }
        _shape = shape;
        if (_body) {
            assert(_shape);
            _bodyInterface->SetShape(_body->GetID(), _shape, false, JPH::EActivation::DontActivate);
            updateCCDConfiguration();
        }
    } else if (shape) {
        // we need to release unused reference to shape
        getShapeManager()->releaseShape(shape);
    }
}

void ObjectMotionState::setBodyTransform(const glm::vec3& position, const glm::quat& rotation, bool activate) {
    if (_body && _bodyInterface) {
        _bodyInterface->SetPositionAndRotation(_body->GetID(), glmToJoltR(position), glmToJolt(rotation),
                                               activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    }
}

void ObjectMotionState::getBodyTransform(glm::vec3& position, glm::quat& rotation) const {
    if (_body) {
        position = joltToGLM(_body->GetPosition());
        rotation = joltToGLM(_body->GetRotation());
    }
}

void ObjectMotionState::activateBody() {
    if (_body && _bodyInterface && !_body->IsStatic()) {
        _bodyInterface->ActivateBody(_body->GetID());
    }
}

void ObjectMotionState::deactivateBody() {
    if (_body && _bodyInterface && !_body->IsStatic()) {
        _bodyInterface->DeactivateBody(_body->GetID());
    }
}

void ObjectMotionState::handleEasyChanges(uint32_t& flags) {
    assert(_body && _shape);
    glm::vec3 bodyPosition;
    glm::quat bodyRotation;
    getBodyTransform(bodyPosition, bodyRotation);

    if (flags & (Simulation::DIRTY_POSITION | Simulation::DIRTY_ROTATION)) {
        glm::vec3 newPosition = bodyPosition;
        glm::quat newRotation = bodyRotation;
        if (flags & Simulation::DIRTY_POSITION) {
            newPosition = getObjectPosition();
            float delta = glm::length(newPosition - bodyPosition);
            if (delta > ACTIVATION_POSITION_DELTA) {
                flags |= Simulation::DIRTY_PHYSICS_ACTIVATION;
            }
        }
        if (flags & Simulation::DIRTY_ROTATION) {
            newRotation = getObjectRotation();
            float alignmentDot = fabsf(glm::dot(bodyRotation, newRotation));
            if (alignmentDot < ACTIVATION_ALIGNMENT_DOT) {
                flags |= Simulation::DIRTY_PHYSICS_ACTIVATION;
            }
        }
        // a moved static body is placed without activation; the engine wakes what it touches
        setBodyTransform(newPosition, newRotation, false);
    }

    if (!_body->IsStatic() && _shape->GetType() != JPH::EShapeType::Mesh) {
        if (flags & Simulation::DIRTY_LINEAR_VELOCITY) {
            glm::vec3 newLinearVelocity = getObjectLinearVelocity();
            if (!(flags & Simulation::DIRTY_PHYSICS_ACTIVATION)) {
                float delta = glm::length(newLinearVelocity - getBodyLinearVelocity());
                if (delta > ACTIVATION_LINEAR_VELOCITY_DELTA) {
                    flags |= Simulation::DIRTY_PHYSICS_ACTIVATION;
                }
            }
            setBodyLinearVelocity(newLinearVelocity);

            glm::vec3 newGravity = getObjectGravity();
            if (!(flags & Simulation::DIRTY_PHYSICS_ACTIVATION)) {
                float delta = glm::length(newGravity - _gravity);
                if (delta > ACTIVATION_GRAVITY_DELTA ||
                        (delta > 0.0f && glm::length2(_gravity) == 0.0f)) {
                    flags |= Simulation::DIRTY_PHYSICS_ACTIVATION;
                }
            }
            setBodyGravity(newGravity);
        }
        if (flags & Simulation::DIRTY_ANGULAR_VELOCITY) {
            glm::vec3 newAngularVelocity = getObjectAngularVelocity();
            if (!(flags & Simulation::DIRTY_PHYSICS_ACTIVATION)) {
                float delta = glm::length(newAngularVelocity - getBodyAngularVelocity());
                if (delta > ACTIVATION_ANGULAR_VELOCITY_DELTA) {
                    flags |= Simulation::DIRTY_PHYSICS_ACTIVATION;
                }
            }
            setBodyAngularVelocity(newAngularVelocity);
        }
    }

    if (flags & Simulation::DIRTY_MATERIAL) {
        updateBodyMaterialProperties();
    }

    if (flags & Simulation::DIRTY_MASS) {
        updateBodyMassProperties();
    }
}

// The entity's damping d means "the velocity loses the fraction d of itself each second": v(t) = v0 (1-d)^t.
// Jolt's damping c is the rate in dv/dt = -c v, so v(t) = v0 e^(-c t). The two agree when c = -ln(1-d).
static float joltDamping(float damping) {
    const float MAX_DAMPING = 0.999f;
    damping = glm::clamp(fabsf(damping), 0.0f, MAX_DAMPING);
    return -logf(1.0f - damping);
}

void ObjectMotionState::updateBodyMaterialProperties() {
    if (!_body || !_bodyInterface) {
        return;
    }
    _bodyInterface->SetRestitution(_body->GetID(), getObjectRestitution());
    _bodyInterface->SetFriction(_body->GetID(), getObjectFriction());
    JPH::MotionProperties* motion = _body->GetMotionPropertiesUnchecked();
    if (motion) {
        motion->SetLinearDamping(joltDamping(getObjectLinearDamping()));
        motion->SetAngularDamping(joltDamping(getObjectAngularDamping()));
    }
}

void ObjectMotionState::updateBodyVelocities() {
    setBodyLinearVelocity(getObjectLinearVelocity());
    setBodyAngularVelocity(getObjectAngularVelocity());
    setBodyGravity(getObjectGravity());
    activateBody();
}

void ObjectMotionState::updateLastKinematicStep() {
    // The step counter advances before each substep's push, so starting at the current step gives exactly
    // one substep of motion on the first push. (Starting a step earlier gave two, and that doubled velocity
    // was then read back from the body into the entity.)
    _lastKinematicStep = ObjectMotionState::getWorldSimulationStep();
}

void ObjectMotionState::updateBodyMassProperties() {
    if (!_body) {
        return;
    }
    JPH::MotionProperties* motion = _body->GetMotionPropertiesUnchecked();
    if (!motion || _body->GetMotionType() != JPH::EMotionType::Dynamic) {
        return;
    }
    float mass = getMass();
    const float MIN_DYNAMIC_MASS = 0.01f;
    if (mass != mass || mass < MIN_DYNAMIC_MASS) {
        mass = MIN_DYNAMIC_MASS;
    }
    JPH::MassProperties massProperties = _shape->GetMassProperties();
    massProperties.ScaleToMass(mass);
    motion->SetMassProperties(motion->GetAllowedDOFs(), massProperties);
}
