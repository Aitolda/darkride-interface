//
//  EditorGizmo.h
//  interface/src/editor
//
//  The Move gizmo: three axis arrows, three plane squares and a centre handle, drawn by the
//  render engine in the front layer (always on top), hit-tested and dragged in C++.
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

enum class GizmoHandle {
    None,
    AxisX,
    AxisY,
    AxisZ,
    PlaneYZ,   // the square facing X
    PlaneXZ,   // the square facing Y
    PlaneXY,   // the square facing Z
    Center
};

// What the render thread needs: written on the main thread, read when drawing.
struct EditorGizmoState {
    bool visible { false };
    glm::vec3 position;
    glm::quat rotation;
    float scale { 1.0f };
    GizmoHandle hover { GizmoHandle::None };
    GizmoHandle active { GizmoHandle::None };
};

class EditorGizmoShared {
public:
    EditorGizmoState state() const;
    void setState(const EditorGizmoState& state);
    void render(RenderArgs* args, bool planes);

private:
    mutable std::mutex _mutex;
    EditorGizmoState _state;
};

// Two render items share the state: the opaque parts and the translucent plane squares.
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
    void setVisible(bool visible);
    bool isVisible() const { return _state.visible; }
    void setTransform(const glm::vec3& position, const glm::quat& rotation, float scale);
    void setHover(GizmoHandle handle);
    void setActive(GizmoHandle handle);
    const glm::vec3& position() const { return _state.position; }

    GizmoHandle hitTest(const PickRay& ray) const;

    // a drag: the world-space translation of the handle since the drag began
    void beginDrag(GizmoHandle handle, const PickRay& ray, const glm::vec3& cameraForward);
    glm::vec3 dragDelta(const PickRay& ray);
    void endDrag();
    bool isDragging() const { return _dragging; }

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
    glm::vec3 _lastDelta;
};

#endif // hifi_EditorGizmo_h
