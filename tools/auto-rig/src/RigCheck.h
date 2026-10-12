//
//  RigCheck.h
//  tools/auto-rig/src
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_RigCheck_h
#define hifi_RigCheck_h

#include <utility>
#include <vector>

#include <QString>

#include <hfm/HFM.h>

#include <AutoRig.h>

// An answer key: a rigged model's own joints and skin weights, over the same vertices as the Body gathered from it.
struct KeyRig {
    std::vector<QString> names;
    std::vector<int> parents;
    std::vector<glm::vec3> positions;  // metres, scaled like the body
    std::vector<int> standard;         // each joint's standard joint: its own name, or its nearest ancestor's (-1 none)
    std::vector<std::vector<std::pair<int, float>>> weights;  // per body vertex: (key joint, weight)
};

KeyRig readKeyRig(const HFMModel& key, float scale);

// How alike the weights are: the mean L1 distance per vertex over the standard joints (0 the same, 2 nothing shared)
// and how often both give a vertex the same main joint.
void compareWeights(const autorig::Result& ours, const KeyRig& key);

// Each test pose turns one joint's part of the body on both rigs; prints how far apart the two meshes end up (cm).
void compareDeformation(const autorig::Body& body, const autorig::Result& ours, const KeyRig& key);

// Reads a written avatar back with the engine's own reader and checks it: every joint where it was placed, with the
// axes it was given, one skeleton root, and the Avatar Doctor's main rules. Returns false if something is wrong.
bool checkWritten(const QString& fbxPath, const autorig::Body& body, const autorig::Result& ours);

#endif // hifi_RigCheck_h
