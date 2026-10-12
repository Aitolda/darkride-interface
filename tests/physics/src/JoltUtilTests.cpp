//
//  JoltUtilTests.cpp
//  tests/physics/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "JoltUtilTests.h"

#include <JoltUtil.h>
#include <NumericalConstants.h>
#include <GLMHelpers.h>

// Add additional qtest functionality (the include order is important!)
#include <test-utils/QTestExtensions.h>

QTEST_MAIN(JoltUtilTests)

const float ACCEPTABLE_ERROR = 1.0e-5f;
const float ACCEPTABLE_ANGLE = 1.0e-4f; // radians, for QCOMPARE_QUATS

void JoltUtilTests::fromJoltToGLM() {
    // vec3
    JPH::Vec3 jV(1.23f, 4.56f, 7.89f);
    glm::vec3 gV = joltToGLM(jV);
    QCOMPARE(gV.x, jV.GetX());
    QCOMPARE(gV.y, jV.GetY());
    QCOMPARE(gV.z, jV.GetZ());

    // quat
    float angle = 0.317f * PI;
    JPH::Vec3 axis = JPH::Vec3(1.23f, 2.34f, 3.45f).Normalized();
    JPH::Quat jQ = JPH::Quat::sRotation(axis, angle);
    glm::quat gQ = joltToGLM(jQ);
    QCOMPARE(gQ.x, jQ.GetX());
    QCOMPARE(gQ.y, jQ.GetY());
    QCOMPARE(gQ.z, jQ.GetZ());
    QCOMPARE(gQ.w, jQ.GetW());

    // both sides rotate a vector the same way
    glm::vec3 gRotated = gQ * Vectors::UNIT_X;
    JPH::Vec3 jRotated = jQ * JPH::Vec3::sAxisX();
    QCOMPARE_WITH_ABS_ERROR(gRotated, joltToGLM(jRotated), ACCEPTABLE_ERROR);

    // mat4
    JPH::Vec3 origin(100.0f, 200.0f, 300.0f);
    JPH::Mat44 jM = JPH::Mat44::sRotationTranslation(jQ, origin);
    glm::mat4 gM = joltToGLM(jM);

    glm::vec3 translation = extractTranslation(gM);
    QCOMPARE(translation.x, origin.GetX());
    QCOMPARE(translation.y, origin.GetY());
    QCOMPARE(translation.z, origin.GetZ());

    glm::quat rotation = glmExtractRotation(gM);
    QCOMPARE_QUATS(rotation, gQ, ACCEPTABLE_ANGLE);

    // As a sanity check, transform points by their corresponding matrices and compare the results.
    QCOMPARE_WITH_ABS_ERROR(transformPoint(gM, Vectors::UNIT_X), joltToGLM(jM * JPH::Vec3::sAxisX()), ACCEPTABLE_ERROR);
    QCOMPARE_WITH_ABS_ERROR(transformPoint(gM, Vectors::UNIT_Y), joltToGLM(jM * JPH::Vec3::sAxisY()), ACCEPTABLE_ERROR);
    QCOMPARE_WITH_ABS_ERROR(transformPoint(gM, Vectors::UNIT_Z), joltToGLM(jM * JPH::Vec3::sAxisZ()), ACCEPTABLE_ERROR);

    // and the way back gives the same matrix
    QCOMPARE_WITH_ABS_ERROR(joltToGLM(glmToJolt(gM)), gM, ACCEPTABLE_ERROR);
}

void JoltUtilTests::fromGLMToJolt() {
    // vec3
    glm::vec3 gV(1.23f, 4.56f, 7.89f);
    JPH::Vec3 jV = glmToJolt(gV);
    QCOMPARE(jV.GetX(), gV.x);
    QCOMPARE(jV.GetY(), gV.y);
    QCOMPARE(jV.GetZ(), gV.z);

    // quat
    float angle = 0.317f * PI;
    glm::vec3 axis = glm::normalize(glm::vec3(1.23f, 2.34f, 3.45f));
    glm::quat gQ = glm::angleAxis(angle, axis);
    JPH::Quat jQ = glmToJolt(gQ);
    QCOMPARE(jQ.GetX(), gQ.x);
    QCOMPARE(jQ.GetY(), gQ.y);
    QCOMPARE(jQ.GetZ(), gQ.z);
    QCOMPARE(jQ.GetW(), gQ.w);

    // both sides rotate a vector the same way
    glm::vec3 gRotated = gQ * Vectors::UNIT_Y;
    JPH::Vec3 jRotated = jQ * JPH::Vec3::sAxisY();
    QCOMPARE_WITH_ABS_ERROR(joltToGLM(jRotated), gRotated, ACCEPTABLE_ERROR);

    // mat4
    glm::mat4 gM = createMatFromQuatAndPos(gQ, gV);
    JPH::Mat44 jM = glmToJolt(gM);

    JPH::Vec3 jTranslation = jM.GetTranslation();
    QCOMPARE(jTranslation.GetX(), gV.x);
    QCOMPARE(jTranslation.GetY(), gV.y);
    QCOMPARE(jTranslation.GetZ(), gV.z);

    glm::quat gRotation = joltToGLM(jM.GetQuaternion());
    QCOMPARE_QUATS(gRotation, gQ, ACCEPTABLE_ANGLE);

    // the matrices agree on where a point goes
    glm::vec3 point(-3.0f, 2.0f, 5.0f);
    QCOMPARE_WITH_ABS_ERROR(joltToGLM(jM * glmToJolt(point)), transformPoint(gM, point), ACCEPTABLE_ERROR);

    // and the way back gives the same values
    QCOMPARE_WITH_ABS_ERROR(joltToGLM(glmToJolt(gV)), gV, ACCEPTABLE_ERROR);
    QCOMPARE_WITH_ABS_ERROR(joltToGLM(glmToJolt(gQ)), gQ, ACCEPTABLE_ERROR);
    QCOMPARE_WITH_ABS_ERROR(joltToGLM(jM), gM, ACCEPTABLE_ERROR);
}

void JoltUtilTests::realVectorRoundTrip() {
    // RVec3 is Vec3 in a single precision build and a double vector in a double precision one;
    // the glm side is float either way
    glm::vec3 gV(1.23f, 4.56f, 7.89f);
    JPH::RVec3 rV = glmToJoltR(gV);
    QCOMPARE((float)rV.GetX(), gV.x);
    QCOMPARE((float)rV.GetY(), gV.y);
    QCOMPARE((float)rV.GetZ(), gV.z);

    glm::vec3 back = joltToGLM(rV);
    QCOMPARE(back.x, gV.x);
    QCOMPARE(back.y, gV.y);
    QCOMPARE(back.z, gV.z);
}

void JoltUtilTests::clampLengthTest() {
    JPH::Vec3 vec(1.0f, 3.0f, 2.0f);
    JPH::Vec3 clampedVec1 = clampLength(vec, 1.0f);
    JPH::Vec3 clampedVec2 = clampLength(vec, 2.0f);
    JPH::Vec3 normalizedVec = vec.Normalized();

    QCOMPARE(clampedVec1.GetX(), normalizedVec.GetX());
    QCOMPARE(clampedVec1.GetY(), normalizedVec.GetY());
    QCOMPARE(clampedVec1.GetZ(), normalizedVec.GetZ());

    QCOMPARE(clampedVec2.GetX(), normalizedVec.GetX() * 2.0f);
    QCOMPARE(clampedVec2.GetY(), normalizedVec.GetY() * 2.0f);
    QCOMPARE(clampedVec2.GetZ(), normalizedVec.GetZ() * 2.0f);

    // a vector already within the limit comes back unchanged
    JPH::Vec3 unclampedVec = clampLength(vec, 10.0f);
    QCOMPARE(unclampedVec.GetX(), vec.GetX());
    QCOMPARE(unclampedVec.GetY(), vec.GetY());
    QCOMPARE(unclampedVec.GetZ(), vec.GetZ());
}
