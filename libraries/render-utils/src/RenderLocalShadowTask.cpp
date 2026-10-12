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
#include <glm/gtx/component_wise.hpp>

#include <gpu/Context.h>
#include <shaders/Shaders.h>

#include <ViewFrustum.h>

#include <render/CullTask.h>
#include <render/SortTask.h>
#include <render/DrawTask.h>
#include <render/Scene.h>
#include <render/TransitionStage.h>

#include "FadeEffect.h"

using namespace render;

extern void initZPassPipelines(ShapePlumber& plumber, gpu::StatePointer state, const render::ShapePipeline::BatchSetter& batchSetter, const render::ShapePipeline::ItemSetter& itemSetter);

const int LocalShadows::ATLAS_SIZE = 2048;
const int LocalShadows::TILE_SIZE = 1024;
const int LocalShadows::TILE_COUNT = 4;
const int LocalShadows::LIGHT_COUNT = 256;
const int LocalShadows::COOKIE_SIZE = 512;
const int LocalShadows::COOKIE_LAYERS = 8;
const int LocalShadows::COOKIE_LIGHT_COUNT = 16;

static_assert(LocalShadows::TILE_COUNT == RenderLocalShadowTask::SLOT_COUNT, "a shadow map is drawn for each tile");
static_assert(LocalShadows::TILE_COUNT <= (LocalShadows::ATLAS_SIZE / LocalShadows::TILE_SIZE) * (LocalShadows::ATLAS_SIZE / LocalShadows::TILE_SIZE),
              "the tiles fit in the atlas");

namespace {
    // the table's layout, as LocalShadow.slh reads it: a tile number for each light, then each tile's entry, then a
    // cookie number for each light, then each cookie's entry
    const int LIGHT_TEXELS = LocalShadows::LIGHT_COUNT / 4;   // a number for each light, four to a texel
    const int TILE_TEXELS = 6;                                 // per tile: a matrix, the tile's bounds, its parameters
    const int COOKIE_MAP_TEXEL = LIGHT_TEXELS + LocalShadows::TILE_COUNT * TILE_TEXELS;
    const int COOKIE_TEXEL = COOKIE_MAP_TEXEL + LIGHT_TEXELS;
    const int COOKIE_TEXELS = 5;                               // per cookie: a matrix, its layer
    const int TABLE_TEXELS = COOKIE_TEXEL + LocalShadows::COOKIE_LIGHT_COUNT * COOKIE_TEXELS;

    // the lookup moves off the surface by about this many shadow map texels, along its normal and toward the light,
    // so that it doesn't shadow itself
    const float NORMAL_OFFSET_TEXELS = 1.5f;
    const float LIGHT_OFFSET_TEXELS = 1.0f;
    // texels kept clear inside a tile's edges, so that filtering never reads the next tile
    const float TILE_MARGIN_TEXELS = 1.0f;

    // a wider cone would spread its shadow map too thin, and a square map can't take in 180 degrees at all
    const float MAX_SPOT_ANGLE = glm::radians(75.0f);
    // a cookie spreads over the cone's whole width, but nothing projects to 180 degrees either
    const float MAX_COOKIE_SPOT_ANGLE = glm::radians(85.0f);
    // the pictures in use are copied in again this often, for the detail a picture gains as it finishes loading
    const uint64_t COOKIE_REFRESH_FRAMES = 30;
    const float FIELD_OF_VIEW_MARGIN = glm::radians(2.0f);   // so the edge of the cone is filtered like its middle
    const float MIN_NEAR_CLIP = 0.05f;
    const float NEAR_CLIP_SCALE = 0.01f;

    // a light that had a tile last frame keeps it unless another outranks it by this much, so that two lights that
    // matter about as much don't take turns at casting
    const float KEEP_TILE_BONUS = 1.25f;
    // what the lights may shine on is fetched once, through a view taking in all of them, at this resolution
    const int COARSE_QUERY_RESOLUTION = 2048;

    // from the projection's [-1, 1] cube to [0, 1] texture coordinates and depth
    const glm::mat4 BIAS_MATRIX {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.5f, 0.5f, 0.5f, 1.0f };

    // from the projection's [-1, 1] square to a picture's texture coordinates; a picture's first row (t = 0) is its
    // top, so up from the light is up in the picture: it lands as it looks to someone standing behind the light
    const glm::mat4 COOKIE_BIAS_MATRIX {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
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

    // how much a light matters to the view: how big its reach looks from the camera, which grows as the camera
    // nears it and stays large inside it, times how bright it is
    float importance(const graphics::Light& light, const glm::vec3& viewPosition) {
        float range = light.getMaximumRadius();
        float gap = std::max(glm::distance(viewPosition, light.getPosition()) - range, 0.1f * range);
        float size = range / gap;
        return size * size * light.getIntensity() * std::max(glm::compMax(light.getColor()), 0.01f);
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

    _tiles.resize(TILE_COUNT);
    _cookieLayers.resize(COOKIE_LAYERS);
    _tableData.resize(TABLE_TEXELS, glm::vec4(0.0f));
    _table = std::make_shared<gpu::Buffer>();
    uploadTable();
}

const gpu::BufferPointer& LocalShadows::getEmptyTable() {
    static const gpu::BufferPointer EMPTY_TABLE = [] {
        std::vector<glm::vec4> texels(TABLE_TEXELS, glm::vec4(0.0f));
        return std::make_shared<gpu::Buffer>(texels.size() * sizeof(glm::vec4), (const gpu::Byte*)texels.data());
    }();
    return EMPTY_TABLE;
}

glm::ivec4 LocalShadows::getTileViewport(int tile) const {
    int tilesPerSide = ATLAS_SIZE / TILE_SIZE;
    return glm::ivec4((tile % tilesPerSide) * TILE_SIZE, (tile / tilesPerSide) * TILE_SIZE, TILE_SIZE, TILE_SIZE);
}

bool LocalShadows::hasLights() const {
    return std::any_of(_tiles.begin(), _tiles.end(), [](const Tile& tile) { return (bool)tile.light; });
}

void LocalShadows::clear() {
    for (auto& tile : _tiles) {
        tile.light.reset();
        tile.lightIndex = LightStage::INVALID_INDEX;
    }
    _cookieCount = 0;
    if (++_frame % COOKIE_REFRESH_FRAMES == 0) {
        for (auto& layer : _cookieLayers) {
            layer.due = true;
        }
    }
    std::fill(_tableData.begin(), _tableData.end(), glm::vec4(0.0f));
}

void LocalShadows::setTile(int tile, const graphics::LightPointer& light, LightStage::Index lightIndex, const ViewFrustum& frustum) {
    if (tile < 0 || tile >= getTileCount() || lightIndex < 0 || lightIndex >= LIGHT_COUNT) {
        return;
    }
    auto& entry = _tiles[tile];
    entry.light = light;
    entry.lightIndex = lightIndex;
    if (!entry.frustum) {
        entry.frustum = std::make_shared<ViewFrustum>();
    }
    *entry.frustum = frustum;

    _tableData[lightIndex / 4][lightIndex % 4] = (float)(tile + 1);

    glm::ivec4 viewport = getTileViewport(tile);
    glm::vec2 low = (glm::vec2(viewport.x, viewport.y) + TILE_MARGIN_TEXELS) / (float)ATLAS_SIZE;
    glm::vec2 high = (glm::vec2(viewport.x + viewport.z, viewport.y + viewport.w) - TILE_MARGIN_TEXELS) / (float)ATLAS_SIZE;
    glm::mat4 worldToTile = tileMatrix(viewport) * BIAS_MATRIX * frustum.getProjection() * glm::inverse(frustum.getView());
    float texelSizePerMeter = 2.0f * tanf(0.5f * glm::radians(frustum.getFieldOfView())) / viewport.z;   // the frustum's field of view is in degrees

    int texel = LIGHT_TEXELS + tile * TILE_TEXELS;
    for (int column = 0; column < 4; ++column) {
        _tableData[texel + column] = worldToTile[column];
    }
    _tableData[texel + 4] = glm::vec4(low, high);
    _tableData[texel + 5] = glm::vec4(texelSizePerMeter, NORMAL_OFFSET_TEXELS, LIGHT_OFFSET_TEXELS, 1.0f / ATLAS_SIZE);
}

bool LocalShadows::tileIsStale(int tile, const render::ShapeBounds& casters, const render::Scene& scene) {
    if (tile < 0 || tile >= getTileCount() || !_tiles[tile].light || !_tiles[tile].frustum) {
        return false;
    }
    auto& entry = _tiles[tile];

    // the casters in the light's view this frame; whether one of them has changed since the map was drawn, or is in
    // a transition (fading in), which changes it every frame without a word to the scene
    _casterIds.clear();
    bool changed = false;
    for (const auto& shapes : casters) {
        for (const auto& bound : shapes.second) {
            _casterIds.push_back(bound.id);
            const auto& item = scene.getItem(bound.id);
            changed = changed || item.getChangeFrame() > entry.drawnFrame
                || !render::TransitionStage::isIndexInvalid(item.getTransitionId());
        }
    }
    std::sort(_casterIds.begin(), _casterIds.end());

    const glm::mat4& view = entry.frustum->getView();
    const glm::mat4& projection = entry.frustum->getProjection();
    if (_caching && entry.drawn && !changed && view == entry.drawnView && projection == entry.drawnProjection
            && _casterIds == entry.drawnCasters) {
        return false;
    }
    entry.drawn = true;
    entry.drawnView = view;
    entry.drawnProjection = projection;
    entry.drawnCasters.swap(_casterIds);
    entry.drawnFrame = scene.getFrame();
    return true;
}

bool LocalShadows::setCookie(LightStage::Index lightIndex, const gpu::TexturePointer& picture, const ViewFrustum& frustum) {
    if (!picture || _cookieCount >= COOKIE_LIGHT_COUNT || lightIndex < 0 || lightIndex >= LIGHT_COUNT) {
        return false;
    }
    int layer = cookieLayer(picture);
    if (layer < 0) {
        return false;
    }
    int cookie = _cookieCount++;
    _tableData[COOKIE_MAP_TEXEL + lightIndex / 4][lightIndex % 4] = (float)(cookie + 1);

    glm::mat4 worldToCookie = COOKIE_BIAS_MATRIX * frustum.getProjection() * glm::inverse(frustum.getView());
    int texel = COOKIE_TEXEL + cookie * COOKIE_TEXELS;
    for (int column = 0; column < 4; ++column) {
        _tableData[texel + column] = worldToCookie[column];
    }
    _tableData[texel + 4] = glm::vec4((float)layer, 0.0f, 0.0f, 0.0f);
    return true;
}

int LocalShadows::cookieLayer(const gpu::TexturePointer& picture) {
    // the layer the picture is in already; else an empty one, or the one unused longest, but not one another light
    // uses this frame
    int chosen = -1;
    for (int i = 0; i < (int)_cookieLayers.size(); ++i) {
        auto& layer = _cookieLayers[i];
        auto held = layer.picture.lock();
        if (held == picture) {
            layer.lastUsed = _frame;
            return i;
        }
        if (held && layer.lastUsed == _frame) {
            continue;
        }
        if (chosen < 0) {
            chosen = i;
            continue;
        }
        bool empty = !held;
        bool chosenEmpty = _cookieLayers[chosen].picture.expired();
        if ((empty && !chosenEmpty) || (empty == chosenEmpty && layer.lastUsed < _cookieLayers[chosen].lastUsed)) {
            chosen = i;
        }
    }
    if (chosen < 0) {
        return -1;
    }

    if (!_cookies) {
        // made when the first light has a cookie: sRGB like the pictures, with mips for seeing cookies from afar
        gpu::Sampler::Desc samplerDesc;
        samplerDesc._filter = gpu::Sampler::FILTER_ANISOTROPIC;
        samplerDesc._wrapModeU = gpu::Sampler::WRAP_CLAMP;
        samplerDesc._wrapModeV = gpu::Sampler::WRAP_CLAMP;
        samplerDesc._maxAnisotropy = 8;
        _cookies = gpu::Texture::createRenderBufferArray(gpu::Element::COLOR_SRGBA_32, COOKIE_SIZE, COOKIE_SIZE,
            COOKIE_LAYERS, gpu::Texture::MAX_NUM_MIPS, gpu::Sampler(samplerDesc));
        for (int i = 0; i < COOKIE_LAYERS; ++i) {
            auto framebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create("Spot light cookie"));
            framebuffer->setRenderBuffer(0, _cookies, i);
            _cookieFramebuffers.push_back(framebuffer);
        }
        _cookiePipeline = gpu::Pipeline::create(gpu::Shader::createProgram(shader::gpu::program::drawUnitQuatTextureOpaque),
            std::make_shared<gpu::State>());
    }
    auto& layer = _cookieLayers[chosen];
    layer.picture = picture;
    layer.lastUsed = _frame;
    layer.due = true;
    return chosen;
}

bool LocalShadows::cookiesDue() const {
    return std::any_of(_cookieLayers.begin(), _cookieLayers.end(), [this](const CookieLayer& layer) {
        return layer.due && layer.lastUsed == _frame && !layer.picture.expired();
    });
}

void LocalShadows::drawCookies(gpu::Batch& batch) {
    bool drawn = false;
    for (int i = 0; i < (int)_cookieLayers.size(); ++i) {
        auto& layer = _cookieLayers[i];
        auto picture = layer.picture.lock();
        if (!layer.due || layer.lastUsed != _frame || !picture) {
            continue;   // a layer not in use now is copied when it is next used
        }
        layer.due = false;
        if (!drawn) {
            batch.enableStereo(false);
            batch.setPipeline(_cookiePipeline);
            drawn = true;
        }
        // the whole picture, scaled to the layer
        batch.setFramebuffer(_cookieFramebuffers[i]);
        batch.setViewportTransform(glm::ivec4(0, 0, COOKIE_SIZE, COOKIE_SIZE));
        batch.setResourceTexture(0, picture);
        batch.draw(gpu::TRIANGLE_STRIP, 4);
    }
    if (drawn) {
        batch.setResourceTexture(0, nullptr);
        batch.generateTextureMips(_cookies);
    }
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
    const auto setupOutput = task.addJob<RenderLocalShadowSetup>("LocalShadowSetup", input);
    const auto queryResolution = setupOutput.getN<RenderLocalShadowSetup::Output>(1);
    const auto localShadows = setupOutput.getN<RenderLocalShadowSetup::Output>(2);

    // what any of the lights might shine on, fetched once through a view that takes them all in
    const auto fetchInput = FetchSpatialTree::Inputs(casterReceiverFilter, queryResolution).asVarying();
    const auto selection = task.addJob<FetchSpatialTree>("FetchLocalShadowTree", fetchInput);
    const auto selectionInputs = CullSpatialSelection::Inputs(selection, casterReceiverFilter).asVarying();
    const auto items = task.addJob<CullSpatialSelection>("FilterLocalShadowSelection", selectionInputs, nullptr, true, RenderDetails::SHADOW);
    const auto sortedPipelines = task.addJob<PipelineSortShapes>("PipelineSortLocalShadow", items);
    const auto sortedShapes = task.addJob<DepthSortShapes>("DepthSortLocalShadow", sortedPipelines, true);

    // then each tile in turn: the casters its light sees, drawn into the tile
    for (int slot = 0; slot < SLOT_COUNT; ++slot) {
        char jobName[64];
        snprintf(jobName, sizeof(jobName), "LocalShadowSlotSetup%d", slot);
        const auto slotOutput = task.addJob<RenderLocalShadowSlotSetup>(jobName, localShadows, slot, casterReceiverFilter);
        const auto slotFilter = slotOutput.getN<RenderLocalShadowSlotSetup::Output>(0);
        const auto slotLight = slotOutput.getN<RenderLocalShadowSlotSetup::Output>(1);
        const auto slotCullFunctor = slotOutput.getN<RenderLocalShadowSlotSetup::Output>(2);

        snprintf(jobName, sizeof(jobName), "CullLocalShadowCasters%d", slot);
        const auto cullInputs = CullShadowBounds::Inputs(sortedShapes, slotFilter, render::Varying(ViewFrustumPointer()),
            slotLight, slotCullFunctor).asVarying();
        const auto casters = task.addJob<CullShadowBounds>(jobName, cullInputs);

        snprintf(jobName, sizeof(jobName), "RenderLocalShadowMap%d", slot);
        const auto renderInputs = RenderLocalShadowMap::Inputs(casters.getN<CullShadowBounds::Outputs>(0), localShadows).asVarying();
        task.addJob<RenderLocalShadowMap>(jobName, renderInputs, shapePlumber, slot);

        snprintf(jobName, sizeof(jobName), "LocalShadowSlotTeardown%d", slot);
        task.addJob<RenderShadowCascadeTeardown>(jobName, slotFilter);
    }
    task.addJob<RenderLocalShadowTeardown>("LocalShadowTeardown", setupOutput);
}

RenderLocalShadowSetup::RenderLocalShadowSetup() :
    _coarseFrustum{ std::make_shared<ViewFrustum>() } {
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
    _localShadows->setCaching(_caching);
    // which light had which tile last frame
    std::vector<LightStage::Index> previousTiles;
    for (int tile = 0; tile < _localShadows->getTileCount(); ++tile) {
        previousTiles.push_back(_localShadows->getTile(tile).light ? _localShadows->getTile(tile).lightIndex : LightStage::INVALID_INDEX);
    }
    _localShadows->clear();
    if (shadowFrame) {
        shadowFrame->_localShadows = nullptr;
    }

    // the spot lights that reach into view: those that cast shadows are candidates for a tile, those whose cookie has
    // loaded get it; the ones that matter most first
    struct Candidate {
        LightStage::Index index;
        graphics::LightPointer light;
        float importance;
        gpu::TexturePointer cookie;
    };
    std::vector<Candidate> candidates;
    std::vector<Candidate> cookieLights;
    if (shadowFrame && lightFrame && lightStage && lightingModel && lightingModel->isSpotLightEnabled()) {
        const ViewFrustum& view = args->getViewFrustum();
        for (auto index : lightFrame->_spotLights) {
            if (index < 0 || index >= LocalShadows::LIGHT_COUNT) {
                continue;   // beyond the light array the shaders hold
            }
            auto light = lightStage->getLight(index);
            if (!light || !view.sphereIntersectsKeyhole(light->getPosition(), light->getMaximumRadius())) {
                continue;
            }
            float rank = importance(*light, view.getPosition());
            auto cookieSource = light->getCookie();
            gpu::TexturePointer cookie = cookieSource ? cookieSource->getGPUTexture() : gpu::TexturePointer();
            if (cookie && light->getSpotAngle() <= MAX_COOKIE_SPOT_ANGLE) {
                cookieLights.push_back({ index, light, rank, cookie });
            }
            if (lightingModel->isShadowEnabled() && light->getCastShadows() && light->getSpotAngle() <= MAX_SPOT_ANGLE) {
                bool hadTile = std::find(previousTiles.begin(), previousTiles.end(), index) != previousTiles.end();
                candidates.push_back({ index, light, hadTile ? rank * KEEP_TILE_BONUS : rank, nullptr });
            }
        }
    }
    auto byImportance = [](const Candidate& a, const Candidate& b) {
        return a.importance > b.importance;
    };
    std::sort(candidates.begin(), candidates.end(), byImportance);
    std::sort(cookieLights.begin(), cookieLights.end(), byImportance);
    if (candidates.size() > (size_t)_localShadows->getTileCount()) {
        candidates.resize(_localShadows->getTileCount());
    }

    // each cookie projected from its light down the light's -Z axis, exactly as wide as the cone, so that the picture
    // fills the square the cone fits in; a light whose picture finds no layer this frame shines without it
    ViewFrustum frustum;
    for (const auto& cookieLight : cookieLights) {
        const auto& light = cookieLight.light;
        float farClip = std::max(light->getMaximumRadius(), 2.0f * MIN_NEAR_CLIP);
        frustum.setPosition(light->getPosition());
        frustum.setOrientation(light->getOrientation());
        frustum.setProjection(glm::perspective(2.0f * light->getSpotAngle(), 1.0f, MIN_NEAR_CLIP, farClip));
        frustum.calculate();
        _localShadows->setCookie(cookieLight.index, cookieLight.cookie, frustum);
    }
    if (_localShadows->cookiesDue()) {
        gpu::doInBatch("RenderLocalShadowSetup::drawCookies", args->_context, [&](gpu::Batch& batch) {
            _localShadows->drawCookies(batch);
        });
    }

    if (candidates.empty()) {
        _localShadows->uploadTable();
        if (_localShadows->hasCookies()) {
            shadowFrame->_localShadows = _localShadows;
        }
        renderContext->taskFlow.abortTask();
        return;
    }

    // lights keep the tile they had; the others take the free ones
    std::vector<int> tiles(candidates.size(), -1);
    std::vector<bool> taken(_localShadows->getTileCount(), false);
    for (size_t i = 0; i < candidates.size(); ++i) {
        auto kept = std::find(previousTiles.begin(), previousTiles.end(), candidates[i].index);
        if (kept != previousTiles.end()) {
            tiles[i] = (int)(kept - previousTiles.begin());
            taken[tiles[i]] = true;
        }
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (tiles[i] < 0) {
            tiles[i] = (int)(std::find(taken.begin(), taken.end(), false) - taken.begin());
            taken[tiles[i]] = true;
        }
    }

    // each light's view: from the light down its -Z axis, wide enough for its cone, as far as it reaches
    AABox reach;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& light = candidates[i].light;
        float nearClip = std::max(MIN_NEAR_CLIP, light->getMaximumRadius() * NEAR_CLIP_SCALE);
        float farClip = std::max(light->getMaximumRadius(), 2.0f * nearClip);
        frustum.setPosition(light->getPosition());
        frustum.setOrientation(light->getOrientation());
        frustum.setProjection(glm::perspective(2.0f * light->getSpotAngle() + FIELD_OF_VIEW_MARGIN, 1.0f, nearClip, farClip));
        frustum.calculate();
        _localShadows->setTile(tiles[i], light, candidates[i].index, frustum);
        reach += AABox(light->getPosition() - glm::vec3(farClip), 2.0f * farClip);
    }
    _localShadows->uploadTable();
    shadowFrame->_localShadows = _localShadows;

    // a view down onto everything the lights reach, for fetching what they might shine on once for all of them
    glm::vec3 halfSize = 0.5f * reach.getScale();
    _coarseFrustum->setPosition(reach.calcCenter() + glm::vec3(0.0f, 0.0f, halfSize.z + 1.0f));
    _coarseFrustum->setOrientation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));   // looking down -Z
    _coarseFrustum->setProjection(glm::ortho(-halfSize.x, halfSize.x, -halfSize.y, halfSize.y, 0.5f, 2.0f * halfSize.z + 1.5f));
    _coarseFrustum->calculate();

    output.edit0() = args->_renderMode;
    output.edit1() = glm::ivec2(COARSE_QUERY_RESOLUTION);
    output.edit2() = _localShadows;

    args->pushViewFrustum(*_coarseFrustum);
    args->_renderMode = RenderArgs::SHADOW_RENDER_MODE;
}

void RenderLocalShadowSlotSetup::run(const render::RenderContextPointer& renderContext, const Input& input, Output& output) {
    const auto& localShadows = input;
    if (!localShadows || _slot >= localShadows->getTileCount() || !localShadows->getTile(_slot).light) {
        output.edit0() = ItemFilter::Builder::nothing();
        output.edit1() = graphics::LightPointer();
        output.edit2() = RenderShadowTask::CullFunctor();
        return;
    }
    const auto& tile = localShadows->getTile(_slot);
    renderContext->args->pushViewFrustum(*tile.frustum);
    output.edit0() = _filter;
    output.edit1() = tile.light;
    output.edit2() = RenderShadowTask::CullFunctor();   // every caster, however small
}

void RenderLocalShadowMap::run(const render::RenderContextPointer& renderContext, const Inputs& inputs) {
    const auto& casters = inputs.get0();
    const auto& localShadows = inputs.get1();
    if (!localShadows || _slot >= localShadows->getTileCount() || !localShadows->getTile(_slot).light) {
        return;
    }
    RenderArgs* args = renderContext->args;
    args->_details._spotShadowMaps++;
    if (!localShadows->tileIsStale(_slot, casters, *renderContext->_scene)) {
        return;   // the map in the tile still shows what the light sees
    }
    args->_details._spotShadowMapsDrawn++;

    gpu::doInBatch("RenderLocalShadowMap::run", args->_context, [&](gpu::Batch& batch) {
        args->_batch = &batch;
        batch.enableStereo(false);

        glm::ivec4 viewport = localShadows->getTileViewport(_slot);
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
