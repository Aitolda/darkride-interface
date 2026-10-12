//
//  TrackEntityItem.h
//  libraries/entities/src
//
//  The Track entity (PHYSICS.md 4.8): a curve through control points in the entity's own space, with a bank angle and
//  a dwell time per point, open or closed, and the schedule a train keeps on it. The curve itself (a centripetal
//  Catmull-Rom by arc length) is libraries/vehicles' TrackCurve, built from these properties by the physics on each
//  client and by the physics server; the editor's gizmo draws and edits the points. An entity with trackID set
//  rides this track at trackOffset metres along the train; the physics moves it. The track has no body of its own.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TrackEntityItem_h
#define hifi_TrackEntityItem_h

#include "EntityItem.h"

class TrackEntityItem : public EntityItem {
    using Pointer = std::shared_ptr<TrackEntityItem>;
public:
    static EntityItemPointer factory(const EntityItemID& entityID, const EntityItemProperties& properties);

    TrackEntityItem(const EntityItemID& entityItemID);

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

    bool shouldBePhysical() const override { return false; }

    static const int MAX_POINTS;
    static const float DEFAULT_SPEED;          // m/s
    static const float DEFAULT_ACCELERATION;   // m/s^2
    static const float MAX_SPEED;
    static const float MAX_ACCELERATION;

    void setTrackPoints(const QVector<glm::vec3>& points);
    QVector<glm::vec3> getTrackPoints() const;

    void setTrackBank(const QVector<float>& bank);   // degrees per point
    QVector<float> getTrackBank() const;

    void setTrackDwell(const QVector<float>& dwell);   // seconds per point; more than zero makes a station
    QVector<float> getTrackDwell() const;

    void setTrackClosed(bool closed);
    bool getTrackClosed() const;

    void setTrackSpeed(float speed);
    float getTrackSpeed() const;

    void setTrackAcceleration(float acceleration);
    float getTrackAcceleration() const;

    // counts every change to the properties above, so a curve built from them knows when to rebuild
    uint32_t getTrackVersion() const;

protected:
    QVector<glm::vec3> _trackPoints;
    QVector<float> _trackBank;
    QVector<float> _trackDwell;
    bool _trackClosed { false };
    float _trackSpeed { DEFAULT_SPEED };
    float _trackAcceleration { DEFAULT_ACCELERATION };
    uint32_t _trackVersion { 1 };
};

#endif // hifi_TrackEntityItem_h
