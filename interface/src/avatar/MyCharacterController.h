//
//  MyCharacterController.h
//  interface/src/avatar
//
//  Created by AndrewMeadows 2015.10.21
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MyCharacterController_h
#define hifi_MyCharacterController_h

#include <CharacterController.h>
#include <PhysicsEngine.h>

class MyAvatar;
class DetailedMotionState;

// MyAvatar's character: the engine's character plus what only the avatar needs, the capsule built from the
// skeleton, the detailed per-joint shapes, the walkability probe, and the ray test against other avatars.
class MyCharacterController : public CharacterController {
public:
    explicit MyCharacterController(std::shared_ptr<MyAvatar> avatar);
    ~MyCharacterController ();

    void addToWorld() override;
    void updateShapeIfNecessary() override;

    // Sweeping a "probe" along the character's path to find walkable floor: a set of rays from the front of
    // the capsule, sent along the step, tells whether the step lands on floor or on a wall too steep to walk.
    class RayShotgunResult {
    public:
        void reset();
        float hitFraction { 1.0f };
        bool walkable { true };
    };

    /// return true if RayShotgun hits anything
    bool testRayShotgun(const glm::vec3& position, const glm::vec3& step, RayShotgunResult& result);

    void setDensity(float density) { _density = density; }

    const JPH::Shape* createDetailedCollisionShapeForJoint(int32_t jointIndex);
    DetailedMotionState* createDetailedMotionStateForJoint(int32_t jointIndex);
    std::vector<DetailedMotionState*>& getDetailedMotionStates() { return _detailedMotionStates; }
    void clearDetailedMotionStates();

    void buildPhysicsTransaction(PhysicsEngine::Transaction& transaction);

    struct RayAvatarResult {
        bool _intersect { false };
        bool _isBound { false };
        QUuid _intersectWithAvatar;
        int32_t _intersectWithJoint { -1 };
        float _distance { 0.0f };
        float _maxDistance { 0.0f };
        QVariantMap _extraInfo;
        glm::vec3 _intersectionPoint;
        glm::vec3 _intersectionNormal;
        std::vector<int32_t> _boundJoints;
    };
    /// every avatar joint along a ray, nearest first (world frame in, world frame out)
    std::vector<RayAvatarResult> rayTest(const glm::vec3& origin, const glm::vec3& direction, float length,
                                         const QVector<uint>& jointsToExclude) const;

    int32_t computeCollisionMask() const override;
    void handleChangedCollisionMask() override;

    void setCollideWithOtherAvatars(bool collideWithOtherAvatars) { _collideWithOtherAvatars = collideWithOtherAvatars; }
    bool needsSafeLandingSupport() const;

protected:
    void initRayShotgun();
    void updateMassProperties() override;

private:
    JPH::RefConst<JPH::Shape> computeShape() const;

protected:
    std::shared_ptr<MyAvatar> _avatar { nullptr };

    // shotgun scan data
    std::vector<glm::vec3> _topPoints;
    std::vector<glm::vec3> _bottomPoints;
    float _density { 1.0f };
    std::vector<DetailedMotionState*> _detailedMotionStates;
    bool _collideWithOtherAvatars { true };
};

#endif // hifi_MyCharacterController_h
