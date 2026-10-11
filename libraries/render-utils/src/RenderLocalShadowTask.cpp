//
//  RenderLocalShadowTask.cpp
//  render-utils/src/
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderLocalShadowTask.h"

#include <algorithm>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#include <gpu/Context.h>

#include <ViewFrustum.h>

#include <render/CullTask.h>
#include <render/SortTask.h>
#include <render/DrawTask.h>

#include "FadeEffect.h"

using namespace render;

extern void initZPassPipelines(ShapePlumber& plumber, gpu::StatePointer state, const render::ShapePipeline::BatchSetter& batchSetter, const render::ShapePipeline::ItemSetter& itemSetter);

const int LocalShadows::ATLAS_SIZE = 2048;
const int LocalShadows::TILE_SIZE = 1024;
const int LocalShadows::LIGHT_COUNT = 256;

namespace {
    // the table's layout, as LocalShadow.slh reads it
    const int LIGHT_TEXELS = LocalShadows::LIGHT_COUNT / 4;   // a tile number for each light, four to a texel
    const int TILE_TEXELS = 6;                                 // per tile: a matrix, the tile's bounds, its parameters

    // the lookup moves off the surface by about this many shadow map texels, along its normal and toward the light,
    // so that it doesn't shadow itself
    const float NORMAL_OFFSET_TEXELS = 1.5f;
    const float LIGHT_OFFSET_TEXELS = 1.0f;
    // texels kept clear inside a tile's edges, so that filtering never reads the next tile
    const float TILE_MARGIN_TEXELS = 1.0f;

    // a wider cone would spread its shadow map too thin, and a square map can't take in 180 degrees at all
    const float MAX_SPOT_ANGLE = glm::radians(75.0f);
    const float FIELD_OF_VIEW_MARGIN = glm::radians(2.0f);   // so the edge of the cone is filtered like its middle
    const float MIN_NEAR_CLIP = 0.05f;
    const float NEAR_CLIP_SCALE = 0.01f;

    // from the projection's [-1, 1] cube to [0, 1] texture coordinates and depth
    const glm::mat4 BIAS_MATRIX {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.5f, 0.5f, 0.5f, 1.0f };

    // from [0, 1] texture coordinates over a whole shadow map to its tile's place in the atlas
    glm::mat4 tileMatrix(const glm::ivec4& viewport) {
        float scale = (float)viewport.z / LocalShadows::ATLAS_SIZE;
        glm::vec2 offset = glm::vec2(viewport.x, viewport.y) / (float)LocalShadows::ATLAS_SIZE;
        return glm::mat4(
            scale, 0.0f, 0.0f, 0.0f,
            0.0f, scale, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            offset.x, offset.y, 0.0f, 1.0f);
    }
}

LocalShadows::LocalShadows() {
    auto depthFormat = gpu::Element(gpu::SCALAR, gpu::FLOAT, gpu::DEPTH);
    gpu::Sampler::Desc samplerDesc;
    samplerDesc._borderColor = glm::vec4(1.0f);
    samplerDesc._wrapModeU = gpu::Sampler::WRAP_BORDER;
    samplerDesc._wrapModeV = gpu::Sampler::WRAP_BORDER;
    samplerDesc._filter = gpu::Sampler::FILTER_MIN_MAG_LINEAR;
    samplerDesc._comparisonFunc = gpu::LESS;
    _atlas = gpu::TexturePointer(gpu::Texture::createRenderBuffer(depthFormat, ATLAS_SIZE, ATLAS_SIZE,
        gpu::Texture::SINGLE_MIP, gpu::Sampler(samplerDesc)));
    _framebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create("Local shadow atlas"));
    _framebuffer->setDepthBuffer(_atlas, depthFormat);

    _tableData.resize(LIGHT_TEXELS + getTileCount() * TILE_TEXELS, glm::vec4(0.0f));
    _table = std::make_shared<gpu::Buffer>();
    uploadTable();
}

const gpu::BufferPointer& LocalShadows::getEmptyTable() {
    static const gpu::BufferPointer EMPTY_TABLE = [] {
        std::vector<glm::vec4> texels(LIGHT_TEXELS, glm::vec4(0.0f));
        return std::make_shared<gpu::Buffer>(texels.size() * sizeof(glm::vec4), (const gpu::Byte*)texels.data());
    }();
    return EMPTY_TABLE;
}

int LocalShadows::getTileCount() const {
    int tilesPerSide = ATLAS_SIZE / TILE_SIZE;
    return tilesPerSide * tilesPerSide;
}

glm::ivec4 LocalShadows::getTileViewport(int tile) const {
    int tilesPerSide = ATLAS_SIZE / TILE_SIZE;
    return glm::ivec4((tile % tilesPerSide) * TILE_SIZE, (tile / tilesPerSide) * TILE_SIZE, TILE_SIZE, TILE_SIZE);
}

void LocalShadows::clearTiles() {
    std::fill(_tableData.begin(), _tableData.end(), glm::vec4(0.0f));
    uploadTable();
}

void LocalShadows::setTile(int tile, LightStage::Index lightIndex, const glm::mat4& worldToTile, float texelSizePerMeter) {
    if (tile < 0 || tile >= getTileCount() || lightIndex < 0 || lightIndex >= LIGHT_COUNT) {
        return;
    }
    _tableData[lightIndex / 4][lightIndex % 4] = (float)(tile + 1);

    glm::ivec4 viewport = getTileViewport(tile);
    glm::vec2 low = (glm::vec2(viewport.x, viewport.y) + TILE_MARGIN_TEXELS) / (float)ATLAS_SIZE;
    glm::vec2 high = (glm::vec2(viewport.x + viewport.z, viewport.y + viewport.w) - TILE_MARGIN_TEXELS) / (float)ATLAS_SIZE;

    int texel = LIGHT_TEXELS + tile * TILE_TEXELS;
    for (int column = 0; column < 4; ++column) {
        _tableData[texel + column] = worldToTile[column];
    }
    _tableData[texel + 4] = glm::vec4(low, high);
    _tableData[texel + 5] = glm::vec4(texelSizePerMeter, NORMAL_OFFSET_TEXELS, LIGHT_OFFSET_TEXELS, 1.0f / ATLAS_SIZE);
    uploadTable();
}

void LocalShadows::uploadTable() {
    _table->setData(_tableData.size() * sizeof(glm::vec4), (const gpu::Byte*)_tableData.data());
}

void RenderLocalShadowTask::build(JobModel& task, const render::Varying& input, render::Varying& output, uint8_t tagBits, uint8_t tagMask) {
    ShapePlumberPointer shapePlumber = std::make_shared<ShapePlumber>();
    {
        auto state = std::make_shared<gpu::State>();
        state->setCullMode(gpu::State::CULL_BACK);
        state->setDepthTest(true, true, gpu::LESS_EQUAL);

        auto fadeEffect = DependencyManager::get<FadeEffect>();
        initZPassPipelines(*shapePlumber, state, fadeEffect->getBatchSetter(), fadeEffect->getItemUniformSetter());
    }

    const ItemFilter casterReceiverFilter = ItemFilter::Builder::visibleWorldItems().withOpaque().withoutLayered()
        .withTagBits(tagBits, tagMask).build();
    const auto setupOutput = task.addJob<RenderLocalShadowSetup>("LocalShadowSetup", input, casterReceiverFilter);
    const auto queryResolution = setupOutput.getN<RenderLocalShadowSetup::Output>(1);
    const auto localShadows = setupOutput.getN<RenderLocalShadowSetup::Output>(2);
    const auto light = setupOutput.getN<RenderLocalShadowSetup::Output>(3);
    const auto filter = setupOutput.getN<RenderLocalShadowSetup::Output>(4);
    const auto cullFunctor = setupOutput.getN<RenderLocalShadowSetup::Output>(5);

    // what the light's view takes in, nearest first
    const auto fetchInput = FetchSpatialTree::Inputs(casterReceiverFilter, queryResolution).asVarying();
    const auto selection = task.addJob<FetchSpatialTree>("FetchLocalShadowTree", fetchInput);
    const auto selectionInputs = CullSpatialSelection::Inputs(selection, casterReceiverFilter).asVarying();
    const auto items = task.addJob<CullSpatialSelection>("FilterLocalShadowSelection", selectionInputs, nullptr, true, RenderDetails::SHADOW);
    const auto sortedPipelines = task.addJob<PipelineSortShapes>("PipelineSortLocalShadow", items);
    const auto sortedShapes = task.addJob<DepthSortShapes>("DepthSortLocalShadow", sortedPipelines, true);

    // of that, only what casts shadows
    const auto cullInputs = CullShadowBounds::Inputs(sortedShapes, filter, render::Varying(ViewFrustumPointer()), light, cullFunctor).asVarying();
    const auto casters = task.addJob<CullShadowBounds>("CullLocalShadowCasters", cullInputs);

    const auto renderInputs = RenderLocalShadowMap::Inputs(casters.getN<CullShadowBounds::Outputs>(0), localShadows).asVarying();
    task.addJob<RenderLocalShadowMap>("RenderLocalShadowMap", renderInputs, shapePlumber);
    task.addJob<RenderLocalShadowTeardown>("LocalShadowTeardown", setupOutput);
}

RenderLocalShadowSetup::RenderLocalShadowSetup(render::ItemFilter filter) :
    _filter{ filter },
    _frustum{ std::make_shared<ViewFrustum>() } {
}

void RenderLocalShadowSetup::run(const render::RenderContextPointer& renderContext, const Input& input, Output& output) {
    RenderArgs* args = renderContext->args;
    const auto& lightFrame = input.get0();
    const auto& lightingModel = input.get1();
    const auto& shadowFrame = input.get2();
    auto lightStage = renderContext->_scene->getStage<LightStage>();

    if (!_localShadows) {
        _localShadows = std::make_shared<LocalShadows>();
    }
    _localShadows->clearTiles();
    if (shadowFrame) {
        shadowFrame->_localShadows = nullptr;
    }

    // the light to give a shadow, one for now: the nearest that casts shadows and reaches into view
    LightStage::Index lightIndex = LightStage::INVALID_INDEX;
    graphics::LightPointer light;
    if (shadowFrame && lightFrame && lightStage && lightingModel &&
        lightingModel->isShadowEnabled() && lightingModel->isSpotLightEnabled()) {
        const ViewFrustum& view = args->getViewFrustum();
        float nearest = std::numeric_limits<float>::max();
        for (auto index : lightFrame->_spotLights) {
            if (index < 0 || index >= LocalShadows::LIGHT_COUNT) {
                continue;   // beyond the light array the shaders hold
            }
            auto candidate = lightStage->getLight(index);
            if (!candidate || !candidate->getCastShadows() || candidate->getSpotAngle() > MAX_SPOT_ANGLE) {
                continue;
            }
            float range = candidate->getMaximumRadius();
            float distance = glm::distance(view.getPosition(), candidate->getPosition()) - range;
            if (distance < nearest && view.sphereIntersectsKeyhole(candidate->getPosition(), range)) {
                nearest = distance;
                light = candidate;
                lightIndex = index;
            }
        }
    }
    if (!light) {
        renderContext->taskFlow.abortTask();
        return;
    }

    // the light's view: from the light down its -Z axis, wide enough for its cone, as far as it reaches
    float nearClip = std::max(MIN_NEAR_CLIP, light->getMaximumRadius() * NEAR_CLIP_SCALE);
    float farClip = std::max(light->getMaximumRadius(), 2.0f * nearClip);
    float fieldOfView = 2.0f * light->getSpotAngle() + FIELD_OF_VIEW_MARGIN;
    _frustum->setPosition(light->getPosition());
    _frustum->setOrientation(light->getOrientation());
    _frustum->setProjection(glm::perspective(fieldOfView, 1.0f, nearClip, farClip));
    _frustum->calculate();

    const int TILE = 0;
    glm::mat4 worldToTile = tileMatrix(_localShadows->getTileViewport(TILE)) * BIAS_MATRIX *
        _frustum->getProjection() * glm::inverse(_frustum->getView());
    _localShadows->setTile(TILE, lightIndex, worldToTile, 2.0f * tanf(0.5f * fieldOfView) / LocalShadows::TILE_SIZE);
    shadowFrame->_localShadows = _localShadows;

    output.edit0() = args->_renderMode;
    output.edit1() = glm::ivec2(LocalShadows::TILE_SIZE);
    output.edit2() = _localShadows;
    output.edit3() = light;
    output.edit4() = _filter;
    output.edit5() = RenderShadowTask::CullFunctor();   // every caster, however small

    args->pushViewFrustum(*_frustum);
    args->_renderMode = RenderArgs::SHADOW_RENDER_MODE;
}

void RenderLocalShadowMap::run(const render::RenderContextPointer& renderContext, const Inputs& inputs) {
    const auto& casters = inputs.get0();
    const auto& localShadows = inputs.get1();
    if (!localShadows) {
        return;
    }

    RenderArgs* args = renderContext->args;
    gpu::doInBatch("RenderLocalShadowMap::run", args->_context, [&](gpu::Batch& batch) {
        args->_batch = &batch;
        batch.enableStereo(false);

        const int TILE = 0;
        glm::ivec4 viewport = localShadows->getTileViewport(TILE);
        batch.setFramebuffer(localShadows->getFramebuffer());
        batch.setViewportTransform(viewport);
        batch.setStateScissorRect(viewport);
        batch.clearDepthFramebuffer(1.0f, true);

        glm::mat4 projection;
        Transform view;
        args->getViewFrustum().evalProjectionMatrix(projection);
        args->getViewFrustum().evalViewTransform(view);
        batch.setProjectionTransform(projection);
        batch.setViewTransform(view, false);

        renderShadowCasters(renderContext, _shapePlumber, casters);

        args->_batch = nullptr;
    });
}

void RenderLocalShadowTeardown::run(const render::RenderContextPointer& renderContext, const Input& input) {
    RenderArgs* args = renderContext->args;
    if (args->_renderMode == RenderArgs::SHADOW_RENDER_MODE) {
        args->popViewFrustum();
    }
    args->_renderMode = input.get0();
}
