//
//  RenderableTerrainEntityItem.cpp
//  libraries/entities-renderer/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderableTerrainEntityItem.h"

#include <mutex>

#include <glm/gtx/transform.hpp>
#include <QUrl>

#include <DependencyManager.h>
#include <EntityTree.h>
#include <ResourceManager.h>
#include <ResourceRequest.h>
#include <SimulationFlags.h>
#include <StencilMaskPass.h>
#include <render/ShapePipeline.h>
#include <shaders/Shaders.h>
#include <terrain/TerrainFile.h>

#include "EntitiesRendererLogging.h"

using namespace render;
using namespace render::entities;

namespace {

struct TerrainVertex {
    glm::vec3 position;
    glm::vec3 normal;
    uint32_t material;   // the material index in the low byte, read as the colour attribute's red channel
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
        _vertexFormat->setAttribute(gpu::Stream::COLOR, 0, gpu::Element(gpu::VEC4, gpu::NUINT8, gpu::RGBA), offsetof(TerrainVertex, material));
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
    _grid.reset(voxelCount);
    _grid.fillBelow(0.5f * voxelCount.y);
    _chunkMeshes.clear();
    _shapeStale = true;
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
    _grid = std::move(loaded);
    _chunkMeshes.clear();
    _shapeStale = true;
    return true;
}

void RenderableTerrainEntityItem::clearGrid() {
    _grid.reset(glm::ivec3(0));
    _chunkMeshes.clear();
    _shapeStale = true;
}

bool RenderableTerrainEntityItem::remeshDirtyChunks() {
    if (_grid.getDirtyChunks().empty()) {
        return false;
    }
    const terrain::ChunkSet dirty = _grid.getDirtyChunks();
    ++_meshVersion;
    for (const glm::ivec3& chunk : dirty) {
        terrain::ChunkMesh mesh = terrain::meshChunk(_grid, chunk);
        if (mesh.isEmpty()) {
            _chunkMeshes.erase(chunk);
            continue;
        }
        ChunkMeshEntry& entry = _chunkMeshes[chunk];
        entry.mesh = std::move(mesh);
        entry.version = _meshVersion;
    }
    _grid.clearDirtyChunks();
    return true;
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

// The chunk payload

gpu::BufferPointer TerrainChunkPayload::makeVertexBuffer(const terrain::ChunkMesh& mesh) {
    std::vector<TerrainVertex> vertices(mesh.getVertexCount());
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].position = mesh.positions[i];
        vertices[i].normal = mesh.normals[i];
        vertices[i].material = 0xFF000000u | mesh.materials[i];
    }
    return std::make_shared<gpu::Buffer>(vertices.size() * sizeof(TerrainVertex), (const gpu::Byte*)vertices.data());
}

gpu::BufferPointer TerrainChunkPayload::makeIndexBuffer(const terrain::ChunkMesh& mesh) {
    return std::make_shared<gpu::Buffer>(mesh.indices.size() * sizeof(uint32_t), (const gpu::Byte*)mesh.indices.data());
}

void TerrainChunkPayload::setBuffers(const gpu::BufferPointer& vertices, const gpu::BufferPointer& indices, uint32_t indexCount) {
    _vertexBuffer = vertices;
    _indexBuffer = indices;
    _indexCount = indexCount;
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
    if (!_vertexBuffer || !_indexBuffer || _indexCount == 0) {
        return;
    }
    gpu::Batch& batch = *args->_batch;
    batch.setModelTransform(_transform);
    batch.setInputFormat(_vertexFormat);
    batch.setInputBuffer(0, _vertexBuffer, 0, sizeof(TerrainVertex));
    batch.setIndexBuffer(gpu::UINT32, _indexBuffer, 0);
    batch.drawIndexed(gpu::TRIANGLES, _indexCount, 0);
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
    if (!entity->getGrid().getDirtyChunks().empty() || entity->collisionShapeIsStale()) {
        return true;
    }
    if (entity->getMeshVersion() != _meshVersion) {
        return true;
    }
    if (entity->getVoxelScale() != _lastVoxelScale) {
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

    // The meshes and the collision shape.
    if (entity->hasGrid()) {
        entity->remeshDirtyChunks();
        if (entity->collisionShapeIsStale()) {
            entity->updateCollisionShape();
        }
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
            payload->setBuffers(TerrainChunkPayload::makeVertexBuffer(entry.mesh), TerrainChunkPayload::makeIndexBuffer(entry.mesh),
                                (uint32_t)entry.mesh.indices.size());
            payload->setPlacement(placement);
            payload->setKey(key);
            payload->setWireframe(wireframe);
            ChunkItem item;
            item.id = scene->allocateID();
            item.meshVersion = entry.version;
            transaction.resetItem(item.id, std::make_shared<Payload<TerrainChunkPayload>>(payload));
            _chunkItems[chunk] = item;
            continue;
        }
        const bool meshChanged = found->second.meshVersion != entry.version;
        if (!meshChanged && !placementChanged && !keyChanged) {
            continue;
        }
        found->second.meshVersion = entry.version;
        // The buffers are built here, on the main thread, and handed over: the update runs later on the render thread.
        gpu::BufferPointer vertices = meshChanged ? TerrainChunkPayload::makeVertexBuffer(entry.mesh) : nullptr;
        gpu::BufferPointer indices = meshChanged ? TerrainChunkPayload::makeIndexBuffer(entry.mesh) : nullptr;
        const uint32_t indexCount = (uint32_t)entry.mesh.indices.size();
        transaction.updateItem<TerrainChunkPayload>(found->second.id, [vertices, indices, indexCount, placement, key, wireframe](TerrainChunkPayload& payload) {
            if (vertices) {
                payload.setBuffers(vertices, indices, indexCount);
            }
            payload.setPlacement(placement);
            payload.setKey(key);
            payload.setWireframe(wireframe);
        });
    }
}

void TerrainEntityRenderer::fetchTerrain(const QString& url, uint32_t version) {
    dropRequest();
    _requestURL = url;
    _requestVersion = version;
    _request = DependencyManager::get<ResourceManager>()->createResourceRequest(this, QUrl(url), true, -1, "TerrainEntityRenderer::fetchTerrain");
    if (!_request) {
        qCWarning(entitiesrenderer) << "Terrain file" << url << "cannot be fetched: no request for that kind of URL";
        if (auto entity = std::dynamic_pointer_cast<RenderableTerrainEntityItem>(_entity)) {
            entity->clearGrid();
            entity->setGridSource(url, version);
        }
        return;
    }
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
