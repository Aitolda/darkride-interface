//
//  EditorLightGizmos.h
//  interface/src/editor
//
//  Unity's light gizmos. Every light near the camera gets an icon while Gizmos is on, and a selected light always
//  does: a small cone opening the way it shines, with a line out along its beam, for a spot light; a small circle
//  with rays for a point light. Clicking an icon selects its light, which can't be picked otherwise (a light has
//  no surface). A light selected on its own also gets Unity's handles: a box at the end of a spot light's beam for
//  its range and four round the rim of its cone for its angle, six round a point light's sphere for its range.
//  Icons and handles keep their size on screen and are drawn over everything.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorLightGizmos_h
#define hifi_EditorLightGizmos_h

#include <memory>
#include <mutex>
#include <vector>

#include <QtCore/QUuid>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <RegisteredMetaTypes.h>
#include <render/Forward.h>
#include <render/Item.h>
#include <render/Scene.h>

#include "EditorGizmoLines.h"

// a light, as its gizmo shows it
struct EditorLightGizmoLight {
    QUuid id;
    bool spot { false };
    glm::vec3 position;
    glm::quat rotation;     // a spot light shines down its -Z axis
    float range { 0.0f };
    float angle { 0.0f };   // a spot light's, from its axis to its side, in radians
    bool selected { false };
};

// one of the handles of the light that has them
struct EditorLightHandle {
    enum Kind { None, Range, Angle };
    Kind kind { None };
    int index { -1 };   // which box: the range box at the end of a spot light's beam is 0

    bool isNone() const { return kind == None; }
    bool operator==(const EditorLightHandle& other) const { return kind == other.kind && index == other.index; }
    bool operator!=(const EditorLightHandle& other) const { return !(*this == other); }
};

class EditorLightGizmosShared {
public:
    void setLines(const EditorGizmoLines& lines, const glm::vec3& low, const glm::vec3& high);
    void render(RenderArgs* args);
    render::Item::Bound bound() const;

private:
    mutable std::mutex _mutex;
    EditorGizmoLines _lines;
    glm::vec3 _low { 0.0f };
    glm::vec3 _high { 0.0f };
    int _bufferId { -1 };
};

class EditorLightGizmosRenderData {
public:
    using Payload = render::Payload<EditorLightGizmosRenderData>;
    using Pointer = Payload::DataPointer;
    explicit EditorLightGizmosRenderData(const std::shared_ptr<EditorLightGizmosShared>& shared) : _shared(shared) {}
    std::shared_ptr<EditorLightGizmosShared> _shared;
};

namespace render {
    template <> const ItemKey payloadGetKey(const EditorLightGizmosRenderData::Pointer& data);
    template <> const Item::Bound payloadGetBound(const EditorLightGizmosRenderData::Pointer& data);
    template <> void payloadRender(const EditorLightGizmosRenderData::Pointer& data, RenderArgs* args);
}

class EditorLightGizmos {
public:
    // the lights to show and the one that gets handles (a null id for none), seen from the camera; pixelSize is
    // the size of a viewport pixel one metre in front of the camera, which keeps icons and handles a set size
    void update(const std::vector<EditorLightGizmoLight>& lights, const QUuid& handlesFor,
                const glm::vec3& cameraPosition, const glm::vec3& viewDirection, float pixelSize);
    void clear();

    // the light whose icon the ray meets first, or a null id
    QUuid iconAt(const PickRay& ray) const;
    EditorLightHandle handleAt(const PickRay& ray) const;
    void setHover(const EditorLightHandle& handle);
    void setActive(const EditorLightHandle& handle);

    // the light that has handles, as last shown
    bool handleLight(EditorLightGizmoLight& light) const;
    // what a handle's drag asks for, from where the pointer is: a range in metres, or an angle in radians
    float rangeAt(const PickRay& ray, const EditorLightHandle& handle) const;
    float angleAt(const PickRay& ray, const EditorLightHandle& handle) const;

private:
    struct Spot {
        glm::vec3 center;
        float radius;
    };
    void rebuild();
    std::vector<glm::vec3> handlePositions(const EditorLightGizmoLight& light) const;
    glm::vec3 handleDirection(const EditorLightGizmoLight& light, int index) const;
    float pixelsAt(const glm::vec3& point) const;   // the size of a pixel there
    void ensureItem();
    void publish(const EditorGizmoLines& lines, const glm::vec3& low, const glm::vec3& high);

    std::vector<EditorLightGizmoLight> _lights;
    EditorLightGizmoLight _handleLight;
    bool _hasHandleLight { false };
    glm::vec3 _cameraPosition;
    glm::vec3 _viewDirection { 0.0f, 0.0f, -1.0f };
    float _pixelSize { 0.001f };
    EditorLightHandle _hover;
    EditorLightHandle _active;

    std::vector<std::pair<QUuid, Spot>> _iconSpots;   // where each icon can be clicked

    std::shared_ptr<EditorLightGizmosShared> _shared { std::make_shared<EditorLightGizmosShared>() };
    render::ItemID _item { render::Item::INVALID_ITEM_ID };
    bool _empty { true };
};

#endif // hifi_EditorLightGizmos_h
