//
//  RigPreview.h
//  tools/auto-rig/src
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_RigPreview_h
#define hifi_RigPreview_h

#include <vector>

#include <QString>

#include <hfm/HFM.h>

#include <AutoRig.h>

// Draws the rigged character from the front and the side, at rest and posed by one frame of an animation, into a PNG.
// The animation is put on the skeleton the way the client does it (AnimClip.cpp): each joint takes the animation's
// absolute rotation, so wrong joint axes show up as twisted limbs. No animation: the rest pose only.
// The posed views use dual quaternion skinning, as the client does for avatars (SkeletonModel.cpp). With heat (one value
// per vertex, 0 to 1) the first panel colours the rest pose by it, and with keyHeat the second panel shows the
// answer key's, for comparing one joint's weights.
bool renderPreview(const autorig::Body& body, const autorig::Result& rig, const HFMModel* animation, int frame,
                   const QString& pngPath, const QString& focusJoint = QString(), float zoom = 1.0f,
                   const std::vector<float>* heat = nullptr, const std::vector<float>* keyHeat = nullptr,
                   bool sideHeat = false);

#endif // hifi_RigPreview_h
