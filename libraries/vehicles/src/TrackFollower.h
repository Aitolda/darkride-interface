//
//  TrackFollower.h
//  libraries/vehicles/src
//
//  A train's motion along a TrackCurve (PHYSICS.md 4.8): a trapezoidal speed profile between stations (accelerate,
//  cruise, brake to a stop at the station), a dwell at each, round and round on a closed track and back and forth on
//  an open one, whose ends are always stops. The state is small and the step is deterministic, so a server and its
//  clients can run the same follower from the same state (round 5). Plain maths: no Qt, no entities.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TrackFollower_h
#define hifi_TrackFollower_h

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "TrackCurve.h"

class TrackFollower {
public:
    struct Schedule {
        float speed { 5.0f };            // m/s, the cruise
        float acceleration { 1.0f };     // m/s^2, also the braking
        std::vector<float> dwell;        // seconds per control point; more than zero makes the point a station
    };

    struct State {
        float s { 0.0f };                // the train's reference point: an arc length along the track
        float speed { 0.0f };            // m/s, along the direction of travel
        int direction { 1 };             // +1 with the arc length, -1 against it (a shuttle on its way back)
        float dwellRemaining { 0.0f };   // seconds left standing at the station; 0 when moving
        int station { -1 };              // the control point the train stands at or heads for; -1 for none
    };

    static constexpr float MIN_ACCELERATION = 0.01f;   // m/s^2
    static constexpr float CRAWL_SPEED = 0.1f;         // m/s: the braking curve never asks for less until arrival

    const State& state() const { return _state; }
    void setState(const State& state) { _state = state; }
    bool isDwelling() const { return _state.dwellRemaining > 0.0f; }

    // put the train at an arc length, at a speed (standing by default), heading for the next station that way
    void reset(const TrackCurve& curve, const Schedule& schedule, float s = 0.0f, int direction = 1, float speed = 0.0f);
    // step the state by dt seconds
    void advance(const TrackCurve& curve, const Schedule& schedule, float dt);
    // the pose of a car `offset` metres along the track from the train's reference point, with the velocities a
    // kinematic body wants: linear down the tangent, angular from the curve's turn
    void carPose(const TrackCurve& curve, float offset, glm::vec3& position, glm::quat& rotation,
                 glm::vec3& linearVelocity, glm::vec3& angularVelocity) const;

    static bool isStation(const TrackCurve& curve, const Schedule& schedule, size_t point);
    static float dwellAt(const Schedule& schedule, int point);
    // the distance from s to the arc length `target` travelling in `direction`: on a closed track always ahead (a
    // full lap when the target is s itself), on an open one negative when it lies behind
    static float distanceAhead(const TrackCurve& curve, float s, float target, int direction);

private:
    void retarget(const TrackCurve& curve, const Schedule& schedule);
    int nextStation(const TrackCurve& curve, const Schedule& schedule, float s, int direction, float& distance) const;

    State _state;
};

#endif // hifi_TrackFollower_h
