//
//  AutoRig.h
//  libraries/auto-rigging/src
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_AutoRig_h
#define hifi_AutoRig_h

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace hfm {
class Model;
}

namespace autorig {

// The character as one triangle soup in model space (metres, Y up), gathered from every shape of a model.
struct Body {
    // One of the model's meshes as placed by one node: its vertices sit together in Body::vertices.
    struct Piece {
        uint32_t mesh { 0 };
        uint32_t joint { 0 };             // the node that places it
        uint32_t firstVertex { 0 };
        uint32_t vertexCount { 0 };
        glm::mat4 transform;              // the node's transform: mesh space to model space
        std::vector<uint32_t> shapes;     // the model's shapes drawn from this piece (one per part and material)
    };

    std::vector<glm::vec3> vertices;
    std::vector<glm::uvec3> triangles;
    std::vector<Piece> pieces;
    glm::mat4 adjustment;  // scaling and turning applied after gathering (see scale and turnAround)
    glm::vec3 minimum { 0.0f };
    glm::vec3 maximum { 0.0f };

    void measure();
    void scale(float factor);
    void turnAround();  // half a turn about Y, for a character that faces -Z
};

// A joint of the standard skeleton as placed in the character.
struct RiggedJoint {
    QString name;
    int parent { -1 };
    glm::vec3 position { 0.0f };  // metres, in the pose the mesh is in
    glm::quat rotation;           // the joint's absolute axes: Mixamo's, turned to fit the bone
};

// How much each joint moves a vertex: up to four joints, weights summing to one.
struct VertexWeights {
    uint16_t joints[4] { 0, 0, 0, 0 };  // indices into Result::joints
    float weights[4] { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct Options {
    float voxelsPerHeight { 220.0f };
    bool profiles { false };    // also report the measurements along each arm
    bool skin { true };         // compute skin weights as well as the joints
    int weighting { 1 };        // 1: joint planes (crisp, like hand-painted rigs); 0: plain geodesic falloff
    float sharpness { 8.0f };   // joint planes: how sharply the nearest bone (in its own thicknesses) wins
    float blend { 0.6f };       // joint planes: half the width of the change across a joint, in that bone's thickness
    float falloff { 0.2f };     // geodesic falloff: how fast a bone's influence fades with distance
    int smoothing { 4 };        // geodesic falloff: passes of smoothing over the mesh surface
};

struct Result {
    bool ok { false };
    QString error;
    std::vector<RiggedJoint> joints;  // in the standard skeleton's order
    std::vector<VertexWeights> weights;  // with Options::skin: one per vertex of the Body
    QStringList notes;                // what was measured and decided, for the log
    QStringList profiles;             // with Options::profiles: one line per point along each arm
};

Body gatherBody(const hfm::Model& model);

// Rigs a character standing in a T-pose or an A-pose, facing +Z (Body::turnAround first for one facing -Z): places
// the standard skeleton and, with
// Options::skin, computes skin weights by geodesic voxel binding (Dionne and de Lasa 2013).
Result rig(const Body& body, const Options& options = Options());

}

#endif // hifi_AutoRig_h
