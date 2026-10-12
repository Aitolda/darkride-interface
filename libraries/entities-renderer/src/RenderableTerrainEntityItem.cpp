//
//  RenderableTerrainEntityItem.cpp
//  libraries/entities-renderer/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderableTerrainEntityItem.h"

#include <cfloat>
#include <mutex>

#include <glm/gtx/transform.hpp>
#include <QUrl>
#include <QUrlQuery>

#include <AABox.h>
#include <DependencyManager.h>
#include <EntityTree.h>
#include <GeometryUtil.h>
#include <ResourceManager.h>
#include <ResourceRequest.h>
#include <SimulationFlags.h>
#include <StencilMaskPass.h>
#include <render/ShapePipeline.h>
#include <shaders/Shaders.h>
#include <terrain/TerrainFile.h>

#include "EntitiesRendererLogging.h"
#include "entities-renderer/ShaderConstants.h"

using namespace render;
using namespace render::entities;

namespace {

struct TerrainVertex {
    glm::vec3 position;
    glm::vec3 normal;
    uint32_t materials;   // the triangle's three materials in the low bytes, this corner (0-2) in the high byte;
                          // read as the colour attribute, a byte per channel
};

// The layer parameters as terrain.slf reads them (std140: vec4s and a mat4 only).
struct TerrainLayerParams {
    glm::vec4 color;    // rgb: the plain colour; a: 1 while a texture is bound
    glm::vec4 tiling;   // x: repeats per metre
};
struct TerrainParams {
    glm::mat4 worldToTerrain;
    TerrainLayerParams layers[TerrainLayerSet::LAYER_COUNT];
};

// The plain colours of layers without a texture: grass, dirt, rock, sand, clay, snow, dark stone, moss.
const glm::vec3 LAYER_PALETTE[TerrainLayerSet::LAYER_COUNT] = {
    glm::vec3(0.33f, 0.52f, 0.22f), glm::vec3(0.42f, 0.30f, 0.18f), glm::vec3(0.50f, 0.50f, 0.50f), glm::vec3(0.78f, 0.70f, 0.50f),
    glm::vec3(0.62f, 0.32f, 0.22f), glm::vec3(0.92f, 0.93f, 0.95f), glm::vec3(0.28f, 0.28f, 0.30f), glm::vec3(0.24f, 0.40f, 0.20f)
};

uint8_t CUSTOM_PIPELINE_NUMBER = 0;
std::map<std::pair<bool, bool>, ShapePipelinePointer> _pipelines;   // (forward, wireframe)
gpu::Stream::FormatPointer _vertexFormat;

// The main pass asks for the chunks' pipeline through their custom shape key; shadow passes draw them with the
// standard shadow pipeline (renderShadowCasters picks it by the key's deformation bits, not the custom number).
ShapePipelinePointer terrainPipelineFactory(const ShapePlumber& plumber, const ShapeKey& key, RenderArgs* args) {
    if (_pipelines.empty()) {
        namespace program = shader::entities_renderer::program;
        for (int forward = 0; forward < 2; ++forward) {
            for (int wireframe = 0; wireframe < 2; ++wireframe) {
                auto state = std::make_shared<gpu::State>();
                state->setCullMode(gpu::State::CULL_BACK);
                state->setDepthTest(true, true, gpu::LESS_EQUAL);
                PrepareStencil::testMaskDrawShape(*state);
                if (wireframe) {
                    state->setFillMode(gpu::State::FILL_LINE);
                }
                auto shaderProgram = gpu::Shader::createProgram(forward ? program::terrain_forward : program::terrain);
                _pipelines[std::make_pair(forward != 0, wireframe != 0)] =
                    std::make_shared<ShapePipeline>(gpu::Pipeline::create(shaderProgram, state), nullptr, nullptr, nullptr);
            }
        }
    }
    return _pipelines[std::make_pair(args->_renderMethod == Args::RenderMethod::FORWARD, key.isWireframe())];
}

void initTerrainPipelines() {
    static std::once_flag once;
    std::call_once(once, [] {
        CUSTOM_PIPELINE_NUMBER = ShapePipeline::registerCustomShapePipelineFactory(terrainPipelineFactory);
        _vertexFormat = std::make_shared<gpu::Stream::Format>();
        _vertexFormat->setAttribute(gpu::Stream::POSITION, 0, gpu::Element(gpu::VEC3, gpu::FLOAT, gpu::XYZ), offsetof(TerrainVertex, position));
        _vertexFormat->setAttribute(gpu::Stream::NORMAL, 0, gpu::Element(gpu::VEC3, gpu::FLOAT, gpu::XYZ), offsetof(TerrainVertex, normal));
        _vertexFormat->setAttribute(gpu::Stream::COLOR, 0, gpu::Element(gpu::VEC4, gpu::NUINT8, gpu::RGBA), offsetof(TerrainVertex, materials));
    });
}

} // namespace

// The entity item

EntityItemPointer RenderableTerrainEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    std::shared_ptr<RenderableTerrainEntityItem> entity(new RenderableTerrainEntityItem(entityID), [](EntityItem* ptr) { ptr->deleteLater(); });
    entity->setProperties(properties);
    return entity;
}

RenderableTerrainEntityItem::RenderableTerrainEntityItem(const EntityItemID& entityItemID) : TerrainEntityItem(entityItemID) {
}

void RenderableTerrainEntityItem::makeDefaultGrid() {
    const glm::ivec3 voxelCount = getVoxelCount();
    terrain::VoxelGrid grid(voxelCount);
    grid.fillBelow(0.5f * voxelCount.y);
    replaceGrid(std::move(grid));
}

bool RenderableTerrainEntityItem::loadGrid(const QByteArray& file, QString& error) {
    terrain::VoxelGrid loaded;
    std::string why;
    if (!terrain::loadTerrain((const uint8_t*)file.constData(), (size_t)file.size(), loaded, &why)) {
        error = QString::fromStdString(why);
        return false;
    }
    const glm::ivec3 expected = getVoxelCount();
    if (expected != loaded.getVoxelCount()) {
        qCWarning(entitiesrenderer) << "Terrain" << getID() << "file holds" << loaded.getVoxelCount().x << "x" << loaded.getVoxelCount().y
                                    << "x" << loaded.getVoxelCount().z << "voxels but the entity's dimensions hold" << expected.x << "x"
                                    << expected.y << "x" << expected.z << "; drawing the file's";
    }
    replaceGrid(std::move(loaded));
    return true;
}

void RenderableTerrainEntityItem::clearGrid() {
    replaceGrid(terrain::VoxelGrid());
}

void RenderableTerrainEntityItem::replaceGrid(terrain::VoxelGrid grid) {
    _grid = std::move(grid);
    _mesher.reset();   // meshes of the old grid still being built are dropped when they come
    // a fresh grid has every chunk dirty, so the meshes within it are all replaced in time; those beyond it go now
    const terrain::ChunkSet& dirty = _grid.getDirtyChunks();
    bool erased = false;
    for (auto it = _chunkMeshes.begin(); it != _chunkMeshes.end();) {
        if (dirty.count(it->first)) {
            ++it;
        } else {
            it = _chunkMeshes.erase(it);
            erased = true;
        }
    }
    if (erased) {
        ++_meshVersion;
    }
    _shapeStale = true;
}

bool RenderableTerrainEntityItem::updateMeshes() {
    bool changed = false;
    std::vector<terrain::AsyncMesher::Result> results = _mesher.collect();
    if (!results.empty()) {
        ++_meshVersion;
        for (terrain::AsyncMesher::Result& result : results) {
            if (result.mesh.isEmpty()) {
                _chunkMeshes.erase(result.chunk);
                continue;
            }
            ChunkMeshEntry& entry = _chunkMeshes[result.chunk];
            entry.mesh = std::move(result.mesh);
            entry.version = _meshVersion;
        }
        changed = true;
    }
    _mesher.submit(_grid);
    return changed;
}

void RenderableTerrainEntityItem::updateCollisionShape() {
    ShapeInfo::PointList points;
    ShapeInfo::TriangleIndices indices;
    const glm::mat4 voxelToLocal = voxelToLocalMatrix();
    for (const auto& pair : _chunkMeshes) {
        const glm::vec3 origin(pair.first * terrain::CHUNK_SIZE);
        const uint32_t base = (uint32_t)points.size();
        for (const glm::vec3& position : pair.second.mesh.positions) {
            points.push_back(glm::vec3(voxelToLocal * glm::vec4(position + origin, 1.0f)));
        }
        for (uint32_t index : pair.second.mesh.indices) {
            indices.push_back((int32_t)(base + index));
        }
    }

    ++_shapeSerial;
    ShapeInfo info;
    if (points.size() >= 3 && indices.size() >= 3) {
        // the key names this build of the shape, so the shape manager makes a new one rather than hashing the points
        info.setParams(SHAPE_TYPE_STATIC_MESH, 0.5f * getScaledDimensions(),
                       QString("terrain/%1/%2").arg(getID().toString()).arg(_shapeSerial));
        info.setPointCollection({ points });
        info.getTriangleIndices() = std::move(indices);
        adjustShapeInfoByRegistration(info);
    } else {
        info.setParams(SHAPE_TYPE_NONE, 0.5f * getScaledDimensions());
    }
    withWriteLock([&] {
        _shapeInfo = info;
        _shapeReady = true;
        _shapeStale = false;
        if (!_collisionless) {
            _flags |= Simulation::DIRTY_SHAPE | Simulation::DIRTY_MASS;
        }
    });
    if (auto tree = getTree()) {
        tree->entityChanged(getThisPointer());
    }
}

glm::vec3 RenderableTerrainEntityItem::getVoxelScale() const {
    if (!_grid.isEmpty()) {
        return getScaledDimensions() / glm::vec3(_grid.getVoxelCount());
    }
    return glm::vec3(getVoxelSize());
}

glm::mat4 RenderableTerrainEntityItem::voxelToLocalMatrix() const {
    return glm::translate(-0.5f * getScaledDimensions()) * glm::scale(getVoxelScale());
}

glm::mat4 RenderableTerrainEntityItem::voxelToWorldMatrix() const {
    bool success = false;
    const Transform toCenter = getTransformToCenter(success);
    return toCenter.getMatrix() * voxelToLocalMatrix();
}

glm::mat4 RenderableTerrainEntityItem::worldToVoxelMatrix() const {
    return glm::inverse(voxelToWorldMatrix());
}

bool RenderableTerrainEntityItem::rayHitsGround(const glm::vec3& origin, const glm::vec3& direction, float& t, glm::vec3& normal) const {
    bool hit = false;
    float best = FLT_MAX;
    const glm::vec3 invDirection = 1.0f / direction;
    for (const auto& pair : _chunkMeshes) {
        // the chunk's mesh lies within one voxel outside the chunk on the low side and inside it elsewhere
        const glm::vec3 chunkOrigin(pair.first * terrain::CHUNK_SIZE);
        const AABox box(chunkOrigin - 1.0f, glm::vec3((float)terrain::CHUNK_SIZE + 2.0f));
        if (!box.contains(origin)) {
            float boxDistance = 0.0f;
            BoxFace boxFace = UNKNOWN_FACE;
            glm::vec3 boxNormal;
            if (!box.findRayIntersection(origin, direction, invDirection, boxDistance, boxFace, boxNormal) || boxDistance > best) {
                continue;
            }
        }
        const terrain::ChunkMesh& mesh = pair.second.mesh;
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const glm::vec3 v0 = mesh.positions[mesh.indices[i]] + chunkOrigin;
            const glm::vec3 v1 = mesh.positions[mesh.indices[i + 1]] + chunkOrigin;
            const glm::vec3 v2 = mesh.positions[mesh.indices[i + 2]] + chunkOrigin;
            float triangleDistance = 0.0f;
            // from either side: a dig is seen from inside the ground's box, and a cave's ceiling from below
            if (findRayTriangleIntersection(origin, direction, v0, v1, v2, triangleDistance, true)
                    && triangleDistance >= 0.0f && triangleDistance < best) {
                best = triangleDistance;
                normal = glm::cross(v1 - v0, v2 - v0);   // counter-clockwise seen from the air: out of the ground
                hit = true;
            }
        }
    }
    if (hit) {
        t = best;
        const float length = glm::length(normal);
        normal = length > 1.0e-12f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return hit;
}

bool RenderableTerrainEntityItem::findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction, OctreeElementPointer& element,
                                                              float& distance, BoxFace& face, glm::vec3& surfaceNormal, QVariantMap& extraInfo,
                                                              bool precisionPicking) const {
    if (_chunkMeshes.empty()) {
        return false;   // no ground yet (or none left): nothing to land on
    }
    if (!precisionPicking) {
        return true;    // the box, which the element has already met
    }
    const glm::mat4 worldToVoxel = worldToVoxelMatrix();
    const glm::vec3 voxelOrigin(worldToVoxel * glm::vec4(origin, 1.0f));
    const glm::vec3 voxelDirection(worldToVoxel * glm::vec4(direction, 0.0f));
    float t = 0.0f;
    glm::vec3 voxelNormal;
    if (!rayHitsGround(voxelOrigin, voxelDirection, t, voxelNormal)) {
        return false;
    }
    distance = t;   // the parameter along the caller's direction, as the box test measures it
    // normals go through the inverse transpose of the voxel-to-world matrix, which is the transpose of its inverse
    const glm::vec3 worldNormal(glm::transpose(worldToVoxel) * glm::vec4(voxelNormal, 0.0f));
    const float length = glm::length(worldNormal);
    surfaceNormal = length > 1.0e-12f ? worldNormal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    return true;
}

void RenderableTerrainEntityItem::setUnscaledDimensions(const glm::vec3& value) {
    const glm::vec3 before = getUnscaledDimensions();
    TerrainEntityItem::setUnscaledDimensions(value);
    if (before != getUnscaledDimensions()) {
        _shapeStale = true;   // the collision mesh is in local space and stretches with the box
    }
}

ShapeType RenderableTerrainEntityItem::getShapeType() const {
    return _collisionless ? SHAPE_TYPE_NONE : SHAPE_TYPE_STATIC_MESH;
}

bool RenderableTerrainEntityItem::shouldBePhysical() const {
    // EntityItem keeps static meshes out of physics and only models opt back in; the ground must be walked on
    return !isDead() && !isLocalEntity() && getShapeType() != SHAPE_TYPE_NONE;
}

bool RenderableTerrainEntityItem::isReadyToComputeShape() const {
    if (getShapeType() == SHAPE_TYPE_NONE) {
        return true;
    }
    return resultWithReadLock<bool>([&] { return _shapeReady; });
}

void RenderableTerrainEntityItem::computeShapeInfo(ShapeInfo& info) {
    if (getShapeType() == SHAPE_TYPE_NONE) {
        info.setParams(SHAPE_TYPE_NONE, 0.5f * getScaledDimensions());
        return;
    }
    withReadLock([&] { info = _shapeInfo; });
}

// The layer set

TerrainLayerSet::TerrainLayerSet(const std::vector<TerrainLayerSpec>& specs) {
    auto textureCache = DependencyManager::get<TextureCache>();
    for (int i = 0; i < LAYER_COUNT; ++i) {
        Layer& layer = _layers[i];
        if (i < (int)specs.size()) {
            layer.spec = specs[i];
        }
        if (!layer.spec.albedoURL.isEmpty()) {
            layer.texture = textureCache->getTexture(QUrl(layer.spec.albedoURL), image::TextureUsage::ALBEDO_TEXTURE);
        }
    }
    _params = std::make_shared<gpu::Buffer>(sizeof(TerrainParams), nullptr);
}

void TerrainLayerSet::setWorldToTerrain(const glm::mat4& worldToTerrain) {
    if (_worldToTerrain != worldToTerrain) {
        _worldToTerrain = worldToTerrain;
        _paramsDirty = true;
    }
}

void TerrainLayerSet::updateParams() {
    TerrainParams params;
    params.worldToTerrain = _worldToTerrain;
    for (int i = 0; i < LAYER_COUNT; ++i) {
        const Layer& layer = _layers[i];
        params.layers[i].color = glm::vec4(LAYER_PALETTE[i], layer.textured ? 1.0f : 0.0f);
        params.layers[i].tiling = glm::vec4(1.0f / std::max(layer.spec.tiling, 0.01f), 0.0f, 0.0f, 0.0f);
    }
    _params->setSubData(0, params);
    _paramsDirty = false;
}

void TerrainLayerSet::bind(gpu::Batch& batch) {
    auto textureCache = DependencyManager::get<TextureCache>();
    for (int i = 0; i < LAYER_COUNT; ++i) {
        Layer& layer = _layers[i];
        gpu::TexturePointer texture = layer.texture ? layer.texture->getGPUTexture() : nullptr;
        const bool textured = texture != nullptr;   // the plain colour until the texture has arrived
        if (textured != layer.textured) {
            layer.textured = textured;
            _paramsDirty = true;
        }
        batch.setResourceTexture(ENTITIES_TEXTURE_TERRAIN_LAYER0 + i, textured ? texture : textureCache->getWhiteTexture());
    }
    if (_paramsDirty) {
        updateParams();
    }
    batch.setUniformBuffer(ENTITIES_BUFFER_TERRAIN_LAYERS, _params, 0, sizeof(TerrainParams));
}

// The chunk payload

gpu::BufferPointer TerrainChunkPayload::makeVertexBuffer(const terrain::ChunkMesh& mesh) {
    // de-indexed: a triangle's three vertices share its three materials and differ in their corner
    std::vector<TerrainVertex> vertices(mesh.indices.size());
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t materials = (uint32_t)mesh.materials[mesh.indices[i]]
                                 | ((uint32_t)mesh.materials[mesh.indices[i + 1]] << 8)
                                 | ((uint32_t)mesh.materials[mesh.indices[i + 2]] << 16);
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const uint32_t index = mesh.indices[i + corner];
            TerrainVertex& vertex = vertices[i + corner];
            vertex.position = mesh.positions[index];
            vertex.normal = mesh.normals[index];
            vertex.materials = materials | (corner << 24);
        }
    }
    return std::make_shared<gpu::Buffer>(vertices.size() * sizeof(TerrainVertex), (const gpu::Byte*)vertices.data());
}

void TerrainChunkPayload::setBuffers(const gpu::BufferPointer& vertices, uint32_t vertexCount) {
    _vertexBuffer = vertices;
    _vertexCount = vertexCount;
}

void TerrainChunkPayload::setWorldToTerrain(const glm::mat4& worldToTerrain) {
    if (_layers) {
        _layers->setWorldToTerrain(worldToTerrain);
    }
}

void TerrainChunkPayload::setPlacement(const Transform& chunkToWorld) {
    _transform = chunkToWorld;
    // The mesh's vertices lie within one voxel outside the chunk on the low side (SurfaceNets.h) and inside it elsewhere.
    AABox bound(glm::vec3(-1.0f), glm::vec3((float)terrain::CHUNK_SIZE + 1.0f));
    bound.transform(chunkToWorld);
    _bound = bound;
}

ShapeKey TerrainChunkPayload::getShapeKey() const {
    ShapeKey::Builder builder = ShapeKey::Builder().withCustom(CUSTOM_PIPELINE_NUMBER);
    if (_wireframe) {
        builder.withWireframe();
    }
    return builder.build();
}

void TerrainChunkPayload::render(RenderArgs* args) {
    if (!_vertexBuffer || _vertexCount == 0 || !_layers) {
        return;
    }
    gpu::Batch& batch = *args->_batch;
    batch.setModelTransform(_transform);
    batch.setInputFormat(_vertexFormat);
    batch.setInputBuffer(0, _vertexBuffer, 0, sizeof(TerrainVertex));
    _layers->bind(batch);   // the shadow pass binds them too, and ignores them
    batch.draw(gpu::TRIANGLES, _vertexCount, 0);
}

namespace render {
template <> const ItemKey payloadGetKey(const TerrainChunkPayload::Pointer& payload) {
    return payload ? payload->getKey() : ItemKey::Builder::opaqueShape().build();
}
template <> const Item::Bound payloadGetBound(const TerrainChunkPayload::Pointer& payload) {
    return payload ? payload->getBound() : Item::Bound();
}
template <> const ShapeKey shapeGetShapeKey(const TerrainChunkPayload::Pointer& payload) {
    return payload ? payload->getShapeKey() : ShapeKey::Builder::invalid();
}
template <> void payloadRender(const TerrainChunkPayload::Pointer& payload, RenderArgs* args) {
    if (payload) {
        payload->render(args);
    }
}
}

// The renderer

TerrainEntityRenderer::TerrainEntityRenderer(const EntityItemPointer& entity) : Parent(entity) {
    initTerrainPipelines();
}

TerrainEntityRenderer::~TerrainEntityRenderer() {
    dropRequest();
}

ItemKey TerrainEntityRenderer::getKey() {
    // this item only groups the chunks, which are the shapes
    ItemKey::Builder builder = ItemKey::Builder().withTypeMeta().withTagBits(getTagMask()).withLayer(getHifiRenderLayer());
    if (!_visible) {
        builder.withInvisible();
    }
    return builder;
}

uint32_t TerrainEntityRenderer::metaFetchMetaSubItems(ItemIDs& subItems) const {
    for (const auto& pair : _chunkItems) {
        subItems.push_back(pair.second.id);
    }
    return (uint32_t)_chunkItems.size();
}

void TerrainEntityRenderer::removeFromScene(const ScenePointer& scene, Transaction& transaction) {
    for (const auto& pair : _chunkItems) {
        transaction.removeItem(pair.second.id);
    }
    _chunkItems.clear();
    Parent::removeFromScene(scene, transaction);
}

bool TerrainEntityRenderer::needsRenderUpdateFromTypedEntity(const TypedEntityPointer& entity) const {
    if (!entity->hasGridSource() || entity->getTerrainURL() != entity->getGridURL() || entity->getTerrainVersion() != entity->getGridVersion()) {
        return true;
    }
    if (!entity->meshesAreSettled() || entity->collisionShapeIsStale()) {
        return true;
    }
    if (entity->getMeshVersion() != _meshVersion) {
        return true;
    }
    if (entity->getVoxelScale() != _lastVoxelScale) {
        return true;
    }
    if (!_layerSetMade || entity->getTerrainLayers() != _lastLayersJSON) {
        return true;
    }
    if ((_primitiveMode == PrimitiveMode::LINES) != _lastWireframe) {
        return true;
    }
    return false;
}

void TerrainEntityRenderer::doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) {
    // The voxels: the file the entity points at, or flat ground while it points at none.
    const QString url = entity->getTerrainURL();
    const uint32_t version = entity->getTerrainVersion();
    if (!entity->hasGridSource() || url != entity->getGridURL() || version != entity->getGridVersion()) {
        if (url.isEmpty()) {
            dropRequest();
            entity->makeDefaultGrid();
            entity->setGridSource(url, version);
        } else if (!_request || _requestURL != url || _requestVersion != version) {
            fetchTerrain(url, version);
        }
    }

    // The meshes, which come back from the mesher's threads over a few frames, and then the collision shape, once
    // they have all come: a grid without ground (none, or cleared) settles at once and gets no shape.
    entity->updateMeshes();
    if (entity->collisionShapeIsStale() && entity->meshesAreSettled()) {
        entity->updateCollisionShape();
    }
    if (!entity->meshesAreSettled()) {
        emit requestRenderUpdate();   // the tree renderer only comes back when asked: next frame, for the rest
    }

    // The layers: what the voxel materials look like. A changed list makes a new set; the textures come in on
    // their own time and the set binds them as they do.
    const QString layersJSON = entity->getTerrainLayers();
    bool layersChanged = false;
    if (!_layerSetMade || layersJSON != _lastLayersJSON) {
        QString error;
        const std::vector<TerrainLayerSpec> specs = TerrainEntityItem::parseTerrainLayers(layersJSON, &error);
        if (!error.isEmpty()) {
            qCWarning(entitiesrenderer) << "Terrain" << entity->getID() << "layers are not usable:" << error;
        }
        _layerSet = std::make_shared<TerrainLayerSet>(specs);
        _lastLayersJSON = layersJSON;
        _layerSetMade = true;
        layersChanged = true;
    }

    // The chunk items.
    const Transform& model = getModelTransform();
    const glm::vec3 dimensions = entity->getScaledDimensions();
    const glm::vec3 voxelScale = entity->getVoxelScale();
    const bool wireframe = _primitiveMode == PrimitiveMode::LINES;
    const ItemKey key = chunkKey();
    const bool placementChanged = model != _lastTransform || dimensions != _lastDimensions || voxelScale != _lastVoxelScale;
    const bool keyChanged = key != _lastChunkKey || wireframe != _lastWireframe;
    _lastTransform = model;
    _lastDimensions = dimensions;
    _lastVoxelScale = voxelScale;
    _lastWireframe = wireframe;
    _lastChunkKey = key;
    _meshVersion = entity->getMeshVersion();
    const glm::mat4 worldToTerrain = glm::inverse(model.getMatrix());
    const TerrainLayerSet::Pointer layerSet = _layerSet;

    const auto& meshes = entity->getChunkMeshes();
    for (auto it = _chunkItems.begin(); it != _chunkItems.end();) {
        if (meshes.find(it->first) == meshes.end()) {
            transaction.removeItem(it->second.id);
            it = _chunkItems.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& pair : meshes) {
        const glm::ivec3& chunk = pair.first;
        const RenderableTerrainEntityItem::ChunkMeshEntry& entry = pair.second;
        const Transform placement = chunkToWorld(model, dimensions, voxelScale, chunk);
        auto found = _chunkItems.find(chunk);
        if (found == _chunkItems.end()) {
            auto payload = std::make_shared<TerrainChunkPayload>();
            payload->setBuffers(TerrainChunkPayload::makeVertexBuffer(entry.mesh), TerrainChunkPayload::getVertexCount(entry.mesh));
            payload->setPlacement(placement);
            payload->setKey(key);
            payload->setWireframe(wireframe);
            payload->setLayers(layerSet);
            payload->setWorldToTerrain(worldToTerrain);
            ChunkItem item;
            item.id = scene->allocateID();
            item.meshVersion = entry.version;
            transaction.resetItem(item.id, std::make_shared<Payload<TerrainChunkPayload>>(payload));
            _chunkItems[chunk] = item;
            continue;
        }
        const bool meshChanged = found->second.meshVersion != entry.version;
        if (!meshChanged && !placementChanged && !keyChanged && !layersChanged) {
            continue;
        }
        found->second.meshVersion = entry.version;
        // The buffers are built here, on the main thread, and handed over: the update runs later on the render thread.
        gpu::BufferPointer vertices = meshChanged ? TerrainChunkPayload::makeVertexBuffer(entry.mesh) : nullptr;
        const uint32_t vertexCount = TerrainChunkPayload::getVertexCount(entry.mesh);
        transaction.updateItem<TerrainChunkPayload>(found->second.id,
                [vertices, vertexCount, placement, key, wireframe, layerSet, worldToTerrain](TerrainChunkPayload& payload) {
            if (vertices) {
                payload.setBuffers(vertices, vertexCount);
            }
            payload.setPlacement(placement);
            payload.setKey(key);
            payload.setWireframe(wireframe);
            payload.setLayers(layerSet);
            payload.setWorldToTerrain(worldToTerrain);
        });
    }
}

void TerrainEntityRenderer::fetchTerrain(const QString& url, uint32_t version) {
    dropRequest();
    _requestURL = url;
    _requestVersion = version;
    // a save writes the file at the same URL, so the version goes into the request: caches on the way (the client's
    // own, a CDN's) see a new address, and the request itself asks the network rather than the local cache
    QUrl fetchURL(url);
    QUrlQuery query(fetchURL);
    query.addQueryItem("v", QString::number(version));
    fetchURL.setQuery(query);
    _request = DependencyManager::get<ResourceManager>()->createResourceRequest(this, fetchURL, true, -1, "TerrainEntityRenderer::fetchTerrain");
    if (!_request) {
        qCWarning(entitiesrenderer) << "Terrain file" << url << "cannot be fetched: no request for that kind of URL";
        if (auto entity = std::dynamic_pointer_cast<RenderableTerrainEntityItem>(_entity)) {
            entity->clearGrid();
            entity->setGridSource(url, version);
        }
        return;
    }
    _request->setCacheEnabled(false);
    connect(_request, &ResourceRequest::finished, this, [this, url, version] {
        ResourceRequest* request = _request;
        _request = nullptr;
        if (!request) {
            return;
        }
        request->deleteLater();
        auto entity = std::dynamic_pointer_cast<RenderableTerrainEntityItem>(_entity);
        if (!entity) {
            return;
        }
        if (request->getResult() != ResourceRequest::Success) {
            qCWarning(entitiesrenderer) << "Terrain file" << url << "could not be fetched:" << request->getResultString();
            entity->clearGrid();
        } else {
            QString error;
            if (!entity->loadGrid(request->getData(), error)) {
                qCWarning(entitiesrenderer) << "Terrain file" << url << "is not usable:" << error;
                entity->clearGrid();
            }
        }
        entity->setGridSource(url, version);
        entity->gridEdited();
    });
    _request->send();
}

void TerrainEntityRenderer::dropRequest() {
    if (_request) {
        _request->disconnect(this);
        _request->deleteLater();
        _request = nullptr;
    }
}

ItemKey TerrainEntityRenderer::chunkKey() const {
    ItemKey::Builder builder = ItemKey::Builder::opaqueShape().withTagBits(getTagMask()).withLayer(getHifiRenderLayer());
    if (_canCastShadow) {
        builder.withShadowCaster();
    }
    if (_cullWithParent) {
        builder.withSubMetaCulled();
    }
    if (!_visible) {
        builder.withInvisible();
    }
    return builder.build();
}

Transform TerrainEntityRenderer::chunkToWorld(const Transform& model, const glm::vec3& dimensions, const glm::vec3& voxelScale, const glm::ivec3& chunk) {
    // The entity's model transform is its box's centre; voxel (0, 0, 0) is the box's low corner.
    Transform transform = model;
    transform.postTranslate(-0.5f * dimensions);
    transform.postScale(voxelScale);
    transform.postTranslate(glm::vec3(chunk * terrain::CHUNK_SIZE));
    return transform;
}
