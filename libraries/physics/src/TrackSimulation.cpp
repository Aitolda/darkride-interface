//
//  TrackSimulation.cpp
//  libraries/physics/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TrackSimulation.h"

#include <cmath>

#include <EntityTree.h>
#include <TrackEntityItem.h>
#include <PhysicsHelpers.h>

#include "ObjectMotionState.h"

namespace {
    // a train not driven for this many substeps is picked up afresh from where its car is
    const uint32_t RESYNC_AFTER_STEPS = 10;
}

bool TrackSimulation::drive(const EntityItemPointer& car, uint32_t step, glm::vec3& position, glm::quat& rotation,
                            glm::vec3& linearVelocity, glm::vec3& angularVelocity) {
    QUuid trackID = car->getTrackID();
    EntityTreePointer tree = car->getTree();
    if (trackID.isNull() || !tree) {
        return false;
    }
    auto track = std::dynamic_pointer_cast<TrackEntityItem>(tree->findEntityByID(trackID));
    if (!track) {
        _trains.erase(trackID);
        return false;
    }
    Train& train = _trains[trackID];

    // the curve is in the track's own space
    glm::vec3 trackPosition = track->getWorldPosition();
    glm::quat trackRotation = track->getWorldOrientation();
    glm::quat toTrack = glm::inverse(trackRotation);

    uint32_t version = track->getTrackVersion();
    if (train.version != version) {
        QVector<glm::vec3> points = track->getTrackPoints();
        QVector<float> bank = track->getTrackBank();
        QVector<float> dwell = track->getTrackDwell();
        train.curve.build(std::vector<glm::vec3>(points.begin(), points.end()),
                          std::vector<float>(bank.begin(), bank.end()), track->getTrackClosed());
        train.schedule.speed = track->getTrackSpeed();
        train.schedule.acceleration = track->getTrackAcceleration();
        train.schedule.dwell.assign(dwell.begin(), dwell.end());
        train.version = version;
        if (train.running && train.curve.isValid()) {
            // the track changed under a running train: keep its place, its way and its speed, find the stations again
            TrackFollower::State state = train.follower.state();
            train.follower.reset(train.curve, train.schedule, state.s, state.direction, state.speed);
        }
    }
    if (!train.curve.isValid()) {
        return false;
    }

    float offset = car->getTrackOffset();
    if (!train.running || step > train.lastStep + RESYNC_AFTER_STEPS) {
        // start driving from where the car is now: its nearest point on the curve, at its speed along it
        glm::vec3 carOnTrack = toTrack * (car->getWorldPosition() - trackPosition);
        float carArcLength = train.curve.nearest(carOnTrack);
        glm::vec3 velocityOnTrack = toTrack * car->getWorldVelocity();
        float along = glm::dot(velocityOnTrack, train.curve.poseAt(carArcLength).tangent);
        train.follower.reset(train.curve, train.schedule, carArcLength - offset, along < 0.0f ? -1 : 1, std::fabs(along));
        train.running = true;
        train.lastStep = step;
    } else if (step > train.lastStep) {
        train.follower.advance(train.curve, train.schedule, (float)(step - train.lastStep) * PHYSICS_ENGINE_FIXED_SUBSTEP);
        train.lastStep = step;
    }

    glm::vec3 localPosition, localLinear, localAngular;
    glm::quat localRotation;
    train.follower.carPose(train.curve, offset, localPosition, localRotation, localLinear, localAngular);
    position = trackPosition + trackRotation * localPosition;
    rotation = glm::normalize(trackRotation * localRotation);
    linearVelocity = trackRotation * localLinear;
    angularVelocity = trackRotation * localAngular;
    return true;
}
