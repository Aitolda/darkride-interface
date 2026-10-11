//
//  EditorGizmo.h
//  interface/src/editor
//
//  The transform gizmos, laid out like Unity's: Move (an arrow per axis and a small square per
//  plane, in the corner nearest the camera), Rotate (a ring per axis, of which the half facing the
//  camera is drawn, an outer ring facing the camera and a trackball inside) and Scale (a cube per
//  axis and one in the centre), drawn by the render engine in the front layer (always on top),
//  hit-tested and dragged in C++.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorGizmo_h
#define hifi_EditorGizmo_h

#include <memory>
#include <mutex>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <RegisteredMetaTypes.h>
#include <render/Forward.h>
#include <render/Item.h>
#include <render/Scene.h>

enum class GizmoMode {
    Move,
    Rotate,
    Scale
};

enum class GizmoHandle {
    None,
    AxisX,
    AxisY,
    AxisZ,
    PlaneYZ,     // the square facing X
    PlaneXZ,     // the square facing Y
    PlaneXY,     // the square facing Z
    RingX,
    RingY,
    RingZ,
    RingView,    // the outer ring: turns about the view direction
    Sphere,      // inside the rings: free rotation, like a trackball
    ScaleX,
    ScaleY,
    ScaleZ,
    ScaleUniform
};

// What the render thread needs: written on the main thread, read when drawing.
struct EditorGizmoState {
    bool visible { false };
    GizmoMode mode { GizmoMode::Move };
    glm::vec3 position;
    glm::quat rotation;
    float scale { 1.0f };
    glm::vec3 viewDirection { 0.0f, 0.0f, -1.0f };   // the line of sight, from the camera to the gizmo
    glm::vec3 stretch { 1.0f };   // the Scale gizmo's axes, stretched while dragging
    glm::vec3 planeSigns { 1.0f };   // per axis, the side the Move squares sit on (the camera's)
    GizmoHandle hover { GizmoHandle::None };
    GizmoHandle active { GizmoHandle::None };
};

class EditorGizmoShared {
public:
    EditorGizmoState state() const;
    void setState(const EditorGizmoState& state);
    void render(RenderArgs* args, bool translucentPass);

private:
    mutable std::mutex _mutex;
    EditorGizmoState _state;
    int _lineIds[2] { -1, -1 };   // the vertex buffers for the lines of each pass (opaque, translucent)
};

// Two render items share the state: the opaque parts and the translucent ones.
class EditorGizmoRenderData {
public:
    using Payload = render::Payload<EditorGizmoRenderData>;
    using Pointer = Payload::DataPointer;
    explicit EditorGizmoRenderData(const std::shared_ptr<EditorGizmoShared>& shared) : _shared(shared) {}
    std::shared_ptr<EditorGizmoShared> _shared;
};

class EditorGizmoPlanesRenderData {
public:
    using Payload = render::Payload<EditorGizmoPlanesRenderData>;
    using Pointer = Payload::DataPointer;
    explicit EditorGizmoPlanesRenderData(const std::shared_ptr<EditorGizmoShared>& shared) : _shared(shared) {}
    std::shared_ptr<EditorGizmoShared> _shared;
};

namespace render {
    template <> const ItemKey payloadGetKey(const EditorGizmoRenderData::Pointer& data);
    template <> const Item::Bound payloadGetBound(const EditorGizmoRenderData::Pointer& data);
    template <> void payloadRender(const EditorGizmoRenderData::Pointer& data, RenderArgs* args);
    template <> const ItemKey payloadGetKey(const EditorGizmoPlanesRenderData::Pointer& data);
    template <> const Item::Bound payloadGetBound(const EditorGizmoPlanesRenderData::Pointer& data);
    template <> void payloadRender(const EditorGizmoPlanesRenderData::Pointer& data, RenderArgs* args);
}

// The main-thread side: placement, hit testing and the drag maths.
class EditorGizmo {
public:
    // the gizmo's size on screen, in the viewport's pixels (points on a Retina display): one unit of the
    // gizmo's own space
    static constexpr float SIZE_PIXELS = 110.0f;

    void setVisible(bool visible);
    bool isVisible() const { return _state.visible; }
    void setMode(GizmoMode mode);
    GizmoMode mode() const { return _state.mode; }
    void setTransform(const glm::vec3& position, const glm::quat& rotation, float scale, const glm::vec3& viewDirection);
    void setHover(GizmoHandle handle);
    void setActive(GizmoHandle handle);
    void setStretch(const glm::vec3& stretch);
    const glm::vec3& position() const { return _state.position; }
    const glm::quat& rotation() const { return _state.rotation; }

    GizmoHandle hitTest(const PickRay& ray) const;

    // a drag, measured from where it began: a translation, an angle about dragAxis(), or scale factors
    void beginDrag(GizmoHandle handle, const PickRay& ray);
    glm::vec3 dragDelta(const PickRay& ray);
    float dragAngle(const PickRay& ray);
    // the trackball: mouse motion in pixels turns the selection about the axis across that motion
    glm::quat dragFree(float dx, float dy);
    glm::vec3 dragScale(const PickRay& ray);
    const glm::vec3& dragAxis() const { return _dragAxis; }
    void endDrag();
    bool isDragging() const { return _dragging; }
    GizmoHandle dragHandle() const { return _dragHandle; }

    static bool isRing(GizmoHandle handle);
    static bool isScale(GizmoHandle handle);
    static glm::vec3 axisOf(GizmoHandle handle, const glm::quat& rotation);

private:
    void ensureItems();
    void publish();

    std::shared_ptr<EditorGizmoShared> _shared { std::make_shared<EditorGizmoShared>() };
    render::ItemID _opaqueItem { render::Item::INVALID_ITEM_ID };
    render::ItemID _planesItem { render::Item::INVALID_ITEM_ID };
    EditorGizmoState _state;

    bool _dragging { false };
    GizmoHandle _dragHandle { GizmoHandle::None };
    glm::vec3 _dragOrigin;
    glm::vec3 _dragAxis;
    glm::vec3 _dragPlaneNormal;
    float _dragStartParam { 0.0f };
    glm::vec3 _dragStartPoint;
    glm::vec3 _dragStartVector;
    glm::vec3 _lastDelta;
    float _lastAngle { 0.0f };
    glm::quat _freeRotation;
    glm::vec3 _lastScale { 1.0f };
};

#endif // hifi_EditorGizmo_h
