//
//  JoltUtil.h
//  libraries/physics/src
//
//  Conversions between glm and Jolt types. Jolt's RVec3 is Vec3 unless the build uses double precision,
//  in which case the R variants carry the extra range.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_JoltUtil_h
#define hifi_JoltUtil_h

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Mat44.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

inline glm::vec3 joltToGLM(JPH::Vec3Arg v) {
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

inline glm::quat joltToGLM(JPH::QuatArg q) {
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

inline JPH::Vec3 glmToJolt(const glm::vec3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

inline JPH::Quat glmToJolt(const glm::quat& q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

#ifdef JPH_DOUBLE_PRECISION
inline glm::vec3 joltToGLM(JPH::RVec3Arg v) {
    return glm::vec3((float)v.GetX(), (float)v.GetY(), (float)v.GetZ());
}
inline JPH::RVec3 glmToJoltR(const glm::vec3& v) {
    return JPH::RVec3(v.x, v.y, v.z);
}
#else
inline JPH::RVec3 glmToJoltR(const glm::vec3& v) {
    return glmToJolt(v);
}
#endif

inline glm::mat4 joltToGLM(JPH::Mat44Arg m) {
    glm::mat4 result;
    for (int c = 0; c < 4; ++c) {
        JPH::Vec4 column = m.GetColumn4(c);
        result[c] = glm::vec4(column.GetX(), column.GetY(), column.GetZ(), column.GetW());
    }
    return result;
}

inline JPH::Mat44 glmToJolt(const glm::mat4& m) {
    return JPH::Mat44(JPH::Vec4(m[0][0], m[0][1], m[0][2], m[0][3]),
                      JPH::Vec4(m[1][0], m[1][1], m[1][2], m[1][3]),
                      JPH::Vec4(m[2][0], m[2][1], m[2][2], m[2][3]),
                      JPH::Vec4(m[3][0], m[3][1], m[3][2], m[3][3]));
}

inline JPH::Vec3 clampLength(JPH::Vec3Arg v, float maxLength) {
    float length = v.Length();
    return (length > maxLength && length > 0.0f) ? v * (maxLength / length) : v;
}

#endif // hifi_JoltUtil_h
