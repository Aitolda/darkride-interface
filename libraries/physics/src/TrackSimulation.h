//
//  TrackSimulation.h
//  libraries/physics/src
//
//  The trains on the tracks (PHYSICS.md 4.8, round 4). For each Track entity: its curve, rebuilt when the entity's
//  properties change, and the follower that moves the train along it. A car (an entity with trackID) is driven by
//  the client that owns its simulation: every substep its motion state asks here for the car's pose, and the first
//  car of a train to ask in a step advances the train. The train's state lives here, in the owner's memory, and is
//  picked up from wherever the car is when this client starts driving; other clients move the car by the velocities
//  the owner sends, as any kinematic entity. Round 5 moves the state to the physics server and shares its clock.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TrackSimulation_h
#define hifi_TrackSimulation_h

#include <map>

#include <QtCore/QUuid>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <EntityItem.h>
#include <TrackCurve.h>
#include <TrackFollower.h>

class TrackSimulation {
public:
    // the pose and velocities a car should have at this substep, in its parent's coordinates (the world: a car is
    // unparented this round); the train is advanced to the step on the first call for it. False when the car's
    // track is missing or has no curve.
    bool drive(const EntityItemPointer& car, uint32_t step, glm::vec3& position, glm::quat& rotation,
               glm::vec3& linearVelocity, glm::vec3& angularVelocity);
    void clear() { _trains.clear(); }

private:
    struct Train {
        uint32_t version { 0 };   // the Track entity's property version the curve was built from
        TrackCurve curve;
        TrackFollower::Schedule schedule;
        TrackFollower follower;
        uint32_t lastStep { 0 };
        bool running { false };
    };
    std::map<QUuid, Train> _trains;
};

#endif // hifi_TrackSimulation_h
