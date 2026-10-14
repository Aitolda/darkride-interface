//
//  Layers.cpp
//  libraries/terrain/src/terrain
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Layers.h"

using namespace terrain;

bool Layer::operator==(const Layer& other) const {
    return slot == other.slot && name == other.name && materialURL == other.materialURL && color == other.color
        && tint == other.tint && tiling == other.tiling && offset == other.offset && opacity == other.opacity && blend == other.blend
        && visible == other.visible && normalStrength == other.normalStrength;
}

bool LayerStack::isValid() const {
    if (layers.size() > (size_t)MAX_LAYERS) {
        return false;
    }
    bool taken[MAX_LAYERS] = { false };
    for (const Layer& layer : layers) {
        if (layer.slot >= MAX_LAYERS || taken[layer.slot]) {
            return false;
        }
        taken[layer.slot] = true;
    }
    return true;
}

const Layer* LayerStack::find(uint8_t slot) const {
    const int index = indexOf(slot);
    return index >= 0 ? &layers[index] : nullptr;
}

Layer* LayerStack::find(uint8_t slot) {
    const int index = indexOf(slot);
    return index >= 0 ? &layers[index] : nullptr;
}

int LayerStack::indexOf(uint8_t slot) const {
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].slot == slot) {
            return (int)i;
        }
    }
    return -1;
}

int LayerStack::freeSlot() const {
    for (int slot = 0; slot < MAX_LAYERS; ++slot) {
        if (indexOf((uint8_t)slot) < 0) {
            return slot;
        }
    }
    return -1;
}

std::vector<uint8_t> LayerStack::order() const {
    std::vector<uint8_t> slots;
    slots.reserve(layers.size());
    for (const Layer& layer : layers) {
        slots.push_back(layer.slot);
    }
    return slots;
}

LayerStack LayerStack::defaults() {
    struct Default {
        const char* name;
        const char* file;
        glm::vec3 color;
        float tiling;
    };
    // the textures live in the client's resources (interface/resources/terrain/materials)
    static const Default DEFAULTS[MAX_LAYERS] = {
        { "Grass", "grass.jpg", glm::vec3(0.33f, 0.52f, 0.22f), 4.0f },
        { "Dirt", "dirt.jpg", glm::vec3(0.42f, 0.30f, 0.18f), 3.0f },
        { "Rock", "rock.jpg", glm::vec3(0.50f, 0.50f, 0.50f), 6.0f },
        { "Sand", "sand.jpg", glm::vec3(0.78f, 0.70f, 0.50f), 3.0f },
        { "Gravel", "gravel.jpg", glm::vec3(0.55f, 0.52f, 0.48f), 2.0f },
        { "Mud", "mud.jpg", glm::vec3(0.36f, 0.28f, 0.20f), 3.0f },
        { "Moss", "moss.jpg", glm::vec3(0.24f, 0.40f, 0.20f), 2.0f },
        { "Snow", "snow.jpg", glm::vec3(0.92f, 0.93f, 0.95f), 4.0f }
    };
    LayerStack stack;
    for (int slot = 0; slot < MAX_LAYERS; ++slot) {
        Layer layer;
        layer.slot = (uint8_t)slot;
        layer.name = DEFAULTS[slot].name;
        layer.materialURL = std::string("qrc:///terrain/materials/") + DEFAULTS[slot].file;
        layer.color = DEFAULTS[slot].color;
        layer.tiling = DEFAULTS[slot].tiling;
        stack.layers.push_back(layer);
    }
    return stack;
}
