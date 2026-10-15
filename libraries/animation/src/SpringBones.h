//
//  SpringBones.h
//  libraries/animation/src
//
//  Spring bones for imported avatars: the secondary animation of hair, ears, tails, ribbons and the like, driven by
//  the avatar's own component data (the .fst line "springBones = {...}", one entry per component: the joints it
//  moves, its forces, limits, colliders and curves, written by the importer in the components' own field names).
//  A solver beside Flow (the engine's older spring bones, keyed by joint names) that shares its plumbing: the rest
//  pose from the animated relative poses, the avatar's frame (world orientation, the avatar's translation removed),
//  the rotations written back into the rig's poses so they travel in the avatar packet.
//
//  The law is the one measured from recordings of the components' own runtime, step by step (research/spring-bones
//  in darkride-ops): a fixed step with the frames between interpolated, a two-term linear return to rest per particle
//  (velocity kept, a pull toward the rest), positions kept in the world so that a turning parent moves the rest away
//  from the particle by its full arc, the parent's translation carried except for a fraction of the avatar's own
//  movement (immobile). The coefficients' dependence on the component's pull, spring, immobile, gravity and version is
//  being read from a parameter sweep; until then the functions in SpringBoneLaw hold the fitted points.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_SpringBones_h
#define hifi_SpringBones_h

#include <array>
#include <map>
#include <memory>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include "AnimPose.h"

class AnimSkeleton;

namespace springbones {

// the simulation's own step; the frames between two steps are interpolated
const float STEP = 1.0f / 30.0f;
const int MAX_STEPS_PER_FRAME = 4;
const float TELEPORT_DISTANCE = 1.0f;   // m: a root that jumps this far restarts its chain at rest

// A Unity animation curve, unweighted cubic Hermite keys [time, value, inSlope, outSlope, inWeight, outWeight, mode].
struct Curve {
    std::vector<std::array<float, 7>> keys;
    bool empty() const { return keys.empty(); }
    float evaluate(float t) const;
};

enum class Shape { Sphere = 0, Capsule = 1, Plane = 2 };
enum class LimitType { None = 0, Angle = 1, Hinge = 2, Polar = 3 };
enum class ImmobileType { AllMotion = 0, World = 1 };
enum class MultiChildType { Ignore = 0, First = 1, Average = 2 };

// One component, the data as the .fst gives it.
struct ComponentSpec {
    QString root;
    QStringList joints;                 // every joint it moves, depth first
    int version { 1 };                  // 0 = 1.0, 1 = 1.1
    int integrationType { 0 };          // 0 simplified, 1 advanced
    float pull { 0.2f };
    float spring { 0.2f };              // momentum, in the advanced integration
    float stiffness { 0.2f };
    float gravity { 0.0f };
    float gravityFalloff { 0.0f };
    float immobile { 0.0f };
    ImmobileType immobileType { ImmobileType::AllMotion };
    LimitType limitType { LimitType::None };
    float maxAngleX { 45.0f };
    float maxAngleZ { 45.0f };
    glm::quat limitRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    float radius { 0.0f };
    std::vector<int> colliders;         // indices into Data::colliders
    bool allowCollision { true };
    glm::vec3 endpoint { 0.0f };        // a virtual bone past every leaf, in the leaf's frame (0 for none)
    MultiChildType multiChildType { MultiChildType::Ignore };
    bool isAnimated { false };
    float maxStretch { 0.0f };
    float maxSquish { 0.0f };
    float stretchMotion { 0.0f };
    bool enabled { true };
    std::map<QString, Curve> curves;    // by field name; multiplies the field from the root (0) to the tip (1)

    float curved(const QString& field, float base, float t) const;
};

struct ColliderSpec {
    QString joint;
    Shape shape { Shape::Sphere };
    float radius { 0.0f };
    float height { 0.0f };              // capsules: along the shape's local Y, the half-spheres included
    glm::vec3 position { 0.0f };        // in the joint's frame
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    bool insideBounds { false };
    bool bonesAsSpheres { false };
};

struct Data {
    int version { 0 };
    std::vector<ComponentSpec> components;
    std::vector<ColliderSpec> colliders;
    bool empty() const { return components.empty(); }
    static Data fromJson(const QJsonObject& json);
};

// The law's coefficients per step, as fitted; one place to change when the sweep is read.
struct Law {
    static float velocityKept(const ComponentSpec& spec, float spring);
    static float pull(const ComponentSpec& spec, float pull);
    // the fraction of the avatar's own movement per step that is not carried by the chain (the immobile term)
    static float avatarMoveFelt(const ComponentSpec& spec, float immobile);
};

struct Particle {
    int joint { -1 };                   // skeleton joint, or -1 for an endpoint
    int parent { -1 };                  // particle index; -1 for the chain root
    std::vector<int> children;
    int depth { 0 };
    glm::vec3 restLocal { 0.0f };       // position in the parent joint's frame (rig units); endpoints: the endpoint
    float length { 0.0f };

    // the settings after the curves
    float pull { 0.0f };
    float spring { 0.0f };
    float stiffness { 0.0f };
    float gravity { 0.0f };
    float gravityFalloff { 0.0f };
    float immobile { 0.0f };
    float radius { 0.0f };
    float maxAngleX { 0.0f };
    float maxAngleZ { 0.0f };

    // this frame's animation (the rest)
    glm::quat animRotation { 1.0f, 0.0f, 0.0f, 0.0f };   // relative to the parent joint
    glm::vec3 animPosition { 0.0f };                       // avatar frame

    // the simulation's state at its last two steps, avatar frame
    glm::vec3 position { 0.0f };
    glm::vec3 previousPosition { 0.0f };
    glm::vec3 presented { 0.0f };
    // the position in the chain parent's frame at the last two steps: their difference is the velocity the law keeps
    glm::vec3 localPosition { 0.0f };
    glm::vec3 previousLocalPosition { 0.0f };
    // the fitted rotations at the last two steps (joints only) and the presented one
    glm::quat localRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::quat previousLocalRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::quat worldRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::quat presentedLocalRotation { 1.0f, 0.0f, 0.0f, 0.0f };
};

struct Chain {
    const ComponentSpec* spec { nullptr };
    std::vector<Particle> particles;    // the root first, parents before children
    int rootJoint { -1 };
    int rootParentJoint { -1 };
    // the root's parent at the last step, avatar frame
    glm::vec3 lastParentPosition { 0.0f };
    glm::quat lastParentRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    bool hasLast { false };
};

struct Collider {
    const ColliderSpec* spec { nullptr };
    int joint { -1 };
    // this frame, avatar frame
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    // keeps `point` (a particle of radius `radius`, its bone running from `from`) out of the shape; true when it moved it
    bool pushOut(const glm::vec3& from, glm::vec3& point, float radius, float scale) const;
};

class Solver {
public:
    // Builds the chains over the skeleton. `lengthScale` takes the data's metres to rig units (the .fst's scale).
    bool init(const Data& data, const std::shared_ptr<AnimSkeleton>& skeleton, const AnimPoseVec& relativePoses,
              const AnimPoseVec& absolutePoses, float lengthScale);
    void cleanUp();
    bool isInitialized() const { return _initialized; }
    void setActive(bool active) { _active = active; }
    bool getActive() const { return _active; }

    // The avatar's transform this frame (as Flow's setTransform: the sensor-to-world scale, the position, the
    // orientation turned to the rig's facing).
    void setTransform(float scale, const glm::vec3& position, const glm::quat& rotation);
    // Simulates the frame and writes the chain joints' rotations into the relative poses (and their absolute poses).
    void update(float deltaTime, AnimPoseVec& relativePoses, AnimPoseVec& absolutePoses, const std::vector<bool>& overrideFlags);
    // Copies the chain joints' rotations from one pose set to another (the network pose set gets the same chains).
    void copyJointRotations(const AnimPoseVec& fromRelativePoses, AnimPoseVec& toRelativePoses) const;

    const std::vector<Chain>& getChains() const { return _chains; }
    // A test's lockstep: once on, an update advances the simulation only by the time a test has handed it since the
    // last update (advance()), whatever the frames take, so a recording can be replayed frame by frame.
    static void setLockstep(bool on) { _lockstep = on; _pendingTime = 0.0f; }
    static bool getLockstep() { return _lockstep; }
    static void advance(float seconds) { _pendingTime += seconds; }
    const Data& getData() const { return _data; }
    int getJointCount() const { return _jointCount; }

private:
    void readAnimation(Chain& chain, const AnimPoseVec& relativePoses, const AnimPoseVec& absolutePoses);
    void resetChain(Chain& chain);
    // One step of a chain: its root's parent went from the last frame to the next (position and rotation in the
    // avatar's frame); `avatarMove` is the avatar's own movement over the step.
    void stepChain(Chain& chain, const glm::vec3& avatarMove, const glm::vec3& lastParentPosition, const glm::quat& lastParentRotation,
                   const glm::vec3& parentPosition, const glm::quat& parentRotation);
    void updateColliders(const AnimPoseVec& absolutePoses);
    glm::vec3 jointPosition(const AnimPoseVec& absolutePoses, int joint) const;
    glm::quat jointRotation(const AnimPoseVec& absolutePoses, int joint) const;

    Data _data;
    std::vector<Chain> _chains;
    std::vector<Collider> _colliders;
    std::shared_ptr<AnimSkeleton> _skeleton;
    float _lengthScale { 1.0f };
    float _scale { 1.0f };
    glm::vec3 _entityPosition { 0.0f };
    glm::quat _entityRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 _lastEntityPosition { 0.0f };
    bool _hasLastEntity { false };
    glm::vec3 _avatarMoveSinceStep { 0.0f };   // world
    float _accumulator { 0.0f };
    int _jointCount { 0 };
    bool _initialized { false };
    bool _active { false };
    static bool _lockstep;
    static float _pendingTime;
};

}

#endif // hifi_SpringBones_h
