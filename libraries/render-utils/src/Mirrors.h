//
//  Mirrors.h
//  libraries/render-utils/src
//
//  The scene's mirrors: a shape whose userData has "mirror": true shows, on its local +Z face, what the viewer would
//  see reflected in it. RenderMirrorsTask draws that picture before the main view, from a camera behind the face at
//  the viewer's reflection, looking back through the face with a frustum fitted to the face's rectangle (so the
//  face itself is the near plane and nothing behind the mirror gets in). The mirror's material shows the picture
//  as an unlit albedo map, flipped the way a mirror flips. There are a few slots a frame; they go to the mirrors in
//  view that take the most of it, and a mirror keeps its slot while it is drawn. The picture is sized by the
//  mirror's size on screen, scaled by a setting, so a small mirror far away costs little.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Mirrors_h
#define hifi_Mirrors_h

#include <array>
#include <map>
#include <mutex>

#include <QtCore/QObject>
#include <QtCore/QUuid>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <gpu/Framebuffer.h>
#include <gpu/Texture.h>
#include <ViewFrustum.h>

class Mirrors {
public:
    static const int SLOT_COUNT = 4;    // mirrors drawn in a frame at most

    // a mirror as its entity stands
    struct Surface {
        glm::vec3 position;      // world
        glm::quat orientation;   // world
        glm::vec3 dimensions;    // scaled: x and y the face, z the thickness
    };

    struct Slot {
        QUuid mirror;                          // the mirror it is drawing; null: idle
        bool active { false };                 // drawn this frame
        ViewFrustum frustum;                   // the camera behind the face, when active
        gpu::FramebufferPointer framebuffer;   // the picture
    };

    static Mirrors& instance();

    // from the entity renderers, on their thread
    void setSurface(const QUuid& id, const Surface& surface);
    void removeSurface(const QUuid& id);
    // the picture a mirror's material shows this frame (null: it has no slot), and a count that moves whenever the
    // picture changes (a slot taken or lost, a picture resized), so a renderer knows when to take it again
    gpu::TexturePointer textureFor(const QUuid& id, uint32_t& generation) const;
    uint32_t generation(const QUuid& id) const;

    // the render engine's config of a slot's view: switched off while the slot idles, so nothing is drawn for it
    void setSlotView(int slot, QObject* viewConfig) { _slotViews[slot] = viewConfig; }
    // from the render task, once a frame before the slots are drawn: gives the frame's slots to the mirrors in view
    void plan(const ViewFrustum& view, const glm::ivec2& viewport);
    const Slot& slot(int index) const { return _slots[index]; }
    // where an idle slot's view goes: a few texels (the half-resolution passes need a size)
    const gpu::FramebufferPointer& idleFramebuffer();

    bool isEnabled() const { return _enabled; }
    void setEnabled(bool enabled) { _enabled = enabled; }
    // the picture's texels per pixel of the mirror on screen: 1 (the default) is crisp, 0.5 costs a quarter
    float getResolutionScale() const { return _resolutionScale; }
    void setResolutionScale(float scale) { _resolutionScale = glm::clamp(scale, 0.125f, 2.0f); }
    int getMaxActive() const { return _maxActive; }
    void setMaxActive(int count) { _maxActive = glm::clamp(count, 0, SLOT_COUNT); }
    int getMaxPixels() const { return _maxPixels; }
    void setMaxPixels(int pixels) { _maxPixels = glm::clamp(pixels, 128, 4096); }

private:
    struct Entry {
        Surface surface;
        int slot { -1 };
        uint32_t generation { 1 };
    };
    // the camera and the picture size for a mirror seen from the view; false when it is out of view, edge-on, or
    // the viewer is behind it
    bool cameraFor(const Surface& surface, const ViewFrustum& view, const glm::ivec2& viewport, ViewFrustum& camera,
                   glm::ivec2& pixels, float& area) const;

    mutable std::mutex _mutex;
    std::map<QUuid, Entry> _entries;
    std::array<Slot, SLOT_COUNT> _slots;
    std::array<QObject*, SLOT_COUNT> _slotViews {};
    gpu::FramebufferPointer _idle;
    bool _enabled { true };
    float _resolutionScale { 1.0f };   // a texel a pixel; 0.5 costs a quarter, for a cheaper mirror
    int _maxActive { SLOT_COUNT };
    int _maxPixels { 2048 };
};

#endif // hifi_Mirrors_h
