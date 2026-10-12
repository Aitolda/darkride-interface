//
//  AvatarMotionState.h
//  interface/src/avatar/
//
//  Created by Andrew Meadows 2015.05.14
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_AvatarMotionState_h
#define hifi_AvatarMotionState_h

#include <QSet>

#include <ObjectMotionState.h>
#include <JoltUtil.h>

#include "OtherAvatar.h"

// Another person's avatar in the physics world: a dynamic capsule that springs toward where the network says
// the avatar is, so that it pushes and is pushed like a body but never drifts from its owner's position.
class AvatarMotionState : public ObjectMotionState {
public:
    AvatarMotionState(OtherAvatarPointer avatar, const JPH::Shape* shape);

    void handleEasyChanges(uint32_t& flags) override;

    PhysicsMotionType getMotionType() const override { return _motionType; }

    uint32_t getIncomingDirtyFlags() const override;
    void clearIncomingDirtyFlags(uint32_t mask = DIRTY_PHYSICS_FLAGS) override;

    PhysicsMotionType computePhysicsMotionType() const override;

    bool isMoving() const override;

    void pullTransformFromBody() override;

    float getObjectRestitution() const override;
    float getObjectFriction() const override;
    float getObjectLinearDamping() const override;
    float getObjectAngularDamping() const override;

    glm::vec3 getObjectPosition() const override;
    glm::quat getObjectRotation() const override;
    glm::vec3 getObjectLinearVelocity() const override;
    glm::vec3 getObjectAngularVelocity() const override;
    glm::vec3 getObjectGravity() const override;

    const QUuid getObjectID() const override;

    QString getName() const override;
    ShapeType getShapeType() const override { return SHAPE_TYPE_CAPSULE_Y; }

    QUuid getSimulatorID() const override;

    void setBoundingBox(const glm::vec3& corner, const glm::vec3& diagonal);

    void addDirtyFlags(uint32_t flags) { _dirtyFlags |= flags; }

    void setCollisionGroup(int32_t group) { _collisionGroup = group; }
    int32_t getCollisionGroup() { return _collisionGroup; }

    void computeCollisionGroupAndMask(int32_t& group, int32_t& mask) const override;

    // an avatar's body never tumbles
    JPH::EAllowedDOFs getAllowedDOFs() const override { return JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ; }

    float getMass() const override;

    friend class AvatarManager;
    friend class Avatar;

protected:
    void setShape(const JPH::Shape* shape) override;
    void cacheShapeDiameter();

    // The AvatarMotionState keeps a RAW backpointer to its Avatar because all AvatarMotionState
    // instances are "owned" by their corresponding Avatar instance and are deleted in the Avatar dtor.
    // In other words, the Avatar will always be around for the lifetime of AvatarMotionState.
    ~AvatarMotionState();

    OtherAvatarPointer _avatar;
    float _diameter { 0.0f };
    int32_t _collisionGroup;
    uint32_t _dirtyFlags;
};

#endif // hifi_AvatarMotionState_h
