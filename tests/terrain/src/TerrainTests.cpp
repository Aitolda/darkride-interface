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
#include <terrain/Resample.h>
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
    QCOMPARE(grid.getDirtyChunks(), ChunkKeySet({ glm::ivec3(0, 0, 0) }));
    QVERIFY(!grid.set(glm::ivec3(5, 10, 5), Voxel::air()));

    grid.clearDirtyChunks();
    QVERIFY(grid.set(glm::ivec3(31, 10, 5), Voxel::air()));
    QCOMPARE(grid.getDirtyChunks(), ChunkKeySet({ glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0) }));

    grid.clearDirtyChunks();
    QVERIFY(grid.set(glm::ivec3(32, 10, 32), Voxel::ground()));
    QCOMPARE(grid.getDirtyChunks(), ChunkKeySet({ glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, 1), glm::ivec3(1, 0, 1) }));

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
    const ChunkKeySet dirty = grid.getDirtyChunks();
    QCOMPARE((int)dirty.size(), 4);
    std::map<ChunkKey, ChunkMesh> expected;
    for (const ChunkKey& chunk : dirty) {
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
        const ChunkMesh& wanted = expected.at(result.key);
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
    QCOMPARE(grid.getDirtyChunks(), ChunkKeySet({ glm::ivec3(1, 0, 1) }));
    QCOMPARE(mesher.submit(grid), 1);
    QVERIFY(grid.set(voxel + glm::ivec3(1, 0, 0), Voxel::ground()));
    QCOMPARE(grid.getDirtyChunks(), ChunkKeySet({ glm::ivec3(1, 0, 1) }));
    QCOMPARE(mesher.submit(grid), 0);
    pool.waitForDone();
    results = mesher.collect();
    QCOMPARE(results.size(), (size_t)1);
    QVERIFY(results[0].key == ChunkKey(glm::ivec3(1, 0, 1)));
    QVERIFY(mesher.isIdle());
    QCOMPARE(grid.getDirtyChunks(), ChunkKeySet({ glm::ivec3(1, 0, 1) }));
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

void TerrainTests::resampledGridKeepsTheSurfaceAndItsMaterials() {
    // where a column's distance crosses zero: the height of the surface there
    auto crossing = [](const VoxelGrid& grid, int x, int z) {
        for (int y = 0; y + 1 < grid.getVoxelCount().y; ++y) {
            const float below = grid.get(glm::ivec3(x, y, z)).getDistance();
            const float above = grid.get(glm::ivec3(x, y + 1, z)).getDistance();
            if (below < 0.0f && above >= 0.0f) {
                return y + below / (below - above);
            }
        }
        return -1.0f;
    };
    const int columns[][2] = { { 3, 3 }, { 16, 16 }, { 20, 45 }, { 31, 32 }, { 48, 7 }, { 60, 60 } };

    // hills in the lower half of a 64-voxel cube: the chunks above them hold one value, air at the limit
    VoxelGrid source(glm::ivec3(64, 64, 64));
    source.fillHeightField(hills, 3);
    QCOMPARE(source.countDenseChunks(), 4);
    for (int z = 10; z <= 20; ++z) {
        for (int x = 10; x <= 20; ++x) {
            for (int y = 0; y < 4; ++y) {
                source.set(glm::ivec3(x, y, z), Voxel(source.get(glm::ivec3(x, y, z)).distance, 5));
            }
        }
    }

    // twice the voxels along each side over the same box: the surface lies at twice the height in voxels
    const VoxelGrid fine = resampleGrid(source, glm::ivec3(128, 128, 128));
    QCOMPARE(fine.getVoxelCount(), glm::ivec3(128, 128, 128));
    QCOMPARE(fine.getChunkCount(), glm::ivec3(4, 4, 4));
    QCOMPARE((int)fine.getDirtyChunks().size(), 64);
    for (const auto& column : columns) {
        const float expected = 2.0f * crossing(source, column[0], column[1]);
        QVERIFY(expected > 0.0f);
        QVERIFY(std::abs(crossing(fine, 2 * column[0], 2 * column[1]) - expected) < 0.1f);
    }
    // the chunks around the surface are sampled, those read from the uniform source chunks stay one value
    QVERIFY(!fine.getChunk(glm::ivec3(1, 0, 1)).isUniform());
    QVERIFY(fine.getChunk(glm::ivec3(1, 3, 1)).isUniform());
    QVERIFY(fine.getChunk(glm::ivec3(1, 3, 1)).getUniformValue() == Voxel(DISTANCE_MAX, 3));
    QVERIFY(fine.countDenseChunks() <= 32);
    // far from the surface the distances stay at the byte's limit
    QVERIFY(fine.get(glm::ivec3(5, 127, 5)).distance == DISTANCE_MAX);
    QVERIFY(fine.get(glm::ivec3(5, 0, 5)).distance == -DISTANCE_MAX);
    // a voxel takes the nearest source voxel's material
    QCOMPARE((int)fine.get(glm::ivec3(30, 4, 30)).material, 5);
    QCOMPARE((int)fine.get(glm::ivec3(60, 4, 60)).material, 3);

    // and back down, on one thread: the surface returns to where it was
    const VoxelGrid back = resampleGrid(fine, glm::ivec3(64, 64, 64), 1);
    QCOMPARE(back.getVoxelCount(), source.getVoxelCount());
    for (const auto& column : columns) {
        QVERIFY(std::abs(crossing(back, column[0], column[1]) - crossing(source, column[0], column[1])) < 0.1f);
    }
    QCOMPARE((int)back.get(glm::ivec3(15, 1, 15)).material, 5);

    // an uneven ratio: 64 voxels into 100, the surface at 100/64 of its height
    const VoxelGrid odd = resampleGrid(source, glm::ivec3(100, 100, 100));
    for (int x : { 16, 32, 48 }) {
        const float expected = (100.0f / 64.0f) * crossing(source, x, x);
        QVERIFY(std::abs(crossing(odd, x * 100 / 64, x * 100 / 64) - expected) < 0.15f);
    }

    // ground through and through stays one value a chunk, at the limit
    VoxelGrid solid(glm::ivec3(32, 32, 32), Voxel::ground(2));
    const VoxelGrid solidFine = resampleGrid(solid, glm::ivec3(64, 64, 64));
    QCOMPARE(solidFine.getChunkCount(), glm::ivec3(2, 2, 2));
    QCOMPARE(solidFine.countDenseChunks(), 0);
    QVERIFY(solidFine.get(glm::ivec3(40, 40, 40)) == Voxel::ground(2));

    // the same count gives the same grid; no grid gives none
    const VoxelGrid same = resampleGrid(source, source.getVoxelCount());
    QVERIFY(same.get(glm::ivec3(16, 9, 16)) == source.get(glm::ivec3(16, 9, 16)));
    QVERIFY(resampleGrid(VoxelGrid(), glm::ivec3(32)).isEmpty());
}

void TerrainTests::resampledPaintMapKeepsItsPaint() {
    PaintMap map(glm::ivec2(64, 64), 2);
    PaintBrush brush;
    brush.center = glm::vec2(20.0f, 20.0f);
    brush.radius = 8.0f;
    brush.strength = 1.0f;
    brush.hardness = 1.0f;
    brush.layer = 2;
    QVERIFY(paint(map, brush, false) > 0);
    brush.center = glm::vec2(44.0f, 44.0f);
    brush.radius = 6.0f;
    brush.strength = 0.5f;
    brush.layer = 5;
    QVERIFY(paint(map, brush, false) > 0);
    auto painted = [](const PaintMap& m, int layer) {
        int count = 0;
        for (int j = 0; j < m.getSize().y; ++j) {
            for (int i = 0; i < m.getSize().x; ++i) {
                count += m.at(glm::ivec2(i, j))[layer] > 127 ? 1 : 0;
            }
        }
        return count;
    };

    // the same footprint at four texels a voxel: the same picture, four times the texels across
    const PaintMap fine = resamplePaint(map, glm::ivec2(64, 64), 8);
    QCOMPARE(fine.getSize(), glm::ivec2(512, 512));
    QCOMPARE(fine.getTexelsPerVoxel(), 8);
    QCOMPARE((int)fine.getDirtyColumns().size(), 4);
    QVERIFY(fine.isPainted());
    QCOMPARE((int)fine.at(glm::ivec2(160, 160))[2], 255);
    QVERIFY(std::abs((int)fine.at(glm::ivec2(352, 352))[5] - 128) <= 1);
    for (int k = 0; k < PAINT_LAYERS; ++k) {
        QCOMPARE((int)fine.at(glm::ivec2(400, 100))[k], 0);
    }
    const float grown = (float)painted(fine, 2) / (float)painted(map, 2);
    QVERIFY(grown > 14.5f && grown < 17.5f);   // sixteen times the texels, give or take the edge
    for (int j = 0; j < fine.getSize().y; ++j) {
        for (int i = 0; i < fine.getSize().x; ++i) {
            int sum = 0;
            for (int k = 0; k < PAINT_LAYERS; ++k) {
                sum += fine.at(glm::ivec2(i, j))[k];
            }
            QVERIFY(sum <= 255);
        }
    }

    // another footprint (the terrain resampled to more voxels) at the texel count the editor would pick
    const PaintMap moved = resamplePaint(map, glm::ivec2(100, 100), 3);
    QCOMPARE(moved.getSize(), glm::ivec2(300, 300));
    QCOMPARE((int)moved.at(glm::ivec2(93, 93))[2], 255);   // voxel (20, 20) of 64 is voxel (31.25, 31.25) of 100

    // an unpainted map gives an unpainted one of the size asked
    const PaintMap blank = resamplePaint(PaintMap(glm::ivec2(64, 64), 2), glm::ivec2(32, 32), 1);
    QCOMPARE(blank.getSize(), glm::ivec2(32, 32));
    QVERIFY(!blank.isPainted());
}

// --- chunk levels -------------------------------------------------------------------------------

namespace {

// Where a column's field crosses zero between two heights, read between the voxels: the surface's height there.
float surfaceHeight(const VoxelGrid& grid, float x, float z, float from, float to, float step = 0.25f) {
    float below = grid.fieldAt(glm::vec3(x, from, z)).distance;
    for (float y = from + step; y <= to + 1.0e-4f; y += step) {
        const float above = grid.fieldAt(glm::vec3(x, y, z)).distance;
        if (below < 0.0f && above >= 0.0f) {
            return y - step + step * below / (below - above);
        }
        below = above;
    }
    return -1.0f;
}

// Every chunk touching another differs from it by at most one level.
bool twoToOneRuleHolds(const VoxelGrid& grid) {
    bool holds = true;
    grid.forEachChunk([&](const ChunkKey& key, const Chunk&) {
        const glm::ivec3 origin = key.getOrigin();
        for (const ChunkKey& neighbour : grid.chunksCovering(origin - 1, origin + key.getSpan())) {
            if (std::abs(neighbour.level - key.level) > 1) {
                holds = false;
            }
        }
    });
    return holds;
}

} // namespace

void TerrainTests::chunkKeysNameTheirGridVoxels() {
    const ChunkKey coarse(0, glm::ivec3(1, 2, 3));
    QCOMPARE(coarse.getScale(), 1);
    QCOMPARE(coarse.getSpan(), 32);
    QCOMPARE(coarse.getOrigin(), glm::ivec3(32, 64, 96));
    QCOMPARE(coarse.getSlot(), glm::ivec3(1, 2, 3));
    const ChunkKey child = coarse.getChild(5);   // bits: x and z
    QCOMPARE(child.level, 1);
    QCOMPARE(child.coords, glm::ivec3(3, 4, 7));
    QCOMPARE(child.getSpan(), 16);
    QCOMPARE(child.getOrigin(), glm::ivec3(48, 64, 112));
    QCOMPARE(child.getSlot(), glm::ivec3(1, 2, 3));
    QVERIFY(child.getParent() == coarse);
    const ChunkKey grandchild = child.getChild(7);
    QCOMPARE(grandchild.level, 2);
    QCOMPARE(grandchild.getSpan(), 8);
    QCOMPARE(grandchild.getOrigin(), glm::ivec3(56, 72, 120));
    QVERIFY(grandchild.intersects(glm::ivec3(60, 72, 120), glm::ivec3(60, 72, 120)));
    QVERIFY(grandchild.intersects(glm::ivec3(0), glm::ivec3(56, 79, 127)));
    QVERIFY(!grandchild.intersects(glm::ivec3(64, 72, 120), glm::ivec3(70, 80, 130)));
    QVERIFY(ChunkKey(0, 1, 0, 0) < ChunkKey(1, 0, 0, 0));
    QVERIFY(ChunkKey(1, 0, 0, 0) < ChunkKey(1, 0, 0, 1));
    QVERIFY(ChunkKey(glm::ivec3(2, 0, 0)) == ChunkKey(0, 2, 0, 0));

    VoxelGrid grid(glm::ivec3(70, 40, 50));
    QVERIFY(grid.containsChunk(ChunkKey(0, 2, 1, 1)));
    QVERIFY(!grid.containsChunk(ChunkKey(0, 3, 1, 1)));
    QVERIFY(grid.containsChunk(ChunkKey(2, 11, 7, 7)));    // slot (2, 1, 1)
    QVERIFY(!grid.containsChunk(ChunkKey(2, 12, 7, 7)));   // slot (3, 1, 1)
    QVERIFY(!grid.containsChunk(ChunkKey(MAX_CHUNK_LEVEL + 1, 0, 0, 0)));
    QVERIFY(grid.hasChunk(ChunkKey(0, 2, 1, 1)));
    QVERIFY(!grid.hasChunk(ChunkKey(1, 4, 2, 2)));
    QVERIFY(grid.chunkCovering(glm::ivec3(69, 39, 49)) == ChunkKey(0, 2, 1, 1));
    QVERIFY(grid.chunkCovering(glm::ivec3(-5, 100, 0)) == ChunkKey(0, 0, 1, 0));
    QCOMPARE((int)grid.chunksCovering(glm::ivec3(0), glm::ivec3(69, 39, 49)).size(), 12);
    QCOMPARE((int)grid.chunksCovering(glm::ivec3(31, 0, 0), glm::ivec3(32, 0, 0)).size(), 2);
    QCOMPARE((int)grid.chunksCovering(glm::ivec3(80, 0, 0), glm::ivec3(90, 0, 0)).size(), 1);   // the last chunks reach to 96
    QVERIFY(grid.chunksCovering(glm::ivec3(96, 0, 0), glm::ivec3(99, 0, 0)).empty());
    QCOMPARE(grid.countChunks(), 12);
    QCOMPARE(grid.getFinestLevel(), 0);
    QCOMPARE(grid.getMaxLevel(), MAX_CHUNK_LEVEL);
}

void TerrainTests::splitChunkKeepsTheGroundAndWhatTheNeighboursSample() {
    VoxelGrid grid(glm::ivec3(64, 32, 64));
    grid.fillHeightField(hills, 2);
    const VoxelGrid before = grid;
    const ChunkKey parent(0, 1, 0, 1);
    ChunkSamples neighbourBefore;
    grid.sampleChunk(ChunkKey(0, 0, 0, 1), neighbourBefore);
    const uint64_t structure = grid.getStructureVersion();
    grid.clearDirtyChunks();

    QVERIFY(grid.splitChunk(parent));
    QVERIFY(grid.getStructureVersion() > structure);
    QVERIFY(!grid.hasChunk(parent));
    QVERIFY(!grid.findChunk(parent));
    QCOMPARE(grid.countChunks(), 11);
    QCOMPARE(grid.getFinestLevel(), 1);
    for (int i = 0; i < 8; ++i) {
        QVERIFY(grid.hasChunk(parent.getChild(i)));
        QVERIFY(grid.getDirtyChunks().count(parent.getChild(i)) > 0);
    }
    // the chunks that sample the split one are dirty too, and nothing dirty is a chunk that is gone
    QCOMPARE((int)grid.getDirtyChunks().size(), 11);
    for (const ChunkKey& key : grid.getDirtyChunks()) {
        QVERIFY(grid.hasChunk(key));
    }
    QVERIFY(grid.chunkCovering(glm::ivec3(40, 10, 40)) == ChunkKey(1, 2, 0, 2));
    QVERIFY(grid.chunkCovering(glm::ivec3(50, 10, 40)) == ChunkKey(1, 3, 0, 2));
    QVERIFY(grid.chunkCovering(glm::ivec3(10, 10, 40)) == ChunkKey(0, 0, 0, 1));
    QCOMPARE((int)grid.chunksCovering(glm::ivec3(31, 0, 31), glm::ivec3(48, 0, 48)).size(), 7);
    QVERIFY(!grid.splitChunk(parent));   // gone
    QVERIFY(twoToOneRuleHolds(grid));

    // The ground is where it was: at a child's even voxels the parent's, between them the field between.
    const Chunk& child = grid.getChunk(ChunkKey(1, 2, 0, 2));
    QVERIFY(!child.isUniform());
    for (int z = 0; z < 32; z += 5) {
        for (int y = 0; y < 32; y += 3) {
            for (int x = 0; x < 32; x += 7) {
                const Voxel& fine = child.get(x, y, z);
                const glm::vec3 position(32.0f + x * 0.5f, y * 0.5f, 32.0f + z * 0.5f);
                const VoxelGrid::FieldSample field = before.fieldAt(position);
                if (std::abs(field.distance) < 1.9f) {
                    QVERIFY(std::abs(fine.getDistance() - 2.0f * field.distance) < 0.05f);
                } else {
                    QCOMPARE(fine.isGround(), field.distance < 0.0f);
                }
                QCOMPARE((int)fine.material, 2);
                // and the grid reads the same back, at its own points and at the fine ones
                QVERIFY(grid.sampleAt(1, glm::ivec3(64 + x, y, 64 + z)) == fine);
                if (std::abs(field.distance) < 1.9f) {
                    QVERIFY(std::abs(grid.fieldAt(position).distance - field.distance) < 0.03f);
                }
            }
        }
    }
    for (int x = 33; x < 63; x += 6) {
        QVERIFY(std::abs(surfaceHeight(grid, (float)x, 40.0f, 2.0f, 20.0f) - surfaceHeight(before, (float)x, 40.0f, 2.0f, 20.0f)) < 0.05f);
        QVERIFY(std::abs(surfaceHeight(grid, x + 0.5f, 40.5f, 2.0f, 20.0f) - surfaceHeight(before, x + 0.5f, 40.5f, 2.0f, 20.0f)) < 0.05f);
    }
    // The level-0 neighbour samples the same ground across the join as before, where the byte can say so.
    ChunkSamples neighbourAfter;
    grid.sampleChunk(ChunkKey(0, 0, 0, 1), neighbourAfter);
    int compared = 0;
    for (int z = -1; z <= 32; ++z) {
        for (int y = -1; y <= 32; ++y) {
            const Voxel& was = neighbourBefore.at(32, y, z);
            const Voxel& is = neighbourAfter.at(32, y, z);
            if (std::abs(was.getDistance()) < 1.9f) {
                QVERIFY(was == is);
                ++compared;
            } else {
                QCOMPARE(was.isGround(), is.isGround());
            }
        }
    }
    QVERIFY(compared > 50);
    // and the fine chunk's samples of the coarse neighbour lie between its voxels
    ChunkSamples fineSamples;
    grid.sampleChunk(ChunkKey(1, 2, 0, 2), fineSamples);
    for (int y = 0; y < 32; y += 2) {
        const float coarse0 = before.get(glm::ivec3(31, y / 2, 40)).getDistance();
        const float coarse1 = before.get(glm::ivec3(32, y / 2, 40)).getDistance();
        if (std::abs(coarse0) < 1.9f && std::abs(coarse1) < 1.9f) {
            QVERIFY(std::abs(fineSamples.at(-1, y, 16).getDistance() - (coarse0 + coarse1)) < 0.05f);
            QVERIFY(std::abs(fineSamples.at(0, y, 16).getDistance() - 2.0f * coarse1) < 0.05f);
        }
    }
    // the fine chunk meshes in its own voxels (half grid voxels, from its origin), over the same ground
    ChunkMesh mesh = meshChunk(grid, ChunkKey(1, 2, 0, 2));
    QVERIFY(mesh.getTriangleCount() > 1000);
    for (const glm::vec3& p : mesh.positions) {
        QVERIFY(p.x >= -1.0f && p.x <= 32.0f && p.z >= -1.0f && p.z <= 32.0f);
        const float groundHeight = hills((int)std::round(32.0f + p.x * 0.5f), (int)std::round(32.0f + p.z * 0.5f));
        QVERIFY(std::abs(p.y * 0.5f - groundHeight) < 1.5f);
    }

    // a grid voxel in the split region reads and sets the fine chunk's voxel there
    QVERIFY(grid.get(glm::ivec3(40, 10, 40)) == child.get(16, 20, 16));
    QVERIFY(grid.set(glm::ivec3(40, 10, 40), Voxel::ground(7)));
    QVERIFY(grid.getChunk(ChunkKey(1, 2, 0, 2)).get(16, 20, 16) == Voxel::ground(7));
}

void TerrainTests::refineKeepsTheTwoToOneRule() {
    VoxelGrid grid(glm::ivec3(96, 96, 96));
    grid.fillBelow(50.0f);
    QCOMPARE(grid.countChunks(), 27);

    // A box inside the middle chunk, at its far corner: the level-2 chunk there touches seven other level-0 chunks,
    // which must come to level 1 for it.
    grid.refine(glm::ivec3(60), glm::ivec3(62), 2);
    const std::vector<ChunkKey> covering = grid.chunksCovering(glm::ivec3(60), glm::ivec3(62));
    QCOMPARE((int)covering.size(), 1);
    QVERIFY(covering[0] == ChunkKey(2, 7, 7, 7));
    QVERIFY(grid.hasChunk(ChunkKey(2, 7, 7, 7)));
    QVERIFY(grid.hasChunk(ChunkKey(1, 2, 2, 2)));   // another child of the middle chunk, not split further
    QVERIFY(grid.hasChunk(ChunkKey(1, 4, 2, 2)));   // the chunk beyond, split once for the rule
    QVERIFY(grid.hasChunk(ChunkKey(0, 0, 0, 0)));   // far away, untouched
    QVERIFY(grid.hasChunk(ChunkKey(0, 0, 1, 1)));   // beside the middle chunk on its low side, untouched
    QCOMPARE(grid.countChunks(), 90);
    QVERIFY(twoToOneRuleHolds(grid));
    QCOMPARE(grid.getFinestLevel(), 2);
    // every chunk's mesh is to be remade: the split ones and those that sample them
    for (const ChunkKey& key : grid.getDirtyChunks()) {
        QVERIFY(grid.hasChunk(key));
    }

    // Refining what is fine already changes nothing.
    const uint64_t structure = grid.getStructureVersion();
    grid.refine(glm::ivec3(60), glm::ivec3(62), 2);
    grid.refine(glm::ivec3(60), glm::ivec3(62), 1);
    QCOMPARE(grid.getStructureVersion(), structure);
    QCOMPARE(grid.countChunks(), 90);

    // The grid's own finest level caps the splitting.
    VoxelGrid capped(glm::ivec3(32, 32, 32));
    capped.setMaxLevel(1);
    capped.refine(glm::ivec3(0), glm::ivec3(3), 2);
    QCOMPARE(capped.getFinestLevel(), 1);
    QCOMPARE(capped.countChunks(), 8);
    capped.setMaxLevel(0);
    capped.refine(glm::ivec3(20), glm::ivec3(23), 2);
    QCOMPARE(capped.countChunks(), 8);
    QVERIFY(twoToOneRuleHolds(capped));
    // the whole grid refined to level 2: sixty-four chunks a level-0 chunk
    VoxelGrid whole(glm::ivec3(64, 32, 32));
    whole.refine(glm::ivec3(0), glm::ivec3(63, 31, 31), 2);
    QCOMPARE(whole.countChunks(), 128);
    QCOMPARE(whole.countDenseChunks(), 0);
}

void TerrainTests::smallBrushDigsFineDetailWithoutResamplingTheGrid() {
    VoxelGrid grid(glm::ivec3(96, 96, 96));
    grid.fillBelow(40.5f, 1);
    grid.clearDirtyChunks();
    QCOMPARE(grid.countChunks(), 27);

    // In chunk (1, 1, 1), toward its low corner: one of its eight children is split for the brush, and the
    // level-2 chunks there touch the seven level-0 chunks at that corner, which come to level 1 for them.
    Brush small;
    small.center = glm::vec3(40.0f, 40.5f, 40.0f);
    small.radius = 1.5f;
    QCOMPARE(brushLevel(grid, small), 2);
    Brush big = small;
    big.radius = 8.0f;
    QCOMPARE(brushLevel(grid, big), 0);
    big.radius = 2.5f;
    QCOMPARE(brushLevel(grid, big), 1);
    grid.setMaxLevel(1);
    QCOMPARE(brushLevel(grid, small), 1);
    grid.setMaxLevel(MAX_CHUNK_LEVEL);

    // A small dig: the chunks under the brush go to level 2 and the hole is round at that level.
    const uint64_t structure = grid.getStructureVersion();
    const int changed = sculpt(grid, small, SculptMode::Dig);
    QVERIFY(changed > 0);
    QVERIFY(grid.getStructureVersion() > structure);
    QVERIFY(grid.chunkCovering(glm::ivec3(40, 40, 40)).level == 2);
    QVERIFY(grid.chunkCovering(glm::ivec3(39, 39, 39)).level == 2);
    QVERIFY(grid.chunkCovering(glm::ivec3(45, 40, 40)).level == 2);
    QVERIFY(grid.chunkCovering(glm::ivec3(50, 40, 40)).level == 1);   // another child of chunk (1, 1, 1), not reached
    QVERIFY(grid.chunkCovering(glm::ivec3(5, 10, 5)).level == 1);     // chunk (0, 0, 0): split once for the 2:1 rule
    QVERIFY(grid.chunkCovering(glm::ivec3(80, 10, 80)).level == 0);   // beyond the rule's reach
    QVERIFY(grid.chunkCovering(glm::ivec3(80, 40, 40)).level == 0);
    QCOMPARE(grid.countChunks(), 90);
    QVERIFY(twoToOneRuleHolds(grid));
    QCOMPARE(grid.getVoxelCount(), glm::ivec3(96, 96, 96));   // the grid itself is as it was
    for (const ChunkKey& key : grid.getDirtyChunks()) {
        QVERIFY(grid.hasChunk(key));
    }
    // inside the sphere, below the old surface: air; below the sphere: ground; the floor at the sphere's bottom
    QVERIFY(grid.fieldAt(glm::vec3(40.0f, 39.5f, 40.0f)).distance > 0.0f);
    QVERIFY(grid.fieldAt(glm::vec3(40.0f, 38.5f, 40.0f)).distance < 0.0f);
    QVERIFY(std::abs(surfaceHeight(grid, 40.0f, 40.0f, 37.0f, 40.0f) - 39.0f) < 0.15f);
    // the hole is round: the floor rises toward the rim as a sphere's does
    QVERIFY(std::abs(surfaceHeight(grid, 41.0f, 40.0f, 37.0f, 40.0f) - (40.5f - std::sqrt(1.5f * 1.5f - 1.0f))) < 0.2f);
    QVERIFY(std::abs(surfaceHeight(grid, 42.0f, 40.0f, 37.0f, 41.0f) - 40.5f) < 0.1f);   // outside the hole
    // the fine chunk's mesh has the hole's floor, in quarter voxels (the centre sits on eight chunks' corner)
    const ChunkKey fine = grid.chunkCovering(glm::ivec3(40, 39, 40));
    ChunkMesh mesh = meshChunk(grid, fine);
    QVERIFY(!mesh.isEmpty());
    float lowest = 1000.0f;
    for (const glm::vec3& p : mesh.positions) {
        lowest = std::min(lowest, (float)fine.getOrigin().y + p.y / (float)fine.getScale());
    }
    QVERIFY(std::abs(lowest - 39.0f) < 0.3f);

    // A big brush stays coarse where the ground is coarse and works the fine chunks finely.
    Brush wide;
    wide.center = glm::vec3(80.0f, 40.5f, 80.0f);
    wide.radius = 6.0f;
    const int chunksBefore = grid.countChunks();
    QVERIFY(sculpt(grid, wide, SculptMode::Dig) > 0);
    QCOMPARE(grid.countChunks(), chunksBefore);
    QVERIFY(grid.chunkCovering(glm::ivec3(80, 40, 80)).level == 0);
    wide.center = glm::vec3(44.0f, 40.5f, 44.0f);   // over the fine chunks: worked in their voxels, split no further
    QVERIFY(sculpt(grid, wide, SculptMode::Dig) > 0);
    QCOMPARE(grid.countChunks(), chunksBefore);
    QVERIFY(std::abs(surfaceHeight(grid, 44.0f, 44.0f, 32.0f, 40.0f) - 34.5f) < 0.15f);
    // another step of the small brush splits nothing more
    const uint64_t refined = grid.getStructureVersion();
    small.center.y -= 0.5f;
    QVERIFY(sculpt(grid, small, SculptMode::Dig) > 0);
    QCOMPARE(grid.getStructureVersion(), refined);

    // Raise lifts by the same height whatever the level: a step of strength 1 is one grid voxel.
    VoxelGrid lifted(glm::ivec3(64, 64, 64));
    lifted.fillBelow(40.5f, 1);
    Brush raise;
    raise.center = glm::vec3(32.0f, 40.5f, 32.0f);
    raise.radius = 1.5f;
    raise.hardness = 1.0f;
    raise.strength = 1.0f;
    QVERIFY(sculpt(lifted, raise, SculptMode::Raise) > 0);
    QVERIFY(std::abs(surfaceHeight(lifted, 32.0f, 32.0f, 39.0f, 44.0f) - 41.5f) < 0.1f);
    Brush coarseRaise = raise;
    coarseRaise.center = glm::vec3(10.0f, 40.5f, 10.0f);
    coarseRaise.radius = 8.0f;
    QVERIFY(sculpt(lifted, coarseRaise, SculptMode::Raise) > 0);
    QVERIFY(std::abs(surfaceHeight(lifted, 10.0f, 10.0f, 39.0f, 44.0f) - 41.5f) < 0.1f);
}

void TerrainTests::setChunkAndPlaceChunkReplaceWhatCoversTheKey() {
    VoxelGrid grid(glm::ivec3(32, 32, 32));
    grid.fillBelow(10.3f, 2);
    grid.clearDirtyChunks();

    // A finer chunk into a coarser one's place: the rest of the place is the old ground, resampled.
    grid.setChunk(ChunkKey(1, 1, 1, 1), Chunk(Voxel::ground(4)));
    QVERIFY(grid.hasChunk(ChunkKey(1, 1, 1, 1)));
    QVERIFY(grid.hasChunk(ChunkKey(1, 0, 0, 0)));
    QVERIFY(!grid.hasChunk(ChunkKey(0, 0, 0, 0)));
    QCOMPARE(grid.countChunks(), 8);
    QVERIFY(grid.get(glm::ivec3(20, 20, 20)) == Voxel::ground(4));
    QVERIFY(std::abs(surfaceHeight(grid, 5.0f, 5.0f, 5.0f, 15.0f) - 10.3f) < 0.05f);
    QVERIFY(std::abs(surfaceHeight(grid, 20.0f, 5.0f, 5.0f, 15.0f) - 10.3f) < 0.05f);   // child (1, 0, 0): the surface, resampled
    QCOMPARE((int)grid.getDirtyChunks().size(), 8);
    for (const ChunkKey& key : grid.getDirtyChunks()) {
        QVERIFY(grid.hasChunk(key));
    }

    // A coarser chunk over finer ones: they go.
    grid.clearDirtyChunks();
    grid.setChunk(ChunkKey(0, 0, 0, 0), Chunk(Voxel::air()));
    QCOMPARE(grid.countChunks(), 1);
    QVERIFY(grid.hasChunk(ChunkKey(0, 0, 0, 0)));
    QVERIFY(!grid.hasChunk(ChunkKey(1, 1, 1, 1)));
    QCOMPARE(grid.getDirtyChunks(), ChunkKeySet({ ChunkKey(0, 0, 0, 0) }));
    QVERIFY(grid.get(glm::ivec3(5, 5, 5)) == Voxel::air());

    // placeChunk fills the rest of a split place with the old chunk's first voxel instead: for sets that cover it.
    grid.fillBelow(10.3f, 2);
    grid.placeChunk(ChunkKey(2, 0, 0, 0), Chunk(Voxel::ground(6)));
    QCOMPARE(grid.countChunks(), 15);
    QVERIFY(grid.hasChunk(ChunkKey(2, 0, 0, 0)));
    QVERIFY(grid.getChunk(ChunkKey(2, 1, 0, 0)).isUniform());
    QVERIFY(grid.getChunk(ChunkKey(1, 1, 0, 0)).isUniform());
    QVERIFY(grid.getChunk(ChunkKey(1, 1, 0, 0)).getUniformValue() == grid.getChunk(ChunkKey(2, 1, 0, 0)).getUniformValue());
    for (int i = 1; i < 8; ++i) {
        grid.placeChunk(ChunkKey(2, 0, 0, 0).getParent().getChild(i), Chunk(Voxel::ground(6)));
        grid.placeChunk(ChunkKey(1, 0, 0, 0).getParent().getChild(i), Chunk(Voxel::air()));
    }
    QCOMPARE(grid.countChunks(), 15);
    QVERIFY(grid.get(glm::ivec3(10, 10, 10)) == Voxel::ground(6));
    QVERIFY(grid.get(glm::ivec3(20, 10, 10)) == Voxel::air());
    QVERIFY(grid.get(glm::ivec3(10, 20, 10)) == Voxel::air());

    // Chunks of the same ground compare equal, spelled out or not.
    Chunk dense(Voxel::air());
    dense.set(1, 1, 1, Voxel::ground());
    dense.set(1, 1, 1, Voxel::air());
    QVERIFY(!dense.isUniform());
    QVERIFY(dense == Chunk(Voxel::air()));
    QVERIFY(Chunk(Voxel::air()) == dense);
    QVERIFY(dense != Chunk(Voxel::ground()));
    dense.set(2, 2, 2, Voxel::ground(3));
    QVERIFY(dense != Chunk(Voxel::air()));
    Chunk other = dense;
    QVERIFY(other == dense);
    other.set(2, 2, 2, Voxel::ground(4));
    QVERIFY(other != dense);

    // A copy of a grid with levels is its own: changing one leaves the other.
    VoxelGrid copy = grid;
    copy.setChunk(ChunkKey(0, 0, 0, 0), Chunk(Voxel::air()));
    QCOMPARE(copy.countChunks(), 1);
    QCOMPARE(grid.countChunks(), 15);
    QVERIFY(grid.get(glm::ivec3(10, 10, 10)) == Voxel::ground(6));
}

void TerrainTests::fileRoundTripKeepsChunkLevels() {
    VoxelGrid grid(glm::ivec3(70, 40, 50));
    grid.fillHeightField(hills, 1);
    Brush brush;
    brush.center = glm::vec3(20.0f, 10.0f, 20.0f);
    brush.radius = 1.5f;
    sculpt(grid, brush, SculptMode::Dig);
    brush.center = glm::vec3(40.0f, 10.0f, 30.0f);
    brush.radius = 2.5f;
    brush.material = 3;
    sculpt(grid, brush, SculptMode::Fill);
    QCOMPARE(grid.getFinestLevel(), 2);
    QVERIFY(grid.countChunks() > 12);

    Bytes bytes = saveTerrain(grid);
    QCOMPARE((int)bytes[4], 3);   // the version: chunks with levels
    VoxelGrid loaded;
    PaintMap loadedPaint;
    std::string error;
    QVERIFY2(loadTerrain(bytes, loaded, &loadedPaint, &error), error.c_str());
    QVERIFY(loadedPaint.isEmpty());   // none was written
    QCOMPARE(loaded.getVoxelCount(), grid.getVoxelCount());
    QCOMPARE(loaded.countChunks(), grid.countChunks());
    QCOMPARE(loaded.countDenseChunks(), grid.countDenseChunks());
    std::vector<std::pair<ChunkKey, const Chunk*>> saved, read;
    grid.forEachChunk([&](const ChunkKey& key, const Chunk& chunk) { saved.emplace_back(key, &chunk); });
    loaded.forEachChunk([&](const ChunkKey& key, const Chunk& chunk) { read.emplace_back(key, &chunk); });
    QCOMPARE(saved.size(), read.size());
    for (size_t i = 0; i < saved.size(); ++i) {
        QVERIFY(saved[i].first == read[i].first);
        QVERIFY(*saved[i].second == *read[i].second);
    }
    QCOMPARE((int)loaded.getDirtyChunks().size(), loaded.countChunks());
    QVERIFY(twoToOneRuleHolds(loaded));

    // With a painted map the map rides along as in version 2.
    PaintMap paint(glm::ivec2(70, 50), 2);
    PaintBrush paintBrush;
    paintBrush.center = glm::vec2(20.0f, 20.0f);
    paintBrush.radius = 5.0f;
    paintBrush.strength = 1.0f;
    paintBrush.layer = 3;
    QVERIFY(terrain::paint(paint, paintBrush, false) > 0);
    Bytes painted = saveTerrain(grid, &paint);
    QCOMPARE((int)painted[4], 3);
    QVERIFY(painted.size() > bytes.size());
    QVERIFY2(loadTerrain(painted, loaded, &loadedPaint, &error), error.c_str());
    QCOMPARE(loaded.countChunks(), grid.countChunks());
    QVERIFY(loadedPaint.getWeights() == paint.getWeights());

    // A file of level-0 chunks is still version 1, and a cut file fails cleanly.
    VoxelGrid flat(glm::ivec3(70, 40, 50));
    flat.fillHeightField(hills, 1);
    QCOMPARE((int)saveTerrain(flat)[4], 1);
    Bytes cut(bytes.begin(), bytes.begin() + bytes.size() * 2 / 3);
    QVERIFY(!loadTerrain(cut, loaded, &loadedPaint, &error));
    QVERIFY(!error.empty());
    QVERIFY(loaded.isEmpty());
    // a level the reader does not know is refused
    Bytes tooFine = bytes;
    tooFine[4 + 4 + 12 + 4 + 4 + 12] = (uint8_t)(MAX_CHUNK_LEVEL + 1);   // the first record's level byte
    QVERIFY(!loadTerrain(tooFine, loaded, &loadedPaint, &error));
    QVERIFY(error.find("level") != std::string::npos);
}

void TerrainTests::resampleReadsTheFineChunks() {
    VoxelGrid source(glm::ivec3(64, 64, 64));
    source.fillBelow(40.5f, 1);
    Brush brush;
    brush.center = glm::vec3(32.0f, 40.5f, 32.0f);
    brush.radius = 1.5f;
    brush.material = 2;
    QVERIFY(sculpt(source, brush, SculptMode::Dig) > 0);
    QCOMPARE(source.getFinestLevel(), 2);

    // Twice the voxels, all at level 0: the fine hole is read from the fine chunks, not from the grid points between.
    const VoxelGrid fine = resampleGrid(source, glm::ivec3(128, 128, 128));
    QCOMPARE(fine.getFinestLevel(), 0);
    QCOMPARE(fine.countChunks(), 64);
    QVERIFY(std::abs(surfaceHeight(fine, 64.0f, 64.0f, 74.0f, 80.0f) - 78.0f) < 0.3f);
    QVERIFY(std::abs(surfaceHeight(fine, 66.0f, 64.0f, 74.0f, 80.0f) - 2.0f * (40.5f - std::sqrt(1.5f * 1.5f - 1.0f))) < 0.4f);
    QVERIFY(std::abs(surfaceHeight(fine, 70.0f, 64.0f, 74.0f, 84.0f) - 81.0f) < 0.2f);
    // Inside the sphere below the old surface the field is the sphere's: radius less the distance from the centre.
    // At these points, between the source's grid voxels, the fine chunks say so and the grid voxels alone would not.
    const glm::vec3 offsets[] = { glm::vec3(0.5f, -1.0f, 0.0f), glm::vec3(-0.5f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.5f),
                                  glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(1.0f, -0.5f, 0.0f), glm::vec3(0.5f, -1.0f, 0.5f) };
    for (const glm::vec3& offset : offsets) {
        const float expected = brush.radius - glm::length(offset);
        QVERIFY(std::abs(source.fieldAt(brush.center + offset).distance - expected) < 0.03f);
        QVERIFY(std::abs(fine.fieldAt(2.0f * (brush.center + offset)).distance - 2.0f * expected) < 0.08f);
    }
    // and half the voxels: the hole is too small to keep, but the ground around it is where it was
    const VoxelGrid coarse = resampleGrid(source, glm::ivec3(32, 32, 32));
    QCOMPARE(coarse.countChunks(), 1);
    QVERIFY(std::abs(surfaceHeight(coarse, 5.0f, 5.0f, 15.0f, 25.0f) - 20.25f) < 0.1f);
}
