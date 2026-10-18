//
//  TopSurface.h
//  libraries/terrain/src/terrain
//
//  The ground the sky sees (TERRAIN.md section 5, paint under the ground): over every column of the grid's voxels,
//  the height where a line coming straight down first meets the ground. The paint map (PaintMap.h) is one picture
//  seen from above, so it lies on that surface alone: the shader paints a fragment only within a voxel or so of it,
//  and a cave's floor, or the ground under an overhang, shows its voxels' own materials; a Paint stroke that lands
//  below it paints those materials instead. The heights follow the grid's edits through the grid's change log
//  (VoxelGrid::takeChanges), a chunk column at a time, and the renderer draws each chunk column with a tile of them,
//  as it does with the paint map's tiles.
//
//  A column's height is read between grid voxels, from the distance field at the grid's points: the crossing between
//  the first ground voxel from the top and the voxel of air above it. A finer chunk's detail between the points, and
//  a slope's surface between columns, stray from the heights by less than TOP_SURFACE_TOLERANCE; a floor under a
//  roof lies at least a roof and some headroom below them.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_terrain_TopSurface_h
#define hifi_terrain_TopSurface_h

#include <vector>

#include <glm/glm.hpp>

#include "PaintMap.h"
#include "VoxelGrid.h"

namespace terrain {

// How far below the top surface, in grid voxels, the ground still counts as on it: the paint map lies on it fully,
// fading out over the next TOP_SURFACE_FADE (the shader's constants are the same).
const float TOP_SURFACE_TOLERANCE = 1.0f;
const float TOP_SURFACE_FADE = 0.5f;
// The height of a column with no ground at all: under every surface, so nothing near it is taken to be below the top.
const float NO_GROUND_HEIGHT = -(float)CHUNK_SIZE;

class TopSurface {
public:
    TopSurface() = default;

    // Every height again: the grid it follows was replaced or resized. update() does this by itself when it sees it.
    void rebuild(const VoxelGrid& grid);
    // Takes the grid's edits since the last call (its TopSurface change log) and finds the heights again in the chunk
    // columns they reached; the tiles those change are marked dirty. Cheap when nothing changed.
    void update(VoxelGrid& grid);

    bool isEmpty() const { return _heights.empty(); }
    const glm::ivec2& getFootprint() const { return _footprint; }   // grid voxels along x and z
    // A column's height in grid voxels; beyond the grid's sides the edge column's (the ground runs open there).
    float heightAt(const glm::ivec2& column) const;
    // Between the columns: bilinear, as the shader reads the tiles.
    float heightAt(const glm::vec2& point) const;
    // Whether a point of the grid (grid voxels) is on the ground the sky sees: no further below the top surface than
    // TOP_SURFACE_TOLERANCE, or above it.
    bool isOnTop(const glm::vec3& point) const;

    // A chunk column's tile for the renderer: getTileSize() heights a side, texel (i, j) the height of the grid's
    // column (column * CHUNK_SIZE - PAINT_TILE_APRON + (i, j)), x fastest, the edge's heights repeating beyond the
    // grid. The shader reads a point x at texel x + PAINT_TILE_APRON's middle, so the filter interpolates as
    // heightAt() does.
    static int getTileSize() { return CHUNK_SIZE + 2 * PAINT_TILE_APRON; }
    void fillTile(const glm::ivec2& column, std::vector<float>& heights) const;

    // The chunk columns whose tiles are out of date. The renderer clears the set once it has remade them.
    const ColumnSet& getDirtyColumns() const { return _dirtyColumns; }
    void clearDirtyColumns() { _dirtyColumns.clear(); }
    // Whether every tile is out of date (a rebuild): the renderer drops them all.
    bool allColumnsDirty() const { return _allDirty; }
    void clearAllColumnsDirty() { _allDirty = false; }

private:
    // the heights of every column in a level-0 chunk column, from the top of the grid down
    void findChunkColumn(const VoxelGrid& grid, const glm::ivec2& column);

    glm::ivec2 _footprint { 0 };
    glm::ivec2 _chunkColumns { 0 };
    std::vector<float> _heights;   // a column's height, x fastest
    ColumnSet _dirtyColumns;
    bool _allDirty { false };
};

} // namespace terrain

#endif // hifi_terrain_TopSurface_h
