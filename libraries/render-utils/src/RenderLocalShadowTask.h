//
//  RenderLocalShadowTask.h
//  render-utils/src/
//
//  Shadows from spot lights. Each frame the spot lights that matter most to the view, of those that cast shadows,
//  get a shadow map each, drawn into a tile of one depth atlas. The light loop the deferred and transparent shading
//  share (LightLocal.slh) finds a light's tile through a small table (LocalShadow.slh).
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

// The atlas the shadow maps are drawn into, the table the shaders read it by, and which light has which tile
class LocalShadows {
public:
    static const int ATLAS_SIZE;    // texels a side
    static const int TILE_SIZE;
    static const int LIGHT_COUNT;   // lights the table covers: the size of the shaders' light array

    // a tile's light this frame, and the view from it
    struct Tile {
        graphics::LightPointer light;
        LightStage::Index lightIndex { LightStage::INVALID_INDEX };
        std::shared_ptr<ViewFrustum> frustum;
    };

    LocalShadows();

    const gpu::TexturePointer& getAtlas() const { return _atlas; }
    const gpu::FramebufferPointer& getFramebuffer() const { return _framebuffer; }
    const gpu::BufferPointer& getTable() const { return _table; }

    // a table in which no light has a shadow, for when there is no atlas
    static const gpu::BufferPointer& getEmptyTable();

    int getTileCount() const { return (int)_tiles.size(); }
    glm::ivec4 getTileViewport(int tile) const;
    const Tile& getTile(int tile) const { return _tiles[tile]; }
    bool hasLights() const;

    void clearTiles();
    // gives the tile to the light, seen through the frustum, and enters it in the table
    void setTile(int tile, const graphics::LightPointer& light, LightStage::Index lightIndex, const ViewFrustum& frustum);

private:
    void uploadTable();

    gpu::TexturePointer _atlas;
    gpu::FramebufferPointer _framebuffer;
    gpu::BufferPointer _table;
    std::vector<glm::vec4> _tableData;
    std::vector<Tile> _tiles;
};

class RenderLocalShadowTask {
public:
    using Input = render::VaryingSet3<LightStage::FramePointer, LightingModelPointer, LightStage::ShadowFramePointer>;
    using JobModel = render::Task::ModelI<RenderLocalShadowTask, Input>;

    // the shadow maps drawn in a frame: one per tile
    static const int SLOT_COUNT { 4 };

    void build(JobModel& task, const render::Varying& input, render::Varying& output, uint8_t tagBits = 0x00, uint8_t tagMask = 0x00);
};

// Picks the lights and gives each a tile, and takes in a view wide enough for all of them, for fetching what they
// might shine on; stops the task when no light needs a shadow
class RenderLocalShadowSetup {
public:
    using Input = RenderLocalShadowTask::Input;
    using Output = render::VaryingSet3<RenderArgs::RenderMode, glm::ivec2, LocalShadowsPointer>;
    using JobModel = render::Job::ModelIO<RenderLocalShadowSetup, Input, Output>;

    RenderLocalShadowSetup();
    void run(const render::RenderContextPointer& renderContext, const Input& input, Output& output);

private:
    std::shared_ptr<ViewFrustum> _coarseFrustum;
    LocalShadowsPointer _localShadows;
};

// One tile's turn: takes the view from its light, or selects nothing when the tile has no light this frame
class RenderLocalShadowSlotSetup {
public:
    using Input = LocalShadowsPointer;
    using Output = render::VaryingSet3<render::ItemFilter, graphics::LightPointer, RenderShadowTask::CullFunctor>;
    using JobModel = render::Job::ModelIO<RenderLocalShadowSlotSetup, Input, Output>;

    RenderLocalShadowSlotSetup(int slot, render::ItemFilter filter) : _slot{ slot }, _filter{ filter } {}
    void run(const render::RenderContextPointer& renderContext, const Input& input, Output& output);

private:
    int _slot;
    render::ItemFilter _filter;
};

class RenderLocalShadowMap {
public:
    using Inputs = render::VaryingSet2<render::ShapeBounds, LocalShadowsPointer>;
    using JobModel = render::Job::ModelI<RenderLocalShadowMap, Inputs>;

    RenderLocalShadowMap(render::ShapePlumberPointer shapePlumber, int slot) : _shapePlumber{ shapePlumber }, _slot{ slot } {}
    void run(const render::RenderContextPointer& renderContext, const Inputs& inputs);

private:
    render::ShapePlumberPointer _shapePlumber;
    int _slot;
};

class RenderLocalShadowTeardown {
public:
    using Input = RenderLocalShadowSetup::Output;
    using JobModel = render::Job::ModelI<RenderLocalShadowTeardown, Input>;
    void run(const render::RenderContextPointer& renderContext, const Input& input);
};

#endif // hifi_RenderLocalShadowTask_h
