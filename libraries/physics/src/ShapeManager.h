//
//  ShapeManager.h
//  libraries/physics/src
//
//  Created by Andrew Meadows 2014.10.29
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ShapeManager_h
#define hifi_ShapeManager_h

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <vector>

#include <QObject>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <ShapeInfo.h>

#include "ShapeFactory.h"
#include "HashKey.h"

// The ShapeManager handles the ref-counting on shapes:
//
// Each object added to the physics simulation must have a shape.  Separate objects
// can share the same shape, and there is a cost to creating and tearing down the shapes,
// so the ShapeManager keeps a cache of them, keyed by the ShapeInfo hash. Jolt's own
// reference counting keeps a shape alive for as long as a body uses it; the manager's count
// decides when the cache lets go.
//
// Shapes are deleted after being released for a while. Big mesh shapes are built
// on a worker thread, and the result arrives through acceptWork(); until then
// getShape() returns nothing for them and the caller waits for the delivery count to change.
//
class ShapeManager : public QObject {
    Q_OBJECT
public:

    ShapeManager();
    ~ShapeManager();

    /// \return pointer to shape (null when none yet) and increments the count for the ShapeInfo
    const JPH::Shape* getShape(const ShapeInfo& info);
    const JPH::Shape* getShapeByKey(uint64_t key);
    bool hasShapeWithKey(uint64_t key) const;

    /// \return true if shape was found and released
    bool releaseShape(const JPH::Shape* shape);

    /// delete shapes that have zero references
    void collectGarbage();

    // validation methods
    int getNumShapes() const { return (int)_shapeMap.size(); }
    int getNumReferences(const ShapeInfo& info) const;
    int getNumReferences(const JPH::Shape* shape) const;
    bool hasShape(const JPH::Shape* shape) const;

    uint32_t getWorkRequestCount() const { return _workRequestCount; }
    uint32_t getWorkDeliveryCount() const { return _workDeliveryCount; }

protected slots:
    void acceptWork(ShapeFactory::Worker* worker);

private:
    void addToGarbage(uint64_t key);
    bool releaseShapeByKey(uint64_t key);

    class ShapeReference {
    public:
        int refCount { 0 };
        ShapeFactory::ShapeRef shape;
        uint64_t key { 0 };
    };

    using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

    class KeyExpiry {
    public:
        KeyExpiry(uint64_t k, std::chrono::time_point<std::chrono::steady_clock> e) : expiry(e), key(k) {}
        TimePoint expiry;
        uint64_t key;
    };

    std::unordered_map<uint64_t, ShapeReference> _shapeMap;
    std::vector<uint64_t> _garbageRing;
    std::vector<uint64_t> _pendingMeshShapes;
    std::vector<KeyExpiry> _orphans;
    ShapeFactory::Worker* _deadWorker { nullptr };
    TimePoint _nextOrphanExpiry;
    uint32_t _ringIndex { 0 };
    std::atomic_uint _workRequestCount { 0 };
    std::atomic_uint _workDeliveryCount { 0 };
};

#endif // hifi_ShapeManager_h
