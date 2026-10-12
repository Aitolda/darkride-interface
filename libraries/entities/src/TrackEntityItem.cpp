//
//  TrackEntityItem.cpp
//  libraries/entities/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TrackEntityItem.h"

#include "EntityItemProperties.h"

const int TrackEntityItem::MAX_POINTS = 256;
const float TrackEntityItem::DEFAULT_SPEED = 5.0f;
const float TrackEntityItem::DEFAULT_ACCELERATION = 1.0f;
const float TrackEntityItem::MAX_SPEED = 200.0f;
const float TrackEntityItem::MAX_ACCELERATION = 100.0f;

EntityItemPointer TrackEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    Pointer entity(new TrackEntityItem(entityID), [](EntityItem* ptr) { ptr->deleteLater(); });
    entity->setProperties(properties);
    return entity;
}

TrackEntityItem::TrackEntityItem(const EntityItemID& entityItemID) : EntityItem(entityItemID) {
    _type = EntityTypes::Track;
}

EntityItemProperties TrackEntityItem::getProperties(const EntityPropertyFlags& desiredProperties, bool allowEmptyDesiredProperties) const {
    EntityItemProperties properties = EntityItem::getProperties(desiredProperties, allowEmptyDesiredProperties); // get the properties from our base class

    COPY_ENTITY_PROPERTY_TO_PROPERTIES(trackPoints, getTrackPoints);
    COPY_ENTITY_PROPERTY_TO_PROPERTIES(trackBank, getTrackBank);
    COPY_ENTITY_PROPERTY_TO_PROPERTIES(trackDwell, getTrackDwell);
    COPY_ENTITY_PROPERTY_TO_PROPERTIES(trackClosed, getTrackClosed);
    COPY_ENTITY_PROPERTY_TO_PROPERTIES(trackSpeed, getTrackSpeed);
    COPY_ENTITY_PROPERTY_TO_PROPERTIES(trackAcceleration, getTrackAcceleration);

    return properties;
}

bool TrackEntityItem::setSubClassProperties(const EntityItemProperties& properties) {
    bool somethingChanged = false;

    SET_ENTITY_PROPERTY_FROM_PROPERTIES(trackPoints, setTrackPoints);
    SET_ENTITY_PROPERTY_FROM_PROPERTIES(trackBank, setTrackBank);
    SET_ENTITY_PROPERTY_FROM_PROPERTIES(trackDwell, setTrackDwell);
    SET_ENTITY_PROPERTY_FROM_PROPERTIES(trackClosed, setTrackClosed);
    SET_ENTITY_PROPERTY_FROM_PROPERTIES(trackSpeed, setTrackSpeed);
    SET_ENTITY_PROPERTY_FROM_PROPERTIES(trackAcceleration, setTrackAcceleration);

    return somethingChanged;
}

int TrackEntityItem::readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead,
                                                      ReadBitstreamToTreeParams& args,
                                                      EntityPropertyFlags& propertyFlags, bool overwriteLocalData,
                                                      bool& somethingChanged) {
    int bytesRead = 0;
    const unsigned char* dataAt = data;

    READ_ENTITY_PROPERTY(PROP_TRACK_POINTS, QVector<glm::vec3>, setTrackPoints);
    READ_ENTITY_PROPERTY(PROP_TRACK_BANK, QVector<float>, setTrackBank);
    READ_ENTITY_PROPERTY(PROP_TRACK_DWELL, QVector<float>, setTrackDwell);
    READ_ENTITY_PROPERTY(PROP_TRACK_CLOSED, bool, setTrackClosed);
    READ_ENTITY_PROPERTY(PROP_TRACK_SPEED, float, setTrackSpeed);
    READ_ENTITY_PROPERTY(PROP_TRACK_ACCELERATION, float, setTrackAcceleration);

    return bytesRead;
}

EntityPropertyFlags TrackEntityItem::getEntityProperties(EncodeBitstreamParams& params) const {
    EntityPropertyFlags requestedProperties = EntityItem::getEntityProperties(params);

    requestedProperties += PROP_TRACK_POINTS;
    requestedProperties += PROP_TRACK_BANK;
    requestedProperties += PROP_TRACK_DWELL;
    requestedProperties += PROP_TRACK_CLOSED;
    requestedProperties += PROP_TRACK_SPEED;
    requestedProperties += PROP_TRACK_ACCELERATION;

    return requestedProperties;
}

void TrackEntityItem::appendSubclassData(OctreePacketData* packetData, EncodeBitstreamParams& params,
                                         EntityTreeElementExtraEncodeDataPointer entityTreeElementExtraEncodeData,
                                         EntityPropertyFlags& requestedProperties,
                                         EntityPropertyFlags& propertyFlags,
                                         EntityPropertyFlags& propertiesDidntFit,
                                         int& propertyCount,
                                         OctreeElement::AppendState& appendState) const {
    bool successPropertyFits = true;

    APPEND_ENTITY_PROPERTY(PROP_TRACK_POINTS, getTrackPoints());
    APPEND_ENTITY_PROPERTY(PROP_TRACK_BANK, getTrackBank());
    APPEND_ENTITY_PROPERTY(PROP_TRACK_DWELL, getTrackDwell());
    APPEND_ENTITY_PROPERTY(PROP_TRACK_CLOSED, getTrackClosed());
    APPEND_ENTITY_PROPERTY(PROP_TRACK_SPEED, getTrackSpeed());
    APPEND_ENTITY_PROPERTY(PROP_TRACK_ACCELERATION, getTrackAcceleration());
}

void TrackEntityItem::setTrackPoints(const QVector<glm::vec3>& points) {
    withWriteLock([&] {
        QVector<glm::vec3> clipped = points.size() > MAX_POINTS ? points.mid(0, MAX_POINTS) : points;
        if (_trackPoints != clipped) {
            _trackPoints = clipped;
            _trackVersion++;
            _needsRenderUpdate = true;
        }
    });
}

QVector<glm::vec3> TrackEntityItem::getTrackPoints() const {
    return resultWithReadLock<QVector<glm::vec3>>([&] { return _trackPoints; });
}

void TrackEntityItem::setTrackBank(const QVector<float>& bank) {
    withWriteLock([&] {
        QVector<float> clipped = bank.size() > MAX_POINTS ? bank.mid(0, MAX_POINTS) : bank;
        if (_trackBank != clipped) {
            _trackBank = clipped;
            _trackVersion++;
            _needsRenderUpdate = true;
        }
    });
}

QVector<float> TrackEntityItem::getTrackBank() const {
    return resultWithReadLock<QVector<float>>([&] { return _trackBank; });
}

void TrackEntityItem::setTrackDwell(const QVector<float>& dwell) {
    withWriteLock([&] {
        QVector<float> clipped = dwell.size() > MAX_POINTS ? dwell.mid(0, MAX_POINTS) : dwell;
        if (_trackDwell != clipped) {
            _trackDwell = clipped;
            _trackVersion++;
        }
    });
}

QVector<float> TrackEntityItem::getTrackDwell() const {
    return resultWithReadLock<QVector<float>>([&] { return _trackDwell; });
}

void TrackEntityItem::setTrackClosed(bool closed) {
    withWriteLock([&] {
        if (_trackClosed != closed) {
            _trackClosed = closed;
            _trackVersion++;
            _needsRenderUpdate = true;
        }
    });
}

bool TrackEntityItem::getTrackClosed() const {
    return resultWithReadLock<bool>([&] { return _trackClosed; });
}

void TrackEntityItem::setTrackSpeed(float speed) {
    speed = glm::clamp(speed, 0.0f, MAX_SPEED);
    withWriteLock([&] {
        if (_trackSpeed != speed) {
            _trackSpeed = speed;
            _trackVersion++;
        }
    });
}

float TrackEntityItem::getTrackSpeed() const {
    return resultWithReadLock<float>([&] { return _trackSpeed; });
}

void TrackEntityItem::setTrackAcceleration(float acceleration) {
    acceleration = glm::clamp(acceleration, 0.0f, MAX_ACCELERATION);
    withWriteLock([&] {
        if (_trackAcceleration != acceleration) {
            _trackAcceleration = acceleration;
            _trackVersion++;
        }
    });
}

float TrackEntityItem::getTrackAcceleration() const {
    return resultWithReadLock<float>([&] { return _trackAcceleration; });
}

uint32_t TrackEntityItem::getTrackVersion() const {
    return resultWithReadLock<uint32_t>([&] { return _trackVersion; });
}
