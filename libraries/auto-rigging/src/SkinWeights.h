//
//  SkinWeights.h
//  libraries/auto-rigging/src
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_SkinWeights_h
#define hifi_SkinWeights_h

#include "AutoRig.h"
#include "VoxelBody.h"

namespace autorig {

// Geodesic voxel binding (Dionne and de Lasa 2013): each joint's influence falls off with the distance through the
// body's volume from its bones, so it cannot jump a gap such as the one under an arm. Then a few passes of smoothing
// over the mesh, with vertices that share a position (UV seams) welded so the mesh cannot crack. Fills
// result.weights, one entry per body vertex, from the joints already in result.joints.
void computeSkinWeights(const Body& body, const VoxelBody& voxels, const Options& options, Result& result);

}

#endif // hifi_SkinWeights_h
