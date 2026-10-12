//
//  RenderableTerrainEntityItem.h
//  libraries/entities-renderer/src
//
//  The Terrain entity on a client (TERRAIN.md). The entity item holds the voxel grid, fetched from the terrain file
//  the entity points at or made flat when it has none yet, the chunk meshes built from it by surface nets, and the
//  collision shape built from those. The renderer puts one render item per chunk in the scene, an opaque shape with
//  the terrain's own pipeline and a shadow caster, under the entity's item, which only groups them. The grid, the
//  meshes and the shape are the main thread's: the renderer's update, the editor's brushes and the physics
//  simulation all run there. Only the meshing itself runs elsewhere: the update samples the dirty chunks and hands
//  the samples to the terrain library's thread pool (terrain::AsyncMesher), and installs the meshes as they come
//  back, asking to be called again each frame until they all have.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_RenderableTerrainEntityItem_h
#define hifi_RenderableTerrainEntityItem_h

#include <map>

#include <terrain/AsyncMesher.h>
#include <terrain/SurfaceNets.h>
#include <terrain/VoxelGrid.h>
#include <TerrainEntityItem.h>

#include "RenderableEntityItem.h"

class ResourceRequest;

namespace render { namespace entities {
class TerrainEntityRenderer;
} }

class RenderableTerrainEntityItem : public TerrainEntityItem {
    friend class render::entities::TerrainEntityRenderer;
public:
    static EntityItemPointer factory(const EntityItemID& entityID, const EntityItemProperties& properties);
    RenderableTerrainEntityItem(const EntityItemID& entityItemID);

    struct ChunkMeshEntry {
        terrain::ChunkMesh mesh;
        uint64_t version { 0 };   // the mesh version the mesh was built at
    };
    using ChunkMeshes = std::map<glm::ivec3, ChunkMeshEntry, terrain::IVec3Less>;

    terrain::VoxelGrid& getGrid() { return _grid; }
    const terrain::VoxelGrid& getGrid() const { return _grid; }
    bool hasGrid() const { return !_grid.isEmpty(); }

    // Which terrain file (URL and version) the grid holds, so the renderer knows when the entity points at another;
    // until the first source is set, the entity has no grid at all, whatever its properties say.
    bool hasGridSource() const { return _gridSourceSet; }
    const QString& getGridURL() const { return _gridURL; }
    uint32_t getGridVersion() const { return _gridVersion; }
    void setGridSource(const QString& url, uint32_t version) { _gridURL = url; _gridVersion = version; _gridSourceSet = true; }

    // Flat ground halfway up the box, for an entity with no terrain file yet.
    void makeDefaultGrid();
    // Replaces the grid with a terrain file's contents; false, with the grid untouched, when the file is bad.
    bool loadGrid(const QByteArray& file, QString& error);
    void clearGrid();

    // Installs the meshes that have come back from the mesher's threads and sends out the chunks the grid marks
    // dirty; returns whether any mesh changed. Called from the renderer's update, every frame while any is out.
    bool updateMeshes();
    // Every chunk's mesh matches the grid: nothing dirty, nothing out, nothing waiting to be installed.
    bool meshesAreSettled() const { return _grid.getDirtyChunks().empty() && _mesher.isIdle(); }
    const ChunkMeshes& getChunkMeshes() const { return _chunkMeshes; }
    uint64_t getMeshVersion() const { return _meshVersion; }

    // Rebuilds the collision shape from the meshes, one static mesh. It costs a Bullet tree build, so the editor
    // asks for it when a stroke ends rather than at every step, a loaded or replaced grid asks for it too, and the
    // renderer builds it once the meshes have settled.
    void updateCollisionShape();
    bool collisionShapeIsStale() const { return _shapeStale; }

    // The editor's brushes edit the grid in place; a stroke's end asks for the collision shape to follow.
    void markCollisionShapeStale() { _shapeStale = true; }
    // Voxels have changed outside the renderer's own update (a brush, an undo, a file that arrived): the renderer
    // only comes to entities that say they changed, so this says so, and it meshes the dirty chunks next frame.
    void gridEdited() { somethingChangedNotification(); }

    // The size of a voxel in the entity's local space: the box's dimensions over the grid's voxel count, so scaling
    // the entity stretches the ground with the box. The voxelSize property is the size the grid was made at.
    glm::vec3 getVoxelScale() const;
    // Voxel space (voxel units, origin at the box's low corner) to the entity's box-centred local space.
    glm::mat4 voxelToLocalMatrix() const;
    glm::mat4 voxelToWorldMatrix() const;
    glm::mat4 worldToVoxelMatrix() const;

    // Picking lands on the ground itself, not the box: a click selects the terrain where there is ground under it,
    // and the editor's brushes find where to work. The ray is in voxel space, its direction as long as it likes:
    // t is the parameter along it (the same in any space the ray is mapped to affinely), the normal in voxel space.
    bool rayHitsGround(const glm::vec3& origin, const glm::vec3& direction, float& t, glm::vec3& normal) const;
    bool supportsDetailedIntersection() const override { return true; }
    bool findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction, OctreeElementPointer& element,
                                     float& distance, BoxFace& face, glm::vec3& surfaceNormal, QVariantMap& extraInfo,
                                     bool precisionPicking) const override;

    void setUnscaledDimensions(const glm::vec3& value) override;

    ShapeType getShapeType() const override;
    bool shouldBePhysical() const override;
    bool isReadyToComputeShape() const override;
    void computeShapeInfo(ShapeInfo& info) override;

private:
    // The grid becomes this one; meshes of chunks the new grid will mesh again stay up until it has, so a reload
    // does not blink, and the others go.
    void replaceGrid(terrain::VoxelGrid grid);

    terrain::VoxelGrid _grid;
    bool _gridSourceSet { false };
    QString _gridURL;
    uint32_t _gridVersion { 0 };
    terrain::AsyncMesher _mesher;
    ChunkMeshes _chunkMeshes;
    uint64_t _meshVersion { 0 };
    ShapeInfo _shapeInfo;
    bool _shapeReady { false };
    bool _shapeStale { false };
    uint64_t _shapeSerial { 0 };
};

namespace render { namespace entities {

// One chunk's mesh as a render item.
class TerrainChunkPayload {
public:
    using Pointer = std::shared_ptr<TerrainChunkPayload>;

    static gpu::BufferPointer makeVertexBuffer(const terrain::ChunkMesh& mesh);
    static gpu::BufferPointer makeIndexBuffer(const terrain::ChunkMesh& mesh);

    void setBuffers(const gpu::BufferPointer& vertices, const gpu::BufferPointer& indices, uint32_t indexCount);
    // The transform of the chunk's voxel (0, 0, 0) in the world; the bound follows from it.
    void setPlacement(const Transform& chunkToWorld);
    void setKey(const ItemKey& key) { _key = key; }
    void setWireframe(bool wireframe) { _wireframe = wireframe; }

    const ItemKey& getKey() const { return _key; }
    const Item::Bound& getBound() const { return _bound; }
    ShapeKey getShapeKey() const;
    void render(RenderArgs* args);

private:
    gpu::BufferPointer _vertexBuffer;
    gpu::BufferPointer _indexBuffer;
    uint32_t _indexCount { 0 };
    Transform _transform;
    Item::Bound _bound;
    ItemKey _key;
    bool _wireframe { false };
};

class TerrainEntityRenderer : public TypedEntityRenderer<RenderableTerrainEntityItem> {
    using Parent = TypedEntityRenderer<RenderableTerrainEntityItem>;
    friend class EntityRenderer;
public:
    TerrainEntityRenderer(const EntityItemPointer& entity);
    ~TerrainEntityRenderer();

protected:
    ItemKey getKey() override;
    uint32_t metaFetchMetaSubItems(ItemIDs& subItems) const override;
    void removeFromScene(const ScenePointer& scene, Transaction& transaction) override;
    bool needsRenderUpdateFromTypedEntity(const TypedEntityPointer& entity) const override;
    void doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) override;
    void doRender(RenderArgs* args) override {}   // the chunk items draw; this item only groups them

private:
    void fetchTerrain(const QString& url, uint32_t version);
    void dropRequest();
    ItemKey chunkKey() const;
    static Transform chunkToWorld(const Transform& model, const glm::vec3& dimensions, const glm::vec3& voxelScale, const glm::ivec3& chunk);

    struct ChunkItem {
        ItemID id { Item::INVALID_ITEM_ID };
        uint64_t meshVersion { 0 };
    };
    std::map<glm::ivec3, ChunkItem, terrain::IVec3Less> _chunkItems;
    ItemKey _lastChunkKey;
    Transform _lastTransform;
    glm::vec3 _lastDimensions { 0.0f };
    glm::vec3 _lastVoxelScale { 0.0f };
    bool _lastWireframe { false };
    uint64_t _meshVersion { 0 };

    ResourceRequest* _request { nullptr };
    QString _requestURL;
    uint32_t _requestVersion { 0 };
};

} }

namespace render {
template <> const ItemKey payloadGetKey(const entities::TerrainChunkPayload::Pointer& payload);
template <> const Item::Bound payloadGetBound(const entities::TerrainChunkPayload::Pointer& payload);
template <> const ShapeKey shapeGetShapeKey(const entities::TerrainChunkPayload::Pointer& payload);
template <> void payloadRender(const entities::TerrainChunkPayload::Pointer& payload, RenderArgs* args);
}

#endif // hifi_RenderableTerrainEntityItem_h
