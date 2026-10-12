//
//  TrackCurve.h
//  libraries/vehicles/src
//
//  A track's centreline (PHYSICS.md 4.8): a centripetal Catmull-Rom spline through control points, a bank angle per
//  point, open or closed, evaluated by arc length. The follower drives a car along it and the editor's gizmo draws
//  and edits it, so both read this one class. Plain maths on glm: no Qt, no entities.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TrackCurve_h
#define hifi_TrackCurve_h

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class TrackCurve {
public:
    // where the track is at an arc length: the pose's -Z runs down the tangent and its +Y is the up vector, level
    // and then rolled by the bank
    struct Pose {
        glm::vec3 position { 0.0f };
        glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 tangent { 0.0f, 0.0f, -1.0f };
        glm::vec3 up { 0.0f, 1.0f, 0.0f };
    };

    static constexpr size_t MIN_POINTS = 2;
    static constexpr size_t MAX_POINTS = 256;

    // points in the track's own space; bank in degrees per point, a short list padded with zero; closed joins the
    // last point back to the first (three points at least). Fewer than two distinct points make an invalid curve.
    void build(const std::vector<glm::vec3>& points, const std::vector<float>& bankDegrees, bool closed);
    void clear();

    bool isValid() const { return _samples.size() >= 2; }
    bool isClosed() const { return _closed; }
    float length() const { return _length; }
    size_t pointCount() const { return _points.size(); }
    size_t segmentCount() const;
    const std::vector<glm::vec3>& points() const { return _points; }

    // the arc length at which the curve passes control point i (0 for the first)
    float arcLengthAtPoint(size_t i) const;
    // an arc length brought onto the track: wrapped on a closed track, clamped to its ends on an open one
    float normalize(float s) const;

    Pose poseAt(float s) const;
    glm::vec3 positionAt(float s) const { return poseAt(s).position; }

    // the arc length of the point of the curve nearest to a point, for inserting a control point there
    float nearest(const glm::vec3& point) const;
    // the curve as a polyline with about `spacing` metres between vertices, for drawing it; a closed track's
    // polyline ends where it began
    std::vector<glm::vec3> polyline(float spacing) const;

private:
    struct Sample {
        float s;             // arc length
        int segment;
        float u;             // the segment's parameter, 0..1
        glm::vec3 position;
        glm::vec3 tangent;
        glm::vec3 up;        // level, before the bank
        float bank;          // radians
    };

    glm::vec3 controlPoint(int i) const;
    void segmentKnots(int segment, float knots[4]) const;
    glm::vec3 evaluate(int segment, float u) const;
    glm::vec3 tangentAt(int segment, float u) const;
    float bankAt(int segment, float u) const;
    static glm::vec3 levelUp(const glm::vec3& tangent, const glm::vec3& previousUp);
    static glm::quat rotationFrom(const glm::vec3& tangent, const glm::vec3& up);

    std::vector<glm::vec3> _points;
    std::vector<float> _bank;   // radians, one per point
    bool _closed { false };
    float _length { 0.0f };
    std::vector<Sample> _samples;
    std::vector<float> _pointArcLengths;
};

#endif // hifi_TrackCurve_h
