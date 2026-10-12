//
//  VoxelGrid.h
//  libraries/terrain/src/terrain
//
//  The voxels of one Terrain entity: a box of voxelCount voxels cut into chunks (Chunk.h). Voxel (x, y, z) sits at
//  that integer point of voxel space; the entity scales voxel space by its voxel size and places it in the world.
//  The grid tracks which chunks' meshes are out of date and hands the mesher the samples one chunk needs
//  (ChunkSamples), its own voxels and one layer of its neighbours' all around, so neighbouring meshes meet without
//  a seam. Nothing here is thread safe: edit on one thread, sample there, mesh the samples anywhere (AsyncMesher
//  does that on a thread pool).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_terrain_VoxelGrid_h
#define hifi_terrain_VoxelGrid_h

#include <functional>
#include <set>
#include <vector>

#include <glm/glm.hpp>

#include "Chunk.h"

namespace terrain {

struct IVec3Less {
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.z < b.z;
    }
};
using ChunkSet = std::set<glm::ivec3, IVec3Less>;

const int SAMPLE_SIZE = CHUNK_SIZE + 2;

// The voxels one chunk's mesh is built from. Local coordinates run from -1 to CHUNK_SIZE inclusive.
class ChunkSamples {
public:
    ChunkSamples() : _voxels(SAMPLE_SIZE * SAMPLE_SIZE * SAMPLE_SIZE) {}

    static int indexOf(int x, int y, int z) { return ((z + 1) * SAMPLE_SIZE + (y + 1)) * SAMPLE_SIZE + (x + 1); }

    const Voxel& at(int x, int y, int z) const { return _voxels[indexOf(x, y, z)]; }
    Voxel& at(int x, int y, int z) { return _voxels[indexOf(x, y, z)]; }

    // True when every sample is ground or every sample is air: no surface crosses this chunk.
    bool isOneSided() const;

    const glm::ivec3& getChunk() const { return _chunk; }
    void setChunk(const glm::ivec3& chunk) { _chunk = chunk; }

private:
    glm::ivec3 _chunk { 0 };
    std::vector<Voxel> _voxels;
};

class VoxelGrid {
public:
    VoxelGrid() = default;
    VoxelGrid(const glm::ivec3& voxelCount, const Voxel& fill = Voxel());

    void reset(const glm::ivec3& voxelCount, const Voxel& fill = Voxel());

    const glm::ivec3& getVoxelCount() const { return _voxelCount; }
    const glm::ivec3& getChunkCount() const { return _chunkCount; }
    bool isEmpty() const { return _chunks.empty(); }

    // Floor division, so voxel -1 lies in chunk -1.
    static glm::ivec3 chunkOf(const glm::ivec3& voxel);
    static glm::ivec3 localOf(const glm::ivec3& voxel);
    bool containsVoxel(const glm::ivec3& voxel) const;
    bool containsChunk(const glm::ivec3& chunk) const;

    // Outside the grid the nearest voxel in it is returned, so the surface runs open to the grid's sides and
    // the mesher draws no walls or floor there.
    const Voxel& get(const glm::ivec3& voxel) const;
    // Voxels outside the grid are ignored. Returns true when the voxel changed.
    bool set(const glm::ivec3& voxel, const Voxel& value);

    const Chunk& getChunk(const glm::ivec3& chunk) const;
    // Replaces a whole chunk and marks it and its neighbours dirty.
    void setChunk(const glm::ivec3& chunk, Chunk value);

    // The chunks whose meshes are out of date: a changed voxel dirties its chunk and every neighbour that
    // samples it. The mesher's caller clears the set once it has re-meshed them.
    const ChunkSet& getDirtyChunks() const { return _dirtyChunks; }
    void clearDirtyChunks() { _dirtyChunks.clear(); }
    void clearDirtyChunk(const glm::ivec3& chunk) { _dirtyChunks.erase(chunk); }
    void markChunkDirty(const glm::ivec3& chunk);
    void markAllChunksDirty();

    void sampleChunk(const glm::ivec3& chunk, ChunkSamples& samples) const;

    // Ground below a height field: height(x, z) in voxel units is where the surface lies in that column.
    void fillHeightField(const std::function<float(int x, int z)>& height, uint8_t material = 0);
    void fillBelow(float height, uint8_t material = 0);

    void compactChunks();
    int countDenseChunks() const;

    // f(const glm::ivec3& chunk, const Chunk& value) for every chunk, in storage order.
    template <typename F> void forEachChunk(F&& f) const {
        int index = 0;
        for (int z = 0; z < _chunkCount.z; ++z) {
            for (int y = 0; y < _chunkCount.y; ++y) {
                for (int x = 0; x < _chunkCount.x; ++x) {
                    f(glm::ivec3(x, y, z), _chunks[index++]);
                }
            }
        }
    }

private:
    int chunkIndexOf(const glm::ivec3& chunk) const {
        return (chunk.z * _chunkCount.y + chunk.y) * _chunkCount.x + chunk.x;
    }
    void markDirtyAround(const glm::ivec3& voxel);

    glm::ivec3 _voxelCount { 0 };
    glm::ivec3 _chunkCount { 0 };
    std::vector<Chunk> _chunks;
    ChunkSet _dirtyChunks;
};

} // namespace terrain

#endif // hifi_terrain_VoxelGrid_h
