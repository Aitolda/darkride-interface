//
//  TerrainTests.h
//  tests/terrain/src
//
//  The terrain library: chunks, the voxel grid and its levels of chunks, surface-nets meshing (on this thread and on
//  a pool), brushes, the paint map, resampling and the terrain file.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TerrainTests_h
#define hifi_TerrainTests_h

#include <QtTest/QtTest>

class TerrainTests : public QObject {
    Q_OBJECT
private slots:
    void chunkStaysUniformUntilAVoxelDiffers();
    void gridFillsBelowAHeightAndDirtiesNeighboursAcrossChunkFaces();
    void flatGroundMeshesToOneLevelPlane();
    void neighbouringChunksMeetWithoutASeam();
    void trianglesFaceTheAirLikeTheirNormals();
    void brushesDigFillAndRaise();
    void fileRoundTripKeepsEveryVoxel();
    void asyncMesherMatchesTheMesherAndRedoesChunksEditedMeanwhile();
    void paintMapPaintsErasesTilesAndRoundTripsThroughTheFile();
    void resampledGridKeepsTheSurfaceAndItsMaterials();
    void resampledPaintMapKeepsItsPaint();
    void chunkKeysNameTheirGridVoxels();
    void splitChunkKeepsTheGroundAndWhatTheNeighboursSample();
    void refineKeepsTheTwoToOneRule();
    void smallBrushDigsFineDetailWithoutResamplingTheGrid();
    void chunksOfDifferentLevelsMeetWithoutACrack();
    void setChunkAndPlaceChunkReplaceWhatCoversTheKey();
    void fileRoundTripKeepsChunkLevels();
    void resampleReadsTheFineChunks();
};

#endif // hifi_TerrainTests_h
