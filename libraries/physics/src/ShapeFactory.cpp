//
//  ShapeFactory.cpp
//  libraries/physics/src
//
//  Created by Andrew Meadows 2014.12.01
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ShapeFactory.h"

#include <glm/gtx/norm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <SharedUtil.h> // for MILLIMETERS_PER_METER

#include "JoltUtil.h"
#include "PhysicsLogging.h"

using ShapeFactory::ShapeRef;

// These are the same normalized directions used by Bullet's btShapeHull class.
// 12 points for the face centers of a dodecahedron plus another 30 points
// for the midpoints the edges, for a total of 42.
const uint32_t NUM_UNIT_SPHERE_DIRECTIONS = 42;
static const glm::vec3 _unitSphereDirections[NUM_UNIT_SPHERE_DIRECTIONS] = {
    glm::vec3(0.000000f, -0.000000f, -1.000000f),
    glm::vec3(0.723608f, -0.525725f, -0.447219f),
    glm::vec3(-0.276388f, -0.850649f, -0.447219f),
    glm::vec3(-0.894426f, -0.000000f, -0.447216f),
    glm::vec3(-0.276388f, 0.850649f, -0.447220f),
    glm::vec3(0.723608f, 0.525725f, -0.447219f),
    glm::vec3(0.276388f, -0.850649f, 0.447220f),
    glm::vec3(-0.723608f, -0.525725f, 0.447219f),
    glm::vec3(-0.723608f, 0.525725f, 0.447219f),
    glm::vec3(0.276388f, 0.850649f, 0.447219f),
    glm::vec3(0.894426f, 0.000000f, 0.447216f),
    glm::vec3(-0.000000f, 0.000000f, 1.000000f),
    glm::vec3(0.425323f, -0.309011f, -0.850654f),
    glm::vec3(-0.162456f, -0.499995f, -0.850654f),
    glm::vec3(0.262869f, -0.809012f, -0.525738f),
    glm::vec3(0.425323f, 0.309011f, -0.850654f),
    glm::vec3(0.850648f, -0.000000f, -0.525736f),
    glm::vec3(-0.525730f, -0.000000f, -0.850652f),
    glm::vec3(-0.688190f, -0.499997f, -0.525736f),
    glm::vec3(-0.162456f, 0.499995f, -0.850654f),
    glm::vec3(-0.688190f, 0.499997f, -0.525736f),
    glm::vec3(0.262869f, 0.809012f, -0.525738f),
    glm::vec3(0.951058f, 0.309013f, 0.000000f),
    glm::vec3(0.951058f, -0.309013f, 0.000000f),
    glm::vec3(0.587786f, -0.809017f, 0.000000f),
    glm::vec3(0.000000f, -1.000000f, 0.000000f),
    glm::vec3(-0.587786f, -0.809017f, 0.000000f),
    glm::vec3(-0.951058f, -0.309013f, -0.000000f),
    glm::vec3(-0.951058f, 0.309013f, -0.000000f),
    glm::vec3(-0.587786f, 0.809017f, -0.000000f),
    glm::vec3(-0.000000f, 1.000000f, -0.000000f),
    glm::vec3(0.587786f, 0.809017f, -0.000000f),
    glm::vec3(0.688190f, -0.499997f, 0.525736f),
    glm::vec3(-0.262869f, -0.809012f, 0.525738f),
    glm::vec3(-0.850648f, 0.000000f, 0.525736f),
    glm::vec3(-0.262869f, 0.809012f, 0.525738f),
    glm::vec3(0.688190f, 0.499997f, 0.525736f),
    glm::vec3(0.525730f, 0.000000f, 0.850652f),
    glm::vec3(0.162456f, -0.499995f, 0.850654f),
    glm::vec3(-0.425323f, -0.309011f, 0.850654f),
    glm::vec3(-0.425323f, 0.309011f, 0.850654f),
    glm::vec3(0.162456f, 0.499995f, 0.850654f)
};

namespace {

const float MIN_CONVEX_RADIUS = 0.001f;

// A convex radius keeps contacts cheap and stable, but it must fit inside the shape.
float convexRadiusFor(const glm::vec3& halfExtents) {
    float smallest = glm::min(halfExtents.x, glm::min(halfExtents.y, halfExtents.z));
    return glm::clamp(0.5f * smallest, 0.0f, JPH::cDefaultConvexRadius);
}

template <typename Settings>
ShapeRef create(Settings& settings, const char* what) {
    JPH::Shape::ShapeResult result = settings.Create();
    if (result.HasError()) {
        qCWarning(physics) << "ShapeFactory: could not create" << what << ":" << result.GetError().c_str();
        return nullptr;
    }
    return result.Get();
}

ShapeRef createConvexHull(const ShapeInfo::PointList& points) {
    assert(points.size() > 0);
    if (points.empty()) {
        return nullptr;
    }

    glm::vec3 center = points[0];
    glm::vec3 maxCorner = center;
    glm::vec3 minCorner = center;
    for (size_t i = 1; i < points.size(); i++) {
        center += points[i];
        maxCorner = glm::max(maxCorner, points[i]);
        minCorner = glm::min(minCorner, points[i]);
    }
    center /= (float)(points.size());
    if (glm::any(glm::isnan(center))) {
        // don't feed garbage to the engine
        assert(false); // crash here in DEBUG so we can investigate source of bad input
        return nullptr;
    }

    // Jolt shrinks the hull by the convex radius and adds it back as a rounded margin, so the visible surface
    // and the collision surface agree without the point correction Bullet needed. The radius still has to fit.
    glm::vec3 diagonal = maxCorner - minCorner;
    float convexRadius = glm::clamp(0.25f * glm::min(diagonal.x, glm::min(diagonal.y, diagonal.z)),
                                    0.0f, JPH::cDefaultConvexRadius);

    JPH::Array<JPH::Vec3> hullPoints;
    if (points.size() > (size_t)MAX_HULL_POINTS) {
        // too many points: keep those that project the farthest along the canonical directions
        std::vector<uint32_t> finalIndices;
        finalIndices.reserve(NUM_UNIT_SPHERE_DIRECTIONS);
        for (uint32_t i = 0; i < NUM_UNIT_SPHERE_DIRECTIONS; ++i) {
            uint32_t bestIndex = 0;
            float maxDistance = glm::dot(_unitSphereDirections[i], points[0] - center);
            for (uint32_t j = 1; j < points.size(); ++j) {
                float distance = glm::dot(_unitSphereDirections[i], points[j] - center);
                if (distance > maxDistance) {
                    maxDistance = distance;
                    bestIndex = j;
                }
            }
            if (std::find(finalIndices.begin(), finalIndices.end(), bestIndex) == finalIndices.end()) {
                finalIndices.push_back(bestIndex);
            }
        }
        hullPoints.reserve(finalIndices.size());
        for (uint32_t index : finalIndices) {
            hullPoints.push_back(glmToJolt(points[index]));
        }
    } else {
        hullPoints.reserve(points.size());
        for (const glm::vec3& point : points) {
            hullPoints.push_back(glmToJolt(point));
        }
    }

    JPH::ConvexHullShapeSettings settings(hullPoints, convexRadius);
    return create(settings, "convex hull");
}

ShapeRef createStaticMesh(const ShapeInfo& info) {
    assert(info.getType() == SHAPE_TYPE_STATIC_MESH); // should only get here for mesh shapes

    const ShapeInfo::PointCollection& pointCollection = info.getPointCollection();
    if (pointCollection.size() < 1) {
        // no lists of points to work with
        return nullptr;
    }

    // we only use the first point collection
    const ShapeInfo::PointList& pointList = pointCollection[0];
    if (pointList.size() < 3) {
        // not enough distinct points to make a non-degenerate triangle
        return nullptr;
    }

    const ShapeInfo::TriangleIndices& triangleIndices = info.getTriangleIndices();
    int32_t numIndices = (int32_t)triangleIndices.size();
    if (numIndices < 3) {
        // not enough indices to make a single triangle
        return nullptr;
    }

    JPH::VertexList vertices;
    vertices.reserve(pointList.size());
    for (const glm::vec3& point : pointList) {
        vertices.push_back(JPH::Float3(point.x, point.y, point.z));
    }

    const int32_t VERTICES_PER_TRIANGLE = 3;
    int32_t numTriangles = numIndices / VERTICES_PER_TRIANGLE;
    int32_t numVertices = (int32_t)pointList.size();
    JPH::IndexedTriangleList triangles;
    triangles.reserve(numTriangles);
    for (int32_t t = 0; t < numTriangles; ++t) {
        int32_t a = triangleIndices[3 * t];
        int32_t b = triangleIndices[3 * t + 1];
        int32_t c = triangleIndices[3 * t + 2];
        if (a < 0 || b < 0 || c < 0 || a >= numVertices || b >= numVertices || c >= numVertices) {
            continue; // a bad index would read past the vertex list
        }
        triangles.push_back(JPH::IndexedTriangle((JPH::uint32)a, (JPH::uint32)b, (JPH::uint32)c));
    }
    if (triangles.empty()) {
        return nullptr;
    }

    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    return create(settings, "static mesh");
}

// a shape rotated so its own Y axis lies along the requested axis
ShapeRef rotatedToAxis(const ShapeRef& shape, int axis) {
    if (!shape) {
        return nullptr;
    }
    JPH::Quat rotation = JPH::Quat::sIdentity();
    if (axis == 0) {
        rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), -0.5f * JPH::JPH_PI); // Y -> X
    } else if (axis == 2) {
        rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);  // Y -> Z
    } else {
        return shape;
    }
    JPH::RotatedTranslatedShapeSettings settings(JPH::Vec3::sZero(), rotation, shape);
    return create(settings, "rotated shape");
}

ShapeRef createCapsule(float radius, float halfExtentAlongAxis, int axis) {
    const float MIN_RADIUS = 0.001f;
    radius = glm::max(radius, MIN_RADIUS);
    float cylinderHalfHeight = halfExtentAlongAxis - radius;
    if (cylinderHalfHeight < MIN_RADIUS) {
        return new JPH::SphereShape(radius);
    }
    return rotatedToAxis(new JPH::CapsuleShape(cylinderHalfHeight, radius), axis);
}

ShapeRef createCylinder(const glm::vec3& halfExtents, int axis) {
    float halfHeight = halfExtents[axis];
    float radius = (axis == 1) ? glm::max(halfExtents.x, halfExtents.z)
                               : glm::max(halfExtents[(axis + 1) % 3], halfExtents[(axis + 2) % 3]);
    const float MIN_SIZE = 0.001f;
    halfHeight = glm::max(halfHeight, MIN_SIZE);
    radius = glm::max(radius, MIN_SIZE);
    float convexRadius = glm::clamp(0.5f * glm::min(halfHeight, radius), 0.0f, JPH::cDefaultConvexRadius);
    return rotatedToAxis(new JPH::CylinderShape(halfHeight, radius, convexRadius), axis);
}

ShapeRef compoundOf(const std::vector<ShapeRef>& parts) {
    if (parts.empty()) {
        return nullptr;
    }
    if (parts.size() == 1) {
        return parts[0];
    }
    JPH::StaticCompoundShapeSettings settings;
    for (const ShapeRef& part : parts) {
        if (part) {
            settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), part.GetPtr());
        }
    }
    return create(settings, "compound shape");
}

} // namespace

ShapeRef ShapeFactory::createShapeFromInfo(const ShapeInfo& info) {
    ShapeRef shape;
    int type = info.getType();
    switch(type) {
        case SHAPE_TYPE_BOX: {
            glm::vec3 halfExtents = glm::max(info.getHalfExtents(), glm::vec3(MIN_CONVEX_RADIUS));
            shape = new JPH::BoxShape(glmToJolt(halfExtents), convexRadiusFor(halfExtents));
        }
        break;
        case SHAPE_TYPE_SPHERE: {
            glm::vec3 halfExtents = info.getHalfExtents();
            float radius = glm::max(halfExtents.x, glm::max(halfExtents.y, halfExtents.z));
            shape = new JPH::SphereShape(glm::max(radius, MIN_CONVEX_RADIUS));
        }
        break;
        case SHAPE_TYPE_MULTISPHERE: {
            // Jolt has no multi-sphere primitive; a compound of spheres is the same volume
            auto sphereCollection = info.getSphereCollection();
            if (sphereCollection.size() == 1) {
                const glm::vec4& sphereData = sphereCollection[0];
                JPH::RotatedTranslatedShapeSettings settings(glmToJolt(glm::vec3(sphereData)), JPH::Quat::sIdentity(),
                                                             new JPH::SphereShape(glm::max(sphereData.w, MIN_CONVEX_RADIUS)));
                shape = create(settings, "sphere");
            } else if (!sphereCollection.empty()) {
                JPH::StaticCompoundShapeSettings settings;
                for (auto& sphereData : sphereCollection) {
                    settings.AddShape(glmToJolt(glm::vec3(sphereData)), JPH::Quat::sIdentity(),
                                      new JPH::SphereShape(glm::max(sphereData.w, MIN_CONVEX_RADIUS)));
                }
                shape = create(settings, "multi-sphere");
            }
        }
        break;
        case SHAPE_TYPE_ELLIPSOID: {
            glm::vec3 halfExtents = info.getHalfExtents();
            float radius = halfExtents.x;
            const float MIN_RADIUS = 0.001f;
            const float MIN_RELATIVE_SPHERICAL_ERROR = 0.001f;
            if (radius > MIN_RADIUS
                    && fabsf(radius - halfExtents.y) / radius < MIN_RELATIVE_SPHERICAL_ERROR
                    && fabsf(radius - halfExtents.z) / radius < MIN_RELATIVE_SPHERICAL_ERROR) {
                // close enough to true sphere
                shape = new JPH::SphereShape(radius);
            } else {
                ShapeInfo::PointList points;
                points.reserve(NUM_UNIT_SPHERE_DIRECTIONS);
                for (uint32_t i = 0; i < NUM_UNIT_SPHERE_DIRECTIONS; ++i) {
                    points.push_back(_unitSphereDirections[i] * halfExtents);
                }
                shape = createConvexHull(points);
            }
        }
        break;
        case SHAPE_TYPE_CAPSULE_Y: {
            glm::vec3 halfExtents = info.getHalfExtents();
            shape = createCapsule(halfExtents.x, halfExtents.y, 1);
        }
        break;
        case SHAPE_TYPE_CAPSULE_X: {
            glm::vec3 halfExtents = info.getHalfExtents();
            shape = createCapsule(halfExtents.y, halfExtents.x, 0);
        }
        break;
        case SHAPE_TYPE_CAPSULE_Z: {
            glm::vec3 halfExtents = info.getHalfExtents();
            shape = createCapsule(halfExtents.x, halfExtents.z, 2);
        }
        break;
        case SHAPE_TYPE_CYLINDER_X: {
            shape = createCylinder(info.getHalfExtents(), 0);
        }
        break;
        case SHAPE_TYPE_CYLINDER_Z: {
            shape = createCylinder(info.getHalfExtents(), 2);
        }
        break;
        case SHAPE_TYPE_CIRCLE:
        case SHAPE_TYPE_CYLINDER_Y: {
            shape = createCylinder(info.getHalfExtents(), 1);
        }
        break;
        case SHAPE_TYPE_COMPOUND:
        case SHAPE_TYPE_SIMPLE_HULL: {
            const ShapeInfo::PointCollection& pointCollection = info.getPointCollection();
            uint32_t numSubShapes = info.getNumSubShapes();
            if (numSubShapes == 1) {
                if (!pointCollection.empty()) {
                    shape = createConvexHull(pointCollection[0]);
                }
            } else {
                std::vector<ShapeRef> hulls;
                hulls.reserve(pointCollection.size());
                for (const ShapeInfo::PointList& hullPoints : pointCollection) {
                    ShapeRef hull = createConvexHull(hullPoints);
                    if (hull) {
                        hulls.push_back(hull);
                    }
                }
                shape = compoundOf(hulls);
            }
        }
        break;
        case SHAPE_TYPE_SIMPLE_COMPOUND: {
            const ShapeInfo::PointCollection& pointCollection = info.getPointCollection();
            const ShapeInfo::TriangleIndices& triangleIndices = info.getTriangleIndices();
            uint32_t numIndices = (uint32_t)triangleIndices.size();
            uint32_t numMeshes = info.getNumSubShapes();
            const uint32_t MIN_NUM_SIMPLE_COMPOUND_INDICES = 2; // END_OF_MESH_PART + END_OF_MESH
            if (numMeshes > 0 && numIndices > MIN_NUM_SIMPLE_COMPOUND_INDICES) {
                uint32_t i = 0;
                std::vector<ShapeRef> hulls;
                for (auto& points : pointCollection) {
                    // build a hull around each part
                    while (i < numIndices) {
                        ShapeInfo::PointList hullPoints;
                        hullPoints.reserve(points.size());
                        while (i < numIndices) {
                            int32_t j = triangleIndices[i];
                            ++i;
                            if (j == END_OF_MESH_PART) {
                                // end of part
                                break;
                            }
                            if (j >= 0 && j < (int32_t)points.size()) {
                                hullPoints.push_back(points[j]);
                            }
                        }
                        if (hullPoints.size() > 0) {
                            ShapeRef hull = createConvexHull(hullPoints);
                            if (hull) {
                                hulls.push_back(hull);
                            }
                        }

                        assert(i < numIndices);
                        if (i < numIndices && triangleIndices[i] == END_OF_MESH) {
                            // end of mesh
                            ++i;
                            break;
                        }
                    }
                }
                shape = compoundOf(hulls);
            }
        }
        break;
        case SHAPE_TYPE_STATIC_MESH: {
            shape = createStaticMesh(info);
        }
        break;
        default:
        break;
    }
    if (shape) {
        if (glm::length2(info.getOffset()) > MIN_SHAPE_OFFSET * MIN_SHAPE_OFFSET) {
            // the shape's geometry sits away from the body's origin (the entity's registration point)
            JPH::RotatedTranslatedShapeSettings settings(glmToJolt(info.getOffset()), JPH::Quat::sIdentity(), shape.GetPtr());
            shape = create(settings, "offset shape");
        }
    } else {
        qCDebug(physics) << "ShapeFactory: no shape for type" << ShapeInfo::getNameForShapeType((ShapeType)type);
    }
    return shape;
}

void ShapeFactory::Worker::run() {
    shape = ShapeFactory::createShapeFromInfo(shapeInfo);
    emit submitWork(this);
}
