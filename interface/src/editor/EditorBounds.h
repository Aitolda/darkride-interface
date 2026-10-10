//
//  EditorBounds.h
//  interface/src/editor
//
//  Wireframes for selected entities that have no surface of their own to outline: a zone's box,
//  a light's reach, a particle system's bounds. Unity draws the same gizmos for trigger volumes
//  and lights.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorBounds_h
#define hifi_EditorBounds_h

#include <memory>
#include <mutex>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <render/Forward.h>
#include <render/Item.h>
#include <render/Scene.h>

struct EditorBoundsShape {
    enum Kind { Box, Sphere };
    Kind kind { Box };
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 dimensions { 1.0f };
    glm::vec4 color { 1.0f };

    bool operator==(const EditorBoundsShape& other) const {
        return kind == other.kind && position == other.position && rotation == other.rotation
            && dimensions == other.dimensions && color == other.color;
    }
};

class EditorBoundsShared {
public:
    void setShapes(const std::vector<EditorBoundsShape>& shapes);
    void render(RenderArgs* args);
    render::Item::Bound bound() const;

private:
    mutable std::mutex _mutex;
    std::vector<EditorBoundsShape> _shapes;
    int _linesId { -1 };
};

class EditorBoundsRenderData {
public:
    using Payload = render::Payload<EditorBoundsRenderData>;
    using Pointer = Payload::DataPointer;
    explicit EditorBoundsRenderData(const std::shared_ptr<EditorBoundsShared>& shared) : _shared(shared) {}
    std::shared_ptr<EditorBoundsShared> _shared;
};

namespace render {
    template <> const ItemKey payloadGetKey(const EditorBoundsRenderData::Pointer& data);
    template <> const Item::Bound payloadGetBound(const EditorBoundsRenderData::Pointer& data);
    template <> void payloadRender(const EditorBoundsRenderData::Pointer& data, RenderArgs* args);
}

class EditorBounds {
public:
    // replaces what is drawn; an empty list draws nothing
    void setShapes(const std::vector<EditorBoundsShape>& shapes);

private:
    void ensureItem();
    std::shared_ptr<EditorBoundsShared> _shared { std::make_shared<EditorBoundsShared>() };
    render::ItemID _item { render::Item::INVALID_ITEM_ID };
    bool _empty { true };
};

#endif // hifi_EditorBounds_h
