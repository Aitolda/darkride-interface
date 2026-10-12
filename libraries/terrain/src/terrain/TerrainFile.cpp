//
//  TerrainFile.cpp
//  libraries/terrain/src/terrain
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TerrainFile.h"

#include <zlib.h>

using namespace terrain;

namespace {

const char MAGIC[4] = { 'D', 'R', 'K', 'T' };
const uint32_t VERSION = 1;
const uint8_t KIND_UNIFORM = 0;
const uint8_t KIND_DENSE = 1;
const int MAX_VOXELS_PER_AXIS = 1 << 14;
const size_t MAX_CHUNKS = 1 << 20;
const size_t DENSE_BYTES = 2 * CHUNK_VOXEL_COUNT;

class Writer {
public:
    void u8(uint8_t value) { _bytes.push_back(value); }
    void i8(int8_t value) { _bytes.push_back((uint8_t)value); }
    void u32(uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            _bytes.push_back((uint8_t)(value >> (8 * i)));
        }
    }
    void i32(int32_t value) { u32((uint32_t)value); }
    void raw(const uint8_t* data, size_t size) { _bytes.insert(_bytes.end(), data, data + size); }
    Bytes take() { return std::move(_bytes); }

private:
    Bytes _bytes;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : _data(data), _size(size) {}

    bool ok() const { return _ok; }
    uint8_t u8() { return need(1) ? _data[_at++] : 0; }
    int8_t i8() { return (int8_t)u8(); }
    uint32_t u32() {
        if (!need(4)) {
            return 0;
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value |= (uint32_t)_data[_at++] << (8 * i);
        }
        return value;
    }
    int32_t i32() { return (int32_t)u32(); }
    const uint8_t* raw(size_t size) {
        if (!need(size)) {
            return nullptr;
        }
        const uint8_t* at = _data + _at;
        _at += size;
        return at;
    }

private:
    bool need(size_t size) {
        if (_at + size > _size) {
            _ok = false;
            return false;
        }
        return true;
    }

    const uint8_t* _data;
    size_t _size;
    size_t _at { 0 };
    bool _ok { true };
};

bool fail(std::string* error, const std::string& message, VoxelGrid& grid) {
    grid.reset(glm::ivec3(0));
    if (error) {
        *error = message;
    }
    return false;
}

} // namespace

Bytes terrain::saveTerrain(const VoxelGrid& grid) {
    Writer out;
    out.raw((const uint8_t*)MAGIC, 4);
    out.u32(VERSION);
    const glm::ivec3 voxelCount = grid.getVoxelCount();
    out.i32(voxelCount.x);
    out.i32(voxelCount.y);
    out.i32(voxelCount.z);
    out.i32(CHUNK_SIZE);
    const glm::ivec3 chunkCount = grid.getChunkCount();
    out.u32((uint32_t)(chunkCount.x * chunkCount.y * chunkCount.z));

    std::vector<uint8_t> dense(DENSE_BYTES);
    std::vector<uint8_t> compressed(compressBound((uLong)DENSE_BYTES));
    grid.forEachChunk([&](const glm::ivec3& coords, const Chunk& chunk) {
        out.i32(coords.x);
        out.i32(coords.y);
        out.i32(coords.z);
        if (chunk.isUniform()) {
            out.u8(KIND_UNIFORM);
            out.i8(chunk.getUniformValue().distance);
            out.u8(chunk.getUniformValue().material);
            return;
        }
        const std::vector<Voxel>& voxels = chunk.getVoxels();
        for (int i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
            dense[i] = (uint8_t)voxels[i].distance;
            dense[CHUNK_VOXEL_COUNT + i] = voxels[i].material;
        }
        uLongf length = (uLongf)compressed.size();
        if (compress2(compressed.data(), &length, dense.data(), (uLong)DENSE_BYTES, Z_DEFAULT_COMPRESSION) != Z_OK) {
            // Nothing sensible can go wrong here; store the chunk as if it held nothing but the first voxel.
            out.u8(KIND_UNIFORM);
            out.i8(voxels[0].distance);
            out.u8(voxels[0].material);
            return;
        }
        out.u8(KIND_DENSE);
        out.u32((uint32_t)length);
        out.raw(compressed.data(), length);
    });
    return out.take();
}

bool terrain::loadTerrain(const uint8_t* data, size_t size, VoxelGrid& grid, std::string* error) {
    Reader in(data, size);
    const uint8_t* magic = in.raw(4);
    if (!magic || std::string((const char*)magic, 4) != std::string(MAGIC, 4)) {
        return fail(error, "not a terrain file", grid);
    }
    const uint32_t version = in.u32();
    if (version != VERSION) {
        return fail(error, "terrain file version " + std::to_string(version) + " is not supported", grid);
    }
    glm::ivec3 voxelCount;
    voxelCount.x = in.i32();
    voxelCount.y = in.i32();
    voxelCount.z = in.i32();
    const int32_t chunkSize = in.i32();
    const uint32_t chunkCount = in.u32();
    if (!in.ok()) {
        return fail(error, "terrain file header is cut short", grid);
    }
    if (chunkSize != CHUNK_SIZE) {
        return fail(error, "terrain file has chunks of " + std::to_string(chunkSize) + " voxels, not " + std::to_string(CHUNK_SIZE), grid);
    }
    if (glm::any(glm::lessThan(voxelCount, glm::ivec3(1))) || glm::any(glm::greaterThan(voxelCount, glm::ivec3(MAX_VOXELS_PER_AXIS)))) {
        return fail(error, "terrain file has an impossible voxel count", grid);
    }
    grid.reset(voxelCount);
    const glm::ivec3 chunks = grid.getChunkCount();
    if ((size_t)chunks.x * chunks.y * chunks.z > MAX_CHUNKS || chunkCount > MAX_CHUNKS) {
        return fail(error, "terrain file has too many chunks", grid);
    }

    std::vector<uint8_t> dense(DENSE_BYTES);
    std::vector<Voxel> voxels;
    for (uint32_t i = 0; i < chunkCount; ++i) {
        glm::ivec3 coords;
        coords.x = in.i32();
        coords.y = in.i32();
        coords.z = in.i32();
        const uint8_t kind = in.u8();
        if (!in.ok()) {
            return fail(error, "terrain file is cut short at chunk " + std::to_string(i), grid);
        }
        if (!grid.containsChunk(coords)) {
            return fail(error, "terrain file has a chunk outside the grid", grid);
        }
        if (kind == KIND_UNIFORM) {
            const int8_t distance = in.i8();
            const uint8_t material = in.u8();
            if (!in.ok()) {
                return fail(error, "terrain file is cut short at chunk " + std::to_string(i), grid);
            }
            grid.setChunk(coords, Chunk(Voxel(distance, material)));
            continue;
        }
        if (kind != KIND_DENSE) {
            return fail(error, "terrain file has a chunk of unknown kind", grid);
        }
        const uint32_t length = in.u32();
        const uint8_t* compressed = in.raw(length);
        if (!compressed) {
            return fail(error, "terrain file is cut short at chunk " + std::to_string(i), grid);
        }
        uLongf denseLength = (uLongf)DENSE_BYTES;
        if (uncompress(dense.data(), &denseLength, compressed, length) != Z_OK || denseLength != DENSE_BYTES) {
            return fail(error, "terrain file chunk " + std::to_string(i) + " does not decompress", grid);
        }
        voxels.resize(CHUNK_VOXEL_COUNT);
        for (int v = 0; v < CHUNK_VOXEL_COUNT; ++v) {
            voxels[v] = Voxel((int8_t)dense[v], dense[CHUNK_VOXEL_COUNT + v]);
        }
        Chunk chunk;
        chunk.setVoxels(voxels);
        grid.setChunk(coords, std::move(chunk));
    }
    grid.markAllChunksDirty();
    return true;
}
