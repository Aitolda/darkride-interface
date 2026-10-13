//
//  TerrainFile.h
//  libraries/terrain/src/terrain
//
//  The terrain file: every chunk of a voxel grid, and the paint map over it, in one file, the asset a Terrain
//  entity's terrainURL points at. Little-endian throughout:
//
//    char[4] "DRKT"   uint32 version (1, 2 or 3)   int32 voxelCount x, y, z   int32 chunkSize (32)   uint32 chunkCount
//    then chunkCount times:
//      int32 chunk x, y, z   [version 3: uint8 level]   uint8 kind: 0 uniform, 1 dense
//      uniform:  int8 distance   uint8 material
//      dense:    uint32 length   bytes[length]: a zlib stream of the chunk's CHUNK_VOXEL_COUNT distance bytes
//                followed by its CHUNK_VOXEL_COUNT material bytes, in Chunk::indexOf order
//    versions 2 and 3 go on with the paint map (PaintMap.h):
//      uint32 width, height (texels)   uint32 texelsPerVoxel   uint32 length   bytes[length]: a zlib stream of
//      PAINT_LAYERS planes of width * height bytes, layer 0's weights at every texel (x fastest), then layer 1's…
//      (width and height 0 when there is no map to write)
//
//  Version 3 gives each chunk its level (VoxelGrid.h), its coordinates then being at that level; versions 1 and 2
//  hold level-0 chunks only. A terrain is written in the oldest version that holds it: version 1 while its map was
//  never painted, 2 with paint, 3 once any chunk is split, so a file reads wherever it can. Chunks keep their own
//  coordinates so a later version can leave them in files of their own and fetch them one at a time (streaming,
//  TERRAIN.md section 10). Chunks missing from a file take the grid's fill value.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_terrain_TerrainFile_h
#define hifi_terrain_TerrainFile_h

#include <cstdint>
#include <string>
#include <vector>

#include "PaintMap.h"
#include "VoxelGrid.h"

namespace terrain {

using Bytes = std::vector<uint8_t>;

// The paint map, if given and painted, must cover the grid's footprint; otherwise the file holds the grid alone.
Bytes saveTerrain(const VoxelGrid& grid, const PaintMap* paint = nullptr);

// Replaces the grid, and the paint map if given, with the file's contents; a file without a paint map leaves the
// map empty, for the caller to make a fresh one. On failure both are left empty and error, if given, says why.
bool loadTerrain(const uint8_t* data, size_t size, VoxelGrid& grid, PaintMap* paint, std::string* error = nullptr);
inline bool loadTerrain(const Bytes& data, VoxelGrid& grid, PaintMap* paint, std::string* error = nullptr) {
    return loadTerrain(data.data(), data.size(), grid, paint, error);
}

} // namespace terrain

#endif // hifi_terrain_TerrainFile_h
