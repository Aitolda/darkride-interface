//
//  SpringBones.cpp
//  libraries/animation/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "SpringBones.h"

#include <QJsonArray>

#include <GLMHelpers.h>
#include <NumericalConstants.h>

#include "AnimSkeleton.h"
#include "AnimationLogging.h"

using namespace springbones;

static const glm::quat IDENTITY_QUAT(1.0f, 0.0f, 0.0f, 0.0f);

bool Solver::_lockstep = false;
float Solver::_pendingTime = 0.0f;
long Solver::_stepCount = 0;

// ---- the data ----

float Curve::evaluate(float t) const {
    if (keys.empty()) {
        return 1.0f;
    }
    if (t <= keys.front()[0]) {
        return keys.front()[1];
    }
    if (t >= keys.back()[0]) {
        return keys.back()[1];
    }
    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto& a = keys[i];
        const auto& b = keys[i + 1];
        if (a[0] <= t && t <= b[0]) {
            float dt = b[0] - a[0];
            if (dt <= 0.0f) {
                return b[1];
            }
            float u = (t - a[0]) / dt;
            float m0 = a[3] * dt;   // the out-slope of the key before
            float m1 = b[2] * dt;   // the in-slope of the key after
            float u2 = u * u;
            float u3 = u2 * u;
            float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
            float h10 = u3 - 2.0f * u2 + u;
            float h01 = -2.0f * u3 + 3.0f * u2;
            float h11 = u3 - u2;
            return h00 * a[1] + h10 * m0 + h01 * b[1] + h11 * m1;
        }
    }
    return keys.back()[1];
}

float ComponentSpec::curved(const QString& field, float base, float t) const {
    auto it = curves.find(field);
    if (it == curves.end() || it->second.empty()) {
        return base;
    }
    return base * it->second.evaluate(t);
}

static glm::vec3 vec3Of(const QJsonValue& value, const glm::vec3& fallback = glm::vec3(0.0f)) {
    auto array = value.toArray();
    if (array.size() < 3) {
        return fallback;
    }
    return glm::vec3((float)array[0].toDouble(), (float)array[1].toDouble(), (float)array[2].toDouble());
}

static glm::quat quatOf(const QJsonValue& value) {
    auto array = value.toArray();
    if (array.size() < 4) {
        return IDENTITY_QUAT;
    }
    // (x, y, z, w) in the file
    glm::quat q((float)array[3].toDouble(), (float)array[0].toDouble(), (float)array[1].toDouble(), (float)array[2].toDouble());
    float length = glm::length(q);
    return length > 0.0f ? q / length : IDENTITY_QUAT;
}

Data Data::fromJson(const QJsonObject& json) {
    Data data;
    data.version = json["version"].toInt();
    for (const auto& value : json["colliders"].toArray()) {
        auto o = value.toObject();
        ColliderSpec c;
        c.joint = o["joint"].toString();
        c.shape = (Shape)o["shapeType"].toInt();
        c.radius = (float)o["radius"].toDouble();
        c.height = (float)o["height"].toDouble();
        c.position = vec3Of(o["position"]);
        c.rotation = quatOf(o["rotation"]);
        c.insideBounds = o["insideBounds"].toBool();
        c.bonesAsSpheres = o["bonesAsSpheres"].toBool();
        data.colliders.push_back(c);
    }
    for (const auto& value : json["bones"].toArray()) {
        auto o = value.toObject();
        ComponentSpec s;
        s.root = o["root"].toString();
        for (const auto& j : o["joints"].toArray()) {
            s.joints.append(j.toString());
        }
        s.version = o["version"].toInt(1);
        s.integrationType = o["integrationType"].toInt();
        s.pull = (float)o["pull"].toDouble(0.2);
        s.spring = (float)o["spring"].toDouble(0.2);
        s.stiffness = (float)o["stiffness"].toDouble(0.2);
        s.gravity = (float)o["gravity"].toDouble();
        s.gravityFalloff = (float)o["gravityFalloff"].toDouble();
        s.immobile = (float)o["immobile"].toDouble();
        s.immobileType = (ImmobileType)o["immobileType"].toInt();
        s.limitType = (LimitType)o["limitType"].toInt();
        s.maxAngleX = (float)o["maxAngleX"].toDouble(45.0);
        s.maxAngleZ = (float)o["maxAngleZ"].toDouble(45.0);
        s.limitRotation = quatOf(o["limitRotationQuat"]);
        s.radius = (float)o["radius"].toDouble();
        for (const auto& c : o["colliders"].toArray()) {
            s.colliders.push_back(c.toInt());
        }
        s.allowCollision = o["allowCollision"].toInt(1) != 0;
        s.endpoint = vec3Of(o["endpoint"]);
        s.multiChildType = (MultiChildType)o["multiChildType"].toInt();
        s.isAnimated = o["isAnimated"].toInt() != 0;
        s.maxStretch = (float)o["maxStretch"].toDouble();
        s.maxSquish = (float)o["maxSquish"].toDouble();
        s.stretchMotion = (float)o["stretchMotion"].toDouble();
        s.enabled = o.contains("enabled") ? o["enabled"].toInt(1) != 0 : true;
        auto curves = o["curves"].toObject();
        for (const auto& field : curves.keys()) {
            Curve curve;
            for (const auto& key : curves[field].toArray()) {
                auto k = key.toArray();
                if (k.size() < 4) {
                    continue;
                }
                std::array<float, 7> values { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                for (int i = 0; i < std::min(7, (int)k.size()); i++) {
                    values[i] = (float)k[i].toDouble();
                }
                curve.keys.push_back(values);
            }
            std::sort(curve.keys.begin(), curve.keys.end(), [](const std::array<float, 7>& a, const std::array<float, 7>& b) { return a[0] < b[0]; });
            s.curves[field] = curve;
        }
        data.components.push_back(s);
    }
    return data;
}

// ---- the law: the fitted points until the sweep is read ----

static float interpolateTable(const std::vector<std::pair<float, float>>& table, float x) {
    if (x <= table.front().first) {
        return table.front().second;
    }
    if (x >= table.back().first) {
        return table.back().second;
    }
    for (size_t i = 0; i + 1 < table.size(); i++) {
        if (table[i].first <= x && x <= table[i + 1].first) {
            float u = (x - table[i].first) / (table[i + 1].first - table[i].first);
            return table[i].second + u * (table[i + 1].second - table[i].second);
        }
    }
    return table.back().second;
}

float Law::velocityKept(const ComponentSpec& spec, float spring) {
    // from the one-particle chains' free returns at the step rate (research/spring-bones/decayfit.py, 2026-10-15):
    // spring 0.2 keeps 0.237 of the velocity per step, 0.24 keeps 0.25, 0.6 keeps 0.41, 0.7 about 0.5
    // (the whole-recording fits of the cap ears, fitlaw.py, read 0.25 at spring 0.2)
    static const std::vector<std::pair<float, float>> TABLE = { { 0.0f, 0.15f }, { 0.2f, 0.247f }, { 0.24f, 0.26f }, { 0.6f, 0.41f }, { 0.7f, 0.5f }, { 1.0f, 0.8f } };
    return interpolateTable(TABLE, glm::clamp(spring, 0.0f, 1.0f));
}

float Law::pull(const ComponentSpec& spec, float pull) {
    // the same fits: pull 0.2 closes 0.237 of the gap per step, 0.24 closes 0.255
    // (the whole-recording fits read 0.243 at pull 0.2)
    static const std::vector<std::pair<float, float>> TABLE = { { 0.0f, 0.0f }, { 0.2f, 0.244f }, { 0.24f, 0.26f }, { 1.0f, 1.0f } };
    return interpolateTable(TABLE, glm::clamp(pull, 0.0f, 1.0f));
}

float Law::rigidCarried(const ComponentSpec& spec) {
    // fitted over whole recordings of the test avatar's two cap ears (research/spring-bones/fitlaw.py, 2026-10-15):
    // 0.343 to 0.365 on both, at immobile 0.9 and at immobile 0 alike. The components carry a stiffness of 0.2, and
    // the stiffness applied twice per state shown, 1 - (1 - 0.2)^2, is 0.36: the reading taken until the sweep's
    // stiffness runs confirm or deny it.
    float s = glm::clamp(spec.stiffness, 0.0f, 1.0f);
    return 1.0f - (1.0f - s) * (1.0f - s);
}

float Law::avatarMoveFelt(const ComponentSpec& spec, float immobile) {
    // the same fits: the avatar's own movement is carried by the rigid share plus the immobile share of the rest
    // (0.942 at immobile 0.9, 0.428 at immobile 0), so what is felt is (1 - rigid)(1 - immobile)
    return (1.0f - rigidCarried(spec)) * glm::clamp(1.0f - immobile, 0.0f, 1.0f);
}

// ---- colliders ----

bool Collider::pushOut(const glm::vec3& from, glm::vec3& point, float radius, float scale) const {
    if (!spec) {
        return false;
    }
    float r = spec->radius * scale;
    switch (spec->shape) {
        case Shape::Sphere: {
            glm::vec3 d = point - position;
            float distance = glm::length(d);
            float wanted = r + radius;
            if (spec->insideBounds) {
                float inner = r - radius;
                if (distance > inner && distance > EPSILON) {
                    point = position + d * (inner / distance);
                    return true;
                }
                return false;
            }
            if (distance < wanted) {
                if (distance < EPSILON) {
                    d = glm::normalize(point - from);
                    if (glm::any(glm::isnan(d))) {
                        d = Vectors::UP;
                    }
                    distance = 1.0f;
                }
                point = position + d * (wanted / distance);
                return true;
            }
            return false;
        }
        case Shape::Capsule: {
            float half = glm::max(0.0f, spec->height * scale * 0.5f - r);
            glm::vec3 axis = rotation * Vectors::UP;
            glm::vec3 a = position - axis * half;
            glm::vec3 b = position + axis * half;
            glm::vec3 ab = b - a;
            float t = glm::dot(point - a, ab) / glm::max(glm::dot(ab, ab), EPSILON);
            t = glm::clamp(t, 0.0f, 1.0f);
            glm::vec3 closest = a + ab * t;
            glm::vec3 d = point - closest;
            float distance = glm::length(d);
            float wanted = r + radius;
            if (spec->insideBounds) {
                float inner = r - radius;
                if (distance > inner && distance > EPSILON) {
                    point = closest + d * (inner / distance);
                    return true;
                }
                return false;
            }
            if (distance < wanted) {
                if (distance < EPSILON) {
                    d = glm::cross(axis, point - from);
                    if (glm::length(d) < EPSILON) {
                        d = glm::cross(axis, fabsf(axis.y) < 0.9f ? Vectors::UP : Vectors::RIGHT);
                    }
                    d = glm::normalize(d);
                    distance = 1.0f;
                }
                point = closest + d * (wanted / distance);
                return true;
            }
            return false;
        }
        case Shape::Plane: {
            // a disc of the shape's radius through its position, its normal the shape's up; a particle over the disc
            // stays on the up side. (The test avatar's chest has one such disc whose up points down, 9 cm aside of the
            // hair root it names; the recording leaves that root above it, so the disc is finite, not a whole plane.)
            glm::vec3 normal = rotation * Vectors::UP;
            glm::vec3 d = point - position;
            float height = glm::dot(d, normal);
            glm::vec3 across = d - normal * height;
            if (glm::length(across) > r) {
                return false;
            }
            if (height < radius) {
                point += normal * (radius - height);
                return true;
            }
            return false;
        }
    }
    return false;
}

// ---- the solver ----

// SPRING_BONES_TRACE=<root joint> logs one chain's first particle every step
static bool traceChain(const ComponentSpec& spec) {
    static const QString TRACE = QString::fromLocal8Bit(qgetenv("SPRING_BONES_TRACE"));
    return !TRACE.isEmpty() && spec.root == TRACE;
}

glm::vec3 Solver::jointPosition(const AnimPoseVec& absolutePoses, int joint) const {
    if (joint < 0 || joint >= (int)absolutePoses.size()) {
        return glm::vec3(0.0f);
    }
    return _entityRotation * absolutePoses[joint].trans();
}

glm::quat Solver::jointRotation(const AnimPoseVec& absolutePoses, int joint) const {
    if (joint < 0 || joint >= (int)absolutePoses.size()) {
        return _entityRotation;
    }
    return _entityRotation * absolutePoses[joint].rot();
}

bool Solver::init(const Data& data, const std::shared_ptr<AnimSkeleton>& skeleton, const AnimPoseVec& relativePoses,
                  const AnimPoseVec& absolutePoses, float lengthScale) {
    cleanUp();
    if (!skeleton || data.empty()) {
        return false;
    }
    _data = data;
    _skeleton = skeleton;
    _lengthScale = lengthScale > 0.0f ? lengthScale : 1.0f;
    _jointCount = skeleton->getNumJoints();
    const AnimPoseVec& absolute = absolutePoses.size() == (size_t)_jointCount ? absolutePoses : skeleton->getAbsoluteDefaultPoses();

    for (const auto& c : _data.colliders) {
        Collider collider;
        collider.spec = &c;
        collider.joint = skeleton->nameToJointIndex(c.joint);
        if (collider.joint < 0) {
            qCWarning(animation) << "spring bones: collider joint" << c.joint << "is not in the skeleton";
        }
        _colliders.push_back(collider);
    }

    int built = 0;
    for (const auto& spec : _data.components) {
        int rootJoint = skeleton->nameToJointIndex(spec.root);
        if (rootJoint < 0) {
            qCWarning(animation) << "spring bones: root" << spec.root << "is not in the skeleton";
            continue;
        }
        // the members, parents before children (by depth in the skeleton)
        std::vector<int> members;
        for (const auto& name : spec.joints) {
            int joint = skeleton->nameToJointIndex(name);
            if (joint < 0) {
                qCWarning(animation) << "spring bones: joint" << name << "of" << spec.root << "is not in the skeleton";
                continue;
            }
            members.push_back(joint);
        }
        if (std::find(members.begin(), members.end(), rootJoint) == members.end()) {
            members.insert(members.begin(), rootJoint);
        }
        auto depthOf = [&](int joint) {
            int depth = 0;
            for (int j = skeleton->getParentIndex(joint); j >= 0; j = skeleton->getParentIndex(j)) {
                depth++;
            }
            return depth;
        };
        std::stable_sort(members.begin(), members.end(), [&](int a, int b) { return depthOf(a) < depthOf(b); });

        Chain chain;
        chain.spec = &spec;
        chain.rootJoint = rootJoint;
        chain.rootParentJoint = skeleton->getParentIndex(rootJoint);
        std::map<int, int> particleOfJoint;
        for (int joint : members) {
            Particle p;
            p.joint = joint;
            // the nearest member ancestor is the parent particle
            for (int j = skeleton->getParentIndex(joint); j >= 0; j = skeleton->getParentIndex(j)) {
                auto it = particleOfJoint.find(j);
                if (it != particleOfJoint.end()) {
                    p.parent = it->second;
                    break;
                }
            }
            if (p.parent < 0 && joint != rootJoint) {
                // a member above the root, or detached: not part of this chain
                continue;
            }
            if (p.parent >= 0) {
                const Particle& parent = chain.particles[p.parent];
                p.depth = parent.depth + 1;
                const AnimPose& parentAbs = absolute[parent.joint];
                const AnimPose& ownAbs = absolute[joint];
                p.restLocal = glm::inverse(parentAbs.rot()) * (ownAbs.trans() - parentAbs.trans());
                p.length = glm::length(p.restLocal);
            }
            int index = (int)chain.particles.size();
            particleOfJoint[joint] = index;
            if (p.parent >= 0) {
                chain.particles[p.parent].children.push_back(index);
            }
            chain.particles.push_back(p);
        }
        if (chain.particles.empty()) {
            continue;
        }
        // a virtual bone past every leaf
        if (glm::length(spec.endpoint) > 0.0f) {
            int count = (int)chain.particles.size();
            for (int i = 0; i < count; i++) {
                if (!chain.particles[i].children.empty()) {
                    continue;
                }
                Particle end;
                end.parent = i;
                end.depth = chain.particles[i].depth + 1;
                const AnimPose& leafAbs = absolute[chain.particles[i].joint];
                float leafScale = (leafAbs.scale().x + leafAbs.scale().y + leafAbs.scale().z) / 3.0f;
                end.restLocal = spec.endpoint * _lengthScale * (leafScale > 0.0f ? leafScale : 1.0f);
                end.length = glm::length(end.restLocal);
                int index = (int)chain.particles.size();
                chain.particles[i].children.push_back(index);
                chain.particles.push_back(end);
            }
        }
        int maxDepth = 0;
        for (const auto& p : chain.particles) {
            maxDepth = std::max(maxDepth, p.depth);
        }
        for (auto& p : chain.particles) {
            float t = maxDepth > 0 ? (float)p.depth / (float)maxDepth : 0.0f;
            p.pull = spec.curved("pull", spec.pull, t);
            p.spring = spec.curved("spring", spec.spring, t);
            p.stiffness = spec.curved("stiffness", spec.stiffness, t);
            p.gravity = spec.curved("gravity", spec.gravity, t);
            p.gravityFalloff = spec.curved("gravityFalloff", spec.gravityFalloff, t);
            p.immobile = spec.curved("immobile", spec.immobile, t);
            p.radius = spec.curved("radius", spec.radius, t);
            p.maxAngleX = spec.curved("maxAngleX", spec.maxAngleX, t);
            p.maxAngleZ = spec.curved("maxAngleZ", spec.maxAngleZ, t);
        }
        _chains.push_back(chain);
        built++;
    }
    _initialized = built > 0;
    _active = _initialized;
    _accumulator = 0.0f;
    _hasLastEntity = false;
    qCDebug(animation) << "spring bones: " << built << "chains of" << _data.components.size() << "components," << _colliders.size() << "colliders";
    return _initialized;
}

void Solver::cleanUp() {
    _chains.clear();
    _colliders.clear();
    _data = Data();
    _skeleton.reset();
    _initialized = false;
    _active = false;
    _hasLastEntity = false;
    _accumulator = 0.0f;
    _avatarMoveSinceStep = glm::vec3(0.0f);
}

void Solver::setTransform(float scale, const glm::vec3& position, const glm::quat& rotation) {
    _scale = scale;
    _entityPosition = position;
    _entityRotation = rotation;
}

void Solver::readAnimation(Chain& chain, const AnimPoseVec& relativePoses, const AnimPoseVec& absolutePoses) {
    for (auto& p : chain.particles) {
        if (p.joint < 0) {
            continue;
        }
        if (p.joint < (int)relativePoses.size()) {
            p.animRotation = relativePoses[p.joint].rot();
        }
        p.animPosition = jointPosition(absolutePoses, p.joint);
    }
}

void Solver::updateColliders(const AnimPoseVec& absolutePoses) {
    for (auto& c : _colliders) {
        if (c.joint < 0) {
            continue;
        }
        glm::quat rotation = jointRotation(absolutePoses, c.joint);
        c.rotation = rotation * c.spec->rotation;
        c.position = jointPosition(absolutePoses, c.joint) + rotation * (c.spec->position * _lengthScale * _scale);
    }
}

// The chain at rest under this frame's animation: every bone along its rest direction.
void Solver::resetChain(Chain& chain) {
    const glm::quat parentRotation = chain.lastParentRotation;
    for (size_t i = 0; i < chain.particles.size(); i++) {
        Particle& p = chain.particles[i];
        if (p.parent < 0) {
            p.position = p.previousPosition = p.presented = p.animPosition;
            p.worldRotation = parentRotation * p.animRotation;
        } else {
            const Particle& parent = chain.particles[p.parent];
            p.position = p.previousPosition = p.presented = parent.position + parent.worldRotation * p.restLocal;
            p.worldRotation = parent.worldRotation * p.animRotation;
        }
        p.localRotation = p.previousLocalRotation = p.presentedLocalRotation = p.animRotation;
        p.localPosition = p.previousLocalPosition = glm::inverse(parentRotation) * (p.position - chain.lastParentPosition);
    }
}

void Solver::stepChain(Chain& chain, const glm::vec3& avatarMove, const glm::vec3& lastParentPosition, const glm::quat& lastParentRotation,
                       const glm::vec3& parentPosition, const glm::quat& parentRotation) {
    const ComponentSpec& spec = *chain.spec;
    auto& parts = chain.particles;
    if (parts.empty()) {
        return;
    }
    const glm::quat inverseParentRotation = glm::inverse(parentRotation);
    const glm::quat frameTurn = parentRotation * glm::inverse(lastParentRotation);
    const float rigidCarried = Law::rigidCarried(spec);
    const bool trace = traceChain(spec);

    // the joints' rest rotations under this step's animation: the animated local rotations composed down from the
    // chain's parent (the components reset their bones to the rest before each step)
    std::vector<glm::quat> restWorld(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
        Particle& p = parts[i];
        if (p.parent < 0) {
            restWorld[i] = parentRotation * p.animRotation;
        } else if (p.joint >= 0) {
            restWorld[i] = restWorld[p.parent] * p.animRotation;
        }
    }

    // the root follows its animation; a particle is carried by a fraction of the rigid motion it would have had
    // attached to the root (the root's translation and the parent's turn about it), the rest of that motion being
    // what the rest turns away by and the pull brings it round from
    Particle& root = parts[0];
    root.previousPosition = root.position;
    root.position = root.animPosition;

    for (size_t i = 1; i < parts.size(); i++) {
        Particle& p = parts[i];
        const Particle& parent = parts[p.parent];
        // the velocity: the particle's displacement in the parent's frame over the last step, in that frame now; the
        // position carried by the parent's translation, less the part of the avatar's own movement the chain feels
        glm::vec3 velocity = parentRotation * (p.localPosition - p.previousLocalPosition);
        glm::vec3 rigid = root.position + frameTurn * (p.position - root.previousPosition);
        glm::vec3 carried = p.position + (rigid - p.position) * rigidCarried - avatarMove * Law::avatarMoveFelt(spec, p.immobile);

        // the pull toward the rest: the bone's animated rest direction (the rest rotations composed down from the
        // chain's parent, as the components do), anchored at the parent particle where it is now; the gap is measured
        // from the carried position, before the velocity is added
        int parentIndex = p.parent;
        glm::vec3 restDirection = glm::normalize(restWorld[parentIndex] * p.restLocal);
        glm::vec3 rest = parent.position + restWorld[parentIndex] * p.restLocal;
        glm::vec3 next = carried + velocity * Law::velocityKept(spec, p.spring) + (rest - carried) * Law::pull(spec, p.pull);
        if (trace && i == 1) {
            qCDebug(animation) << "spring trace" << spec.root << "avatarMove" << avatarMove << "rigid" << rigid << "velocity" << velocity
                               << "position" << p.position << "rest" << rest << "next" << next;
        }

        // the limit: a cone about the rest direction turned by the limit's rotation
        glm::vec3 direction = next - parent.position;
        float distance = glm::length(direction);
        direction = distance > EPSILON ? direction / distance : restDirection;
        if (spec.limitType != LimitType::None) {
            glm::vec3 axis = glm::normalize(restWorld[parentIndex] * (spec.limitRotation * glm::normalize(p.restLocal)));
            float maxAngle = glm::radians(glm::clamp(p.maxAngleX, 0.0f, 180.0f));
            float cosAngle = glm::clamp(glm::dot(direction, axis), -1.0f, 1.0f);
            if (cosAngle < cosf(maxAngle)) {
                glm::vec3 hinge = glm::cross(axis, direction);
                float hingeLength = glm::length(hinge);
                if (hingeLength < EPSILON) {
                    hinge = glm::cross(axis, fabsf(axis.y) < 0.9f ? Vectors::UP : Vectors::RIGHT);
                    hingeLength = glm::length(hinge);
                }
                direction = glm::angleAxis(maxAngle, hinge / hingeLength) * axis;
            }
        }
        next = parent.position + direction * p.length;

        // the colliders of this component
        bool collided = false;
        for (int index : spec.colliders) {
            if (index >= 0 && index < (int)_colliders.size() && _colliders[index].joint >= 0) {
                collided |= _colliders[index].pushOut(parent.position, next, p.radius * _lengthScale * _scale, _lengthScale * _scale);
            }
        }
        if (collided) {
            direction = next - parent.position;
            distance = glm::length(direction);
            direction = distance > EPSILON ? direction / distance : restDirection;
            next = parent.position + direction * p.length;
        }
        p.previousPosition = p.position;
        p.position = next;
        p.previousLocalPosition = p.localPosition;
        p.localPosition = inverseParentRotation * (next - parentPosition);
    }

    // the rotations, root to tip: a joint turns so that its bone points at its child particle
    for (size_t i = 0; i < parts.size(); i++) {
        Particle& p = parts[i];
        if (p.joint < 0) {
            continue;
        }
        p.previousLocalRotation = p.localRotation;
        glm::quat grandparent = p.parent < 0 ? parentRotation : parts[p.parent].worldRotation;
        glm::quat restRotation = grandparent * p.animRotation;
        bool turns = !p.children.empty() && !(p.parent < 0 && spec.multiChildType == MultiChildType::Ignore && p.children.size() > 1);
        if (!turns) {
            p.worldRotation = restRotation;
            p.localRotation = p.animRotation;
            continue;
        }
        // the first child decides (the average of several is to come)
        const Particle& child = parts[p.children[0]];
        glm::vec3 restDirection = glm::normalize(restRotation * child.restLocal);
        glm::vec3 direction = child.position - p.position;
        float distance = glm::length(direction);
        direction = distance > EPSILON ? direction / distance : restDirection;
        p.worldRotation = rotationBetween(restDirection, direction) * restRotation;
        p.localRotation = glm::inverse(grandparent) * p.worldRotation;
    }
}

void Solver::update(float deltaTime, AnimPoseVec& relativePoses, AnimPoseVec& absolutePoses, const std::vector<bool>& overrideFlags) {
    if (!(_initialized && _active) || relativePoses.size() != (size_t)_jointCount || absolutePoses.size() != (size_t)_jointCount) {
        return;
    }
    updateColliders(absolutePoses);

    // the avatar's own movement since the last frame; a teleport restarts the chains
    glm::vec3 avatarMove(0.0f);
    bool teleported = false;
    if (_hasLastEntity) {
        avatarMove = _entityPosition - _lastEntityPosition;
        if (glm::length(avatarMove) > TELEPORT_DISTANCE) {
            avatarMove = glm::vec3(0.0f);
            teleported = true;
        }
    }
    _lastEntityPosition = _entityPosition;
    _hasLastEntity = true;
    _avatarMoveSinceStep += avatarMove;

    for (auto& chain : _chains) {
        readAnimation(chain, relativePoses, absolutePoses);
    }

    if (_lockstep) {
        deltaTime = _pendingTime;
        _pendingTime = 0.0f;
    }
    float frameTime = glm::clamp(deltaTime, 0.0f, MAX_STEPS_PER_FRAME * STEP);
    _accumulator += frameTime;
    int steps = (int)(_accumulator / STEP);
    if (steps > MAX_STEPS_PER_FRAME) {
        steps = MAX_STEPS_PER_FRAME;
        _accumulator = STEP * MAX_STEPS_PER_FRAME;
    }
    _accumulator -= steps * STEP;
    _stepCount += steps;
    glm::vec3 movePerStep = steps > 0 ? _avatarMoveSinceStep / (float)steps : glm::vec3(0.0f);

    for (auto& chain : _chains) {
        if (!chain.spec->enabled || chain.particles.empty()) {
            continue;
        }
        const glm::vec3 parentPositionNow = jointPosition(absolutePoses, chain.rootParentJoint);
        const glm::quat parentRotationNow = jointRotation(absolutePoses, chain.rootParentJoint);
        bool restart = teleported || !chain.hasLast || glm::distance(chain.lastParentPosition, parentPositionNow) > TELEPORT_DISTANCE;
        if (restart) {
            chain.lastParentPosition = parentPositionNow;
            chain.lastParentRotation = parentRotationNow;
            resetChain(chain);
            chain.hasLast = true;
            continue;
        }
        // the root's parent glides from where it was at the last step to where it is now across this frame's steps
        const glm::vec3 framePosition = chain.lastParentPosition;
        const glm::quat frameRotation = chain.lastParentRotation;
        const glm::vec3 rootOffset = glm::inverse(parentRotationNow) * (chain.particles[0].animPosition - parentPositionNow);
        for (int s = 1; s <= steps; s++) {
            float alpha = (float)s / (float)steps;
            glm::vec3 stepPosition = glm::mix(framePosition, parentPositionNow, alpha);
            glm::quat stepRotation = safeMix(frameRotation, parentRotationNow, alpha);
            chain.particles[0].animPosition = stepPosition + stepRotation * rootOffset;
            stepChain(chain, movePerStep, chain.lastParentPosition, chain.lastParentRotation, stepPosition, stepRotation);
            chain.lastParentPosition = stepPosition;
            chain.lastParentRotation = stepRotation;
        }
        if (steps > 0) {
            _avatarMoveSinceStep = glm::vec3(0.0f);
        }
    }

    // the presentation: between the last two steps, half way at the step's own frame and the whole way one frame of
    // 60 Hz later (the components' recordings show a new state half at its frame and whole at the next)
    float alpha = glm::clamp(0.5f + _accumulator / STEP, 0.0f, 1.0f);
    for (auto& chain : _chains) {
        for (auto& p : chain.particles) {
            p.presented = glm::mix(p.previousPosition, p.position, alpha);
            if (p.joint >= 0) {
                p.presentedLocalRotation = safeMix(p.previousLocalRotation, p.localRotation, alpha);
            }
        }
        for (const auto& p : chain.particles) {
            if (p.joint < 0 || p.joint >= (int)relativePoses.size() || overrideFlags[p.joint]) {
                continue;
            }
            relativePoses[p.joint].rot() = p.presentedLocalRotation;
            int parent = _skeleton->getParentIndex(p.joint);
            if (parent >= 0) {
                absolutePoses[p.joint] = absolutePoses[parent] * relativePoses[p.joint];
            }
        }
    }
}

void Solver::copyJointRotations(const AnimPoseVec& fromRelativePoses, AnimPoseVec& toRelativePoses) const {
    if (!(_initialized && _active)) {
        return;
    }
    int count = (int)std::min(fromRelativePoses.size(), toRelativePoses.size());
    for (const auto& chain : _chains) {
        for (const auto& p : chain.particles) {
            if (p.joint >= 0 && p.joint < count) {
                toRelativePoses[p.joint].rot() = fromRelativePoses[p.joint].rot();
            }
        }
    }
}
