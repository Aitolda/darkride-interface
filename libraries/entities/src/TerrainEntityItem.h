//
//  TerrainEntityItem.h
//  libraries/entities/src
//
//  The Terrain entity (TERRAIN.md): a box of voxel ground that can be shaped and dug. The voxels themselves are not
//  properties; they live in a terrain file (libraries/terrain) the entity points at with terrainURL, and
//  terrainVersion counts the saves so clients know to fetch the file again. The entity's dimensions are the voxel
//  count along each side times the voxel size. terrainLayers names the materials the voxels' material indices
//  mean (TERRAIN.md section 5): a JSON list of up to eight layers, each a texture and a tiling size in metres.
//  The renderer's subclass holds the voxels, meshes them and gives the entity its collision shape; here, as on the
//  entity server, the entity is only its properties.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TerrainEntityItem_h
#define hifi_TerrainEntityItem_h

#include <vector>

#include "EntityItem.h"

// One of a terrain's material layers, as terrainLayers describes it.
struct TerrainLayerSpec {
    QString albedoURL;        // the colour texture; empty for a plain colour
    float tiling { 4.0f };    // metres per repeat of the texture

    bool operator==(const TerrainLayerSpec& other) const { return albedoURL == other.albedoURL && tiling == other.tiling; }
    bool operator!=(const TerrainLayerSpec& other) const { return !(*this == other); }
};

class TerrainEntityItem : public EntityItem {
    using Pointer = std::shared_ptr<TerrainEntityItem>;
public:
    static EntityItemPointer factory(const EntityItemID& entityID, const EntityItemProperties& properties);

    TerrainEntityItem(const EntityItemID& entityItemID);

    ALLOW_INSTANTIATION // This class can be instantiated

    // methods for getting/setting all properties of an entity
    EntityItemProperties getProperties(const EntityPropertyFlags& desiredProperties, bool allowEmptyDesiredProperties) const override;
    bool setSubClassProperties(const EntityItemProperties& properties) override;

    EntityPropertyFlags getEntityProperties(EncodeBitstreamParams& params) const override;

    void appendSubclassData(OctreePacketData* packetData, EncodeBitstreamParams& params,
                            EntityTreeElementExtraEncodeDataPointer entityTreeElementExtraEncodeData,
                            EntityPropertyFlags& requestedProperties,
                            EntityPropertyFlags& propertyFlags,
                            EntityPropertyFlags& propertiesDidntFit,
                            int& propertyCount,
                            OctreeElement::AppendState& appendState) const override;

    int readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead,
                                         ReadBitstreamToTreeParams& args,
                                         EntityPropertyFlags& propertyFlags, bool overwriteLocalData,
                                         bool& somethingChanged) override;

    static const float DEFAULT_VOXEL_SIZE;
    static const float MIN_VOXEL_SIZE;
    static const float MAX_VOXEL_SIZE;

    virtual void setVoxelSize(float voxelSize);
    float getVoxelSize() const;

    virtual void setTerrainURL(const QString& url);
    QString getTerrainURL() const;

    virtual void setTerrainVersion(uint32_t version);
    uint32_t getTerrainVersion() const;

    static const int MAX_TERRAIN_LAYERS = 8;
    // The layers as JSON: [{"albedo": url, "tiling": metres}, ...]; a bare string stands for a texture at the
    // default tiling. Entries past the eighth are ignored.
    virtual void setTerrainLayers(const QString& layers);
    QString getTerrainLayers() const;
    // The list the JSON describes, empty (with error set, if given) when it is not valid JSON or not a list.
    static std::vector<TerrainLayerSpec> parseTerrainLayers(const QString& json, QString* error = nullptr);

    // How many voxels the entity's dimensions hold along each side at the voxel size, at least one.
    glm::ivec3 getVoxelCount() const;

protected:
    float _voxelSize { DEFAULT_VOXEL_SIZE };
    QString _terrainURL;
    uint32_t _terrainVersion { 0 };
    QString _terrainLayers;
};

#endif // hifi_TerrainEntityItem_h
