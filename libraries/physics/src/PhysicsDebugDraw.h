//
//  PhysicsDebugDraw.h
//  libraries/physics/src
//
//  Created by Anthony Thibault 2018-4-18
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_PhysicsDebugDraw_h
#define hifi_PhysicsDebugDraw_h

#include <stdint.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

// Draws what Jolt wants drawn (body shapes, bounds, constraints) as lines through the engine's DebugDraw,
// which the renderer shows for one frame. Contact points are drawn by the engine itself from its contact map.
class PhysicsDebugDraw : public JPH::DebugRendererSimple {
public:
    enum Mode : uint32_t {
        DRAW_NOTHING = 0,
        DRAW_WIREFRAME = 1 << 0,
        DRAW_AABBS = 1 << 1,
        DRAW_CONTACT_POINTS = 1 << 2,
        DRAW_CONSTRAINTS = 1 << 3,
        DRAW_CONSTRAINT_LIMITS = 1 << 4
    };

    PhysicsDebugDraw();

    void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override;
    void DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3, JPH::ColorArg color, ECastShadow castShadow) override;
    void DrawText3D(JPH::RVec3Arg position, const std::string_view& string, JPH::ColorArg color, float height) override;

    void drawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color);
    void drawContactPoint(const glm::vec3& pointOnB, const glm::vec3& normalOnB, float distance, const glm::vec3& color);

    // the simulation origin's position in the world, added to everything drawn
    void setWorldOffset(const glm::vec3& offset) { _worldOffset = offset; }
    // the transform of the world being drawn (a frame's transform in the domain; identity for the root)
    void setFrameTransform(const glm::vec3& position, const glm::quat& rotation) { _framePosition = position; _frameRotation = rotation; }

private:
    glm::vec3 toWorld(const glm::vec3& point) const { return _framePosition + _frameRotation * point + _worldOffset; }

    glm::vec3 _worldOffset { 0.0f };
    glm::vec3 _framePosition { 0.0f };
    glm::quat _frameRotation;
};

#endif // hifi_PhysicsDebugDraw_h
