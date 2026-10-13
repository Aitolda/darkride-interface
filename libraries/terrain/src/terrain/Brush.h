//
//  Brush.h
//  libraries/terrain/src/terrain
//
//  The sculpt brushes: a sphere in voxel space with a falloff, applied to a grid one step at a time (the tool applies
//  a step per frame while the mouse is down). Raise and Lower grow and shrink the ground under the brush, Dig and
//  Fill cut the brush's sphere out of the ground or add it, Flatten pulls the surface toward a height, Smooth blurs
//  it, PaintVoxels sets the material of the ground voxels. Paint, the mode that paints the paint map from above,
//  is listed with them because the tool offers them as one set; it is applied with paint() (PaintMap.h), not
//  sculpt(). Scripts get the same operations (Terrain.dig in TERRAIN.md).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_terrain_Brush_h
#define hifi_terrain_Brush_h

#include <cstdint>

#include <glm/glm.hpp>

#include "VoxelGrid.h"

namespace terrain {

enum class SculptMode {
    Raise,
    Lower,
    Smooth,
    Flatten,
    Dig,
    Fill,
    Paint,          // the paint map: PaintMap.h
    PaintVoxels     // the voxels' materials
};

struct Brush {
    glm::vec3 center { 0.0f };      // voxel coordinates
    float radius { 4.0f };          // voxels
    float strength { 1.0f };        // 0 to 1: how much of the full effect one step applies
    float hardness { 0.5f };        // 0 to 1: the part of the radius with full effect before the falloff
    uint8_t material { 0 };         // the material ground made by Raise and Fill gets, and PaintVoxels paints
    float flattenHeight { 0.0f };   // the voxel height Flatten pulls the surface toward

    // 1 at the centre, falling smoothly to 0 at the radius.
    float weightAt(const glm::vec3& point) const;
};

// Applies one step of the brush to the grid; returns how many voxels changed. Paint changes no voxel: it is for
// paint() on the terrain's map.
int sculpt(VoxelGrid& grid, const Brush& brush, SculptMode mode);

} // namespace terrain

#endif // hifi_terrain_Brush_h
