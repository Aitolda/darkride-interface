//
//  MyCharacterController.cpp
//  interface/src/avatar
//
//  Created by AndrewMeadows 2015.10.21
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "MyCharacterController.h"

#include <glm/gtx/norm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <JoltUtil.h>
#include <PhysicsCollisionGroups.h>

#include "MyAvatar.h"
#include "DetailedMotionState.h"

void MyCharacterController::RayShotgunResult::reset() {
    hitFraction = 1.0f;
    walkable = true;
}

MyCharacterController::MyCharacterController(std::shared_ptr<MyAvatar> avatar) {

    assert(avatar);
    _avatar = avatar;
    updateShapeIfNecessary();
}

MyCharacterController::~MyCharacterController() {
}

void MyCharacterController::addToWorld() {
    CharacterController::addToWorld();
    if (_character) {
        initRayShotgun();
    }
}

void MyCharacterController::updateShapeIfNecessary() {
    if (_pendingFlags & PENDING_FLAG_UPDATE_SHAPE) {
        _pendingFlags &= ~PENDING_FLAG_UPDATE_SHAPE;

        if (_radius > 0.0f) {
            // the character is rebuilt around the new shape by the remove and add that come with this flag
            _shape = computeShape();
            updateMassProperties();
        } else {
            // TODO: handle this failure case
        }
    }
}

bool MyCharacterController::testRayShotgun(const glm::vec3& position, const glm::vec3& step, RayShotgunResult& result) {
    glm::vec3 rayDirection = step;
    float stepLength = glm::length(rayDirection);
    if (stepLength < FLT_EPSILON || !_character || !_physicsEngine) {
        return false;
    }
    rayDirection /= stepLength;

    // get _ghost ready for ray traces
    glm::vec3 newPosition = position;
    glm::quat rotation = joltToGLM(_character->GetRotation());

    glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 adjustedDirection = rayDirection - glm::dot(rayDirection, _currentUp) * _currentUp;
    glm::vec3 axis = glm::cross(forward, adjustedDirection);
    float lengthAxis = glm::length(axis);
    if (lengthAxis > FLT_EPSILON) {
        // we're walking sideways
        float cosAngle = lengthAxis / glm::length(adjustedDirection);
        if (cosAngle < 1.0f) {
            float angle = acosf(cosAngle);
            if (glm::dot(rayDirection, forward) < 0.0f) {
                angle = PI - angle;
            }
            axis /= lengthAxis;
            rotation = glm::angleAxis(angle, axis) * rotation;
        }
    } else if (glm::dot(rayDirection, forward) < 0.0f) {
        // we're walking backwards
        rotation = glm::angleAxis(PI, _currentUp) * rotation;
    }

    // scan the top
    // NOTE: if we scan an extra distance forward we can detect flat surfaces that are too steep to walk on.
    // The approximate extra distance can be derived with trigonometry.
    //
    //   minimumForward = [ (maxStepHeight + radius / cosTheta - radius) * (cosTheta / sinTheta) - radius ]
    //
    // where: theta = max angle between floor normal and vertical
    //
    // if stepLength is not long enough we can add the difference.
    //
    float cosTheta = _minFloorNormalDotUp;
    float sinTheta = sqrtf(1.0f - cosTheta * cosTheta);
    const float MIN_FORWARD_SLOP = 0.12f; // HACK: not sure why this is necessary to detect steepest walkable slope
    float forwardSlop = (_maxStepHeight + _radius / cosTheta - _radius) * (cosTheta / sinTheta) - (_radius + stepLength) + MIN_FORWARD_SLOP;
    if (forwardSlop < 0.0f) {
        // BIG step, no slop necessary
        forwardSlop = 0.0f;
    }

    const float backSlop = 0.04f;
    int32_t group = BULLET_COLLISION_GROUP_MY_AVATAR;
    int32_t mask = computeCollisionMask() & ~group;
    JPH::IgnoreSingleBodyFilter ignoreMe(getInnerBodyID());
    RayHit hit;
    float closestHitFraction = 1.0f;

    for (const glm::vec3& point : _topPoints) {
        glm::vec3 rayStart = newPosition + rotation * point - backSlop * rayDirection;
        glm::vec3 rayEnd = rayStart + (backSlop + stepLength + forwardSlop) * rayDirection;
        if (_physicsEngine->castRayClosest(rayStart, rayEnd, group, mask, &ignoreMe, hit)) {
            if (hit.fraction < closestHitFraction) {
                closestHitFraction = hit.fraction;
            }
            if (result.walkable) {
                if (glm::dot(hit.normal, _currentUp) < _minFloorNormalDotUp) {
                    result.walkable = false;
                    // the top scan wasn't walkable so don't bother scanning the bottom
                    // remove both forwardSlop and backSlop
                    result.hitFraction = glm::min(1.0f, (closestHitFraction * (backSlop + stepLength + forwardSlop) - backSlop) / stepLength);
                    return result.hitFraction < 1.0f;
                }
            }
        }
    }
    if (_state == State::Hover) {
        // scan the bottom just like the top
        for (const glm::vec3& point : _bottomPoints) {
            glm::vec3 rayStart = newPosition + rotation * point - backSlop * rayDirection;
            glm::vec3 rayEnd = rayStart + (backSlop + stepLength + forwardSlop) * rayDirection;
            if (_physicsEngine->castRayClosest(rayStart, rayEnd, group, mask, &ignoreMe, hit)) {
                if (hit.fraction < closestHitFraction) {
                    closestHitFraction = hit.fraction;
                }
                if (result.walkable) {
                    if (glm::dot(hit.normal, _currentUp) < _minFloorNormalDotUp) {
                        result.walkable = false;
                        // remove both forwardSlop and backSlop
                        result.hitFraction = glm::min(1.0f, (closestHitFraction * (backSlop + stepLength + forwardSlop) - backSlop) / stepLength);
                        return result.hitFraction < 1.0f;
                    }
                }
            }
        }
    } else {
        // scan the bottom looking for nearest step point
        // remove forwardSlop
        result.hitFraction = (closestHitFraction * (backSlop + stepLength + forwardSlop)) / (backSlop + stepLength);

        for (const glm::vec3& point : _bottomPoints) {
            glm::vec3 rayStart = newPosition + rotation * point - backSlop * rayDirection;
            glm::vec3 rayEnd = rayStart + (backSlop + stepLength) * rayDirection;
            if (_physicsEngine->castRayClosest(rayStart, rayEnd, group, mask, &ignoreMe, hit)) {
                if (hit.fraction < closestHitFraction) {
                    closestHitFraction = hit.fraction;
                }
            }
        }
        // remove backSlop
        // NOTE: backSlop removal can produce a NEGATIVE hitFraction!
        // which means the shape is actually in interpenetration
        result.hitFraction = ((closestHitFraction * (backSlop + stepLength)) - backSlop) / stepLength;
    }
    return result.hitFraction < 1.0f;
}

int32_t MyCharacterController::computeCollisionMask() const {
    int32_t collisionMask = BULLET_COLLISION_MASK_MY_AVATAR;
    if (_collisionless && _collisionlessAllowed) {
        collisionMask = BULLET_COLLISION_MASK_COLLISIONLESS;
    } else if (!_collideWithOtherAvatars) {
        collisionMask &= ~BULLET_COLLISION_GROUP_OTHER_AVATAR;
    }
    return collisionMask;
}

void MyCharacterController::handleChangedCollisionMask() {
    if (_pendingFlags & PENDING_FLAG_UPDATE_COLLISION_MASK) {
        // ATM the easiest way to update collision groups/masks is to remove/re-add the body
        _pendingFlags |= (PENDING_FLAG_REMOVE_FROM_SIMULATION | PENDING_FLAG_ADD_TO_SIMULATION);
        _pendingFlags &= ~PENDING_FLAG_UPDATE_COLLISION_MASK;
        updateCurrentGravity();
    }
}

bool MyCharacterController::needsSafeLandingSupport() const {
    return _isStuck && _numStuckSubsteps >= NUM_SUBSTEPS_FOR_SAFE_LANDING_RETRY;
}

JPH::RefConst<JPH::Shape> MyCharacterController::computeShape() const {
    // a capsule standing on its end, the character's position at its centre
    const float MIN_HALF_HEIGHT = 0.001f;
    if (_halfHeight < MIN_HALF_HEIGHT) {
        return new JPH::SphereShape(_radius);
    }
    return new JPH::CapsuleShape(_halfHeight, _radius);
}

// The probe points: a grid over the front of the capsule, split at the step height into a "top" set that
// must not hit anything steep and a "bottom" set that finds the step. They are on the capsule's surface,
// in the character's local frame, forward being -Z.
void MyCharacterController::initRayShotgun() {
    const float fullHalfHeight = _radius + _halfHeight;
    const float divisionLine = -fullHalfHeight + _maxStepHeight; // line between top and bottom
    const float topHeight = fullHalfHeight - divisionLine;
    const float slop = 0.02f;

    const int32_t NUM_ROWS = 5; // must be odd number > 1
    const int32_t NUM_COLUMNS = 5; // must be odd number > 1

    // the front surface of the capsule at local (x, y): the cylinder in the middle, the spheres at the ends
    auto surfaceZ = [&](float x, float y) -> float {
        float dy = 0.0f;
        if (y > _halfHeight) {
            dy = y - _halfHeight;
        } else if (y < -_halfHeight) {
            dy = y + _halfHeight;
        }
        float r2 = _radius * _radius - x * x - dy * dy;
        return r2 > 0.0f ? -sqrtf(r2) : 0.0f;
    };

    { // top points
        _topPoints.clear();
        _topPoints.reserve(NUM_ROWS * NUM_COLUMNS);
        float stepY = (topHeight - slop) / (float)(NUM_ROWS - 1);
        float stepX = 2.0f * (_radius - slop) / (float)(NUM_COLUMNS - 1);
        for (int32_t i = 0; i < NUM_ROWS; ++i) {
            int32_t maxJ = NUM_COLUMNS;
            float offsetX = -(float)((NUM_COLUMNS - 1) / 2) * stepX;
            if (i % 2 == 1) {
                // odd rows have one less point and start offset
                maxJ -= 1;
                offsetX += 0.5f * stepX;
            }
            for (int32_t j = 0; j < maxJ; ++j) {
                float x = offsetX + (float)(j) * stepX;
                float y = divisionLine + (float)(i) * stepY;
                _topPoints.push_back(glm::vec3(x, y, surfaceZ(x, y)));
            }
        }
    }
    { // bottom points
        _bottomPoints.clear();
        _bottomPoints.reserve(NUM_ROWS * NUM_COLUMNS);

        float steepestStepHitHeight = (_radius + 0.04f) * (1.0f - DEFAULT_MIN_FLOOR_NORMAL_DOT_UP);
        float stepY = (_maxStepHeight - slop - steepestStepHitHeight) / (float)(NUM_ROWS - 1);
        float stepX = 2.0f * (_radius - slop) / (float)(NUM_COLUMNS - 1);
        for (int32_t i = 0; i < NUM_ROWS; ++i) {
            int32_t maxJ = NUM_COLUMNS;
            float offsetX = -(float)((NUM_COLUMNS - 1) / 2) * stepX;
            if (i % 2 == 1) {
                // odd rows have one less point and start offset
                maxJ -= 1;
                offsetX += 0.5f * stepX;
            }
            for (int32_t j = 0; j < maxJ; ++j) {
                float x = offsetX + (float)(j) * stepX;
                float y = (divisionLine - slop) - (float)(i) * stepY;
                _bottomPoints.push_back(glm::vec3(x, y, surfaceZ(x, y)));
            }
        }
    }
}

void MyCharacterController::updateMassProperties() {
    float r2 = _radius * _radius;
    float volumeSphere = 4.0f * PI * r2 * _radius / 3.0f;
    float volumeCylinder = TWO_PI * r2 * 2.0f * _halfHeight;
    _mass = _density * (volumeCylinder + volumeSphere);
}

const JPH::Shape* MyCharacterController::createDetailedCollisionShapeForJoint(int32_t jointIndex) {
    ShapeInfo shapeInfo;
    _avatar->computeDetailedShapeInfo(shapeInfo, jointIndex);
    if (shapeInfo.getType() != SHAPE_TYPE_NONE) {
        const JPH::Shape* shape = ObjectMotionState::getShapeManager()->getShape(shapeInfo);
        return shape;
    }
    return nullptr;
}

DetailedMotionState* MyCharacterController::createDetailedMotionStateForJoint(int32_t jointIndex) {
    const JPH::Shape* shape = createDetailedCollisionShapeForJoint(jointIndex);
    if (shape) {
        DetailedMotionState* motionState = new DetailedMotionState(_avatar, shape, jointIndex);
        motionState->setMass(_avatar->computeMass());
        return motionState;
    }
    return nullptr;
}

void MyCharacterController::clearDetailedMotionStates() {
    // we don't actually clear the MotionStates here
    // instead we twiddle some flags as a signal of what to do later
    _pendingFlags |= PENDING_FLAG_REMOVE_DETAILED_FROM_SIMULATION; 
    // We make sure we don't add them again
    _pendingFlags &= ~PENDING_FLAG_ADD_DETAILED_TO_SIMULATION;
}

void MyCharacterController::buildPhysicsTransaction(PhysicsEngine::Transaction& transaction) {
    for (auto& detailedMotionState : _detailedMotionStates) {
        detailedMotionState->forceActive();
    }
    if (_pendingFlags & PENDING_FLAG_REMOVE_DETAILED_FROM_SIMULATION) {
        _pendingFlags &= ~PENDING_FLAG_REMOVE_DETAILED_FROM_SIMULATION;
        for (auto& detailedMotionState : _detailedMotionStates) {
            transaction.objectsToRemove.push_back(detailedMotionState);
        }
        _detailedMotionStates.clear();
    }
    if (_pendingFlags & PENDING_FLAG_ADD_DETAILED_TO_SIMULATION) {
        _pendingFlags &= ~PENDING_FLAG_ADD_DETAILED_TO_SIMULATION;
        for (int32_t i = 0; i < _avatar->getJointCount(); i++) {
            auto dMotionState = createDetailedMotionStateForJoint(i);
            if (dMotionState) {
                _detailedMotionStates.push_back(dMotionState);
                transaction.objectsToAdd.push_back(dMotionState);
            }
        }
    }
}

std::vector<MyCharacterController::RayAvatarResult> MyCharacterController::rayTest(const glm::vec3& origin, const glm::vec3& direction,
                                                                                   float length, const QVector<uint>& jointsToExclude) const {
    std::vector<RayAvatarResult> foundAvatars;
    if (_physicsEngine) {
        glm::vec3 offset = ObjectMotionState::getWorldOffset();
        glm::vec3 start = origin - offset;
        glm::vec3 end = start + length * direction;
        std::vector<RayHit> hits;
        _physicsEngine->castRayAll(start, end, BULLET_COLLISION_GROUP_DETAILED_RAY, BULLET_COLLISION_MASK_DETAILED_RAY, hits);
        foundAvatars.reserve(hits.size());
        for (const RayHit& hit : hits) {
            ObjectMotionState* motionState = hit.motionState;
            if (motionState && motionState->getType() == MOTIONSTATE_TYPE_DETAILED) {
                DetailedMotionState* detailedMotionState = dynamic_cast<DetailedMotionState*>(motionState);
                MyCharacterController::RayAvatarResult result;
                result._intersect = true;
                result._intersectWithAvatar = detailedMotionState->getAvatarID();
                result._intersectionPoint = hit.point + offset;
                result._intersectionNormal = hit.normal;
                result._distance = length * hit.fraction;
                result._intersectWithJoint = detailedMotionState->getJointIndex();
                result._isBound = detailedMotionState->getIsBound(result._boundJoints);
                const JPH::Shape* shape = detailedMotionState->getShape();
                result._maxDistance = shape ? 0.5f * shape->GetLocalBounds().GetSize().Length() : 0.0f;
                foundAvatars.push_back(result);
            }
        }
        std::sort(foundAvatars.begin(), foundAvatars.end(), [](const RayAvatarResult& resultA, const RayAvatarResult& resultB) {
            return resultA._distance < resultB._distance;
        });
    }
    return foundAvatars;
}
