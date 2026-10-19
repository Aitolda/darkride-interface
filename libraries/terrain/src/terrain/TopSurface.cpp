//
//  TopSurface.cpp
//  libraries/terrain/src/terrain
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TopSurface.h"

#include <algorithm>

namespace terrain {

void TopSurface::rebuild(const VoxelGrid& grid) {
    const glm::ivec3 count = grid.getVoxelCount();
    _footprint = glm::ivec2(count.x, count.z);
    _chunkColumns = glm::ivec2(grid.getChunkCount().x, grid.getChunkCount().z);
    _heights.assign((size_t)std::max(_footprint.x, 0) * (size_t)std::max(_footprint.y, 0), NO_GROUND_HEIGHT);
    for (int z = 0; z < _chunkColumns.y; ++z) {
        for (int x = 0; x < _chunkColumns.x; ++x) {
            findChunkColumn(grid, glm::ivec2(x, z));
        }
    }
    _dirtyColumns.clear();
    _allDirty = true;
}

void TopSurface::update(VoxelGrid& grid) {
    std::vector<std::pair<glm::ivec3, glm::ivec3>> boxes;
    const bool told = grid.takeChanges(boxes, VoxelGrid::ChangeLog::TopSurface);
    if (grid.isEmpty()) {
        if (!_heights.empty()) {
            _heights.clear();
            _footprint = glm::ivec2(0);
            _chunkColumns = glm::ivec2(0);
            _dirtyColumns.clear();
            _allDirty = true;
        }
        return;
    }
    const glm::ivec3 count = grid.getVoxelCount();
    if (!told || _heights.empty() || _footprint != glm::ivec2(count.x, count.z)) {
        rebuild(grid);
        return;
    }
    if (boxes.empty()) {
        return;
    }
    // the chunk columns the edits reached, a voxel wider than the boxes: a crossing between a changed voxel and the
    // next one moves too
    ColumnSet reached;
    const glm::ivec2 lastColumn = _chunkColumns - 1;
    for (const auto& box : boxes) {
        const glm::ivec3 low = (box.first >> MAX_CHUNK_LEVEL) - 1;
        const glm::ivec3 high = ((box.second + FINEST_PER_GRID_VOXEL - 1) >> MAX_CHUNK_LEVEL) + 1;
        const glm::ivec2 columnLow = glm::clamp(PaintMap::columnOf(glm::ivec2(low.x, low.z)), glm::ivec2(0), lastColumn);
        const glm::ivec2 columnHigh = glm::clamp(PaintMap::columnOf(glm::ivec2(high.x, high.z)), glm::ivec2(0), lastColumn);
        for (int z = columnLow.y; z <= columnHigh.y; ++z) {
            for (int x = columnLow.x; x <= columnHigh.x; ++x) {
                reached.insert(glm::ivec2(x, z));
            }
        }
    }
    for (const glm::ivec2& column : reached) {
        findChunkColumn(grid, column);
        // its own tile, and the neighbours' whose aprons hold its edge
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const glm::ivec2 neighbour = column + glm::ivec2(dx, dz);
                if (glm::all(glm::greaterThanEqual(neighbour, glm::ivec2(0))) && glm::all(glm::lessThanEqual(neighbour, lastColumn))) {
                    _dirtyColumns.insert(neighbour);
                }
            }
        }
    }
}

void TopSurface::findChunkColumn(const VoxelGrid& grid, const glm::ivec2& column) {
    const glm::ivec3 count = grid.getVoxelCount();
    const int x0 = column.x * CHUNK_SIZE;
    const int z0 = column.y * CHUNK_SIZE;
    const int x1 = std::min(x0 + CHUNK_SIZE, count.x);   // past the last
    const int z1 = std::min(z0 + CHUNK_SIZE, count.z);
    if (x0 >= x1 || z0 >= z1 || count.y <= 0) {
        return;
    }
    const int width = x1 - x0;
    const int depth = z1 - z0;
    // for each column: the distance of the voxel above the one read next (air above the grid's top), and whether
    // its ground has been found
    std::vector<float> above((size_t)width * depth, SATURATED_DISTANCE);
    std::vector<uint8_t> found((size_t)width * depth, 0);
    int remaining = width * depth;
    // the crossing between the column's ground voxel at y and the air voxel above it
    auto settle = [&](int i, int x, int z, int y, float distance) {
        const float air = above[i];
        const float t = air > distance ? -distance / (air - distance) : 0.0f;
        _heights[(size_t)z * _footprint.x + x] = (float)y + glm::clamp(t, 0.0f, 1.0f);
        found[i] = 1;
        --remaining;
    };
    for (int cy = (count.y - 1) >> CHUNK_SHIFT; cy >= 0 && remaining > 0; --cy) {
        const int y0 = cy * CHUNK_SIZE;
        const int y1 = std::min(y0 + CHUNK_SIZE, count.y) - 1;   // the highest voxel of the chunk in the grid
        // the level-0 chunk's own voxels; a place split into finer chunks is read through the field at the grid's points
        const Chunk* chunk = grid.findChunk(ChunkKey(0, glm::ivec3(column.x, cy, column.y)));
        if (chunk && chunk->isUniform()) {
            const float distance = chunk->getUniformValue().getDistance();
            for (int z = z0; z < z1; ++z) {
                for (int x = x0; x < x1; ++x) {
                    const int i = (z - z0) * width + (x - x0);
                    if (found[i]) {
                        continue;
                    }
                    if (distance < 0.0f) {
                        settle(i, x, z, y1, distance);   // all ground: its top is the crossing
                    } else {
                        above[i] = distance;             // all air: on down
                    }
                }
            }
            continue;
        }
        for (int z = z0; z < z1; ++z) {
            for (int x = x0; x < x1; ++x) {
                const int i = (z - z0) * width + (x - x0);
                if (found[i]) {
                    continue;
                }
                for (int y = y1; y >= y0; --y) {
                    const float distance = chunk ? chunk->get(x - x0, y - y0, z - z0).getDistance()
                                                 : grid.sampleAt(0, glm::ivec3(x, y, z)).getDistance();
                    if (distance < 0.0f) {
                        settle(i, x, z, y, distance);
                        break;
                    }
                    above[i] = distance;
                }
            }
        }
    }
    // columns of air all the way down
    for (int z = z0; z < z1; ++z) {
        for (int x = x0; x < x1; ++x) {
            if (!found[(z - z0) * width + (x - x0)]) {
                _heights[(size_t)z * _footprint.x + x] = NO_GROUND_HEIGHT;
            }
        }
    }
}

float TopSurface::heightAt(const glm::ivec2& column) const {
    if (_heights.empty()) {
        return NO_GROUND_HEIGHT;
    }
    const glm::ivec2 clamped = glm::clamp(column, glm::ivec2(0), _footprint - 1);
    return _heights[(size_t)clamped.y * _footprint.x + clamped.x];
}

float TopSurface::heightAt(const glm::vec2& point) const {
    const glm::vec2 base = glm::floor(point);
    const glm::vec2 f = point - base;
    const glm::ivec2 column(base);
    const float low = glm::mix(heightAt(column), heightAt(column + glm::ivec2(1, 0)), f.x);
    const float high = glm::mix(heightAt(column + glm::ivec2(0, 1)), heightAt(column + glm::ivec2(1, 1)), f.x);
    return glm::mix(low, high, f.y);
}

bool TopSurface::isOnTop(const glm::vec3& point) const {
    return _heights.empty() || point.y >= heightAt(glm::vec2(point.x, point.z)) - TOP_SURFACE_TOLERANCE;
}

void TopSurface::fillTile(const glm::ivec2& column, std::vector<float>& heights) const {
    const int size = getTileSize();
    heights.resize((size_t)size * size);
    const glm::ivec2 origin = column * CHUNK_SIZE - PAINT_TILE_APRON;
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            heights[(size_t)j * size + i] = heightAt(origin + glm::ivec2(i, j));
        }
    }
}

} // namespace terrain
