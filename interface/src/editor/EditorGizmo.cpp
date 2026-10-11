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
    // The unit gizmo, laid out like Unity's. One unit is EditorGizmo::SIZE_PIXELS on screen and the whole
    // gizmo is scaled to keep that size, so line widths are given in pixels.
    const float PIXEL = 1.0f / EditorGizmo::SIZE_PIXELS;

    // Move: a line from the centre with a cone at the end, per axis, and a small square per plane in the
    // corner nearest the camera
    const float AXIS_END = 1.0f;
    const float CONE_LENGTH = 0.2f;
    const float CONE_RADIUS = 0.065f;
    const float AXIS_LINE_WIDTH = 1.5f * PIXEL;
    const float PLANE_SIZE = 0.25f;
    const float PLANE_LINE_WIDTH = 1.0f * PIXEL;
    const float PLANE_ALPHA = 0.3f;
    const float PLANE_HOVER_ALPHA = 0.5f;

    // Rotate: a ring per axis, of which only the half facing the camera is drawn (the back half would cross
    // the front one); an outer ring facing the camera; and a faint circle round the trackball inside them
    const float RING_RADIUS = 1.0f;
    const float RING_LINE_WIDTH = 2.0f * PIXEL;
    const float RING_HOVER_LINE_WIDTH = 3.5f * PIXEL;
    const float VIEW_RING_RADIUS = 1.1f;
    const float VIEW_RING_LINE_WIDTH = 1.5f * PIXEL;
    const float SPHERE_RADIUS = 1.0f;
    const float SPHERE_LINE_WIDTH = 1.0f * PIXEL;
    const float FREE_RADIANS_PER_PIXEL = glm::radians(0.5f);
    const int CIRCLE_SEGMENTS = 96;
    // a ring seen almost face on is drawn whole: it has no back half to hide
    const float RING_FACING_ACROSS = 0.1f;

    // Scale: a line with a cube at the end, per axis, and a cube in the centre for all three at once
    const float SCALE_CUBE = 0.1f;
    const float SCALE_CENTER_CUBE = 0.15f;

    // An axis seen almost end on is hidden and cannot be picked: its cone would cover the centre, and a drag
    // along it would race towards or away from the camera. A square seen almost edge on, likewise.
    const float AXIS_HIDE_DOT = 0.98f;    // within about 11 degrees of the line of sight
    const float PLANE_HIDE_DOT = 0.2f;    // within about 11 degrees of edge on
    // the squares change corner when the camera crosses a plane, a little past it so that they don't flicker
    const float PLANE_FLIP_MARGIN = 0.05f;

    const float AXIS_PICK_START = 0.2f;   // nearer the centre, the squares are picked instead
    const float AXIS_PICK_RADIUS = 0.07f;
    const float PLANE_PICK_MARGIN = 0.03f;
    const float RING_PICK_WIDTH = 0.08f;
    const float SCALE_PICK_START = 0.12f;
    const float SCALE_CENTER_PICK_RADIUS = 0.12f;

    // Unity's handle colours
    const glm::vec3 AXIS_COLORS[3] = {
        { 0.86f, 0.24f, 0.11f },
        { 0.60f, 0.95f, 0.28f },
        { 0.23f, 0.48f, 0.97f }
    };
    const glm::vec3 SELECTED_COLOR { 0.96f, 0.95f, 0.2f };   // under the pointer, or being dragged
    const glm::vec3 DISABLED_COLOR { 0.5f, 0.5f, 0.5f };     // the other handles, during a drag
    const glm::vec3 CENTER_COLOR { 0.8f, 0.8f, 0.8f };
    const glm::vec4 SPHERE_COLOR { 0.0f, 0.0f, 0.0f, 0.2f };
    const float SPHERE_HOVER_ALPHA = 0.8f;

    // rotations that take +Y (the cone's axis) to each axis
    const glm::quat Y_TO_AXIS[3] = {
        glm::angleAxis(-PI_OVER_TWO, glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::quat(),
        glm::angleAxis(PI_OVER_TWO, glm::vec3(1.0f, 0.0f, 0.0f))
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
    bool isPlane(GizmoHandle handle) {
        return handle == GizmoHandle::PlaneYZ || handle == GizmoHandle::PlaneXZ || handle == GizmoHandle::PlaneXY;
    }
    glm::vec3 unitAxis(int axis) {
        glm::vec3 v(0.0f);
        v[axis] = 1.0f;
        return v;
    }

    // the handle being dragged, or else the one under the pointer
    GizmoHandle litHandle(const EditorGizmoState& state) {
        return state.active != GizmoHandle::None ? state.active : state.hover;
    }

    // a handle's colour: lit, or its own, or greyed while another is dragged
    glm::vec3 handleColor(const EditorGizmoState& state, bool lit, const glm::vec3& base) {
        if (lit) {
            return SELECTED_COLOR;
        }
        return state.active != GizmoHandle::None ? DISABLED_COLOR : base;
    }

    // `view` is the line of sight in gizmo space
    bool axisShown(const glm::vec3& view, int axis) {
        return std::abs(view[axis]) < AXIS_HIDE_DOT;
    }
    bool planeShown(const glm::vec3& view, int axis) {
        return std::abs(view[axis]) > PLANE_HIDE_DOT;
    }

    // the part of the camera's direction lying in the plane of the ring about `axis`
    glm::vec3 acrossRing(int axis, const glm::vec3& toCamera) {
        glm::vec3 normal = unitAxis(axis);
        return toCamera - normal * glm::dot(toCamera, normal);
    }

    // a point in the plane of the ring about `axis` is on the half facing the camera (all of a ring facing it is)
    bool onFrontHalf(int axis, const glm::vec3& toCamera, const glm::vec3& point) {
        glm::vec3 across = acrossRing(axis, toCamera);
        float length = glm::length(across);
        return length < RING_FACING_ACROSS || glm::dot(point, across / length) >= -RING_PICK_WIDTH;
    }

    // a circle facing `normal`
    QVector<glm::vec3> circle(const glm::vec3& normal, float radius) {
        glm::vec3 first = glm::normalize(glm::cross(normal, std::abs(normal.y) < 0.9f ? Vectors::UNIT_Y : Vectors::UNIT_X));
        glm::vec3 second = glm::cross(normal, first);
        QVector<glm::vec3> points;
        points.reserve(CIRCLE_SEGMENTS);
        for (int i = 0; i < CIRCLE_SEGMENTS; ++i) {
            float angle = TWO_PI * (float)i / (float)CIRCLE_SEGMENTS;
            points << (first * std::cos(angle) + second * std::sin(angle)) * radius;
        }
        return points;
    }

    // the half of the ring about `axis` that faces the camera, or the whole ring when it faces the camera
    QVector<glm::vec3> ringArc(int axis, const glm::vec3& toCamera, float radius, bool& closed) {
        glm::vec3 normal = unitAxis(axis);
        glm::vec3 across = acrossRing(axis, toCamera);
        if (glm::length(across) < RING_FACING_ACROSS) {
            closed = true;
            return circle(normal, radius);
        }
        closed = false;
        glm::vec3 front = glm::normalize(across);
        glm::vec3 side = glm::cross(normal, front);
        const int segments = CIRCLE_SEGMENTS / 2;
        QVector<glm::vec3> points;
        points.reserve(segments + 1);
        for (int i = 0; i <= segments; ++i) {
            float angle = PI * ((float)i / (float)segments - 0.5f);
            points << (front * std::cos(angle) + side * std::sin(angle)) * radius;
        }
        return points;
    }

    // The gizmo's lines, as thin strips turned to face the camera so that they keep their width on screen,
    // and the squares' faces: one pass's worth goes into one vertex buffer, in gizmo space.
    class GizmoLines {
    public:
        explicit GizmoLines(const glm::vec3& toCamera) : _toCamera(toCamera) {}

        bool isEmpty() const { return _points.isEmpty(); }

        void addLine(const QVector<glm::vec3>& line, bool closed, float width, const glm::vec4& color) {
            int count = line.size();
            if (count < 2) {
                return;
            }
            // each point is widened across the line's direction there, so the pieces of a curve meet
            QVector<glm::vec3> across(count);
            for (int i = 0; i < count; ++i) {
                const glm::vec3& previous = i > 0 ? line[i - 1] : (closed ? line[count - 1] : line[i]);
                const glm::vec3& next = i < count - 1 ? line[i + 1] : (closed ? line[0] : line[i]);
                across[i] = acrossLine(next - previous, width * 0.5f);
            }
            int pieces = closed ? count : count - 1;
            for (int i = 0; i < pieces; ++i) {
                int j = (i + 1) % count;
                glm::vec3 a0 = line[i] + across[i];
                glm::vec3 a1 = line[i] - across[i];
                glm::vec3 b0 = line[j] + across[j];
                glm::vec3 b1 = line[j] - across[j];
                push(a0, color);
                push(a1, color);
                push(b0, color);
                push(b0, color);
                push(a1, color);
                push(b1, color);
            }
        }

        // a straight line, longer by half its width at each end so that lines meeting at a corner close it
        void addSegment(const glm::vec3& from, const glm::vec3& to, float width, const glm::vec4& color) {
            glm::vec3 direction = to - from;
            float length = glm::length(direction);
            if (length < 1.0e-6f) {
                return;
            }
            glm::vec3 extra = direction * (width * 0.5f / length);
            addLine({ from - extra, to + extra }, false, width, color);
        }

        void addQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color) {
            push(a, color);
            push(b, color);
            push(c, color);
            push(a, color);
            push(c, color);
            push(d, color);
        }

        void draw(gpu::Batch& batch, GeometryCache& geometryCache, int bufferId) const {
            geometryCache.updateVertices(bufferId, _points, _colors);
            geometryCache.renderVertices(batch, gpu::TRIANGLES, bufferId);
        }

    private:
        // half the line's width, across `direction` and facing the camera
        glm::vec3 acrossLine(const glm::vec3& direction, float halfWidth) const {
            glm::vec3 across = glm::cross(direction, _toCamera);
            float length = glm::length(across);
            if (length < 1.0e-6f) {
                // seen end on: any direction across it will do
                across = glm::cross(direction, std::abs(direction.y) < 0.9f * glm::length(direction) ? Vectors::UNIT_Y : Vectors::UNIT_X);
                length = glm::length(across);
                if (length < 1.0e-6f) {
                    return glm::vec3(0.0f);
                }
            }
            return across * (halfWidth / length);
        }

        void push(const glm::vec3& point, const glm::vec4& color) {
            _points << point;
            _colors << color;
        }

        glm::vec3 _toCamera;
        QVector<glm::vec3> _points;
        QVector<glm::vec4> _colors;
    };

    // One pass of the gizmo, opaque or translucent: solid parts (cones, cubes) are drawn as they come, the
    // lines are collected and drawn by finish().
    class GizmoPass {
    public:
        GizmoPass(RenderArgs* args, const EditorGizmoState& state, bool translucent) :
            view(glm::inverse(state.rotation) * state.viewDirection),
            lines(-view),
            _args(args),
            _translucent(translucent),
            _gizmo(state.rotation, glm::vec3(state.scale), state.position),
            _gizmoMatrix(createMatFromQuatAndPos(state.rotation, state.position) * glm::scale(glm::mat4(1.0f), glm::vec3(state.scale))) {
        }

        // a unit shape (centred, fitting a unit cube; a cone points along +Y) placed inside the gizmo
        void shape(GeometryCache::Shape shape, const glm::quat& rotation, const glm::vec3& scale,
                   const glm::vec3& translation, const glm::vec3& color) {
            auto geometryCache = DependencyManager::get<GeometryCache>();
            glm::mat4 part = createMatFromQuatAndPos(rotation, translation) * glm::scale(glm::mat4(1.0f), scale);
            Transform transform;
            transform.evalFromRawMatrix(_gizmoMatrix * part);
            _args->_batch->setModelTransform(transform);
            geometryCache->renderSolidShapeInstance(_args, *_args->_batch, shape, glm::vec4(color, 1.0f),
                geometryCache->getShapePipelinePointer(_translucent, true, true));
        }

        void finish(int bufferId) {
            if (lines.isEmpty()) {
                return;
            }
            auto geometryCache = DependencyManager::get<GeometryCache>();
            auto& batch = *_args->_batch;
            // the colour is in the vertices; drawn from both sides
            geometryCache->bindSimpleProgram(batch, false, _translucent, true, false, true, true, graphics::MaterialKey::CULL_NONE);
            batch.setModelTransform(_gizmo);
            lines.draw(batch, *geometryCache, bufferId);
        }

        const glm::vec3 view;   // the line of sight, in gizmo space
        GizmoLines lines;

    private:
        RenderArgs* _args;
        bool _translucent;
        Transform _gizmo;
        glm::mat4 _gizmoMatrix;
    };

    void drawMove(GizmoPass& pass, const EditorGizmoState& state, bool translucent) {
        GizmoHandle lit = litHandle(state);
        for (int axis = 0; axis < 3; ++axis) {
            GizmoHandle handle = planeHandle(axis);
            if (state.active != handle && !planeShown(pass.view, axis)) {
                continue;
            }
            // the square facing this axis, between the other two, on the camera's side of each
            int b = (axis + 1) % 3;
            int c = (axis + 2) % 3;
            glm::vec3 u = unitAxis(b) * (PLANE_SIZE * state.planeSigns[b]);
            glm::vec3 v = unitAxis(c) * (PLANE_SIZE * state.planeSigns[c]);
            glm::vec3 color = handleColor(state, lit == handle, AXIS_COLORS[axis]);
            if (translucent) {
                pass.lines.addQuad(glm::vec3(0.0f), u, u + v, v, glm::vec4(color, lit == handle ? PLANE_HOVER_ALPHA : PLANE_ALPHA));
            } else {
                // the outer edges, and an inner edge only where no axis line runs along it
                glm::vec4 edge(color, 1.0f);
                pass.lines.addSegment(u, u + v, PLANE_LINE_WIDTH, edge);
                pass.lines.addSegment(v, u + v, PLANE_LINE_WIDTH, edge);
                if (state.planeSigns[b] < 0.0f) {
                    pass.lines.addSegment(glm::vec3(0.0f), u, PLANE_LINE_WIDTH, edge);
                }
                if (state.planeSigns[c] < 0.0f) {
                    pass.lines.addSegment(glm::vec3(0.0f), v, PLANE_LINE_WIDTH, edge);
                }
            }
        }
        if (translucent) {
            return;
        }
        for (int axis = 0; axis < 3; ++axis) {
            GizmoHandle handle = axisHandle(axis);
            if (state.active != handle && !axisShown(pass.view, axis)) {
                continue;
            }
            // an axis lights up with a square that contains it, as in Unity
            bool axisLit = lit == handle || (isPlane(lit) && axisIndex(lit) != axis);
            glm::vec3 color = handleColor(state, axisLit, AXIS_COLORS[axis]);
            glm::vec3 direction = unitAxis(axis);
            pass.lines.addSegment(glm::vec3(0.0f), direction * (AXIS_END - CONE_LENGTH), AXIS_LINE_WIDTH, glm::vec4(color, 1.0f));
            pass.shape(GeometryCache::Cone, Y_TO_AXIS[axis], glm::vec3(CONE_RADIUS * 2.0f, CONE_LENGTH, CONE_RADIUS * 2.0f),
                direction * (AXIS_END - CONE_LENGTH * 0.5f), color);
        }
    }

    void drawRotate(GizmoPass& pass, const EditorGizmoState& state, bool translucent) {
        glm::vec3 toCamera = -pass.view;
        GizmoHandle lit = litHandle(state);
        if (translucent) {
            // the faint circle round the trackball, brighter while it is hovered or turning
            if (state.active == GizmoHandle::None || state.active == GizmoHandle::Sphere) {
                glm::vec4 color = lit == GizmoHandle::Sphere ? glm::vec4(SELECTED_COLOR, SPHERE_HOVER_ALPHA) : SPHERE_COLOR;
                pass.lines.addLine(circle(toCamera, SPHERE_RADIUS), true, SPHERE_LINE_WIDTH, color);
            }
            return;
        }
        for (int axis = 0; axis < 3; ++axis) {
            GizmoHandle handle = ringHandle(axis);
            bool closed = false;
            QVector<glm::vec3> arc = ringArc(axis, toCamera, RING_RADIUS, closed);
            glm::vec3 color = handleColor(state, lit == handle, AXIS_COLORS[axis]);
            pass.lines.addLine(arc, closed, lit == handle ? RING_HOVER_LINE_WIDTH : RING_LINE_WIDTH, glm::vec4(color, 1.0f));
        }
        bool viewLit = lit == GizmoHandle::RingView;
        glm::vec3 color = handleColor(state, viewLit, CENTER_COLOR);
        pass.lines.addLine(circle(toCamera, VIEW_RING_RADIUS), true, viewLit ? RING_HOVER_LINE_WIDTH : VIEW_RING_LINE_WIDTH,
            glm::vec4(color, 1.0f));
    }

    void drawScale(GizmoPass& pass, const EditorGizmoState& state, bool translucent) {
        if (translucent) {
            return;
        }
        GizmoHandle lit = litHandle(state);
        for (int axis = 0; axis < 3; ++axis) {
            GizmoHandle handle = scaleHandle(axis);
            if (state.active != handle && !axisShown(pass.view, axis)) {
                continue;
            }
            // all three light up with the centre cube
            bool axisLit = lit == handle || lit == GizmoHandle::ScaleUniform;
            glm::vec3 color = handleColor(state, axisLit, AXIS_COLORS[axis]);
            glm::vec3 direction = unitAxis(axis);
            float end = AXIS_END * state.stretch[axis];
            if (end > SCALE_CUBE * 0.5f) {
                pass.lines.addSegment(glm::vec3(0.0f), direction * (end - SCALE_CUBE * 0.5f), AXIS_LINE_WIDTH, glm::vec4(color, 1.0f));
            }
            pass.shape(GeometryCache::Cube, glm::quat(), glm::vec3(SCALE_CUBE), direction * end, color);
        }
        pass.shape(GeometryCache::Cube, glm::quat(), glm::vec3(SCALE_CENTER_CUBE), glm::vec3(0.0f),
            handleColor(state, lit == GizmoHandle::ScaleUniform, CENTER_COLOR));
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
    int& bufferId = _lineIds[translucentPass ? 1 : 0];
    if (bufferId < 0) {
        bufferId = DependencyManager::get<GeometryCache>()->allocateID();
    }
    GizmoPass pass(args, state, translucentPass);
    switch (state.mode) {
        case GizmoMode::Move:
            drawMove(pass, state, translucentPass);
            break;
        case GizmoMode::Rotate:
            drawRotate(pass, state, translucentPass);
            break;
        case GizmoMode::Scale:
            drawScale(pass, state, translucentPass);
            break;
    }
    pass.finish(bufferId);
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
    // the scene files an item by its bound only when the item is added or updated: without this it kept
    // culling the gizmo against the place it first appeared, so the gizmo vanished once the camera looked
    // away from there (after framing the whole island, say)
    if (render::Item::isValidID(_opaqueItem)) {
        if (auto scene = qApp->getMain3DScene()) {
            render::Transaction transaction;
            transaction.updateItem(_opaqueItem);
            transaction.updateItem(_planesItem);
            scene->enqueueTransaction(transaction);
        }
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
    // the Move squares sit on the camera's side of each axis (they stay put during a drag)
    if (!_dragging) {
        glm::vec3 view = glm::inverse(rotation) * viewDirection;
        for (int axis = 0; axis < 3; ++axis) {
            if (view[axis] > PLANE_FLIP_MARGIN) {
                _state.planeSigns[axis] = -1.0f;
            } else if (view[axis] < -PLANE_FLIP_MARGIN) {
                _state.planeSigns[axis] = 1.0f;
            }
        }
    }
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
    glm::vec3 view = inverse * _state.viewDirection;   // hidden handles cannot be picked

    GizmoHandle best = GizmoHandle::None;
    float bestT = FLT_MAX;
    float t;

    switch (_state.mode) {
        case GizmoMode::Move:
            // the axes first: where one runs along a square's edge, it wins (Unity draws them on top)
            for (int axis = 0; axis < 3; ++axis) {
                if (axisShown(view, axis) &&
                    rayHitsSegment(origin, direction, unitAxis(axis), AXIS_PICK_START, AXIS_END, AXIS_PICK_RADIUS, t) && t < bestT) {
                    best = axisHandle(axis);
                    bestT = t;
                }
            }
            if (best != GizmoHandle::None) {
                break;
            }
            for (int axis = 0; axis < 3; ++axis) {
                if (!planeShown(view, axis) || std::abs(direction[axis]) < 1.0e-5f) {
                    continue;
                }
                t = -origin[axis] / direction[axis];
                if (t <= 0.0f || t >= bestT) {
                    continue;
                }
                glm::vec3 hit = origin + direction * t;
                int b = (axis + 1) % 3;
                int c = (axis + 2) % 3;
                float alongB = hit[b] * _state.planeSigns[b];
                float alongC = hit[c] * _state.planeSigns[c];
                if (alongB >= -PLANE_PICK_MARGIN && alongB <= PLANE_SIZE + PLANE_PICK_MARGIN &&
                    alongC >= -PLANE_PICK_MARGIN && alongC <= PLANE_SIZE + PLANE_PICK_MARGIN) {
                    best = planeHandle(axis);
                    bestT = t;
                }
            }
            break;

        case GizmoMode::Rotate: {
            glm::vec3 toCamera = -view;
            for (int axis = 0; axis < 3; ++axis) {
                if (std::abs(direction[axis]) < 1.0e-5f) {
                    continue;
                }
                t = -origin[axis] / direction[axis];
                if (t <= 0.0f || t >= bestT) {
                    continue;
                }
                glm::vec3 hit = origin + direction * t;
                // only the half facing the camera is drawn, so only that half can be picked
                if (std::abs(glm::length(hit) - RING_RADIUS) < RING_PICK_WIDTH && onFrontHalf(axis, toCamera, hit)) {
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
                if (!axisShown(view, axis)) {
                    continue;
                }
                float end = AXIS_END * _state.stretch[axis] + SCALE_CUBE * 0.5f;
                if (rayHitsSegment(origin, direction, unitAxis(axis), SCALE_PICK_START, end, AXIS_PICK_RADIUS, t) && t < bestT) {
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
        case GizmoHandle::PlaneXY: {
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
