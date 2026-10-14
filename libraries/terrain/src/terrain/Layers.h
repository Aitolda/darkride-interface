//
//  Layers.h
//  libraries/terrain/src/terrain
//
//  A terrain's material layers (TERRAIN.md section 5): what its eight material indices look like, and how the paint
//  map's coverage of each is drawn. A layer stands for one slot, the voxel material index and the paint map plane of
//  that number, for as long as it exists; the stack lists the layers bottom to top in drawing order, and reordering
//  or renaming them changes nothing in the voxels or the map. Wherever the ground faces up the shader draws the
//  stack over the voxel material as a painter would: each layer's painted coverage, times its opacity, lays its
//  colour over what is below by its blend mode, so painting a layer covers without taking anything from the layers
//  under it, and a layer turned down or off shows them again. The stack is kept in the terrain file with the chunks
//  and the map (TerrainFile.h, version 4); a terrain without one draws the default stack.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_terrain_Layers_h
#define hifi_terrain_Layers_h

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace terrain {

const int MAX_LAYERS = 8;   // the slots: a voxel material index, a paint map plane

// How a layer's colour meets what is drawn below it where it is painted.
enum class LayerBlend : uint8_t {
    Normal = 0,     // covers
    Multiply = 1,   // darkens: the product
    Screen = 2,     // lightens: one less the product of the complements
    Overlay = 3     // contrast: multiply where the ground is dark, screen where it is light
};
const int LAYER_BLEND_COUNT = 4;

struct Layer {
    uint8_t slot { 0 };                     // which material index and paint plane this is, 0 to 7
    std::string name;
    std::string materialURL;                // the colour texture; empty draws the plain colour
    glm::vec3 color { 0.5f, 0.5f, 0.5f };   // the plain colour, drawn without a texture or until it is in
    glm::vec3 tint { 1.0f, 1.0f, 1.0f };    // multiplies the texture
    float tiling { 4.0f };                  // metres per repeat of the texture
    glm::vec2 offset { 0.0f, 0.0f };        // metres the texture is shifted along the terrain's x and z
    float opacity { 1.0f };                 // 0 to 1: how much of its painted coverage shows
    LayerBlend blend { LayerBlend::Normal };
    bool visible { true };                  // off: its paint is not drawn (the voxel material still is)
    float normalStrength { 1.0f };          // 0 turns the normal map off, once normal maps are drawn

    bool operator==(const Layer& other) const;
    bool operator!=(const Layer& other) const { return !(*this == other); }
};

// The layers bottom to top in drawing order, each slot at most once.
struct LayerStack {
    std::vector<Layer> layers;

    bool isEmpty() const { return layers.empty(); }
    // Whether it is well formed: at most MAX_LAYERS layers, slots below MAX_LAYERS and no slot twice.
    bool isValid() const;
    // The layer of a slot, or null; its index in the stack, or -1.
    const Layer* find(uint8_t slot) const;
    Layer* find(uint8_t slot);
    int indexOf(uint8_t slot) const;
    // The lowest slot no layer has, or -1 with all eight taken.
    int freeSlot() const;
    // The slots bottom to top.
    std::vector<uint8_t> order() const;

    bool operator==(const LayerStack& other) const { return layers == other.layers; }
    bool operator!=(const LayerStack& other) const { return !(*this == other); }

    // The eight layers every terrain starts with, in slot order: grass, dirt, rock, sand, gravel, mud, moss and
    // snow, each a texture the client carries (CC0, from ambientCG).
    static LayerStack defaults();
};

} // namespace terrain

#endif // hifi_terrain_Layers_h
