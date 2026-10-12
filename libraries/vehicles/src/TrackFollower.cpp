//
//  TrackFollower.cpp
//  libraries/vehicles/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TrackFollower.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace {
    const float ARRIVAL_EPSILON = 1.0e-4f;   // metres: closer than this to a station is there
    const float AT_STATION_DISTANCE = 0.25f; // metres: a train placed this close to a station starts with its dwell
    const float ANGULAR_STEP = 0.02f;        // metres along the track over which the turn is measured
    const int MAX_EVENTS_PER_STEP = 8;       // arrivals and departures handled within one advance
    const float INFINITE = std::numeric_limits<float>::infinity();
}

bool TrackFollower::isStation(const TrackCurve& curve, const Schedule& schedule, size_t point) {
    if (point >= curve.pointCount()) {
        return false;
    }
    if (!curve.isClosed() && (point == 0 || point + 1 == curve.pointCount())) {
        return true;
    }
    return point < schedule.dwell.size() && schedule.dwell[point] > 0.0f;
}

float TrackFollower::dwellAt(const Schedule& schedule, int point) {
    if (point < 0 || (size_t)point >= schedule.dwell.size()) {
        return 0.0f;
    }
    return std::max(schedule.dwell[point], 0.0f);
}

float TrackFollower::distanceAhead(const TrackCurve& curve, float s, float target, int direction) {
    float d = direction >= 0 ? target - s : s - target;
    if (curve.isClosed()) {
        float length = curve.length();
        if (length <= 0.0f) {
            return 0.0f;
        }
        d = std::fmod(d, length);
        if (d < 0.0f) {
            d += length;
        }
        if (d <= ARRIVAL_EPSILON) {
            d = length;
        }
    }
    return d;
}

int TrackFollower::nextStation(const TrackCurve& curve, const Schedule& schedule, float s, int direction, float& distance) const {
    int best = -1;
    distance = INFINITE;
    for (size_t i = 0; i < curve.pointCount(); ++i) {
        if (!isStation(curve, schedule, i)) {
            continue;
        }
        float d = distanceAhead(curve, s, curve.arcLengthAtPoint(i), direction);
        if (d <= ARRIVAL_EPSILON) {
            continue;   // behind, or here
        }
        if (d < distance) {
            distance = d;
            best = (int)i;
        }
    }
    return best;
}

void TrackFollower::retarget(const TrackCurve& curve, const Schedule& schedule) {
    float distance;
    _state.station = nextStation(curve, schedule, _state.s, _state.direction, distance);
    if (_state.station < 0 && !curve.isClosed()) {
        // nothing ahead on an open track: turn round
        _state.direction = -_state.direction;
        _state.station = nextStation(curve, schedule, _state.s, _state.direction, distance);
    }
}

void TrackFollower::reset(const TrackCurve& curve, const Schedule& schedule, float s, int direction, float speed) {
    _state = State();
    _state.s = curve.normalize(s);
    _state.direction = direction >= 0 ? 1 : -1;
    _state.speed = std::max(speed, 0.0f);
    if (!curve.isValid()) {
        return;
    }
    if (_state.speed == 0.0f) {
        // a train standing at a station waits its dwell there before it leaves
        for (size_t i = 0; i < curve.pointCount(); ++i) {
            if (!isStation(curve, schedule, i)) {
                continue;
            }
            float there = curve.arcLengthAtPoint(i);
            float apart = std::fabs(_state.s - there);
            if (curve.isClosed()) {
                apart = std::min(apart, curve.length() - apart);
            }
            if (apart <= AT_STATION_DISTANCE && dwellAt(schedule, (int)i) > 0.0f) {
                _state.s = curve.normalize(there);
                _state.station = (int)i;
                _state.dwellRemaining = dwellAt(schedule, (int)i);
                return;
            }
        }
    }
    retarget(curve, schedule);
}

void TrackFollower::advance(const TrackCurve& curve, const Schedule& schedule, float dt) {
    if (!curve.isValid() || dt <= 0.0f) {
        return;
    }
    State& state = _state;
    const float acceleration = std::max(schedule.acceleration, MIN_ACCELERATION);
    const float cruise = std::max(schedule.speed, 0.0f);

    for (int event = 0; dt > 0.0f && event < MAX_EVENTS_PER_STEP; ++event) {
        if (state.dwellRemaining > 0.0f) {
            float used = std::min(dt, state.dwellRemaining);
            state.dwellRemaining -= used;
            dt -= used;
            if (state.dwellRemaining > 0.0f) {
                return;
            }
            state.dwellRemaining = 0.0f;
            retarget(curve, schedule);   // departure
            continue;
        }

        float distance = INFINITE;
        if (state.station >= 0) {
            distance = distanceAhead(curve, state.s, curve.arcLengthAtPoint((size_t)state.station), state.direction);
        }
        // the braking curve: the speed from which the station can still be reached at the acceleration
        float target = cruise;
        if (distance < INFINITE) {
            target = std::min(cruise, std::max(std::sqrt(2.0f * acceleration * std::max(distance, 0.0f)), CRAWL_SPEED));
        }
        if (state.speed < target) {
            state.speed = std::min(state.speed + acceleration * dt, target);
        } else {
            state.speed = std::max(state.speed - acceleration * dt, target);
        }

        float ds = state.speed * dt;
        if (distance < INFINITE && ds >= distance - ARRIVAL_EPSILON) {
            // arrival within this step: the rest of the step is spent standing
            float timeToArrive = state.speed > 1.0e-6f ? std::min(distance / state.speed, dt) : dt;
            state.s = curve.normalize(curve.arcLengthAtPoint((size_t)state.station));
            state.speed = 0.0f;
            dt -= timeToArrive;
            state.dwellRemaining = dwellAt(schedule, state.station);
            if (state.dwellRemaining <= 0.0f) {
                retarget(curve, schedule);   // a stop with no dwell: straight on, or back on an open track
            }
            continue;
        }
        state.s = curve.normalize(state.s + (float)state.direction * ds);
        dt = 0.0f;
    }
}

void TrackFollower::carPose(const TrackCurve& curve, float offset, glm::vec3& position, glm::quat& rotation,
                            glm::vec3& linearVelocity, glm::vec3& angularVelocity) const {
    float s = curve.normalize(_state.s + offset);
    TrackCurve::Pose pose = curve.poseAt(s);
    position = pose.position;
    rotation = pose.rotation;
    float velocity = _state.speed * (float)_state.direction;   // signed along the arc length
    linearVelocity = velocity * pose.tangent;
    angularVelocity = glm::vec3(0.0f);
    if (std::fabs(velocity) > 1.0e-6f) {
        float step = velocity > 0.0f ? ANGULAR_STEP : -ANGULAR_STEP;
        float ahead = curve.normalize(s + step);
        float covered = curve.isClosed() ? step : (ahead - s);   // an open track's end clamps
        if (std::fabs(covered) > 1.0e-6f) {
            glm::quat delta = curve.poseAt(ahead).rotation * glm::inverse(rotation);
            if (delta.w < 0.0f) {
                delta = -delta;
            }
            // the angle from the vector part (sin of the half angle), which keeps its precision for the small turns
            // measured here, where acos of the scalar part would be noise
            glm::vec3 turn(delta.x, delta.y, delta.z);
            float sinHalf = glm::length(turn);
            if (sinHalf > 1.0e-6f) {
                float angle = 2.0f * std::asin(std::min(sinHalf, 1.0f));
                // the pose turns by `angle` over `covered` metres, run at |velocity| m/s
                angularVelocity = (turn / sinHalf) * (angle * std::fabs(velocity) / std::fabs(covered));
            }
        }
    }
}
