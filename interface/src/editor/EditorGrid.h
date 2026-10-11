//
//  EditorGrid.h
//  interface/src/editor
//
//  The build-mode grid: an infinite, fading grid on the world XZ plane, drawn by the render
//  engine as one quad that follows the camera (shader: render-utils/editorGrid.slf).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorGrid_h
#define hifi_EditorGrid_h

#include <memory>
#include <mutex>

#include <glm/glm.hpp>

#include <gpu/Batch.h>
#include <gpu/Pipeline.h>
#include <render/Forward.h>
#include <render/Item.h>
#include <render/Scene.h>

// The render item. Settings are written on the main thread and read on the render thread.
class EditorGridRenderData {
public:
    using Payload = render::Payload<EditorGridRenderData>;
    using Pointer = Payload::DataPointer;

    struct Settings {
        bool visible { false };
        float minorSpacing { 1.0f };       // metres
        float majorSpacing { 10.0f };
        float fadeDistance { 250.0f };     // the lines are gone past this
        float height { 0.0f };             // world Y of the grid plane
        float pixelsPerPoint { 1.0f };     // the render target's pixels per point of the view (2 on Retina)
    };

    Settings settings() const;
    void setSettings(const Settings& settings);
    glm::vec3 lastEye() const;

    void render(RenderArgs* args);

private:
    void ensurePipelines();

    mutable std::mutex _mutex;
    Settings _settings;
    glm::vec3 _lastEye;

    gpu::PipelinePointer _pipeline;
    gpu::PipelinePointer _pipelineForward;
    gpu::BufferPointer _paramsBuffer;
    int _quadId { -1 };
};

namespace render {
    template <> const ItemKey payloadGetKey(const EditorGridRenderData::Pointer& data);
    template <> const Item::Bound payloadGetBound(const EditorGridRenderData::Pointer& data);
    template <> void payloadRender(const EditorGridRenderData::Pointer& data, RenderArgs* args);
}

// The main-thread owner: puts the grid in the scene and holds its settings.
class EditorGrid {
public:
    void setVisible(bool visible);
    bool isVisible() const { return _data->settings().visible; }

    void setHeight(float height);
    void setPixelsPerPoint(float pixelsPerPoint);
    float height() const { return _data->settings().height; }

    EditorGridRenderData::Settings settings() const { return _data->settings(); }
    void setSettings(const EditorGridRenderData::Settings& settings) { _data->setSettings(settings); }

private:
    void ensureItem();

    std::shared_ptr<EditorGridRenderData> _data { std::make_shared<EditorGridRenderData>() };
    render::ItemID _item { render::Item::INVALID_ITEM_ID };
};

#endif // hifi_EditorGrid_h
