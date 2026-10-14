// <!
//  Created by Bradley Austin Davis on 2018/05/25
//  Copyright 2013-2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
// !>

// <@if not ENTITIES_SHADER_CONSTANTS_H@>
// <@def ENTITIES_SHADER_CONSTANTS_H@>

// Hack comment to absorb the extra '//' scribe prepends

#ifndef ENTITIES_SHADER_CONSTANTS_H
#define ENTITIES_SHADER_CONSTANTS_H

// Polyvox
#define ENTITIES_TEXTURE_POLYVOX_XMAP 0
#define ENTITIES_TEXTURE_POLYVOX_YMAP 1
#define ENTITIES_TEXTURE_POLYVOX_ZMAP 2

// Terrain: the layers' colour textures take the material texture slots, the chunk's paint map tiles (layers 0 to 3
// and 4 to 7 of the weights) the two after them, the layers' normal and mask arrays (a slice per layer) at 12 and
// 13 (the forward variant's lighting takes 11 and 14), the terrain's parameters the material buffer slot and the chunk's own (where its tile is read) the
// slot after, both of which the terrain's own pipeline leaves free (the backend has fourteen buffer slots, 0 to 13;
// shadow passes bind their parameters at 2 with a pipeline of their own); an item with a material binds its own
// there before it draws, so a terrain chunk's leftover never reaches one. The slot after is the parameters of the
// draw that fills a slice of the arrays (terrain_layer_maps.slf, a pipeline of its own).
#define ENTITIES_TEXTURE_TERRAIN_LAYER0 0
#define ENTITIES_TEXTURE_TERRAIN_LAYER1 1
#define ENTITIES_TEXTURE_TERRAIN_LAYER2 2
#define ENTITIES_TEXTURE_TERRAIN_LAYER3 3
#define ENTITIES_TEXTURE_TERRAIN_LAYER4 4
#define ENTITIES_TEXTURE_TERRAIN_LAYER5 5
#define ENTITIES_TEXTURE_TERRAIN_LAYER6 6
#define ENTITIES_TEXTURE_TERRAIN_LAYER7 7
#define ENTITIES_TEXTURE_TERRAIN_PAINT0 8
#define ENTITIES_TEXTURE_TERRAIN_PAINT1 9
#define ENTITIES_TEXTURE_TERRAIN_NORMALS 12   // past the forward variant's skybox (11); its ambient LUT is at 14
#define ENTITIES_TEXTURE_TERRAIN_MASKS 13
#define ENTITIES_BUFFER_TERRAIN_LAYERS 1
#define ENTITIES_BUFFER_TERRAIN_CHUNK 2
#define ENTITIES_BUFFER_TERRAIN_MAPS 3



// <!

namespace entities_renderer { namespace slot {

namespace texture {
enum Texture {
    PolyvoxXMap = ENTITIES_TEXTURE_POLYVOX_XMAP,
    PolyvoxYMap = ENTITIES_TEXTURE_POLYVOX_YMAP,
    PolyvoxZMap = ENTITIES_TEXTURE_POLYVOX_ZMAP,
    TerrainLayer0 = ENTITIES_TEXTURE_TERRAIN_LAYER0,
    TerrainPaint0 = ENTITIES_TEXTURE_TERRAIN_PAINT0,
    TerrainNormals = ENTITIES_TEXTURE_TERRAIN_NORMALS,
    TerrainMasks = ENTITIES_TEXTURE_TERRAIN_MASKS,
};
} // namespace texture

namespace buffer {
enum Buffer {
    TerrainLayers = ENTITIES_BUFFER_TERRAIN_LAYERS,
    TerrainChunk = ENTITIES_BUFFER_TERRAIN_CHUNK,
    TerrainMaps = ENTITIES_BUFFER_TERRAIN_MAPS,
};
} // namespace buffer

} } // namespace entities::slot

// !>
// Hack Comment

#endif // ENTITIES_SHADER_CONSTANTS_H

// <@if 1@>
// Trigger Scribe include
// <@endif@> <!def that !>

// <@endif@>

// Hack Comment
