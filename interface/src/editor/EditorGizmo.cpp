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
    const float RING_RADIUS = 1.0f;
    const float RING_WIDTH = 0.045f;
    const float VIEW_RING_RADIUS = 1.22f;
    const float SPHERE_RADIUS = 0.93f;             // the trackball inside the rings (a hit area, not drawn)
    const float FREE_RADIANS_PER_PIXEL = glm::radians(0.5f);
    const float SCALE_CUBE = 0.13f;
    const float SCALE_CENTER_CUBE = 0.18f;
    const int RING_SEGMENTS = 72;

    const float AXIS_PICK_RADIUS = 0.09f;
    const float PLANE_PICK_MARGIN = 0.04f;
    const float CENTER_PICK_RADIUS = 0.14f;
    const float RING_PICK_WIDTH = 0.09f;
    const float SCALE_CENTER_PICK_RADIUS = 0.17f;

    const glm::vec3 AXIS_COLORS[3] = {
        { 0.89f, 0.27f, 0.25f },
        { 0.52f, 0.79f, 0.19f },
        { 0.24f, 0.52f, 0.88f }
    };
    const glm::vec3 HOVER_COLOR { 1.0f, 0.85f, 0.1f };
    const glm::vec3 CENTER_COLOR { 0.88f, 0.88f, 0.88f };
    const glm::vec3 VIEW_RING_COLOR { 0.8f, 0.8f, 0.8f };
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
            case GizmoHandle::AxisX: case GizmoHandle::PlaneYZ: case GizmoHandle::RingX: case GizmoHandle::ScaleX: return 0;
            case GizmoHandle::AxisY: case GizmoHandle::PlaneXZ: case GizmoHandle::RingY: case GizmoHandle::ScaleY: return 1;
            case GizmoHandle::AxisZ: case GizmoHandle::PlaneXY: case GizmoHandle::RingZ: case GizmoHandle::ScaleZ: return 2;
            default: return -1;
        }
    }
    GizmoHandle axisHandle(int axis) {
        return axis == 0 ? GizmoHandle::AxisX : axis == 1 ? GizmoHandle::AxisY : GizmoHandle::AxisZ;
    }
    GizmoHandle planeHandle(int axis) {
        return axis == 0 ? GizmoHandle::PlaneYZ : axis == 1 ? GizmoHandle::PlaneXZ : GizmoHandle::PlaneXY;
    }
    GizmoHandle ringHandle(int axis) {
        return axis == 0 ? GizmoHandle::RingX : axis == 1 ? GizmoHandle::RingY : GizmoHandle::RingZ;
    }
    GizmoHandle scaleHandle(int axis) {
        return axis == 0 ? GizmoHandle::ScaleX : axis == 1 ? GizmoHandle::ScaleY : GizmoHandle::ScaleZ;
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

    // a flat ring in the plane facing `axis` (2 = the XY plane), as a triangle strip
    QVector<glm::vec3> ringStrip(int axis, float radius, float width) {
        QVector<glm::vec3> points;
        points.reserve((RING_SEGMENTS + 1) * 2);
        for (int i = 0; i <= RING_SEGMENTS; ++i) {
            float angle = TWO_PI * (float)i / (float)RING_SEGMENTS;
            float c = std::cos(angle);
            float s = std::sin(angle);
            for (float r : { radius + width * 0.5f, radius - width * 0.5f }) {
                glm::vec3 p;
                if (axis == 0) {
                    p = glm::vec3(0.0f, c * r, s * r);
                } else if (axis == 1) {
                    p = glm::vec3(c * r, 0.0f, s * r);
                } else {
                    p = glm::vec3(c * r, s * r, 0.0f);
                }
                points << p;
            }
        }
        return points;
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

    // closest approach between a ray and an axis segment, in gizmo space
    bool rayHitsSegment(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& axis,
                        float from, float to, float radius, float& rayParam) {
        float b = glm::dot(direction, axis);
        float e = glm::dot(origin, direction);
        float f = glm::dot(origin, axis);
        float denominator = 1.0f - b * b;
        float segmentParam;
        if (denominator < 1.0e-5f) {
            segmentParam = glm::clamp(f, from, to);
            rayParam = -e + segmentParam * b;
        } else {
            segmentParam = glm::clamp((f - e * b) / denominator, from, to);
            rayParam = segmentParam * b - e;
        }
        if (rayParam <= 0.0f) {
            return false;
        }
        glm::vec3 onRay = origin + direction * rayParam;
        glm::vec3 onAxis = axis * segmentParam;
        return glm::length(onRay - onAxis) < radius;
    }

    // the orientation that turns +Z onto `direction`
    glm::quat facing(const glm::vec3& direction) {
        return rotationBetween(glm::vec3(0.0f, 0.0f, 1.0f), direction);
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

void EditorGizmoShared::render(RenderArgs* args, bool translucentPass) {
    EditorGizmoState state = this->state();
    if (!state.visible || !args || !args->_batch) {
        return;
    }
    PerformanceTimer perfTimer("editorGizmo");
    switch (state.mode) {
        case GizmoMode::Move:
            renderMove(args, state, translucentPass);
            break;
        case GizmoMode::Rotate:
            renderRotate(args, state, translucentPass);
            break;
        case GizmoMode::Scale:
            renderScale(args, state, translucentPass);
            break;
    }
}

// the gizmo's parts are unit shapes placed by a local transform inside the gizmo's own transform
#define GIZMO_PART_DRAW(state, translucent)                                                                   \
    auto geometryCache = DependencyManager::get<GeometryCache>();                                            \
    auto& batch = *args->_batch;                                                                              \
    auto pipeline = geometryCache->getShapePipelinePointer(translucent, true, true);                          \
    glm::mat4 gizmo = createMatFromQuatAndPos(state.rotation, state.position)                                 \
        * glm::scale(glm::mat4(1.0f), glm::vec3(state.scale));                                                \
    auto draw = [&](GeometryCache::Shape shape, const glm::quat& rotation, const glm::vec3& scale,            \
                    const glm::vec3& translation, const glm::vec4& color) {                                   \
        glm::mat4 part = createMatFromQuatAndPos(rotation, translation) * glm::scale(glm::mat4(1.0f), scale); \
        Transform transform;                                                                                  \
        transform.evalFromRawMatrix(gizmo * part);                                                            \
        batch.setModelTransform(transform);                                                                   \
        geometryCache->renderSolidShapeInstance(args, batch, shape, color, pipeline);                         \
    }

void EditorGizmoShared::renderMove(RenderArgs* args, const EditorGizmoState& state, bool translucentPass) {
    GIZMO_PART_DRAW(state, translucentPass);
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 direction = unitAxis(axis);
        if (translucentPass) {
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
    if (!translucentPass) {
        glm::vec4 color(handleColor(state, GizmoHandle::Center, CENTER_COLOR), 1.0f);
        draw(GeometryCache::Sphere, glm::quat(), glm::vec3(CENTER_RADIUS * 2.0f), glm::vec3(0.0f), color);
    }
}

void EditorGizmoShared::renderRotate(RenderArgs* args, const EditorGizmoState& state, bool translucentPass) {
    if (translucentPass) {
        return;   // the trackball inside the rings is a hit area only, nothing is drawn for it
    }
    auto geometryCache = DependencyManager::get<GeometryCache>();
    auto& batch = *args->_batch;
    for (int i = 0; i < 4; ++i) {
        if (_ringIds[i] < 0) {
            _ringIds[i] = geometryCache->allocateID();
        }
    }
    // the rings are strips with their colour in the vertices, drawn two-sided
    geometryCache->bindSimpleProgram(batch, false, false, true, false, true, true, graphics::MaterialKey::CULL_NONE);

    Transform gizmo(state.rotation, glm::vec3(state.scale), state.position);
    batch.setModelTransform(gizmo);
    for (int axis = 0; axis < 3; ++axis) {
        GizmoHandle handle = ringHandle(axis);
        glm::vec4 color(handleColor(state, handle, AXIS_COLORS[axis]), 1.0f);
        geometryCache->updateVertices(_ringIds[axis], ringStrip(axis, RING_RADIUS, RING_WIDTH), color);
        geometryCache->renderVertices(batch, gpu::TRIANGLE_STRIP, _ringIds[axis]);
    }

    // the outer ring faces the camera
    Transform view(facing(-state.viewDirection), glm::vec3(state.scale), state.position);
    batch.setModelTransform(view);
    glm::vec4 color(handleColor(state, GizmoHandle::RingView, VIEW_RING_COLOR), 1.0f);
    geometryCache->updateVertices(_ringIds[3], ringStrip(2, VIEW_RING_RADIUS, RING_WIDTH * 0.8f), color);
    geometryCache->renderVertices(batch, gpu::TRIANGLE_STRIP, _ringIds[3]);
}

void EditorGizmoShared::renderScale(RenderArgs* args, const EditorGizmoState& state, bool translucentPass) {
    if (translucentPass) {
        return;
    }
    GIZMO_PART_DRAW(state, false);
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 direction = unitAxis(axis);
        GizmoHandle handle = scaleHandle(axis);
        glm::vec4 color(handleColor(state, handle, AXIS_COLORS[axis]), 1.0f);
        float end = AXIS_END * state.stretch[axis];
        float shaftLength = std::max(0.01f, end - SCALE_CUBE * 0.5f - AXIS_START);
        draw(GeometryCache::Cylinder, Y_TO_AXIS[axis],
            glm::vec3(SHAFT_RADIUS * 2.0f, shaftLength, SHAFT_RADIUS * 2.0f),
            direction * (AXIS_START + shaftLength * 0.5f), color);
        draw(GeometryCache::Cube, glm::quat(), glm::vec3(SCALE_CUBE), direction * end, color);
    }
    glm::vec4 color(handleColor(state, GizmoHandle::ScaleUniform, CENTER_COLOR), 1.0f);
    draw(GeometryCache::Cube, glm::quat(), glm::vec3(SCALE_CENTER_CUBE), glm::vec3(0.0f), color);
}

namespace render {
    static Item::Bound gizmoBound(const EditorGizmoState& state) {
        float half = state.scale * 1.5f;
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

bool EditorGizmo::isRing(GizmoHandle handle) {
    return handle == GizmoHandle::RingX || handle == GizmoHandle::RingY || handle == GizmoHandle::RingZ
        || handle == GizmoHandle::RingView || handle == GizmoHandle::Sphere;
}

bool EditorGizmo::isScale(GizmoHandle handle) {
    return handle == GizmoHandle::ScaleX || handle == GizmoHandle::ScaleY || handle == GizmoHandle::ScaleZ
        || handle == GizmoHandle::ScaleUniform;
}

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

void EditorGizmo::setMode(GizmoMode mode) {
    if (_state.mode != mode) {
        _state.mode = mode;
        _state.hover = GizmoHandle::None;
        publish();
    }
}

void EditorGizmo::setTransform(const glm::vec3& position, const glm::quat& rotation, float scale, const glm::vec3& viewDirection) {
    _state.position = position;
    _state.rotation = rotation;
    _state.scale = scale;
    _state.viewDirection = viewDirection;
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

void EditorGizmo::setStretch(const glm::vec3& stretch) {
    if (_state.stretch != stretch) {
        _state.stretch = stretch;
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

    switch (_state.mode) {
        case GizmoMode::Move:
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
                if (rayHitsSegment(origin, direction, unitAxis(axis), AXIS_START, AXIS_END, AXIS_PICK_RADIUS, t) && t < bestT) {
                    best = axisHandle(axis);
                    bestT = t;
                }
            }
            break;

        case GizmoMode::Rotate: {
            for (int axis = 0; axis < 3; ++axis) {
                if (std::abs(direction[axis]) < 1.0e-5f) {
                    continue;
                }
                t = -origin[axis] / direction[axis];
                if (t <= 0.0f || t >= bestT) {
                    continue;
                }
                glm::vec3 hit = origin + direction * t;
                if (std::abs(glm::length(hit) - RING_RADIUS) < RING_PICK_WIDTH) {
                    best = ringHandle(axis);
                    bestT = t;
                }
            }
            // the outer ring, in world space
            glm::vec3 hit;
            if (rayPlane(ray, _state.position, _state.viewDirection, hit)) {
                float r = glm::length(hit - _state.position) / _state.scale;
                float distance = glm::length(hit - ray.origin);
                if (std::abs(r - VIEW_RING_RADIUS) < RING_PICK_WIDTH && distance / _state.scale < bestT) {
                    best = GizmoHandle::RingView;
                    bestT = distance / _state.scale;
                }
            }
            // inside the rings: the trackball
            if (best == GizmoHandle::None && raySphere(origin, direction, SPHERE_RADIUS, t)) {
                best = GizmoHandle::Sphere;
            }
            break;
        }

        case GizmoMode::Scale:
            if (raySphere(origin, direction, SCALE_CENTER_PICK_RADIUS, t)) {
                best = GizmoHandle::ScaleUniform;
                bestT = t;
            }
            for (int axis = 0; axis < 3; ++axis) {
                float end = AXIS_END * _state.stretch[axis] + SCALE_CUBE * 0.5f;
                if (rayHitsSegment(origin, direction, unitAxis(axis), AXIS_START, end, AXIS_PICK_RADIUS, t) && t < bestT) {
                    best = scaleHandle(axis);
                    bestT = t;
                }
            }
            break;
    }
    return best;
}

void EditorGizmo::beginDrag(GizmoHandle handle, const PickRay& ray) {
    _dragging = true;
    _dragHandle = handle;
    _dragOrigin = _state.position;
    _lastDelta = glm::vec3(0.0f);
    _lastAngle = 0.0f;
    _freeRotation = glm::quat();
    _lastScale = glm::vec3(1.0f);
    switch (handle) {
        case GizmoHandle::Sphere:
            break;
        case GizmoHandle::AxisX:
        case GizmoHandle::AxisY:
        case GizmoHandle::AxisZ:
        case GizmoHandle::ScaleX:
        case GizmoHandle::ScaleY:
        case GizmoHandle::ScaleZ:
            _dragAxis = axisOf(handle, _state.rotation);
            if (!closestParamOnLine(ray, _dragOrigin, _dragAxis, _dragStartParam)) {
                _dragStartParam = 0.0f;
            }
            break;
        case GizmoHandle::ScaleUniform:
            // left and right on the screen
            _dragAxis = glm::normalize(glm::cross(_state.viewDirection, glm::vec3(0.0f, 1.0f, 0.0f)));
            if (glm::length(_dragAxis) < 0.5f) {
                _dragAxis = glm::vec3(1.0f, 0.0f, 0.0f);
            }
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
            _dragPlaneNormal = -_state.viewDirection;
            if (!rayPlane(ray, _dragOrigin, _dragPlaneNormal, _dragStartPoint)) {
                _dragStartPoint = _dragOrigin;
            }
            break;
        case GizmoHandle::RingX:
        case GizmoHandle::RingY:
        case GizmoHandle::RingZ:
        case GizmoHandle::RingView: {
            _dragAxis = handle == GizmoHandle::RingView ? -_state.viewDirection : axisOf(handle, _state.rotation);
            _dragPlaneNormal = _dragAxis;
            glm::vec3 hit;
            if (rayPlane(ray, _dragOrigin, _dragPlaneNormal, hit) && glm::length(hit - _dragOrigin) > 1.0e-4f) {
                _dragStartVector = glm::normalize(hit - _dragOrigin);
            } else {
                _dragStartVector = glm::normalize(glm::cross(_dragAxis, glm::vec3(0.0f, 1.0f, 0.01f)));
            }
            break;
        }
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
        case GizmoHandle::PlaneYZ:
        case GizmoHandle::PlaneXZ:
        case GizmoHandle::PlaneXY:
        case GizmoHandle::Center: {
            glm::vec3 hit;
            if (rayPlane(ray, _dragOrigin, _dragPlaneNormal, hit)) {
                _lastDelta = hit - _dragStartPoint;
            }
            break;
        }
        default:
            break;
    }
    return _lastDelta;
}

float EditorGizmo::dragAngle(const PickRay& ray) {
    if (!_dragging || !isRing(_dragHandle)) {
        return 0.0f;
    }
    glm::vec3 hit;
    if (rayPlane(ray, _dragOrigin, _dragPlaneNormal, hit) && glm::length(hit - _dragOrigin) > 1.0e-4f) {
        glm::vec3 v = glm::normalize(hit - _dragOrigin);
        _lastAngle = std::atan2(glm::dot(glm::cross(_dragStartVector, v), _dragAxis), glm::dot(_dragStartVector, v));
    }
    return _lastAngle;
}

glm::quat EditorGizmo::dragFree(float dx, float dy) {
    if (!_dragging || _dragHandle != GizmoHandle::Sphere) {
        return glm::quat();
    }
    // the screen's right and up, in the world; a drag to the right spins about up, a drag down tips
    // the top towards the viewer
    glm::vec3 forward = glm::normalize(_state.viewDirection);
    glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::length(right) < 1.0e-3f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    right = glm::normalize(right);
    glm::vec3 up = glm::normalize(glm::cross(right, forward));
    glm::quat step = glm::angleAxis(dx * FREE_RADIANS_PER_PIXEL, up) * glm::angleAxis(dy * FREE_RADIANS_PER_PIXEL, right);
    _freeRotation = glm::normalize(step * _freeRotation);
    return _freeRotation;
}

glm::vec3 EditorGizmo::dragScale(const PickRay& ray) {
    if (!_dragging || !isScale(_dragHandle)) {
        return glm::vec3(1.0f);
    }
    float param;
    if (closestParamOnLine(ray, _dragOrigin, _dragAxis, param)) {
        // dragging the handle its own length away doubles the size
        float factor = std::max(0.01f, 1.0f + (param - _dragStartParam) / (AXIS_END * _state.scale));
        if (_dragHandle == GizmoHandle::ScaleUniform) {
            _lastScale = glm::vec3(factor);
        } else {
            _lastScale = glm::vec3(1.0f);
            _lastScale[axisIndex(_dragHandle)] = factor;
        }
    }
    return _lastScale;
}

void EditorGizmo::endDrag() {
    _dragging = false;
    _dragHandle = GizmoHandle::None;
    setStretch(glm::vec3(1.0f));
    setActive(GizmoHandle::None);
}
