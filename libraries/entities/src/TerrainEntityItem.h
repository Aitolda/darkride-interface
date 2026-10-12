//
//  TerrainEntityItem.h
//  libraries/entities/src
//
//  The Terrain entity (TERRAIN.md): a box of voxel ground that can be shaped and dug. The voxels themselves are not
//  properties; they live in a terrain file (libraries/terrain) the entity points at with terrainURL, and
//  terrainVersion counts the saves so clients know to fetch the file again. The entity's dimensions are the voxel
//  count along each side times the voxel size. The renderer's subclass holds the voxels, meshes them and gives the
//  entity its collision shape; here, as on the entity server, the entity is only its properties.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TerrainEntityItem_h
#define hifi_TerrainEntityItem_h

#include "EntityItem.h"

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

    // How many voxels the entity's dimensions hold along each side at the voxel size, at least one.
    glm::ivec3 getVoxelCount() const;

protected:
    float _voxelSize { DEFAULT_VOXEL_SIZE };
    QString _terrainURL;
    uint32_t _terrainVersion { 0 };
};

#endif // hifi_TerrainEntityItem_h
