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

#include <QString>

#include <hfm/HFM.h>

#include <AutoRig.h>

// Draws the rigged character from the front and the side, at rest and posed by one frame of an animation, into a PNG.
// The animation is put on the skeleton the way the client does it (AnimClip.cpp): each joint takes the animation's
// absolute rotation, so wrong joint axes show up as twisted limbs. No animation: the rest pose only.
bool renderPreview(const autorig::Body& body, const autorig::Result& rig, const HFMModel* animation, int frame,
                   const QString& pngPath);

#endif // hifi_RigPreview_h
