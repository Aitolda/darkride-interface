//
//  ShapeInfoTests.cpp
//  tests/physics/src
//
//  Created by Andrew Meadows on 2014.11.02
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ShapeInfoTests.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <unordered_map>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/DecoratedShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <HashKey.h>
#include <JoltSupport.h>
#include <ShapeInfo.h>
#include <ShapeFactory.h>
#include <StreamUtils.h>

QTEST_MAIN(ShapeInfoTests)

// Enable this to manually run testHashCollisions
// (NOT a regular unit test; takes ~40 secs to run on an i7)
//#define MANUAL_TEST true

namespace {
    // the shape a decorated shape (RotatedTranslated, Scaled, OffsetCenterOfMass) wraps; null for any other shape
    const JPH::Shape* innerShapeOf(const JPH::Shape* shape) {
        if (shape && shape->GetType() == JPH::EShapeType::Decorated) {
            return static_cast<const JPH::DecoratedShape*>(shape)->GetInnerShape();
        }
        return nullptr;
    }
}

void ShapeInfoTests::initTestCase() {
    // Jolt's allocator and type registry must be in place before the first shape
    QVERIFY(JoltSupport::initialize());
}

void ShapeInfoTests::testHashFunctions() {
#if MANUAL_TEST
    int maxTests = 10000000;
    ShapeInfo info;
    // 64-bit hash -> 32-bit hash of the first shape that produced it
    std::unordered_map<uint64_t, int32_t> hashes;

    const int32_t NUM_HASH_BITS = 32;
    uint32_t bits[NUM_HASH_BITS];
    uint32_t masks[NUM_HASH_BITS];
    for (int i = 0; i < NUM_HASH_BITS; ++i) {
        bits[i] = 0;
        masks[i] = 1UL << i;
    }

    float deltaLength = 1.0f / (HashKey::getNumQuantizedValuesPerMeter() - 3.0f);
    float endLength = 2000.0f * deltaLength;
    int numSteps = (int)(endLength / deltaLength);

    int testCount = 0;
    int numCollisions = 0;

    auto startTime = std::chrono::steady_clock::now();
    for (int i = 1; i < numSteps && testCount < maxTests; ++i) {
        float radiusX = (float)i * deltaLength;
        // test sphere
        info.setSphere(radiusX);
        ++testCount;
        HashKey hashKey(info.getHash());
        auto hashItr = hashes.find(hashKey.getHash64());
        if (hashItr != hashes.end()) {
            std::cout << testCount << "  hash collision sphere radius = " << radiusX
                << "  h = 0x" << std::hex << hashKey.getHash() << " : 0x" << hashItr->second
                << std::dec << std::endl;
            ++numCollisions;
            assert(false);
        } else {
            hashes.insert({ hashKey.getHash64(), hashKey.getHash() });
        }
        // track bit distribution counts to evaluate hash function randomness
        for (int j = 0; j < NUM_HASH_BITS; ++j) {
            if (masks[j] & hashKey.getHash()) {
                ++bits[j];
            }
        }

        for (int y = 1; y < numSteps && testCount < maxTests; ++y) {
            float radiusY = (float)y * deltaLength;
            for (int z = 1; z < numSteps && testCount < maxTests; ++z) {
                float radiusZ = (float)z * deltaLength;
                // test box
                info.setBox(glm::vec3(radiusX, radiusY, radiusZ));
                ++testCount;
                HashKey hashKey(info.getHash());
                auto hashItr = hashes.find(hashKey.getHash64());
                if (hashItr != hashes.end()) {
                    std::cout << testCount << "  hash collision box dimensions = < " << radiusX
                        << ", " << radiusY << ", " << radiusZ << " >"
                        << "  h = 0x" << std::hex << hashKey.getHash() << " : 0x" << hashItr->second << " : 0x" << hashKey.getHash64()
                        << std::dec << std::endl;
                    ++numCollisions;
                    assert(false);
                } else {
                    hashes.insert({ hashKey.getHash64(), hashKey.getHash() });
                }
                // track bit distribution counts to evaluate hash function randomness
                for (int k = 0; k < NUM_HASH_BITS; ++k) {
                    if (masks[k] & hashKey.getHash()) {
                        ++bits[k];
                    }
                }
            }
        }
    }
    auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    std::cout << msec << " msec with " << numCollisions << " collisions out of " << testCount << " hashes" << std::endl;

    // print out distribution of bits
    // ideally the numbers in each bin will be about the same
    for (int i = 0; i < NUM_HASH_BITS; ++i) {
        std::cout << "bit 0x" << std::hex << masks[i] << std::dec << " = " << bits[i] << std::endl;
    }
    QCOMPARE(numCollisions, 0);
#endif // MANUAL_TEST
}

void ShapeInfoTests::testBoxShape() {
    ShapeInfo info;
    glm::vec3 halfExtents(1.23f, 4.56f, 7.89f);
    info.setBox(halfExtents);
    HashKey hashKey(info.getHash());

    ShapeFactory::ShapeRef shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::Box);

    ShapeInfo otherInfo = info;
    HashKey otherKey = otherInfo.getHash();
    QCOMPARE(hashKey.getHash(), otherKey.getHash());
}

void ShapeInfoTests::testSphereShape() {
    ShapeInfo info;
    float radius = 1.23f;
    info.setSphere(radius);
    HashKey hashKey = info.getHash();

    ShapeFactory::ShapeRef shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::Sphere);

    ShapeInfo otherInfo = info;
    HashKey otherKey = otherInfo.getHash();
    QCOMPARE(hashKey.getHash(), otherKey.getHash());
}

void ShapeInfoTests::testCylinderShape() {
    ShapeInfo info;
    float radius = 1.23f;
    float halfHeight = 4.56f;

    // a cylinder along Y is Jolt's own cylinder
    info.setParams(SHAPE_TYPE_CYLINDER_Y, glm::vec3(radius, halfHeight, radius));
    HashKey hashKey(info.getHash());

    ShapeFactory::ShapeRef shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::Cylinder);

    ShapeInfo otherInfo = info;
    HashKey otherKey = otherInfo.getHash();
    QCOMPARE(hashKey.getHash(), otherKey.getHash());

    // along X or Z it is that cylinder rotated into place
    info.setParams(SHAPE_TYPE_CYLINDER_X, glm::vec3(halfHeight, radius, radius));
    shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::RotatedTranslated);
    const JPH::Shape* inner = innerShapeOf(shape.GetPtr());
    QVERIFY(inner != nullptr);
    QCOMPARE((int)inner->GetSubType(), (int)JPH::EShapeSubType::Cylinder);

    info.setParams(SHAPE_TYPE_CYLINDER_Z, glm::vec3(radius, radius, halfHeight));
    shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::RotatedTranslated);
    inner = innerShapeOf(shape.GetPtr());
    QVERIFY(inner != nullptr);
    QCOMPARE((int)inner->GetSubType(), (int)JPH::EShapeSubType::Cylinder);
}

void ShapeInfoTests::testCapsuleShape() {
    ShapeInfo info;
    float radius = 1.23f;
    float cylinderHalfHeight = 4.56f;

    // a capsule along Y is Jolt's own capsule
    info.setCapsuleY(radius, cylinderHalfHeight);
    HashKey hashKey(info.getHash());

    ShapeFactory::ShapeRef shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::Capsule);

    ShapeInfo otherInfo = info;
    HashKey otherKey = otherInfo.getHash();
    QCOMPARE(hashKey.getHash(), otherKey.getHash());

    // along X or Z it is that capsule rotated into place (half extents: radius across, full half length along the axis)
    info.setParams(SHAPE_TYPE_CAPSULE_X, glm::vec3(cylinderHalfHeight + radius, radius, radius));
    shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::RotatedTranslated);
    const JPH::Shape* inner = innerShapeOf(shape.GetPtr());
    QVERIFY(inner != nullptr);
    QCOMPARE((int)inner->GetSubType(), (int)JPH::EShapeSubType::Capsule);

    info.setParams(SHAPE_TYPE_CAPSULE_Z, glm::vec3(radius, radius, cylinderHalfHeight + radius));
    shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::RotatedTranslated);
    inner = innerShapeOf(shape.GetPtr());
    QVERIFY(inner != nullptr);
    QCOMPARE((int)inner->GetSubType(), (int)JPH::EShapeSubType::Capsule);

    // a capsule with no cylinder left between its caps is a sphere
    info.setCapsuleY(radius, 0.0f);
    shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::Sphere);
}

void ShapeInfoTests::testOffsetShape() {
    // a shape whose geometry sits away from the body's origin is wrapped in a RotatedTranslated shape
    ShapeInfo info;
    info.setBox(glm::vec3(1.0f, 2.0f, 3.0f));
    glm::vec3 offset(0.5f, 2.0f, -1.5f);
    info.setOffset(offset);

    ShapeFactory::ShapeRef shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::RotatedTranslated);

    const JPH::RotatedTranslatedShape* offsetShape = static_cast<const JPH::RotatedTranslatedShape*>(shape.GetPtr());
    QCOMPARE((int)offsetShape->GetInnerShape()->GetSubType(), (int)JPH::EShapeSubType::Box);
    JPH::Vec3 position = offsetShape->GetPosition();
    QCOMPARE(position.GetX(), offset.x);
    QCOMPARE(position.GetY(), offset.y);
    QCOMPARE(position.GetZ(), offset.z);

    // an offset too small to matter is ignored
    info.setOffset(glm::vec3(0.1f * MIN_SHAPE_OFFSET));
    shape = ShapeFactory::createShapeFromInfo(info);
    QVERIFY(shape.GetPtr() != nullptr);
    QCOMPARE((int)shape->GetSubType(), (int)JPH::EShapeSubType::Box);
}
