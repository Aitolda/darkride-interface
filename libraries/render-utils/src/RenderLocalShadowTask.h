//
//  RenderLocalShadowTask.h
//  render-utils/src/
//
//  Shadows from spot lights. Each frame the spot lights that cast shadows get a shadow map, drawn into a tile of one
//  depth atlas. The light loop the deferred and transparent shading share (LightLocal.slh) finds a light's tile
//  through a small table (LocalShadow.slh). One light at a time for now; the atlas and the table have room for more.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_RenderLocalShadowTask_h
#define hifi_RenderLocalShadowTask_h

#include <vector>

#include <gpu/Framebuffer.h>

#include <render/CullTask.h>

#include "LightingModel.h"
#include "LightStage.h"
#include "RenderShadowTask.h"

class ViewFrustum;

// The atlas the shadow maps are drawn into, and the table the shaders read it by
class LocalShadows {
public:
    static const int ATLAS_SIZE;    // texels a side
    static const int TILE_SIZE;
    static const int LIGHT_COUNT;   // lights the table covers: the size of the shaders' light array

    LocalShadows();

    const gpu::TexturePointer& getAtlas() const { return _atlas; }
    const gpu::FramebufferPointer& getFramebuffer() const { return _framebuffer; }
    const gpu::BufferPointer& getTable() const { return _table; }

    // a table in which no light has a shadow, for when there is no atlas
    static const gpu::BufferPointer& getEmptyTable();

    int getTileCount() const;
    glm::ivec4 getTileViewport(int tile) const;

    void clearTiles();
    // worldToTile: from world space to the light's view, projected and mapped onto the tile
    void setTile(int tile, LightStage::Index lightIndex, const glm::mat4& worldToTile, float texelSizePerMeter);

private:
    void uploadTable();

    gpu::TexturePointer _atlas;
    gpu::FramebufferPointer _framebuffer;
    gpu::BufferPointer _table;
    std::vector<glm::vec4> _tableData;
};

class RenderLocalShadowTask {
public:
    using Input = render::VaryingSet3<LightStage::FramePointer, LightingModelPointer, LightStage::ShadowFramePointer>;
    using JobModel = render::Task::ModelI<RenderLocalShadowTask, Input>;

    void build(JobModel& task, const render::Varying& input, render::Varying& output, uint8_t tagBits = 0x00, uint8_t tagMask = 0x00);
};

// Picks the light, points the view down it and fills in the table; stops the task when no light needs a shadow
class RenderLocalShadowSetup {
public:
    using Input = RenderLocalShadowTask::Input;
    using Output = render::VaryingSet6<RenderArgs::RenderMode, glm::ivec2, LocalShadowsPointer, graphics::LightPointer,
                                       render::ItemFilter, RenderShadowTask::CullFunctor>;
    using JobModel = render::Job::ModelIO<RenderLocalShadowSetup, Input, Output>;

    RenderLocalShadowSetup(render::ItemFilter filter);
    void run(const render::RenderContextPointer& renderContext, const Input& input, Output& output);

private:
    render::ItemFilter _filter;
    std::shared_ptr<ViewFrustum> _frustum;
    LocalShadowsPointer _localShadows;
};

class RenderLocalShadowMap {
public:
    using Inputs = render::VaryingSet2<render::ShapeBounds, LocalShadowsPointer>;
    using JobModel = render::Job::ModelI<RenderLocalShadowMap, Inputs>;

    RenderLocalShadowMap(render::ShapePlumberPointer shapePlumber) : _shapePlumber{ shapePlumber } {}
    void run(const render::RenderContextPointer& renderContext, const Inputs& inputs);

private:
    render::ShapePlumberPointer _shapePlumber;
};

class RenderLocalShadowTeardown {
public:
    using Input = RenderLocalShadowSetup::Output;
    using JobModel = render::Job::ModelI<RenderLocalShadowTeardown, Input>;
    void run(const render::RenderContextPointer& renderContext, const Input& input);
};

#endif // hifi_RenderLocalShadowTask_h
