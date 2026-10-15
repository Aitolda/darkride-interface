//
//  Mirrors.cpp
//  libraries/render-utils/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Mirrors.h"

#include "RenderUtilsLogging.h"

#include <algorithm>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

namespace {
    // the projection with its near plane moved onto a plane given in camera space (Lengyel's oblique near plane):
    // the camera lies on the plane's negative side, and only the positive side is drawn
    glm::mat4 obliqueProjection(const glm::mat4& projection, const glm::vec4& plane) {
        glm::vec4 q = glm::inverse(projection) * glm::vec4(glm::sign(plane.x), glm::sign(plane.y), 1.0f, 1.0f);
        glm::vec4 c = plane * (2.0f / glm::dot(plane, q));
        glm::mat4 result = projection;
        result[0][2] = c.x;
        result[1][2] = c.y;
        result[2][2] = c.z + 1.0f;
        result[3][2] = c.w;
        return result;
    }
}

Mirrors& Mirrors::instance() {
    static Mirrors mirrors;
    return mirrors;
}

void Mirrors::setSurface(const QUuid& id, const Surface& surface) {
    std::lock_guard<std::mutex> lock(_mutex);
    _entries[id].surface = surface;
}

void Mirrors::removeSurface(const QUuid& id) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto found = _entries.find(id);
    if (found == _entries.end()) {
        return;
    }
    if (found->second.slot >= 0 && _slots[found->second.slot].mirror == id) {
        _slots[found->second.slot].mirror = QUuid();
        _slots[found->second.slot].active = false;
    }
    _entries.erase(found);
}

gpu::TexturePointer Mirrors::textureFor(const QUuid& id, uint32_t& generation) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto found = _entries.find(id);
    if (found == _entries.end()) {
        generation = 0;
        return nullptr;
    }
    generation = found->second.generation;
    int slot = found->second.slot;
    if (slot < 0 || _slots[slot].mirror != id || !_slots[slot].framebuffer) {
        return nullptr;
    }
    return _slots[slot].framebuffer->getRenderBuffer(0);
}

uint32_t Mirrors::generation(const QUuid& id) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto found = _entries.find(id);
    return found == _entries.end() ? 0 : found->second.generation;
}

const gpu::FramebufferPointer& Mirrors::idleFramebuffer() {
    if (!_idle) {
        _idle = gpu::FramebufferPointer(gpu::Framebuffer::create("mirrorIdle", gpu::Element::COLOR_SRGBA_32, 8, 8));
    }
    return _idle;
}

bool Mirrors::cameraFor(const Surface& surface, const ViewFrustum& view, const glm::ivec2& viewport, ViewFrustum& camera,
                        glm::ivec2& pixels, float& area) const {
    glm::vec2 half(surface.dimensions.x * 0.5f, surface.dimensions.y * 0.5f);
    float front = surface.dimensions.z * 0.5f;   // the face's distance from the entity's centre, along +Z
    if (half.x <= 0.0f || half.y <= 0.0f) {
        return false;
    }
    glm::mat4 worldFromMirror = glm::translate(surface.position) * glm::mat4_cast(surface.orientation);
    glm::mat4 mirrorFromWorld = glm::inverse(worldFromMirror);
    glm::vec3 viewer = glm::vec3(mirrorFromWorld * glm::vec4(view.getPosition(), 1.0f));
    float distance = viewer.z - front;   // the viewer's distance in front of the face
    if (distance < 0.01f) {
        return false;   // behind the mirror, or in the glass
    }

    // in view at all?
    glm::vec3 center = glm::vec3(worldFromMirror * glm::vec4(0.0f, 0.0f, front, 1.0f));
    if (!view.sphereIntersectsFrustum(center, glm::length(half))) {
        return false;
    }

    // its size on screen: the face's corners projected; a corner behind the eye makes it the whole view
    glm::mat4 cameraFromWorld = glm::inverse(glm::translate(view.getPosition()) * glm::mat4_cast(view.getOrientation()));
    glm::mat4 clipFromMirror = view.getProjection() * cameraFromWorld * worldFromMirror;
    glm::vec3 corners[4] = { { -half.x, -half.y, front }, { half.x, -half.y, front }, { half.x, half.y, front }, { -half.x, half.y, front } };
    glm::vec2 low(1.0f);
    glm::vec2 high(-1.0f);
    bool behind = false;
    for (const auto& corner : corners) {
        glm::vec4 clip = clipFromMirror * glm::vec4(corner, 1.0f);
        if (clip.w <= 0.001f) {
            behind = true;
            break;
        }
        glm::vec2 ndc = glm::clamp(glm::vec2(clip) / clip.w, glm::vec2(-1.0f), glm::vec2(1.0f));
        low = glm::min(low, ndc);
        high = glm::max(high, ndc);
    }
    glm::vec2 sizeOnScreen = behind ? glm::vec2(viewport) : glm::max(high - low, glm::vec2(0.0f)) * 0.5f * glm::vec2(viewport);
    area = sizeOnScreen.x * sizeOnScreen.y;
    if (area < 4.0f) {
        return false;
    }
    if (surface.mode == Mode::Screen) {
        // the main view reflected in the face's plane: the picture is the whole view, a pixel a pixel. The
        // reflected camera's axes are reflected too, which makes a left-handed camera; its right is turned round to
        // keep a proper one, and the glass flips the picture back across when it shows it. The near plane is moved
        // onto the glass, so nothing behind the mirror is drawn.
        glm::vec3 normal = surface.orientation * glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 onFace = surface.position + normal * front;
        auto reflectPoint = [&](const glm::vec3& p) { return p - 2.0f * glm::dot(p - onFace, normal) * normal; };
        auto reflectDirection = [&](const glm::vec3& d) { return d - 2.0f * glm::dot(d, normal) * normal; };
        glm::vec3 forward = reflectDirection(view.getDirection());
        glm::vec3 up = reflectDirection(view.getUp());
        glm::vec3 right = glm::cross(forward, up);
        camera.setPosition(reflectPoint(view.getPosition()));
        camera.setOrientation(glm::quat_cast(glm::mat3(right, up, -forward)));
        glm::mat4 cameraFromWorld = glm::inverse(glm::translate(camera.getPosition()) * glm::mat4_cast(camera.getOrientation()));
        glm::vec3 normalInCamera = glm::normalize(glm::mat3(cameraFromWorld) * normal);
        glm::vec3 onFaceInCamera = glm::vec3(cameraFromWorld * glm::vec4(onFace, 1.0f));
        glm::vec4 plane(normalInCamera, -glm::dot(normalInCamera, onFaceInCamera));   // the viewer's side is positive
        // the planes, near and far from the view's own projection (the oblique one's far corners land behind the
        // eye); the oblique projection only to draw with
        camera.setProjection(view.getProjection());
        camera.calculate();
        camera.setObliqueProjection(obliqueProjection(view.getProjection(), plane));
        pixels = viewport;
        return true;
    }

    // the picture: the mirror's size on screen (scaled), a texel a pixel, within bounds
    glm::vec2 wanted = glm::clamp(sizeOnScreen * _resolutionScale, glm::vec2(32.0f), glm::vec2((float)_maxPixels));
    pixels = glm::ivec2(glm::round(wanted));

    // the camera: the viewer reflected in the face, looking back through it, with the face's rectangle as its near
    // plane; it is turned half round so that its right is the face's -X, which keeps the image a proper one (no
    // mirrored winding to draw with); the material flips it across to show it as a mirror does
    glm::vec3 eye(viewer.x, viewer.y, 2.0f * front - viewer.z);
    camera.setPosition(glm::vec3(worldFromMirror * glm::vec4(eye, 1.0f)));
    camera.setOrientation(surface.orientation * glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)));
    // the near plane a hair past the glass, so the face itself (which lies exactly at the distance) is clipped
    // away rather than drawn over the whole picture; the rectangle scales with it
    float near = distance * 1.002f + 0.002f;
    float scale = near / distance;
    camera.setProjection(glm::frustum((eye.x - half.x) * scale, (eye.x + half.x) * scale, (-half.y - eye.y) * scale,
                                      (half.y - eye.y) * scale, near, view.getFarClip()));
    camera.calculate();
    return true;
}

void Mirrors::plan(const ViewFrustum& view, const glm::ivec2& viewport) {
    std::lock_guard<std::mutex> lock(_mutex);
    struct Want {
        QUuid id;
        ViewFrustum camera;
        glm::ivec2 pixels;
        float area;
    };
    std::vector<Want> wants;
    if (_enabled && _maxActive > 0) {
        for (const auto& entry : _entries) {
            Want want;
            if (cameraFor(entry.second.surface, view, viewport, want.camera, want.pixels, want.area)) {
                want.id = entry.first;
                wants.push_back(want);
            }
        }
    }
    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.area > b.area; });
    if ((int)wants.size() > _maxActive) {
        wants.resize(_maxActive);
    }

    // a mirror keeps the slot it had; the slots of mirrors no longer wanted free up
    std::array<bool, SLOT_COUNT> taken {};
    for (const auto& want : wants) {
        int slot = _entries[want.id].slot;
        if (slot >= 0 && _slots[slot].mirror == want.id) {
            taken[slot] = true;
        }
    }
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (taken[i]) {
            continue;
        }
        if (!_slots[i].mirror.isNull()) {
            auto found = _entries.find(_slots[i].mirror);
            if (found != _entries.end() && found->second.slot == i) {
                found->second.slot = -1;
                ++found->second.generation;
            }
            _slots[i].mirror = QUuid();
        }
        _slots[i].active = false;
    }
    for (const auto& want : wants) {
        Entry& entry = _entries[want.id];
        if (entry.slot < 0 || _slots[entry.slot].mirror != want.id) {
            int free = (int)(std::find(taken.begin(), taken.end(), false) - taken.begin());
            if (free >= SLOT_COUNT) {
                continue;
            }
            taken[free] = true;
            entry.slot = free;
            ++entry.generation;
            _slots[free].mirror = want.id;
        }
        Slot& slot = _slots[entry.slot];
        slot.active = true;
        slot.frustum = want.camera;
        // the picture is made again only when its size on screen has moved by more than a tenth either way, so
        // a slow approach doesn't make it every frame; in between, the texel stays near a pixel
        bool resize = !slot.framebuffer;
        if (slot.framebuffer) {
            float width = (float)slot.framebuffer->getWidth();
            float height = (float)slot.framebuffer->getHeight();
            resize = glm::abs(want.pixels.x - width) > 0.1f * width || glm::abs(want.pixels.y - height) > 0.1f * height;
        }
        if (resize) {
            // sampled linearly: the picture is often smaller than the mirror on screen, and point sampling would
            // show it blocky
            auto picture = gpu::TexturePointer(gpu::Texture::createRenderBuffer(gpu::Element::COLOR_SRGBA_32, want.pixels.x, want.pixels.y,
                gpu::Texture::SINGLE_MIP, gpu::Sampler(gpu::Sampler::FILTER_MIN_MAG_LINEAR, gpu::Sampler::WRAP_CLAMP)));
            picture->setSource("mirror" + std::to_string(entry.slot));
            slot.framebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create("mirror" + std::to_string(entry.slot)));
            slot.framebuffer->setRenderBuffer(0, picture);
            ++entry.generation;
            qCDebug(renderutils) << "Mirror" << want.id << "in slot" << entry.slot << "draws" << want.pixels.x << "x" << want.pixels.y
                                 << "for" << (int)want.area << "pixels on screen; camera at" << want.camera.getPosition()
                                 << "near" << want.camera.getNearClip() << "far" << want.camera.getFarClip();
        }
    }
    // only the slots in use draw their view
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (_slotViews[i] && _slotViews[i]->property("enabled").toBool() != _slots[i].active) {
            _slotViews[i]->setProperty("enabled", _slots[i].active);
        }
    }
}
