//
//  PhysicsDebugDraw.cpp
//  libraries/physics/src
//
//  Created by Anthony Thibault 2018-4-18
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "PhysicsDebugDraw.h"

#include <DebugDraw.h>
#include <GLMHelpers.h>

#include "JoltUtil.h"
#include "PhysicsLogging.h"

PhysicsDebugDraw::PhysicsDebugDraw() {
    Initialize();
}

static glm::vec4 toColor(JPH::ColorArg color) {
    return glm::vec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, 1.0f);
}

void PhysicsDebugDraw::DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) {
    DebugDraw::getInstance().drawRay(joltToGLM(from) + _worldOffset, joltToGLM(to) + _worldOffset, toColor(color));
}

void PhysicsDebugDraw::DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3, JPH::ColorArg color, ECastShadow castShadow) {
    DrawLine(v1, v2, color);
    DrawLine(v2, v3, color);
    DrawLine(v3, v1, color);
}

void PhysicsDebugDraw::DrawText3D(JPH::RVec3Arg position, const std::string_view& string, JPH::ColorArg color, float height) {
    // no text in the debug draw layer
}

void PhysicsDebugDraw::drawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color) {
    DebugDraw::getInstance().drawRay(from + _worldOffset, to + _worldOffset, glm::vec4(color, 1.0f));
}

void PhysicsDebugDraw::drawContactPoint(const glm::vec3& pointOnB, const glm::vec3& normalOnB, float distance, const glm::vec3& color) {
    glm::vec3 normal, tangent, biNormal;
    generateBasisVectors(normalOnB, Vectors::UNIT_X, normal, tangent, biNormal);

    const float CONTACT_POINT_RADIUS = 0.1f;
    const glm::vec3 GREEN(0.0f, 1.0f, 0.0f);
    const glm::vec3 WHITE(1.0f, 1.0f, 1.0f);

    drawLine(pointOnB - normal * CONTACT_POINT_RADIUS, pointOnB + normal * CONTACT_POINT_RADIUS, GREEN);
    drawLine(pointOnB - tangent * CONTACT_POINT_RADIUS, pointOnB + tangent * CONTACT_POINT_RADIUS, WHITE);
    drawLine(pointOnB - biNormal * CONTACT_POINT_RADIUS, pointOnB + biNormal * CONTACT_POINT_RADIUS, WHITE);
}
