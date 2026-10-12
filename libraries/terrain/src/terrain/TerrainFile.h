//
//  TerrainFile.h
//  libraries/terrain/src/terrain
//
//  The terrain file: every chunk of a voxel grid in one file, the asset a Terrain entity's terrainURL points at.
//  Little-endian throughout:
//
//    char[4] "DRKT"   uint32 version (1)   int32 voxelCount x, y, z   int32 chunkSize (32)   uint32 chunkCount
//    then chunkCount times:
//      int32 chunk x, y, z   uint8 kind: 0 uniform, 1 dense
//      uniform:  int8 distance   uint8 material
//      dense:    uint32 length   bytes[length]: a zlib stream of the chunk's CHUNK_VOXEL_COUNT distance bytes
//                followed by its CHUNK_VOXEL_COUNT material bytes, in Chunk::indexOf order
//
//  Chunks keep their own coordinates so a later version can leave them in files of their own and fetch them one at
//  a time (streaming, TERRAIN.md section 10). Chunks missing from a file take the grid's fill value.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_terrain_TerrainFile_h
#define hifi_terrain_TerrainFile_h

#include <cstdint>
#include <string>
#include <vector>

#include "VoxelGrid.h"

namespace terrain {

using Bytes = std::vector<uint8_t>;

Bytes saveTerrain(const VoxelGrid& grid);

// Replaces the grid with the file's contents. On failure the grid is left empty and error, if given, says why.
bool loadTerrain(const uint8_t* data, size_t size, VoxelGrid& grid, std::string* error = nullptr);
inline bool loadTerrain(const Bytes& data, VoxelGrid& grid, std::string* error = nullptr) {
    return loadTerrain(data.data(), data.size(), grid, error);
}

} // namespace terrain

#endif // hifi_terrain_TerrainFile_h
