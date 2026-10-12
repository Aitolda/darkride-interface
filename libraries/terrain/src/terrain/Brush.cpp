//
//  Brush.cpp
//  libraries/terrain/src/terrain
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Brush.h"

#include <vector>

using namespace terrain;

float Brush::weightAt(const glm::vec3& point) const {
    const float distance = glm::length(point - center);
    if (distance >= radius) {
        return 0.0f;
    }
    const float flat = radius * glm::clamp(hardness, 0.0f, 1.0f);
    if (distance <= flat) {
        return 1.0f;
    }
    const float t = (distance - flat) / (radius - flat);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

int terrain::sculpt(VoxelGrid& grid, const Brush& brush, SculptMode mode) {
    if (grid.isEmpty() || brush.radius <= 0.0f) {
        return 0;
    }
    // Dig and Fill set the field to the distance to the sphere's wall, which the ground just outside the sphere
    // needs too, so they reach one voxel further and use no falloff.
    const bool shaping = mode == SculptMode::Dig || mode == SculptMode::Fill;
    const int reach = shaping ? 1 : 0;
    const glm::ivec3 low = glm::max(glm::ivec3(glm::floor(brush.center - brush.radius)) - reach, glm::ivec3(0));
    const glm::ivec3 high = glm::min(glm::ivec3(glm::ceil(brush.center + brush.radius)) + reach, grid.getVoxelCount() - 1);
    if (glm::any(glm::greaterThan(low, high))) {
        return 0;
    }
    const float strength = glm::clamp(brush.strength, 0.0f, 1.0f);

    // Smooth reads neighbours, so it works from a copy of the region taken before any voxel changes.
    std::vector<float> before;
    const glm::ivec3 beforeLow = low - 1;
    const glm::ivec3 beforeSize = high - low + 3;
    if (mode == SculptMode::Smooth) {
        before.resize((size_t)beforeSize.x * beforeSize.y * beforeSize.z);
        for (int z = 0; z < beforeSize.z; ++z) {
            for (int y = 0; y < beforeSize.y; ++y) {
                for (int x = 0; x < beforeSize.x; ++x) {
                    before[(z * beforeSize.y + y) * beforeSize.x + x] = grid.get(beforeLow + glm::ivec3(x, y, z)).getDistance();
                }
            }
        }
    }
    auto beforeAt = [&](const glm::ivec3& voxel) {
        const glm::ivec3 i = voxel - beforeLow;
        return before[(i.z * beforeSize.y + i.y) * beforeSize.x + i.x];
    };

    int changed = 0;
    for (int z = low.z; z <= high.z; ++z) {
        for (int y = low.y; y <= high.y; ++y) {
            for (int x = low.x; x <= high.x; ++x) {
                const glm::ivec3 voxel(x, y, z);
                const glm::vec3 point(voxel);
                const float weight = shaping ? 1.0f : brush.weightAt(point);
                if (weight <= 0.0f) {
                    continue;
                }
                const Voxel old = grid.get(voxel);
                const float distance = old.getDistance();
                Voxel next = old;
                switch (mode) {
                    case SculptMode::Raise:
                        next.setDistance(distance - strength * weight);
                        break;
                    case SculptMode::Lower:
                        next.setDistance(distance + strength * weight);
                        break;
                    case SculptMode::Dig: {
                        // Carve the sphere out: the field becomes the larger of itself and the sphere's inside.
                        const float sphere = brush.radius - glm::length(point - brush.center);
                        next.setDistance(glm::mix(distance, glm::max(distance, sphere), strength));
                        break;
                    }
                    case SculptMode::Fill: {
                        // Add the sphere: the field becomes the smaller of itself and the sphere's outside.
                        const float sphere = glm::length(point - brush.center) - brush.radius;
                        next.setDistance(glm::mix(distance, glm::min(distance, sphere), strength));
                        break;
                    }
                    case SculptMode::Flatten:
                        next.setDistance(glm::mix(distance, (float)y - brush.flattenHeight, strength * weight));
                        break;
                    case SculptMode::Smooth: {
                        float sum = beforeAt(voxel);
                        for (int axis = 0; axis < 3; ++axis) {
                            glm::ivec3 step(0);
                            step[axis] = 1;
                            sum += beforeAt(voxel - step) + beforeAt(voxel + step);
                        }
                        next.setDistance(glm::mix(distance, sum / 7.0f, strength * weight));
                        break;
                    }
                    case SculptMode::Paint:
                        if (old.isGround()) {
                            next.material = brush.material;
                        }
                        break;
                }
                if ((mode == SculptMode::Raise || mode == SculptMode::Fill) && next.isGround() && !old.isGround()) {
                    next.material = brush.material;
                }
                if (next != old && grid.set(voxel, next)) {
                    ++changed;
                }
            }
        }
    }
    return changed;
}
