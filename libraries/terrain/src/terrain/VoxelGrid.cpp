//
//  VoxelGrid.cpp
//  libraries/terrain/src/terrain
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "VoxelGrid.h"

#include <cassert>

using namespace terrain;

namespace {

int floorDivide(int value, int divisor) {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

} // namespace

bool ChunkSamples::isOneSided() const {
    const bool first = _voxels[0].isGround();
    for (const Voxel& voxel : _voxels) {
        if (voxel.isGround() != first) {
            return false;
        }
    }
    return true;
}

VoxelGrid::VoxelGrid(const glm::ivec3& voxelCount, const Voxel& fill) {
    reset(voxelCount, fill);
}

void VoxelGrid::reset(const glm::ivec3& voxelCount, const Voxel& fill) {
    _voxelCount = glm::max(voxelCount, glm::ivec3(0));
    _chunkCount = (_voxelCount + CHUNK_SIZE - 1) / CHUNK_SIZE;
    _chunks.assign((size_t)_chunkCount.x * _chunkCount.y * _chunkCount.z, Chunk(fill));
    markAllChunksDirty();
}

glm::ivec3 VoxelGrid::chunkOf(const glm::ivec3& voxel) {
    return glm::ivec3(floorDivide(voxel.x, CHUNK_SIZE), floorDivide(voxel.y, CHUNK_SIZE), floorDivide(voxel.z, CHUNK_SIZE));
}

glm::ivec3 VoxelGrid::localOf(const glm::ivec3& voxel) {
    return voxel - chunkOf(voxel) * CHUNK_SIZE;
}

bool VoxelGrid::containsVoxel(const glm::ivec3& voxel) const {
    return glm::all(glm::greaterThanEqual(voxel, glm::ivec3(0))) && glm::all(glm::lessThan(voxel, _voxelCount));
}

bool VoxelGrid::containsChunk(const glm::ivec3& chunk) const {
    return glm::all(glm::greaterThanEqual(chunk, glm::ivec3(0))) && glm::all(glm::lessThan(chunk, _chunkCount));
}

const Voxel& VoxelGrid::get(const glm::ivec3& voxel) const {
    static const Voxel NOTHING;
    if (_chunks.empty()) {
        return NOTHING;
    }
    const glm::ivec3 clamped = glm::clamp(voxel, glm::ivec3(0), _voxelCount - 1);
    const glm::ivec3 local = localOf(clamped);
    return _chunks[chunkIndexOf(chunkOf(clamped))].get(local.x, local.y, local.z);
}

bool VoxelGrid::set(const glm::ivec3& voxel, const Voxel& value) {
    if (!containsVoxel(voxel)) {
        return false;
    }
    const glm::ivec3 local = localOf(voxel);
    if (!_chunks[chunkIndexOf(chunkOf(voxel))].set(local.x, local.y, local.z, value)) {
        return false;
    }
    markDirtyAround(voxel);
    return true;
}

const Chunk& VoxelGrid::getChunk(const glm::ivec3& chunk) const {
    assert(containsChunk(chunk));
    return _chunks[chunkIndexOf(chunk)];
}

void VoxelGrid::setChunk(const glm::ivec3& chunk, Chunk value) {
    if (!containsChunk(chunk)) {
        return;
    }
    _chunks[chunkIndexOf(chunk)] = std::move(value);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                markChunkDirty(chunk + glm::ivec3(dx, dy, dz));
            }
        }
    }
}

void VoxelGrid::markChunkDirty(const glm::ivec3& chunk) {
    if (containsChunk(chunk)) {
        _dirtyChunks.insert(chunk);
    }
}

void VoxelGrid::markAllChunksDirty() {
    _dirtyChunks.clear();
    forEachChunk([&](const glm::ivec3& chunk, const Chunk&) { _dirtyChunks.insert(chunk); });
}

void VoxelGrid::markDirtyAround(const glm::ivec3& voxel) {
    // A neighbour samples one layer of this chunk's voxels on each side (ChunkSamples), so a change on a face
    // dirties the chunk across it as well, and a change on an edge or a corner the chunks around those.
    const glm::ivec3 chunk = chunkOf(voxel);
    const glm::ivec3 local = localOf(voxel);
    const glm::ivec3 low = glm::ivec3(glm::equal(local, glm::ivec3(0))) * -1;
    const glm::ivec3 high = glm::ivec3(glm::equal(local, glm::ivec3(CHUNK_SIZE - 1)));
    for (int dz = low.z; dz <= high.z; ++dz) {
        for (int dy = low.y; dy <= high.y; ++dy) {
            for (int dx = low.x; dx <= high.x; ++dx) {
                markChunkDirty(chunk + glm::ivec3(dx, dy, dz));
            }
        }
    }
}

void VoxelGrid::sampleChunk(const glm::ivec3& chunk, ChunkSamples& samples) const {
    samples.setChunk(chunk);
    const glm::ivec3 origin = chunk * CHUNK_SIZE;
    for (int z = -1; z <= CHUNK_SIZE; ++z) {
        for (int y = -1; y <= CHUNK_SIZE; ++y) {
            for (int x = -1; x <= CHUNK_SIZE; ++x) {
                samples.at(x, y, z) = get(origin + glm::ivec3(x, y, z));
            }
        }
    }
}

void VoxelGrid::fillHeightField(const std::function<float(int x, int z)>& height, uint8_t material) {
    std::vector<float> columnHeights(CHUNK_SIZE * CHUNK_SIZE);
    std::vector<Voxel> voxels;
    int index = 0;
    for (int cz = 0; cz < _chunkCount.z; ++cz) {
        for (int cy = 0; cy < _chunkCount.y; ++cy) {
            for (int cx = 0; cx < _chunkCount.x; ++cx) {
                const glm::ivec3 origin = glm::ivec3(cx, cy, cz) * CHUNK_SIZE;
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    for (int x = 0; x < CHUNK_SIZE; ++x) {
                        columnHeights[z * CHUNK_SIZE + x] = height(origin.x + x, origin.z + z);
                    }
                }
                voxels.assign(CHUNK_VOXEL_COUNT, Voxel());
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    for (int y = 0; y < CHUNK_SIZE; ++y) {
                        for (int x = 0; x < CHUNK_SIZE; ++x) {
                            const float distance = (float)(origin.y + y) - columnHeights[z * CHUNK_SIZE + x];
                            voxels[Chunk::indexOf(x, y, z)] = Voxel(encodeDistance(distance), material);
                        }
                    }
                }
                _chunks[index++].setVoxels(voxels);
            }
        }
    }
    markAllChunksDirty();
}

void VoxelGrid::fillBelow(float height, uint8_t material) {
    fillHeightField([height](int, int) { return height; }, material);
}

void VoxelGrid::compactChunks() {
    for (Chunk& chunk : _chunks) {
        chunk.compact();
    }
}

int VoxelGrid::countDenseChunks() const {
    int count = 0;
    for (const Chunk& chunk : _chunks) {
        if (!chunk.isUniform()) {
            ++count;
        }
    }
    return count;
}
