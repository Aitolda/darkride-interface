//
//  EditorLightGizmos.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorLightGizmos.h"

#include <cfloat>
#include <cmath>

#include <GeometryCache.h>
#include <GLMHelpers.h>
#include <NumericalConstants.h>
#include <RenderHifi.h>
#include <Transform.h>

#include "Application.h"

namespace {
    // sizes on screen, in viewport pixels (points on a Retina display)
    const float LINE_PIXELS = 1.5f;
    const float SPOT_ICON_LENGTH_PIXELS = 18.0f;   // the icon's cone, from the light to its open end
    const float SPOT_ICON_RADIUS = 0.45f;          // of its open end, to its length
    const float SPOT_ICON_BEAM_PIXELS = 26.0f;     // the line out along the beam, beyond the cone
    const float POINT_ICON_RADIUS_PIXELS = 6.0f;
    const float POINT_ICON_RAY_FROM_PIXELS = 9.0f;
    const float POINT_ICON_RAY_TO_PIXELS = 13.0f;
    const int POINT_ICON_RAYS = 8;
    const int ICON_SEGMENTS = 20;
    const float ICON_PICK_PIXELS = 14.0f;
    const float HANDLE_PIXELS = 7.0f;
    const float HANDLE_PICK_PIXELS = 8.0f;

    // Unity's light gizmo colour (as the selected light's wireframe), dimmer for lights not selected
    const glm::vec3 LIGHT_COLOR { 1.0f, 0.96f, 0.55f };
    const float UNSELECTED_BRIGHTNESS = 0.75f;
    const glm::vec3 LIT_HANDLE_COLOR { 0.96f, 0.95f, 0.2f };   // under the pointer, or being dragged, as the gizmo's

    // the handles of a spot light: its range at the end of the beam, then its angle round the rim of the cone
    const int SPOT_RANGE_HANDLE = 0;
    const int SPOT_HANDLE_COUNT = 5;
    const int POINT_HANDLE_COUNT = 6;

    glm::vec3 forwardOf(const glm::quat& rotation) {
        return rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    }

    // two directions across `normal`, at right angles to it and to each other
    void across(const glm::vec3& normal, glm::vec3& first, glm::vec3& second) {
        first = glm::normalize(glm::cross(normal, std::abs(normal.y) < 0.9f ? Vectors::UNIT_Y : Vectors::UNIT_X));
        second = glm::cross(normal, first);
    }

    // where the ray passes nearest the point, when it passes within `radius` of it in front of the ray's start
    bool rayNearPoint(const PickRay& ray, const glm::vec3& point, float radius, float& distanceAlong) {
        glm::vec3 direction = glm::normalize(ray.direction);
        float along = glm::dot(point - ray.origin, direction);
        if (along < 0.0f) {
            return false;
        }
        if (glm::length(ray.origin + direction * along - point) > radius) {
            return false;
        }
        distanceAlong = along;
        return true;
    }
}

// --- render thread ------------------------------------------------------------------------------

void EditorLightGizmosShared::setLines(const EditorGizmoLines& lines, const glm::vec3& low, const glm::vec3& high) {
    std::lock_guard<std::mutex> lock(_mutex);
    _lines = lines;
    _low = low;
    _high = high;
}

render::Item::Bound EditorLightGizmosShared::bound() const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_lines.isEmpty()) {
        return render::Item::Bound(glm::vec3(0.0f), 0.0f);
    }
    return render::Item::Bound(_low, _high - _low);
}

void EditorLightGizmosShared::render(RenderArgs* args) {
    if (!args || !args->_batch) {
        return;
    }
    EditorGizmoLines lines;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        lines = _lines;
    }
    if (lines.isEmpty()) {
        return;
    }
    auto geometryCache = DependencyManager::get<GeometryCache>();
    if (_bufferId < 0) {
        _bufferId = geometryCache->allocateID();
    }
    auto& batch = *args->_batch;
    // the colour is in the vertices; drawn from both sides, in world space
    geometryCache->bindSimpleProgram(batch, false, false, true, false, true, true, graphics::MaterialKey::CULL_NONE);
    batch.setModelTransform(Transform());
    lines.draw(batch, *geometryCache, _bufferId);
}

namespace render {
    template <> const ItemKey payloadGetKey(const EditorLightGizmosRenderData::Pointer& data) {
        return ItemKey::Builder::opaqueShape().withLayer(render::hifi::LAYER_3D_FRONT)
            .withTagBits(ItemKey::TAG_BITS_0).withoutShadowCaster();
    }
    template <> const Item::Bound payloadGetBound(const EditorLightGizmosRenderData::Pointer& data) {
        return data ? data->_shared->bound() : Item::Bound();
    }
    template <> void payloadRender(const EditorLightGizmosRenderData::Pointer& data, RenderArgs* args) {
        if (data) {
            data->_shared->render(args);
        }
    }
}

// --- main thread --------------------------------------------------------------------------------

void EditorLightGizmos::update(const std::vector<EditorLightGizmoLight>& lights, const QUuid& handlesFor,
                               const glm::vec3& cameraPosition, const glm::vec3& viewDirection, float pixelSize) {
    _lights = lights;
    _cameraPosition = cameraPosition;
    _viewDirection = glm::normalize(viewDirection);
    _pixelSize = pixelSize;
    _hasHandleLight = false;
    if (!handlesFor.isNull()) {
        for (const auto& light : _lights) {
            if (light.id == handlesFor) {
                _handleLight = light;
                _hasHandleLight = true;
                break;
            }
        }
    }
    if (!_hasHandleLight) {
        _hover = EditorLightHandle();
        _active = EditorLightHandle();
    }
    rebuild();
}

void EditorLightGizmos::clear() {
    _lights.clear();
    _hasHandleLight = false;
    _hover = EditorLightHandle();
    _active = EditorLightHandle();
    rebuild();
}

void EditorLightGizmos::setHover(const EditorLightHandle& handle) {
    if (handle != _hover) {
        _hover = handle;
        rebuild();
    }
}

void EditorLightGizmos::setActive(const EditorLightHandle& handle) {
    if (handle != _active) {
        _active = handle;
        rebuild();
    }
}

bool EditorLightGizmos::handleLight(EditorLightGizmoLight& light) const {
    if (_hasHandleLight) {
        light = _handleLight;
    }
    return _hasHandleLight;
}

float EditorLightGizmos::pixelsAt(const glm::vec3& point) const {
    // the depth in front of the camera, not the distance, so that things keep their size out at the edges
    const float MIN_DEPTH = 0.05f;
    return _pixelSize * std::max(glm::dot(point - _cameraPosition, _viewDirection), MIN_DEPTH);
}

std::vector<glm::vec3> EditorLightGizmos::handlePositions(const EditorLightGizmoLight& light) const {
    std::vector<glm::vec3> positions;
    if (light.spot) {
        // the range at the end of the beam; the angle at the rim, where the reach meets the cone's side
        glm::vec3 forward = forwardOf(light.rotation);
        positions.push_back(light.position + forward * light.range);
        glm::vec3 rimCenter = light.position + forward * (light.range * std::cos(light.angle));
        float rimRadius = light.range * std::sin(light.angle);
        for (int side = 1; side < SPOT_HANDLE_COUNT; ++side) {
            positions.push_back(rimCenter + handleDirection(light, side) * rimRadius);
        }
    } else {
        for (int index = 0; index < POINT_HANDLE_COUNT; ++index) {
            positions.push_back(light.position + handleDirection(light, index) * light.range);
        }
    }
    return positions;
}

glm::vec3 EditorLightGizmos::handleDirection(const EditorLightGizmoLight& light, int index) const {
    if (light.spot) {
        switch (index) {
            case SPOT_RANGE_HANDLE: return forwardOf(light.rotation);
            case 1: return light.rotation * Vectors::UNIT_X;
            case 2: return light.rotation * Vectors::UNIT_Y;
            case 3: return light.rotation * -Vectors::UNIT_X;
            default: return light.rotation * -Vectors::UNIT_Y;
        }
    }
    // a point light's sphere has a handle at each end of each axis of the world
    glm::vec3 direction(0.0f);
    direction[index / 2] = (index % 2 == 0) ? 1.0f : -1.0f;
    return direction;
}

QUuid EditorLightGizmos::iconAt(const PickRay& ray) const {
    QUuid nearest;
    float nearestAlong = FLT_MAX;
    for (const auto& spot : _iconSpots) {
        float along;
        if (rayNearPoint(ray, spot.second.center, spot.second.radius, along) && along < nearestAlong) {
            nearestAlong = along;
            nearest = spot.first;
        }
    }
    return nearest;
}

EditorLightHandle EditorLightGizmos::handleAt(const PickRay& ray) const {
    EditorLightHandle nearest;
    if (!_hasHandleLight) {
        return nearest;
    }
    float nearestAlong = FLT_MAX;
    auto positions = handlePositions(_handleLight);
    for (int index = 0; index < (int)positions.size(); ++index) {
        float along;
        if (rayNearPoint(ray, positions[index], HANDLE_PICK_PIXELS * pixelsAt(positions[index]), along) && along < nearestAlong) {
            nearestAlong = along;
            nearest.kind = (_handleLight.spot && index != SPOT_RANGE_HANDLE) ? EditorLightHandle::Angle : EditorLightHandle::Range;
            nearest.index = index;
        }
    }
    return nearest;
}

float EditorLightGizmos::rangeAt(const PickRay& ray, const EditorLightHandle& handle) const {
    // the point along the handle's line out from the light nearest the pointer's ray
    glm::vec3 direction = glm::normalize(ray.direction);
    glm::vec3 axis = handleDirection(_handleLight, handle.index);
    glm::vec3 fromLight = ray.origin - _handleLight.position;
    float b = glm::dot(direction, axis);
    float denominator = 1.0f - b * b;
    if (denominator < 1.0e-6f) {
        return _handleLight.range;   // looking straight down the line: no way to tell
    }
    return (glm::dot(axis, fromLight) - b * glm::dot(direction, fromLight)) / denominator;
}

float EditorLightGizmos::angleAt(const PickRay& ray, const EditorLightHandle& handle) const {
    // where the ray meets the plane through the light's axis and the handle, and the angle there from the axis
    glm::vec3 direction = glm::normalize(ray.direction);
    glm::vec3 forward = forwardOf(_handleLight.rotation);
    glm::vec3 side = handleDirection(_handleLight, handle.index);
    glm::vec3 normal = glm::cross(forward, side);
    float facing = glm::dot(direction, normal);
    if (std::abs(facing) < 1.0e-4f) {
        return _handleLight.angle;   // the plane seen edge on
    }
    float along = glm::dot(_handleLight.position - ray.origin, normal) / facing;
    if (along < 0.0f) {
        return _handleLight.angle;
    }
    glm::vec3 fromLight = ray.origin + direction * along - _handleLight.position;
    return std::atan2(glm::dot(fromLight, side), glm::dot(fromLight, forward));
}

void EditorLightGizmos::rebuild() {
    EditorGizmoLines lines;
    _iconSpots.clear();
    glm::vec3 low(FLT_MAX);
    glm::vec3 high(-FLT_MAX);
    auto include = [&](const glm::vec3& point, float reach) {
        low = glm::min(low, point - glm::vec3(reach));
        high = glm::max(high, point + glm::vec3(reach));
    };

    for (const auto& light : _lights) {
        float pixel = pixelsAt(light.position);
        glm::vec3 toCamera = _cameraPosition - light.position;
        glm::vec3 facing = glm::length(toCamera) > 1.0e-6f ? glm::normalize(toCamera) : -_viewDirection;
        lines.setToCamera(facing);
        glm::vec4 color(light.selected ? LIGHT_COLOR : LIGHT_COLOR * UNSELECTED_BRIGHTNESS, 1.0f);
        float width = LINE_PIXELS * pixel;
        if (light.spot) {
            // a small cone opening the way the light shines, and its beam's line beyond it
            glm::vec3 forward = forwardOf(light.rotation);
            glm::vec3 right = light.rotation * Vectors::UNIT_X;
            glm::vec3 up = light.rotation * Vectors::UNIT_Y;
            float length = SPOT_ICON_LENGTH_PIXELS * pixel;
            float radius = length * SPOT_ICON_RADIUS;
            glm::vec3 base = light.position + forward * length;
            QVector<glm::vec3> rim;
            for (int i = 0; i < ICON_SEGMENTS; ++i) {
                float angle = TWO_PI * (float)i / (float)ICON_SEGMENTS;
                rim << base + (right * std::cos(angle) + up * std::sin(angle)) * radius;
            }
            lines.addLine(rim, true, width, color);
            for (int i = 0; i < 4; ++i) {
                float angle = PI_OVER_TWO * (float)i;
                lines.addSegment(light.position, base + (right * std::cos(angle) + up * std::sin(angle)) * radius, width, color);
            }
            glm::vec3 beamEnd = base + forward * (SPOT_ICON_BEAM_PIXELS * pixel);
            lines.addSegment(base, beamEnd, width, color);
            _iconSpots.push_back({ light.id, { light.position + forward * (0.5f * length), ICON_PICK_PIXELS * pixel } });
            include(light.position, length);
            include(beamEnd, width);
        } else {
            // a small circle facing the camera with rays round it
            glm::vec3 first, second;
            across(facing, first, second);
            QVector<glm::vec3> circle;
            for (int i = 0; i < ICON_SEGMENTS; ++i) {
                float angle = TWO_PI * (float)i / (float)ICON_SEGMENTS;
                circle << light.position + (first * std::cos(angle) + second * std::sin(angle)) * (POINT_ICON_RADIUS_PIXELS * pixel);
            }
            lines.addLine(circle, true, width, color);
            for (int i = 0; i < POINT_ICON_RAYS; ++i) {
                float angle = TWO_PI * (float)i / (float)POINT_ICON_RAYS;
                glm::vec3 out = first * std::cos(angle) + second * std::sin(angle);
                lines.addSegment(light.position + out * (POINT_ICON_RAY_FROM_PIXELS * pixel),
                                 light.position + out * (POINT_ICON_RAY_TO_PIXELS * pixel), width, color);
            }
            _iconSpots.push_back({ light.id, { light.position, ICON_PICK_PIXELS * pixel } });
            include(light.position, POINT_ICON_RAY_TO_PIXELS * pixel);
        }
    }

    if (_hasHandleLight) {
        // the handles: small squares facing the camera
        auto positions = handlePositions(_handleLight);
        for (int index = 0; index < (int)positions.size(); ++index) {
            const glm::vec3& position = positions[index];
            glm::vec3 toCamera = _cameraPosition - position;
            glm::vec3 facing = glm::length(toCamera) > 1.0e-6f ? glm::normalize(toCamera) : -_viewDirection;
            glm::vec3 first, second;
            across(facing, first, second);
            EditorLightHandle handle;
            handle.kind = (_handleLight.spot && index != SPOT_RANGE_HANDLE) ? EditorLightHandle::Angle : EditorLightHandle::Range;
            handle.index = index;
            bool lit = !_active.isNone() ? _active == handle : _hover == handle;
            glm::vec4 color(lit ? LIT_HANDLE_COLOR : LIGHT_COLOR, 1.0f);
            float half = 0.5f * HANDLE_PIXELS * pixelsAt(position);
            lines.addQuad(position - first * half - second * half, position + first * half - second * half,
                          position + first * half + second * half, position - first * half + second * half, color);
            include(position, 2.0f * half);
        }
    }

    publish(lines, low, high);
}

void EditorLightGizmos::ensureItem() {
    if (render::Item::isValidID(_item)) {
        return;
    }
    auto scene = qApp->getMain3DScene();
    if (!scene) {
        return;
    }
    render::Transaction transaction;
    _item = scene->allocateID();
    transaction.resetItem(_item, std::make_shared<EditorLightGizmosRenderData::Payload>(
        std::make_shared<EditorLightGizmosRenderData>(_shared)));
    scene->enqueueTransaction(transaction);
}

void EditorLightGizmos::publish(const EditorGizmoLines& lines, const glm::vec3& low, const glm::vec3& high) {
    if (lines.isEmpty() && _empty) {
        return;
    }
    _empty = lines.isEmpty();
    _shared->setLines(lines, low, high);
    if (!_empty) {
        ensureItem();
    }
    if (render::Item::isValidID(_item)) {
        if (auto scene = qApp->getMain3DScene()) {
            render::Transaction transaction;
            transaction.updateItem(_item);
            scene->enqueueTransaction(transaction);
        }
    }
}
