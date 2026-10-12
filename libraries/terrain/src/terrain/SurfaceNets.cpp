//
//  SurfaceNets.cpp
//  libraries/terrain/src/terrain
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "SurfaceNets.h"

#include <cassert>

using namespace terrain;

namespace {

// Cells run from -1 to CHUNK_SIZE - 1 on each axis: the chunk's own and one layer outside on the low side, whose
// vertices the quads along the low faces need.
const int CELL_COUNT = CHUNK_SIZE + 1;

// The corner k of a cell is at the cell's origin plus (k & 1, k >> 1 & 1, k >> 2 & 1).
const glm::vec3 CORNER_OFFSETS[8] = {
    glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f)
};

// The twelve edges of a cell as pairs of corners.
const int EDGES[12][2] = {
    { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },   // along x
    { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },   // along y
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }    // along z
};

const glm::ivec3 AXES[3] = { glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1) };

class Mesher {
public:
    explicit Mesher(const ChunkSamples& samples) : _samples(samples) {}

    ChunkMesh run() {
        if (_samples.isOneSided()) {
            return std::move(_mesh);
        }
        _cellVertices.assign((size_t)CELL_COUNT * CELL_COUNT * CELL_COUNT, -1);

        // Every edge whose low voxel is the chunk's own is this chunk's to mesh. The edge from voxel v along axis a
        // is shared by the four cells with origins v, v - b, v - c and v - b - c (b and c the other two axes).
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    const glm::ivec3 voxel(x, y, z);
                    const bool lowInside = _samples.at(x, y, z).isGround();
                    for (int axis = 0; axis < 3; ++axis) {
                        const glm::ivec3 high = voxel + AXES[axis];
                        if (_samples.at(high.x, high.y, high.z).isGround() == lowInside) {
                            continue;
                        }
                        const glm::ivec3& b = AXES[(axis + 1) % 3];
                        const glm::ivec3& c = AXES[(axis + 2) % 3];
                        const uint32_t v00 = vertexOf(voxel - b - c);
                        const uint32_t v10 = vertexOf(voxel - c);
                        const uint32_t v11 = vertexOf(voxel);
                        const uint32_t v01 = vertexOf(voxel - b);
                        // Seen from +axis, (0,0) (1,0) (1,1) (0,1) in the (b, c) plane runs counter-clockwise, b, c
                        // following axis cyclically. The surface faces +axis when the low voxel is the ground.
                        if (lowInside) {
                            addQuad(v00, v10, v11, v01);
                        } else {
                            addQuad(v00, v01, v11, v10);
                        }
                    }
                }
            }
        }
        return std::move(_mesh);
    }

private:
    static int cellIndex(const glm::ivec3& cell) {
        return ((cell.z + 1) * CELL_COUNT + (cell.y + 1)) * CELL_COUNT + (cell.x + 1);
    }

    uint32_t vertexOf(const glm::ivec3& cell) {
        int32_t& slot = _cellVertices[cellIndex(cell)];
        if (slot >= 0) {
            return (uint32_t)slot;
        }

        float distances[8];
        uint8_t materials[8];
        for (int k = 0; k < 8; ++k) {
            const Voxel& voxel = _samples.at(cell.x + (k & 1), cell.y + ((k >> 1) & 1), cell.z + ((k >> 2) & 1));
            distances[k] = voxel.getDistance();
            materials[k] = voxel.material;
        }

        // The vertex: the mean of the surface's crossings of the cell's edges.
        glm::vec3 sum(0.0f);
        int crossings = 0;
        for (const auto& edge : EDGES) {
            const float d0 = distances[edge[0]];
            const float d1 = distances[edge[1]];
            if ((d0 < 0.0f) == (d1 < 0.0f)) {
                continue;
            }
            const float t = d0 / (d0 - d1);
            sum += glm::mix(CORNER_OFFSETS[edge[0]], CORNER_OFFSETS[edge[1]], t);
            ++crossings;
        }
        assert(crossings > 0);
        const glm::vec3 p = crossings > 0 ? sum / (float)crossings : glm::vec3(0.5f);

        // The normal: the gradient of the trilinear distance field at the vertex, which points out of the ground.
        const float* d = distances;
        const float u = p.x, v = p.y, w = p.z;
        glm::vec3 gradient(
            glm::mix(glm::mix(d[1] - d[0], d[3] - d[2], v), glm::mix(d[5] - d[4], d[7] - d[6], v), w),
            glm::mix(glm::mix(d[2] - d[0], d[3] - d[1], u), glm::mix(d[6] - d[4], d[7] - d[5], u), w),
            glm::mix(glm::mix(d[4] - d[0], d[5] - d[1], u), glm::mix(d[6] - d[2], d[7] - d[3], u), v));
        const float length = glm::length(gradient);
        const glm::vec3 normal = length > 1.0e-6f ? gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);

        // The material: the ground corner's nearest the surface.
        int nearest = -1;
        for (int k = 0; k < 8; ++k) {
            if (distances[k] < 0.0f && (nearest < 0 || distances[k] > distances[nearest])) {
                nearest = k;
            }
        }
        const uint8_t material = materials[nearest >= 0 ? nearest : 0];

        slot = (int32_t)_mesh.positions.size();
        _mesh.positions.push_back(glm::vec3(cell) + p);
        _mesh.normals.push_back(normal);
        _mesh.materials.push_back(material);
        return (uint32_t)slot;
    }

    void addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        _mesh.indices.insert(_mesh.indices.end(), { a, b, c, a, c, d });
    }

    const ChunkSamples& _samples;
    ChunkMesh _mesh;
    std::vector<int32_t> _cellVertices;
};

} // namespace

ChunkMesh terrain::meshChunk(const ChunkSamples& samples) {
    return Mesher(samples).run();
}

ChunkMesh terrain::meshChunk(const VoxelGrid& grid, const glm::ivec3& chunk) {
    ChunkSamples samples;
    grid.sampleChunk(chunk, samples);
    return meshChunk(samples);
}
