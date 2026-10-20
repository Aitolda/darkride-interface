//
//  RenderableTrackEntityItem.cpp
//  libraries/entities-renderer/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderableTrackEntityItem.h"

#include <AbstractViewStateInterface.h>
#include <EntityTree.h>
#include <GLMHelpers.h>
#include <SimulationFlags.h>
#include <TrackCurve.h>
#include <gpu/Batch.h>
#include <render/ShapePipeline.h>

#include "RenderPipelines.h"

using namespace render;
using namespace render::entities;

// The entity

EntityItemPointer RenderableTrackEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    std::shared_ptr<RenderableTrackEntityItem> entity(new RenderableTrackEntityItem(entityID), [](RenderableTrackEntityItem* ptr) { ptr->deleteLater(); });
    entity->setProperties(properties);
    return entity;
}

RenderableTrackEntityItem::RenderableTrackEntityItem(const EntityItemID& entityItemID) : TrackEntityItem(entityItemID) {
}

TrackGuidewaySpec RenderableTrackEntityItem::guidewaySpec() const {
    TrackGuidewaySpec spec;
    spec.profile = TrackGuidewaySpec::profileFromName(getTrackProfile().toStdString());
    spec.width = getTrackWidth();
    spec.height = getTrackHeight();
    spec.clearance = getTrackClearance();
    spec.pylonSpacing = getTrackPylonSpacing();
    spec.pylonFloor = getTrackPylonFloor();
    return spec;
}

ShapeType RenderableTrackEntityItem::getShapeType() const {
    if (_collisionless || TrackGuidewaySpec::profileFromName(getTrackProfile().toStdString()) == TrackGuidewaySpec::Profile::None) {
        return SHAPE_TYPE_NONE;
    }
    return SHAPE_TYPE_STATIC_MESH;
}

bool RenderableTrackEntityItem::shouldBePhysical() const {
    // EntityItem keeps static meshes out of physics and only models opt back in; a beam must be stood on
    return !isDead() && !isLocalEntity() && getShapeType() != SHAPE_TYPE_NONE;
}

bool RenderableTrackEntityItem::isReadyToComputeShape() const {
    if (getShapeType() == SHAPE_TYPE_NONE) {
        return true;
    }
    return resultWithReadLock<bool>([&] { return _shapeReady; });
}

void RenderableTrackEntityItem::computeShapeInfo(ShapeInfo& info) {
    if (getShapeType() == SHAPE_TYPE_NONE) {
        info.setParams(SHAPE_TYPE_NONE, 0.5f * getScaledDimensions());
        return;
    }
    withReadLock([&] { info = _shapeInfo; });
}

void RenderableTrackEntityItem::setUnscaledDimensions(const glm::vec3& value) {
    TrackEntityItem::setUnscaledDimensions(value);
    _needsRenderUpdate = true;   // the registration offset, and so the collision shape, may have moved
}

void RenderableTrackEntityItem::setRegistrationPoint(const glm::vec3& value) {
    TrackEntityItem::setRegistrationPoint(value);
    _needsRenderUpdate = true;
}

glm::vec3 RenderableTrackEntityItem::registrationOffset() const {
    // the curve is in the entity's own space, from its registration point; the collision shape is in box-centred space
    return (glm::vec3(0.5f) - getRegistrationPoint()) * getScaledDimensions();
}

bool RenderableTrackEntityItem::guidewayIsStale() const {
    return !_built || _builtTrackVersion != getTrackVersion() || _builtGuidewayVersion != getGuidewayVersion()
        || _builtOffset != registrationOffset();
}

void RenderableTrackEntityItem::rebuildGuideway() {
    uint32_t trackVersion = getTrackVersion();
    uint32_t guidewayVersion = getGuidewayVersion();
    glm::vec3 offset = registrationOffset();

    QVector<glm::vec3> points = getTrackPoints();
    QVector<float> bank = getTrackBank();
    TrackCurve curve;
    curve.build(std::vector<glm::vec3>(points.begin(), points.end()), std::vector<float>(bank.begin(), bank.end()), getTrackClosed());
    TrackGuidewayMesh mesh;
    TrackGuideway::build(curve, guidewaySpec(), mesh);

    ++_shapeSerial;
    ShapeInfo info;
    if (mesh.vertices.size() >= 3 && mesh.indices.size() >= 3) {
        ShapeInfo::PointList shapePoints;
        shapePoints.reserve(mesh.vertices.size());
        for (const TrackGuidewayMesh::Vertex& vertex : mesh.vertices) {
            shapePoints.push_back(vertex.position - offset);
        }
        ShapeInfo::TriangleIndices indices;
        indices.reserve(mesh.indices.size());
        for (uint32_t index : mesh.indices) {
            indices.push_back((int32_t)index);
        }
        // the key names this build of the shape, so the shape manager makes a new one rather than hashing the points
        info.setParams(SHAPE_TYPE_STATIC_MESH, 0.5f * getScaledDimensions(),
                       QString("track/%1/%2").arg(getID().toString()).arg(_shapeSerial));
        info.setPointCollection({ shapePoints });
        info.getTriangleIndices() = std::move(indices);
        adjustShapeInfoByRegistration(info);
    } else {
        info.setParams(SHAPE_TYPE_NONE, 0.5f * getScaledDimensions());
    }

    withWriteLock([&] {
        _mesh = std::move(mesh);
        ++_meshVersion;
        _builtTrackVersion = trackVersion;
        _builtGuidewayVersion = guidewayVersion;
        _builtOffset = offset;
        _built = true;
        _shapeInfo = info;
        _shapeReady = true;
        if (!_collisionless) {
            _flags |= Simulation::DIRTY_SHAPE | Simulation::DIRTY_MASS;
        }
    });
    if (auto tree = getTree()) {
        tree->entityChanged(getThisPointer());
    }
}

// The renderer

TrackEntityRenderer::TrackEntityRenderer(const EntityItemPointer& entity) : Parent(entity) {
    addMaterial(graphics::MaterialLayer(_material, 0), "0");
}

TrackEntityRenderer::~TrackEntityRenderer() {
}

const gpu::Stream::FormatPointer& TrackEntityRenderer::vertexFormat() {
    // one interleaved buffer in the layout of GeometryCache::ShapeVertex, the one the shape pipelines read
    static gpu::Stream::FormatPointer format;
    if (!format) {
        static_assert(sizeof(TrackGuidewayMesh::Vertex) == 11 * sizeof(float), "the guideway's vertex is position, normal, uv, tangent");
        format = std::make_shared<gpu::Stream::Format>();
        format->setAttribute(gpu::Stream::POSITION, 0, gpu::Element(gpu::VEC3, gpu::FLOAT, gpu::XYZ), offsetof(TrackGuidewayMesh::Vertex, position));
        format->setAttribute(gpu::Stream::NORMAL, 0, gpu::Element(gpu::VEC3, gpu::FLOAT, gpu::XYZ), offsetof(TrackGuidewayMesh::Vertex, normal));
        format->setAttribute(gpu::Stream::TEXCOORD0, 0, gpu::Element(gpu::VEC2, gpu::FLOAT, gpu::UV), offsetof(TrackGuidewayMesh::Vertex, uv));
        format->setAttribute(gpu::Stream::TANGENT, 0, gpu::Element(gpu::VEC3, gpu::FLOAT, gpu::XYZ), offsetof(TrackGuidewayMesh::Vertex, tangent));
    }
    return format;
}

bool TrackEntityRenderer::needsRenderUpdateFromTypedEntity(const TypedEntityPointer& entity) const {
    if (entity->guidewayIsStale() || entity->getGuidewayMeshVersion() != _meshVersion) {
        return true;
    }
    if (toGlm(entity->getColor()) != _color) {
        return true;
    }
    return false;
}

void TrackEntityRenderer::doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) {
    if (entity->guidewayIsStale()) {
        entity->rebuildGuideway();
    }
    uint64_t version = entity->getGuidewayMeshVersion();
    if (version != _meshVersion) {
        const TrackGuidewayMesh& mesh = entity->getGuidewayMesh();
        gpu::BufferPointer vertices;
        gpu::BufferPointer indices;
        uint32_t count = 0;
        if (!mesh.empty()) {
            vertices = std::make_shared<gpu::Buffer>(mesh.vertices.size() * sizeof(TrackGuidewayMesh::Vertex), (const gpu::Byte*)mesh.vertices.data());
            indices = std::make_shared<gpu::Buffer>(mesh.indices.size() * sizeof(uint32_t), (const gpu::Byte*)mesh.indices.data());
            count = (uint32_t)mesh.indices.size();
        }
        withWriteLock([&] {
            _meshVersion = version;
            _vertexBuffer = vertices;
            _indexBuffer = indices;
            _indexCount = count;
            _localBound = mesh.empty() ? AABox() : AABox(mesh.boundMin, mesh.boundMax - mesh.boundMin);
        });
    }

    void* key = (void*)this;
    AbstractViewStateInterface::instance()->pushPostUpdateLambda(key, [this, entity]() {
        withWriteLock([&] {
            // the curve is in the entity's own space: from its position (the registration point), turned by its
            // rotation, in metres whatever its dimensions
            Transform transform;
            transform.setTranslation(entity->getWorldPosition());
            transform.setRotation(entity->getWorldOrientation());
            _trackTransform = transform;
            AABox bound = _localBound;
            bound.transform(transform);
            _trackBound = bound;
        });
    });
}

void TrackEntityRenderer::doRenderUpdateAsynchronousTyped(const TypedEntityPointer& entity) {
    withWriteLock([&] {
        glm::vec3 color = toGlm(entity->getColor());
        if (_color != color) {
            _color = color;
            _material->setAlbedo(color);
            auto materials = _materials.find("0");
            if (materials != _materials.end()) {
                materials->second.setNeedsUpdate(true);
            }
        }
    });
}

ShapeKey TrackEntityRenderer::getShapeKey() {
    ShapeKey::Builder builder;
    auto mat = _materials.find("0");
    if (mat == _materials.end()) {
        return builder.withOwnPipeline().build();
    }
    if (mat->second.shouldUpdate()) {
        RenderPipelines::updateMultiMaterial(mat->second);
    }
    // the material pipeline whatever the material: the guideway is a lit mesh with its own normals, and a Material
    // entity's maps apply to it as to a model's part
    builder.withMaterial();
    if (_keyIsTransparent) {
        builder.withTranslucent();
    }
    if (_primitiveMode == PrimitiveMode::LINES) {
        builder.withWireframe();
    }
    graphics::MaterialKey drawMaterialKey = mat->second.getMaterialKey();
    if (drawMaterialKey.isNormalMap()) {
        builder.withTangents();
    }
    if (drawMaterialKey.isLightMap()) {
        builder.withLightMap();
    }
    if (drawMaterialKey.isUnlit()) {
        builder.withUnlit();
    }
    builder.withCullFaceMode(mat->second.getCullFaceMode());
    return builder.build();
}

Item::Bound TrackEntityRenderer::getBound() {
    // the guideway hangs below the ride line and the pylons below that: the mesh's own box, not the entity's
    return resultWithReadLock<Item::Bound>([&] { return _indexCount > 0 ? _trackBound : Parent::getBound(); });
}

bool TrackEntityRenderer::isTransparent() const {
    auto mat = _materials.find("0");
    if (mat != _materials.end() && mat->second.getMaterialKey().isTranslucent()) {
        return true;
    }
    return Parent::isTransparent();
}

void TrackEntityRenderer::doRender(RenderArgs* args) {
    Q_ASSERT(args->_batch);
    gpu::Batch& batch = *args->_batch;

    graphics::MultiMaterial materials;
    Transform transform;
    gpu::BufferPointer vertices;
    gpu::BufferPointer indices;
    uint32_t count = 0;
    withReadLock([&] {
        transform = _trackTransform;
        materials = _materials["0"];
        vertices = _vertexBuffer;
        indices = _indexBuffer;
        count = _indexCount;
    });
    if (!vertices || !indices || count == 0) {
        return;
    }

    batch.setModelTransform(transform);
    if (RenderPipelines::bindMaterials(materials, batch, args->_renderMode, args->_enableTexturing)) {
        args->_details._materialSwitches++;
    }
    batch.setInputFormat(vertexFormat());
    batch.setInputBuffer(0, vertices, 0, sizeof(TrackGuidewayMesh::Vertex));
    batch.setIndexBuffer(gpu::UINT32, indices, 0);
    batch.drawIndexed(gpu::TRIANGLES, count, 0);
    args->_details._trianglesRendered += (int)(count / 3);
}
