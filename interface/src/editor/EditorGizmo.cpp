//
//  EditorGizmo.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorGizmo.h"

#include <cfloat>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include <GeometryCache.h>
#include <GLMHelpers.h>
#include <NumericalConstants.h>
#include <PerfStat.h>
#include <RenderHifi.h>
#include <Transform.h>

#include "Application.h"

namespace {
    // the unit gizmo; the whole thing is scaled to a constant size on screen
    const float AXIS_START = 0.12f;
    const float AXIS_END = 1.0f;
    const float SHAFT_RADIUS = 0.018f;
    const float CONE_LENGTH = 0.18f;
    const float CONE_RADIUS = 0.07f;
    const float PLANE_MIN = 0.3f;
    const float PLANE_MAX = 0.6f;
    const float PLANE_THICKNESS = 0.012f;
    const float CENTER_RADIUS = 0.1f;
    const float AXIS_PICK_RADIUS = 0.09f;
    const float PLANE_PICK_MARGIN = 0.04f;
    const float CENTER_PICK_RADIUS = 0.14f;

    const glm::vec3 AXIS_COLORS[3] = {
        { 0.89f, 0.27f, 0.25f },
        { 0.52f, 0.79f, 0.19f },
        { 0.24f, 0.52f, 0.88f }
    };
    const glm::vec3 HOVER_COLOR { 1.0f, 0.85f, 0.1f };
    const glm::vec3 CENTER_COLOR { 0.88f, 0.88f, 0.88f };
    const float DIM_FACTOR = 0.45f;
    const float PLANE_ALPHA = 0.55f;
    const float PLANE_HOVER_ALPHA = 0.85f;

    // rotations that take +Y (the cylinder and cone axis) to each world axis
    const glm::quat Y_TO_AXIS[3] = {
        glm::angleAxis(-PI_OVER_TWO, glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::quat(),
        glm::angleAxis(PI_OVER_TWO, glm::vec3(1.0f, 0.0f, 0.0f))
    };
    // rotations that take +Z (the plane square's thin axis) to each world axis
    const glm::quat Z_TO_AXIS[3] = {
        glm::angleAxis(PI_OVER_TWO, glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::angleAxis(-PI_OVER_TWO, glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::quat()
    };

    int axisIndex(GizmoHandle handle) {
        switch (handle) {
            case GizmoHandle::AxisX: case GizmoHandle::PlaneYZ: return 0;
            case GizmoHandle::AxisY: case GizmoHandle::PlaneXZ: return 1;
            case GizmoHandle::AxisZ: case GizmoHandle::PlaneXY: return 2;
            default: return -1;
        }
    }
    GizmoHandle axisHandle(int axis) {
        return axis == 0 ? GizmoHandle::AxisX : axis == 1 ? GizmoHandle::AxisY : GizmoHandle::AxisZ;
    }
    GizmoHandle planeHandle(int axis) {
        return axis == 0 ? GizmoHandle::PlaneYZ : axis == 1 ? GizmoHandle::PlaneXZ : GizmoHandle::PlaneXY;
    }
    glm::vec3 unitAxis(int axis) {
        glm::vec3 v(0.0f);
        v[axis] = 1.0f;
        return v;
    }

    // the colour of a handle given what is hovered and what is being dragged
    glm::vec3 handleColor(const EditorGizmoState& state, GizmoHandle handle, const glm::vec3& base) {
        if (state.active != GizmoHandle::None) {
            return state.active == handle ? HOVER_COLOR : base * DIM_FACTOR;
        }
        return state.hover == handle ? HOVER_COLOR : base;
    }

    bool raySphere(const glm::vec3& origin, const glm::vec3& direction, float radius, float& t) {
        float b = glm::dot(origin, direction);
        float c = glm::dot(origin, origin) - radius * radius;
        float discriminant = b * b - c;
        if (discriminant < 0.0f) {
            return false;
        }
        t = -b - std::sqrt(discriminant);
        return t > 0.0f;
    }

    // the parameter along the line (point, axis) closest to the ray
    bool closestParamOnLine(const PickRay& ray, const glm::vec3& point, const glm::vec3& axis, float& param) {
        glm::vec3 w = ray.origin - point;
        float b = glm::dot(ray.direction, axis);
        float e = glm::dot(w, ray.direction);
        float f = glm::dot(w, axis);
        float denominator = 1.0f - b * b;
        if (denominator < 1.0e-5f) {
            return false;   // looking straight down the axis
        }
        param = (f - e * b) / denominator;
        return true;
    }

    bool rayPlane(const PickRay& ray, const glm::vec3& point, const glm::vec3& normal, glm::vec3& hit) {
        float denominator = glm::dot(ray.direction, normal);
        if (std::abs(denominator) < 1.0e-5f) {
            return false;
        }
        float t = glm::dot(point - ray.origin, normal) / denominator;
        if (t < 0.0f) {
            return false;
        }
        hit = ray.origin + ray.direction * t;
        return true;
    }
}

// --- render side --------------------------------------------------------------------------------

EditorGizmoState EditorGizmoShared::state() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _state;
}

void EditorGizmoShared::setState(const EditorGizmoState& state) {
    std::lock_guard<std::mutex> lock(_mutex);
    _state = state;
}

void EditorGizmoShared::render(RenderArgs* args, bool planes) {
    EditorGizmoState state = this->state();
    if (!state.visible || !args || !args->_batch) {
        return;
    }
    PerformanceTimer perfTimer("editorGizmo");

    auto geometryCache = DependencyManager::get<GeometryCache>();
    auto& batch = *args->_batch;
    // unlit, and the front layer draws straight into the lit frame, so always the forward pipelines
    auto pipeline = geometryCache->getShapePipelinePointer(planes, true, true);

    glm::mat4 gizmo = createMatFromQuatAndPos(state.rotation, state.position)
        * glm::scale(glm::mat4(1.0f), glm::vec3(state.scale));
    auto draw = [&](GeometryCache::Shape shape, const glm::quat& rotation, const glm::vec3& scale,
                    const glm::vec3& translation, const glm::vec4& color) {
        glm::mat4 part = createMatFromQuatAndPos(rotation, translation) * glm::scale(glm::mat4(1.0f), scale);
        Transform transform;
        transform.evalFromRawMatrix(gizmo * part);
        batch.setModelTransform(transform);
        geometryCache->renderSolidShapeInstance(args, batch, shape, color, pipeline);
    };

    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 direction = unitAxis(axis);
        if (planes) {
            int b = (axis + 1) % 3;
            int c = (axis + 2) % 3;
            glm::vec3 center = (unitAxis(b) + unitAxis(c)) * ((PLANE_MIN + PLANE_MAX) * 0.5f);
            float side = PLANE_MAX - PLANE_MIN;
            GizmoHandle handle = planeHandle(axis);
            bool lit = state.hover == handle || state.active == handle;
            glm::vec4 color(handleColor(state, handle, AXIS_COLORS[axis]), lit ? PLANE_HOVER_ALPHA : PLANE_ALPHA);
            draw(GeometryCache::Cube, Z_TO_AXIS[axis], glm::vec3(side, side, PLANE_THICKNESS), center, color);
        } else {
            GizmoHandle handle = axisHandle(axis);
            glm::vec4 color(handleColor(state, handle, AXIS_COLORS[axis]), 1.0f);
            float shaftLength = AXIS_END - CONE_LENGTH - AXIS_START;
            draw(GeometryCache::Cylinder, Y_TO_AXIS[axis],
                glm::vec3(SHAFT_RADIUS * 2.0f, shaftLength, SHAFT_RADIUS * 2.0f),
                direction * (AXIS_START + shaftLength * 0.5f), color);
            draw(GeometryCache::Cone, Y_TO_AXIS[axis],
                glm::vec3(CONE_RADIUS * 2.0f, CONE_LENGTH, CONE_RADIUS * 2.0f),
                direction * (AXIS_END - CONE_LENGTH * 0.5f), color);
        }
    }
    if (!planes) {
        glm::vec4 color(handleColor(state, GizmoHandle::Center, CENTER_COLOR), 1.0f);
        draw(GeometryCache::Sphere, glm::quat(), glm::vec3(CENTER_RADIUS * 2.0f), glm::vec3(0.0f), color);
    }
}

namespace render {
    static Item::Bound gizmoBound(const EditorGizmoState& state) {
        float half = state.scale * 1.2f;
        return Item::Bound(state.position - glm::vec3(half), 2.0f * half);
    }

    template <> const ItemKey payloadGetKey(const EditorGizmoRenderData::Pointer& data) {
        return ItemKey::Builder::opaqueShape().withLayer(render::hifi::LAYER_3D_FRONT)
            .withTagBits(ItemKey::TAG_BITS_0).withoutShadowCaster();
    }
    template <> const Item::Bound payloadGetBound(const EditorGizmoRenderData::Pointer& data) {
        return gizmoBound(data->_shared->state());
    }
    template <> void payloadRender(const EditorGizmoRenderData::Pointer& data, RenderArgs* args) {
        if (data) {
            data->_shared->render(args, false);
        }
    }

    template <> const ItemKey payloadGetKey(const EditorGizmoPlanesRenderData::Pointer& data) {
        return ItemKey::Builder::transparentShape().withLayer(render::hifi::LAYER_3D_FRONT)
            .withTagBits(ItemKey::TAG_BITS_0).withoutShadowCaster();
    }
    template <> const Item::Bound payloadGetBound(const EditorGizmoPlanesRenderData::Pointer& data) {
        return gizmoBound(data->_shared->state());
    }
    template <> void payloadRender(const EditorGizmoPlanesRenderData::Pointer& data, RenderArgs* args) {
        if (data) {
            data->_shared->render(args, true);
        }
    }
}

// --- main-thread side ---------------------------------------------------------------------------

void EditorGizmo::ensureItems() {
    if (render::Item::isValidID(_opaqueItem)) {
        return;
    }
    auto scene = qApp->getMain3DScene();
    if (!scene) {
        return;
    }
    render::Transaction transaction;
    _opaqueItem = scene->allocateID();
    transaction.resetItem(_opaqueItem, std::make_shared<EditorGizmoRenderData::Payload>(
        std::make_shared<EditorGizmoRenderData>(_shared)));
    _planesItem = scene->allocateID();
    transaction.resetItem(_planesItem, std::make_shared<EditorGizmoPlanesRenderData::Payload>(
        std::make_shared<EditorGizmoPlanesRenderData>(_shared)));
    scene->enqueueTransaction(transaction);
}

void EditorGizmo::publish() {
    _shared->setState(_state);
    if (_state.visible) {
        ensureItems();
    }
}

void EditorGizmo::setVisible(bool visible) {
    if (_state.visible != visible) {
        _state.visible = visible;
        publish();
    }
}

void EditorGizmo::setTransform(const glm::vec3& position, const glm::quat& rotation, float scale) {
    _state.position = position;
    _state.rotation = rotation;
    _state.scale = scale;
    publish();
}

void EditorGizmo::setHover(GizmoHandle handle) {
    if (_state.hover != handle) {
        _state.hover = handle;
        publish();
    }
}

void EditorGizmo::setActive(GizmoHandle handle) {
    if (_state.active != handle) {
        _state.active = handle;
        publish();
    }
}

glm::vec3 EditorGizmo::axisOf(GizmoHandle handle, const glm::quat& rotation) {
    int axis = axisIndex(handle);
    return axis < 0 ? glm::vec3(0.0f) : rotation * unitAxis(axis);
}

GizmoHandle EditorGizmo::hitTest(const PickRay& ray) const {
    if (!_state.visible || _state.scale <= 0.0f) {
        return GizmoHandle::None;
    }
    // into gizmo space, where the handles are axis-aligned and unit sized
    glm::quat inverse = glm::inverse(_state.rotation);
    glm::vec3 origin = inverse * (ray.origin - _state.position) / _state.scale;
    glm::vec3 direction = glm::normalize(inverse * ray.direction);

    GizmoHandle best = GizmoHandle::None;
    float bestT = FLT_MAX;

    float t;
    if (raySphere(origin, direction, CENTER_PICK_RADIUS, t)) {
        best = GizmoHandle::Center;
        bestT = t;
    }

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) > 1.0e-5f) {
            t = -origin[axis] / direction[axis];
            if (t > 0.0f && t < bestT) {
                glm::vec3 hit = origin + direction * t;
                int b = (axis + 1) % 3;
                int c = (axis + 2) % 3;
                if (hit[b] >= PLANE_MIN - PLANE_PICK_MARGIN && hit[b] <= PLANE_MAX + PLANE_PICK_MARGIN &&
                    hit[c] >= PLANE_MIN - PLANE_PICK_MARGIN && hit[c] <= PLANE_MAX + PLANE_PICK_MARGIN) {
                    best = planeHandle(axis);
                    bestT = t;
                }
            }
        }
    }

    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 a = unitAxis(axis);
        // closest points between the ray and the axis segment
        float b = glm::dot(direction, a);
        float e = glm::dot(origin, direction);
        float f = glm::dot(origin, a);
        float denominator = 1.0f - b * b;
        float segmentParam;
        float rayParam;
        if (denominator < 1.0e-5f) {
            segmentParam = glm::clamp(f, AXIS_START, AXIS_END);
            rayParam = -e + segmentParam * b;
        } else {
            segmentParam = glm::clamp((f - e * b) / denominator, AXIS_START, AXIS_END);
            rayParam = segmentParam * b - e;
        }
        if (rayParam <= 0.0f || rayParam >= bestT) {
            continue;
        }
        glm::vec3 onRay = origin + direction * rayParam;
        glm::vec3 onAxis = a * segmentParam;
        if (glm::length(onRay - onAxis) < AXIS_PICK_RADIUS) {
            best = axisHandle(axis);
            bestT = rayParam;
        }
    }
    return best;
}

void EditorGizmo::beginDrag(GizmoHandle handle, const PickRay& ray, const glm::vec3& cameraForward) {
    _dragging = true;
    _dragHandle = handle;
    _dragOrigin = _state.position;
    _lastDelta = glm::vec3(0.0f);
    switch (handle) {
        case GizmoHandle::AxisX:
        case GizmoHandle::AxisY:
        case GizmoHandle::AxisZ:
            _dragAxis = axisOf(handle, _state.rotation);
            if (!closestParamOnLine(ray, _dragOrigin, _dragAxis, _dragStartParam)) {
                _dragStartParam = 0.0f;
            }
            break;
        case GizmoHandle::PlaneYZ:
        case GizmoHandle::PlaneXZ:
        case GizmoHandle::PlaneXY:
            _dragPlaneNormal = axisOf(handle, _state.rotation);
            if (!rayPlane(ray, _dragOrigin, _dragPlaneNormal, _dragStartPoint)) {
                _dragStartPoint = _dragOrigin;
            }
            break;
        case GizmoHandle::Center:
            // slides in the plane facing the camera
            _dragPlaneNormal = -cameraForward;
            if (!rayPlane(ray, _dragOrigin, _dragPlaneNormal, _dragStartPoint)) {
                _dragStartPoint = _dragOrigin;
            }
            break;
        default:
            _dragging = false;
            break;
    }
    setActive(_dragging ? handle : GizmoHandle::None);
}

glm::vec3 EditorGizmo::dragDelta(const PickRay& ray) {
    if (!_dragging) {
        return glm::vec3(0.0f);
    }
    switch (_dragHandle) {
        case GizmoHandle::AxisX:
        case GizmoHandle::AxisY:
        case GizmoHandle::AxisZ: {
            float param;
            if (closestParamOnLine(ray, _dragOrigin, _dragAxis, param)) {
                _lastDelta = _dragAxis * (param - _dragStartParam);
            }
            break;
        }
        default: {
            glm::vec3 hit;
            if (rayPlane(ray, _dragOrigin, _dragPlaneNormal, hit)) {
                _lastDelta = hit - _dragStartPoint;
            }
            break;
        }
    }
    return _lastDelta;
}

void EditorGizmo::endDrag() {
    _dragging = false;
    _dragHandle = GizmoHandle::None;
    setActive(GizmoHandle::None);
}
