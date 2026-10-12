//
//  AvatarMotionState.cpp
//  interface/src/avatar/
//
//  Created by Andrew Meadows 2015.05.14
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "AvatarMotionState.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <PhysicsCollisionGroups.h>
#include <PhysicsEngine.h>
#include <PhysicsHelpers.h>

AvatarMotionState::AvatarMotionState(OtherAvatarPointer avatar, const JPH::Shape* shape) : ObjectMotionState(shape), _avatar(avatar) {
    assert(_avatar);
    _type = MOTIONSTATE_TYPE_AVATAR;
    _collisionGroup = BULLET_COLLISION_GROUP_OTHER_AVATAR;
    cacheShapeDiameter();
}

void AvatarMotionState::handleEasyChanges(uint32_t& flags) {
    ObjectMotionState::handleEasyChanges(flags);
    if (flags & Simulation::DIRTY_PHYSICS_ACTIVATION && !isActive()) {
        activateBody();
    }
}

AvatarMotionState::~AvatarMotionState() {
    assert(_avatar);
    _avatar = nullptr;
}

// virtual
uint32_t AvatarMotionState::getIncomingDirtyFlags() const {
    return _body ? _dirtyFlags : 0;
}

void AvatarMotionState::clearIncomingDirtyFlags(uint32_t mask) {
    if (_body) {
        _dirtyFlags &= ~mask;
    }
}

PhysicsMotionType AvatarMotionState::computePhysicsMotionType() const {
    // TODO?: support non-DYNAMIC motion for avatars? (e.g. when sitting)
    return MOTION_TYPE_DYNAMIC;
}

// virtual and protected
bool AvatarMotionState::isMoving() const {
    return false;
}

// After a step: the body is where the simulation put it, the avatar is where the network says it is.
// Far apart, the body jumps to the avatar; close, it springs toward it so pushes still happen.
void AvatarMotionState::pullTransformFromBody() {
    if (!_body) {
        return;
    }
    const float SPRING_TIMESCALE = 0.5f;
    float tau = PHYSICS_ENGINE_FIXED_SUBSTEP / SPRING_TIMESCALE;
    glm::vec3 currentPosition;
    glm::quat currentRotation;
    getBodyTransform(currentPosition, currentRotation);
    glm::vec3 offsetToTarget = getObjectPosition() - currentPosition;
    float distance = glm::length(offsetToTarget);
    if ((1.0f - tau) * distance > _diameter) {
        // the avatar body is far from its target --> slam position
        setBodyTransform(currentPosition + offsetToTarget, getObjectRotation(), true);
        setBodyLinearVelocity(getObjectLinearVelocity());
        setBodyAngularVelocity(getObjectAngularVelocity());
    } else {
        // the avatar body is near its target --> slam velocity
        glm::vec3 velocity = getObjectLinearVelocity() + (1.0f / SPRING_TIMESCALE) * offsetToTarget;
        setBodyLinearVelocity(velocity);
        setBodyAngularVelocity(getObjectAngularVelocity());
        // slam its rotation
        setBodyTransform(currentPosition, getObjectRotation(), true);
    }
}

// These pure virtual methods must be implemented for each MotionState type
// and make it possible to implement more complicated methods in this base class.

// virtual
float AvatarMotionState::getObjectRestitution() const {
    return 0.5f;
}

// virtual
float AvatarMotionState::getObjectFriction() const {
    return 0.5f;
}

// virtual
float AvatarMotionState::getObjectLinearDamping() const {
    return 0.5f;
}

// virtual
float AvatarMotionState::getObjectAngularDamping() const {
    return 0.5f;
}

// Another person's avatar arrives as a parent and a local pose. Riding a frame, the body lives in the
// frame's world and follows that local pose, so the rider holds still on the deck however the frame moves.
// virtual
glm::vec3 AvatarMotionState::getObjectPosition() const {
    Transform parentInFrame;
    glm::vec3 v, w;
    if (ObjectMotionState::getParentTransformInFrame(_avatar, parentInFrame, v, w)) {
        return parentInFrame.transform(_avatar->getLocalPosition());
    }
    return _avatar->getWorldPosition();
}

// virtual
glm::quat AvatarMotionState::getObjectRotation() const {
    Transform parentInFrame;
    glm::vec3 v, w;
    if (ObjectMotionState::getParentTransformInFrame(_avatar, parentInFrame, v, w)) {
        return parentInFrame.getRotation() * _avatar->getLocalOrientation();
    }
    return _avatar->getWorldOrientation();
}

// virtual
glm::vec3 AvatarMotionState::getObjectLinearVelocity() const {
    Transform parentInFrame;
    glm::vec3 v, w;
    if (ObjectMotionState::getParentTransformInFrame(_avatar, parentInFrame, v, w)) {
        return v + parentInFrame.getRotation() * _avatar->getLocalVelocity();
    }
    return _avatar->getWorldVelocity();
}

// virtual
QUuid AvatarMotionState::getFrameID() const {
    SpatiallyNestablePointer frame = ObjectMotionState::findFrameAncestor(_avatar);
    return frame ? frame->getID() : QUuid();
}

// virtual
glm::vec3 AvatarMotionState::getObjectAngularVelocity() const {
    return glm::vec3(0.0f);
}

// virtual
glm::vec3 AvatarMotionState::getObjectGravity() const {
    return _avatar->getAcceleration();
}

// virtual
const QUuid AvatarMotionState::getObjectID() const {
    return _avatar->getSessionUUID();
}

QString AvatarMotionState::getName() const {
    return _avatar->getName();
}

// virtual
QUuid AvatarMotionState::getSimulatorID() const {
    return _avatar->getSessionUUID();
}

// virtual
void AvatarMotionState::computeCollisionGroupAndMask(int32_t& group, int32_t& mask) const {
    group = _collisionGroup;
    mask = Physics::getDefaultCollisionMask(group);
    if (!_avatar->getCollideWithOtherAvatars()) {
        mask &= ~(BULLET_COLLISION_GROUP_MY_AVATAR | BULLET_COLLISION_GROUP_OTHER_AVATAR);
    }
}

// virtual
float AvatarMotionState::getMass() const {
    return _avatar->computeMass();
}

void AvatarMotionState::cacheShapeDiameter() {
    if (_shape) {
        _diameter = _shape->GetLocalBounds().GetSize().GetX();
    } else {
        _diameter = 0.0f;
    }
}

void AvatarMotionState::setShape(const JPH::Shape* shape) {
    ObjectMotionState::setShape(shape);
    cacheShapeDiameter();
}
