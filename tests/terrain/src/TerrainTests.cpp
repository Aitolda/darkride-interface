//
//  TerrainTests.cpp
//  tests/terrain/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TerrainTests.h"

#include <cmath>
#include <map>

#include <QElapsedTimer>
#include <QThreadPool>

#include <terrain/AsyncMesher.h>
#include <terrain/Brush.h>
#include <terrain/PaintMap.h>
#include <terrain/SurfaceNets.h>
#include <terrain/TerrainFile.h>
#include <terrain/VoxelGrid.h>

QTEST_MAIN(TerrainTests)

using namespace terrain;

namespace {

// Rolling ground: a few voxels of hills in both directions.
float hills(int x, int z) {
    return 10.0f + 3.0f * std::sin(x * 0.3f) + 2.0f * std::cos(z * 0.4f);
}

bool near(const glm::vec3& a, const glm::vec3& b, float tolerance) {
    return glm::length(a - b) <= tolerance;
}

} // namespace

void TerrainTests::chunkStaysUniformUntilAVoxelDiffers() {
    Chunk chunk(Voxel::air());
    QVERIFY(chunk.isUniform());
    QVERIFY(!chunk.set(1, 2, 3, Voxel::air()));
    QVERIFY(chunk.isUniform());

    QVERIFY(chunk.set(1, 2, 3, Voxel::ground(7)));
    QVERIFY(!chunk.isUniform());
    QVERIFY(chunk.get(1, 2, 3).isGround());
    QCOMPARE((int)chunk.get(1, 2, 3).material, 7);
    QVERIFY(!chunk.get(0, 0, 0).isGround());
    QCOMPARE((int)chunk.getVoxels().size(), CHUNK_VOXEL_COUNT);

    QVERIFY(chunk.set(1, 2, 3, Voxel::air()));
    QVERIFY(!chunk.isUniform());
    QVERIFY(chunk.compact());
    QVERIFY(chunk.isUniform());
    QVERIFY(chunk.getUniformValue() == Voxel::air());
}

void TerrainTests::gridFillsBelowAHeightAndDirtiesNeighboursAcrossChunkFaces() {
    VoxelGrid grid(glm::ivec3(64, 32, 64));
    QCOMPARE(grid.getChunkCount(), glm::ivec3(2, 1, 2));
    QCOMPARE((int)grid.getDirtyChunks().size(), 4);

    grid.fillBelow(10.5f, 3);
    QVERIFY(grid.get(glm::ivec3(5, 10, 5)).isGround());
    QVERIFY(!grid.get(glm::ivec3(5, 11, 5)).isGround());
    QCOMPARE((int)grid.get(glm::ivec3(5, 10, 5)).material, 3);
    QVERIFY(std::abs(grid.get(glm::ivec3(5, 10, 5)).getDistance() + 0.5f) < 1.0e-6f);
    // Every chunk holds the surface, so none is uniform.
    QCOMPARE(grid.countDenseChunks(), 4);
    // Outside the grid the nearest voxel in it is returned.
    QVERIFY(grid.get(glm::ivec3(-5, 10, 5)) == grid.get(glm::ivec3(0, 10, 5)));
    QVERIFY(grid.get(glm::ivec3(5, 100, 5)) == grid.get(glm::ivec3(5, 31, 5)));

    grid.clearDirtyChunks();
    QVERIFY(grid.set(glm::ivec3(5, 10, 5), Voxel::air()));
    QCOMPARE(grid.getDirtyChunks(), ChunkSet({ glm::ivec3(0, 0, 0) }));
    QVERIFY(!grid.set(glm::ivec3(5, 10, 5), Voxel::air()));

    grid.clearDirtyChunks();
    QVERIFY(grid.set(glm::ivec3(31, 10, 5), Voxel::air()));
    QCOMPARE(grid.getDirtyChunks(), ChunkSet({ glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0) }));

    grid.clearDirtyChunks();
    QVERIFY(grid.set(glm::ivec3(32, 10, 32), Voxel::ground()));
    QCOMPARE(grid.getDirtyChunks(), ChunkSet({ glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, 1), glm::ivec3(1, 0, 1) }));

    QVERIFY(!grid.set(glm::ivec3(64, 10, 5), Voxel::air()));
}

void TerrainTests::flatGroundMeshesToOneLevelPlane() {
    VoxelGrid grid(glm::ivec3(32, 32, 32));
    grid.fillBelow(10.3f, 2);
    ChunkMesh mesh = meshChunk(grid, glm::ivec3(0, 0, 0));

    // One vertex per cell in the chunk and the layer outside its low faces, one quad per column.
    QCOMPARE((int)mesh.getVertexCount(), 33 * 33);
    QCOMPARE((int)mesh.getTriangleCount(), 2 * 32 * 32);
    for (size_t i = 0; i < mesh.getVertexCount(); ++i) {
        QVERIFY(std::abs(mesh.positions[i].y - 10.3f) < 0.02f);
        QVERIFY(mesh.positions[i].x >= -1.0f && mesh.positions[i].x < 32.0f);
        QVERIFY(mesh.positions[i].z >= -1.0f && mesh.positions[i].z < 32.0f);
        QVERIFY(near(mesh.normals[i], glm::vec3(0.0f, 1.0f, 0.0f), 1.0e-4f));
        QCOMPARE((int)mesh.materials[i], 2);
    }
    for (uint32_t index : mesh.indices) {
        QVERIFY(index < mesh.getVertexCount());
    }

    // A chunk with no surface in it meshes to nothing.
    QVERIFY(meshChunk(VoxelGrid(glm::ivec3(32, 32, 32), Voxel::ground()), glm::ivec3(0, 0, 0)).isEmpty());
}

void TerrainTests::neighbouringChunksMeetWithoutASeam() {
    VoxelGrid grid(glm::ivec3(64, 32, 32));
    grid.fillHeightField(hills);
    ChunkMesh left = meshChunk(grid, glm::ivec3(0, 0, 0));
    ChunkMesh right = meshChunk(grid, glm::ivec3(1, 0, 0));
    QVERIFY(!left.isEmpty() && !right.isEmpty());

    // The right chunk's vertices in the layer outside its low x face are the left chunk's in its last column:
    // where both have a vertex for a cell, it is the same vertex.
    std::map<std::tuple<int, int, int>, size_t> leftByCell;
    for (size_t i = 0; i < left.getVertexCount(); ++i) {
        const glm::vec3& p = left.positions[i];
        if (p.x >= 31.0f) {
            leftByCell[std::make_tuple((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z))] = i;
        }
    }
    int matched = 0;
    for (size_t i = 0; i < right.getVertexCount(); ++i) {
        const glm::vec3 p = right.positions[i] + glm::vec3(32.0f, 0.0f, 0.0f);
        if (p.x >= 32.0f) {
            continue;
        }
        auto found = leftByCell.find(std::make_tuple((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z)));
        if (found == leftByCell.end()) {
            continue;
        }
        QVERIFY(near(left.positions[found->second], p, 1.0e-5f));
        QVERIFY(near(left.normals[found->second], right.normals[i], 1.0e-5f));
        ++matched;
    }
    QVERIFY(matched >= 30);
}

void TerrainTests::trianglesFaceTheAirLikeTheirNormals() {
    VoxelGrid grid(glm::ivec3(32, 32, 32));
    grid.fillHeightField(hills);
    ChunkMesh mesh = meshChunk(grid, glm::ivec3(0, 0, 0));
    QVERIFY(mesh.getTriangleCount() > 1000);

    glm::vec3 meanNormal(0.0f);
    for (size_t t = 0; t < mesh.getTriangleCount(); ++t) {
        const uint32_t a = mesh.indices[3 * t], b = mesh.indices[3 * t + 1], c = mesh.indices[3 * t + 2];
        const glm::vec3 faceNormal = glm::cross(mesh.positions[b] - mesh.positions[a], mesh.positions[c] - mesh.positions[a]);
        if (glm::length(faceNormal) < 1.0e-8f) {
            continue;
        }
        const glm::vec3 vertexNormal = mesh.normals[a] + mesh.normals[b] + mesh.normals[c];
        QVERIFY(glm::dot(faceNormal, vertexNormal) > 0.0f);
        meanNormal += glm::normalize(faceNormal);
    }
    meanNormal = glm::normalize(meanNormal);
    QVERIFY(meanNormal.y > 0.9f);
}

void TerrainTests::brushesDigFillAndRaise() {
    VoxelGrid grid(glm::ivec3(64, 64, 64));
    grid.fillBelow(40.5f, 1);
    grid.clearDirtyChunks();

    Brush brush;
    brush.center = glm::vec3(32.0f, 40.5f, 32.0f);
    brush.radius = 8.0f;
    brush.material = 2;
    QVERIFY(sculpt(grid, brush, SculptMode::Dig) > 0);
    QVERIFY(!grid.get(glm::ivec3(32, 38, 32)).isGround());   // inside the sphere, below the old surface
    QVERIFY(grid.get(glm::ivec3(32, 30, 32)).isGround());    // below the sphere
    QVERIFY(grid.get(glm::ivec3(50, 38, 32)).isGround());    // beside it
    QVERIFY(!grid.getDirtyChunks().empty());

    // The crater's floor is at the sphere's bottom, 32.5; the mesh's positions are relative to the chunk's origin.
    ChunkMesh mesh = meshChunk(grid, glm::ivec3(1, 1, 1));
    float lowest = 1000.0f;
    for (const glm::vec3& p : mesh.positions) {
        lowest = std::min(lowest, p.y + 32.0f);
    }
    QVERIFY(std::abs(lowest - 32.5f) < 0.6f);

    brush.center = glm::vec3(10.0f, 44.0f, 10.0f);
    brush.radius = 3.0f;
    QVERIFY(sculpt(grid, brush, SculptMode::Fill) > 0);
    QVERIFY(grid.get(glm::ivec3(10, 44, 10)).isGround());
    QCOMPARE((int)grid.get(glm::ivec3(10, 44, 10)).material, 2);
    QVERIFY(!grid.get(glm::ivec3(10, 48, 10)).isGround());

    brush.center = glm::vec3(50.0f, 40.5f, 50.0f);
    brush.radius = 4.0f;
    brush.strength = 1.0f;
    QVERIFY(!grid.get(glm::ivec3(50, 41, 50)).isGround());
    QVERIFY(sculpt(grid, brush, SculptMode::Raise) > 0);
    QVERIFY(grid.get(glm::ivec3(50, 41, 50)).isGround());
    QCOMPARE((int)grid.get(glm::ivec3(50, 41, 50)).material, 2);
    QVERIFY(!grid.get(glm::ivec3(50, 43, 50)).isGround());

    brush.material = 5;
    QVERIFY(sculpt(grid, brush, SculptMode::PaintVoxels) > 0);
    QCOMPARE((int)grid.get(glm::ivec3(50, 40, 50)).material, 5);
    QCOMPARE((int)grid.get(glm::ivec3(50, 43, 50)).material, 1);   // air keeps its material
}

void TerrainTests::fileRoundTripKeepsEveryVoxel() {
    VoxelGrid grid(glm::ivec3(70, 40, 50));
    QCOMPARE(grid.getChunkCount(), glm::ivec3(3, 2, 2));
    grid.fillHeightField(hills, 1);
    Brush brush;
    brush.center = glm::vec3(20.0f, 10.0f, 20.0f);
    brush.radius = 6.0f;
    sculpt(grid, brush, SculptMode::Dig);
    QVERIFY(grid.countDenseChunks() > 0);
    QVERIFY(grid.countDenseChunks() < 12);

    Bytes bytes = saveTerrain(grid);
    QVERIFY(bytes.size() > 32);
    QVERIFY(bytes.size() < (size_t)(70 * 40 * 50 * 2) / 4);

    VoxelGrid loaded;
    std::string error;
    QVERIFY2(loadTerrain(bytes, loaded, nullptr, &error), error.c_str());
    QCOMPARE(loaded.getVoxelCount(), grid.getVoxelCount());
    QCOMPARE(loaded.countDenseChunks(), grid.countDenseChunks());
    for (int z = 0; z < 50; ++z) {
        for (int y = 0; y < 40; ++y) {
            for (int x = 0; x < 70; ++x) {
                QVERIFY(loaded.get(glm::ivec3(x, y, z)) == grid.get(glm::ivec3(x, y, z)));
            }
        }
    }
    QCOMPARE((int)loaded.getDirtyChunks().size(), 12);

    Bytes cut(bytes.begin(), bytes.begin() + bytes.size() / 2);
    QVERIFY(!loadTerrain(cut, loaded, nullptr, &error));
    QVERIFY(!error.empty());
    QVERIFY(loaded.isEmpty());

    Bytes wrong = bytes;
    wrong[0] = 'X';
    QVERIFY(!loadTerrain(wrong, loaded, nullptr, &error));
}

void TerrainTests::asyncMesherMatchesTheMesherAndRedoesChunksEditedMeanwhile() {
    VoxelGrid grid(glm::ivec3(64, 32, 64));
    grid.fillHeightField(hills);
    const ChunkSet dirty = grid.getDirtyChunks();
    QCOMPARE((int)dirty.size(), 4);
    std::map<glm::ivec3, ChunkMesh, IVec3Less> expected;
    for (const glm::ivec3& chunk : dirty) {
        expected[chunk] = meshChunk(grid, chunk);
    }

    QThreadPool pool;
    pool.setMaxThreadCount(3);
    AsyncMesher mesher(&pool);
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(mesher.submit(grid), (int)dirty.size());
    qDebug() << "sampling and sending" << dirty.size() << "chunks took" << timer.nsecsElapsed() / 1000 << "us on this thread";
    QVERIFY(grid.getDirtyChunks().empty());
    QCOMPARE(mesher.getInFlightCount(), dirty.size());
    QVERIFY(!mesher.isIdle());
    QCOMPARE(mesher.submit(grid), 0);
    pool.waitForDone();
    QVERIFY(mesher.hasResults());
    std::vector<AsyncMesher::Result> results = mesher.collect();
    QCOMPARE(results.size(), dirty.size());
    QVERIFY(mesher.isIdle());
    int meshed = 0;
    for (const AsyncMesher::Result& result : results) {
        const ChunkMesh& wanted = expected.at(result.chunk);
        QVERIFY(result.mesh.positions == wanted.positions);
        QVERIFY(result.mesh.normals == wanted.normals);
        QVERIFY(result.mesh.materials == wanted.materials);
        QVERIFY(result.mesh.indices == wanted.indices);
        if (!result.mesh.isEmpty()) {
            ++meshed;
        }
    }
    QCOMPARE(meshed, 4);

    // A chunk edited while its job is out stays dirty, is not sent again meanwhile, and goes out once its mesh is
    // back; that mesh is the one of the samples taken, the next one of the edit.
    const glm::ivec3 voxel(40, 10, 40);   // inside chunk (1, 0, 1), above the hills there
    QVERIFY(grid.set(voxel, Voxel::ground()));
    QCOMPARE(grid.getDirtyChunks(), ChunkSet({ glm::ivec3(1, 0, 1) }));
    QCOMPARE(mesher.submit(grid), 1);
    QVERIFY(grid.set(voxel + glm::ivec3(1, 0, 0), Voxel::ground()));
    QCOMPARE(grid.getDirtyChunks(), ChunkSet({ glm::ivec3(1, 0, 1) }));
    QCOMPARE(mesher.submit(grid), 0);
    pool.waitForDone();
    results = mesher.collect();
    QCOMPARE(results.size(), (size_t)1);
    QCOMPARE(results[0].chunk, glm::ivec3(1, 0, 1));
    QVERIFY(mesher.isIdle());
    QCOMPARE(grid.getDirtyChunks(), ChunkSet({ glm::ivec3(1, 0, 1) }));
    QCOMPARE(mesher.submit(grid), 1);
    pool.waitForDone();
    results = mesher.collect();
    QCOMPARE(results.size(), (size_t)1);
    QVERIFY(results[0].mesh.positions == meshChunk(grid, glm::ivec3(1, 0, 1)).positions);
    QVERIFY(grid.getDirtyChunks().empty());

    // A chunk with no surface in its samples is answered at once, with an empty mesh.
    VoxelGrid solid(glm::ivec3(32, 32, 32), Voxel::ground());
    QCOMPARE(mesher.submit(solid), 1);
    QVERIFY(mesher.hasResults());
    results = mesher.collect();
    QCOMPARE(results.size(), (size_t)1);
    QVERIFY(results[0].mesh.isEmpty());

    // reset() forgets the jobs out: a replaced grid gets none of the old one's meshes.
    QVERIFY(grid.set(voxel + glm::ivec3(0, 1, 0), Voxel::ground()));
    QCOMPARE(mesher.submit(grid), 1);
    mesher.reset();
    QVERIFY(mesher.isIdle());
    pool.waitForDone();
    QVERIFY(!mesher.hasResults());
    QVERIFY(mesher.collect().empty());
}

void TerrainTests::paintMapPaintsErasesTilesAndRoundTripsThroughTheFile() {
    // two texels a voxel over a footprint of 70 by 50 voxels: three columns by two
    PaintMap map(glm::ivec2(70, 50), 2);
    QVERIFY(!map.isEmpty());
    QVERIFY(!map.isPainted());
    QCOMPARE(map.getSize(), glm::ivec2(140, 100));
    QCOMPARE(map.getVoxelFootprint(), glm::ivec2(70, 50));
    QCOMPARE(map.getColumnCount(), glm::ivec2(3, 2));
    QCOMPARE((int)map.getDirtyColumns().size(), 6);
    map.clearDirtyColumns();

    // a hard full-strength brush paints its layer alone, within its radius only
    PaintBrush brush;
    brush.center = glm::vec2(20.0f, 20.0f);
    brush.radius = 6.0f;
    brush.strength = 1.0f;
    brush.hardness = 1.0f;
    brush.layer = 2;
    QVERIFY(paint(map, brush, false) > 0);
    QVERIFY(map.isPainted());
    const uint8_t* centre = map.at(glm::ivec2(40, 40));
    QCOMPARE((int)centre[2], 255);
    QCOMPARE((int)centre[0] + centre[1] + centre[3] + centre[4] + centre[5] + centre[6] + centre[7], 0);
    const uint8_t* outside = map.at(glm::ivec2(80, 40));
    for (int k = 0; k < PAINT_LAYERS; ++k) {
        QCOMPARE((int)outside[k], 0);
    }
    // it lies well inside column (0, 0): that column alone is dirty
    QCOMPARE((int)map.getDirtyColumns().size(), 1);
    QVERIFY(map.getDirtyColumns().count(glm::ivec2(0, 0)));
    map.clearDirtyColumns();

    // half strength of another layer takes half of what is there; the weights never sum past 255
    brush.layer = 5;
    brush.strength = 0.5f;
    QVERIFY(paint(map, brush, false) > 0);
    QVERIFY(std::abs((int)centre[2] - 128) <= 1);
    QVERIFY(std::abs((int)centre[5] - 128) <= 1);
    int sum = 0;
    for (int k = 0; k < PAINT_LAYERS; ++k) {
        sum += centre[k];
    }
    QVERIFY(sum <= 255);
    QVERIFY(sum >= 253);

    // erasing at full strength leaves the voxel material alone again
    brush.strength = 1.0f;
    QVERIFY(paint(map, brush, true) > 0);
    for (int k = 0; k < PAINT_LAYERS; ++k) {
        QCOMPARE((int)centre[k], 0);
    }
    QVERIFY(!map.isPainted());
    map.clearDirtyColumns();

    // a texel within a voxel of the next column lands in that column's apron: both columns' tiles are out of date
    brush.center = glm::vec2(31.75f, 10.25f);   // texel (63, 20): voxel 31, the last of column 0
    brush.radius = 0.4f;
    brush.layer = 0;
    QCOMPARE(paint(map, brush, false), 1);
    QCOMPARE((int)map.at(glm::ivec2(63, 20))[0], 255);
    QCOMPARE((int)map.getDirtyColumns().size(), 2);
    QVERIFY(map.getDirtyColumns().count(glm::ivec2(0, 0)));
    QVERIFY(map.getDirtyColumns().count(glm::ivec2(1, 0)));

    // the tile of column (1, 0): 34 voxels a side at two texels each, its texel (0, 0) at the map's (62, -2)
    QCOMPARE(map.getTileSize(), 68);
    std::vector<uint8_t> low, high;
    map.fillTile(glm::ivec2(1, 0), low, high);
    QCOMPARE((int)low.size(), 68 * 68 * 4);
    QCOMPARE((int)high.size(), 68 * 68 * 4);
    QCOMPARE((int)low[((20 + 2) * 68 + (63 - 62)) * 4 + 0], 255);   // the painted texel, through the apron
    QCOMPARE((int)low[((20 + 2) * 68 + (63 - 62)) * 4 + 1], 0);
    // and the tile of column (0, 0) repeats the map's edge beyond it
    map.at(glm::ivec2(0, 5))[7] = 99;
    map.fillTile(glm::ivec2(0, 0), low, high);
    QCOMPARE((int)high[((5 + 2) * 68 + 0) * 4 + 3], 99);   // tile texel x 0 is map texel -2, clamped to 0
    QCOMPARE((int)high[((5 + 2) * 68 + 2) * 4 + 3], 99);   // tile texel x 2 is map texel 0 itself
    QCOMPARE((int)high[((5 + 2) * 68 + 3) * 4 + 3], 0);
    map.at(glm::ivec2(0, 5))[7] = 0;

    // a block copied out comes back as it was
    brush.center = glm::vec2(20.0f, 20.0f);
    brush.radius = 6.0f;
    brush.layer = 3;
    paint(map, brush, false);
    const PaintBlock block = map.copy(glm::ivec2(20, 20), glm::ivec2(60, 60));
    QCOMPARE(block.low, glm::ivec2(20, 20));
    QCOMPARE(block.size, glm::ivec2(41, 41));
    const std::vector<uint8_t> painted = map.getWeights();
    paint(map, brush, true);
    QVERIFY(map.getWeights() != painted);
    map.paste(block);
    QVERIFY(map.getWeights() == painted);
    QVERIFY(map.copy(glm::ivec2(200, 200), glm::ivec2(300, 300)).isEmpty());

    // the file keeps the map with the grid, and a never-painted map keeps the file at version 1
    VoxelGrid grid(glm::ivec3(70, 40, 50));
    grid.fillHeightField(hills, 1);
    const Bytes withPaint = saveTerrain(grid, &map);
    VoxelGrid loadedGrid;
    PaintMap loadedMap;
    std::string error;
    QVERIFY2(loadTerrain(withPaint, loadedGrid, &loadedMap, &error), error.c_str());
    QCOMPARE(loadedGrid.getVoxelCount(), grid.getVoxelCount());
    QCOMPARE(loadedMap.getSize(), map.getSize());
    QCOMPARE(loadedMap.getTexelsPerVoxel(), 2);
    QVERIFY(loadedMap.getWeights() == map.getWeights());
    QCOMPARE((int)loadedMap.getDirtyColumns().size(), 6);

    PaintMap blank(glm::ivec2(70, 50), 2);
    const Bytes withoutPaint = saveTerrain(grid, &blank);
    QVERIFY(withoutPaint.size() < withPaint.size());
    QVERIFY(withoutPaint == saveTerrain(grid));
    QVERIFY2(loadTerrain(withoutPaint, loadedGrid, &loadedMap, &error), error.c_str());
    QVERIFY(loadedMap.isEmpty());

    // a map over another footprint is not the grid's: left out of the file
    PaintMap other(glm::ivec2(10, 10), 2);
    other.at(glm::ivec2(1, 1))[0] = 255;
    QVERIFY(saveTerrain(grid, &other) == saveTerrain(grid));

    // a cut file fails cleanly
    Bytes cut(withPaint.begin(), withPaint.begin() + withPaint.size() - 10);
    QVERIFY(!loadTerrain(cut, loadedGrid, &loadedMap, &error));
    QVERIFY(loadedGrid.isEmpty());
    QVERIFY(loadedMap.isEmpty());
}
