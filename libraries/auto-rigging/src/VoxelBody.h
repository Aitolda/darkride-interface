//
//  VoxelBody.h
//  libraries/auto-rigging/src
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_VoxelBody_h
#define hifi_VoxelBody_h

#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "AutoRig.h"

namespace autorig {

// A character as a solid grid of voxels: which voxels are inside the body, and how deep each lies below the surface.
class VoxelBody {
public:
    VoxelBody(const Body& body, float voxelSize);

    float voxelSize() const { return _size; }
    const glm::ivec3& dimensions() const { return _dimensions; }
    size_t count() const { return _inside.size(); }
    size_t insideCount() const { return _insideCount; }

    bool inside(size_t index) const { return _inside[index] != 0; }
    float depth(size_t index) const { return _depth[index]; }  // metres to the nearest outside voxel
    glm::vec3 center(size_t index) const;
    long find(const glm::vec3& point) const;  // the voxel holding a point, or -1 off the grid
    long nearestInside(const glm::vec3& point, int radius) const;

    // How far the body reaches through a point along a direction, both ways (metres).
    float thickness(const glm::vec3& point, const glm::vec3& direction) const;
    // How far the body reaches from a point one way (metres).
    float reach(const glm::vec3& point, const glm::vec3& direction) const;

    struct Paths {
        std::vector<float> distance;
        std::vector<int32_t> previous;
    };
    // Shortest paths through the body from the seeds. Medial paths pay less in deep voxels, so they keep to the
    // middle of the limbs.
    // Paths longer than maxDistance (metres, or the paths' own units when medial) are not followed.
    Paths shortestPaths(const std::vector<size_t>& seeds, bool medial,
                        float maxDistance = std::numeric_limits<float>::infinity()) const;
    std::vector<size_t> trace(const Paths& paths, size_t from) const;

private:
    size_t index(int x, int y, int z) const { return ((size_t)z * _dimensions.y + y) * _dimensions.x + x; }
    glm::ivec3 cell(size_t index) const;

    glm::ivec3 _dimensions;
    glm::vec3 _origin;
    float _size;
    std::vector<uint8_t> _inside;
    std::vector<float> _depth;
    size_t _insideCount { 0 };
};

}

#endif // hifi_VoxelBody_h
