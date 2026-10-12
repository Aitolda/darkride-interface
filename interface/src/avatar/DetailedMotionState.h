//
//  DetailedMotionState.h
//  interface/src/avatar/
//
//  Created by Luis Cuenca 1/11/2019
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_DetailedMotionState_h
#define hifi_DetailedMotionState_h

#include <QSet>

#include <ObjectMotionState.h>
#include <JoltUtil.h>

#include "OtherAvatar.h"

// One joint of an avatar in the physics world: a kinematic body that follows the joint, so that rays can
// find which avatar and which joint they hit and so that hands can push things.
class DetailedMotionState : public ObjectMotionState {
public:
    DetailedMotionState(AvatarPointer avatar, const JPH::Shape* shape, int jointIndex);

    void handleEasyChanges(uint32_t& flags) override;

    PhysicsMotionType getMotionType() const override { return _motionType; }

    uint32_t getIncomingDirtyFlags() const override;
    void clearIncomingDirtyFlags(uint32_t mask = DIRTY_PHYSICS_FLAGS) override;

    PhysicsMotionType computePhysicsMotionType() const override;

    bool isMoving() const override;

    void pushTransformToBody(float dt) override;

    float getObjectRestitution() const override;
    float getObjectFriction() const override;
    float getObjectLinearDamping() const override;
    float getObjectAngularDamping() const override;

    // in the coordinates of the avatar's frame when it rides one, else the domain's
    glm::vec3 getObjectPosition() const override;
    glm::quat getObjectRotation() const override;
    glm::vec3 getObjectLinearVelocity() const override;
    glm::vec3 getObjectAngularVelocity() const override;
    glm::vec3 getObjectGravity() const override;
    QUuid getFrameID() const override;

    const QUuid getObjectID() const override;

    QString getName() const override;
    ShapeType getShapeType() const override { return SHAPE_TYPE_HULL; }

    QUuid getSimulatorID() const override;

    void addDirtyFlags(uint32_t flags) { _dirtyFlags |= flags; }

    void computeCollisionGroupAndMask(int32_t& group, int32_t& mask) const override;

    JPH::EAllowedDOFs getAllowedDOFs() const override { return JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ; }

    float getMass() const override;
    void forceActive();
    QUuid getAvatarID() const { return _avatar->getID(); }
    int32_t getJointIndex() const { return _jointIndex; }
    void setIsBound(bool isBound, const std::vector<int32_t>& boundJoints) { _isBound = isBound; _boundJoints = boundJoints; }
    bool getIsBound(std::vector<int32_t>& boundJoints) const { boundJoints = _boundJoints; return _isBound; }

    friend class AvatarManager;
    friend class Avatar;

protected:
    void setShape(const JPH::Shape* shape) override;

    // The DetailedMotionState keeps a RAW backpointer to its Avatar because all AvatarMotionState
    // instances are "owned" by their corresponding Avatar instance and are deleted in the Avatar dtor.
    // In other words, the Avatar will always be around for the lifetime of AvatarMotionState.
    ~DetailedMotionState();

    AvatarPointer _avatar;
    float _diameter { 0.0f };
    uint32_t _dirtyFlags;
    int32_t _jointIndex { -1 };
    OtherAvatarPointer _otherAvatar { nullptr };
    bool _isBound { false };
    std::vector<int32_t> _boundJoints;
};

#endif // hifi_DetailedMotionState_h
