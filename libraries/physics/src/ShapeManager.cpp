//
//  ShapeManager.cpp
//  libraries/physics/src
//
//  Created by Andrew Meadows 2014.10.29
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ShapeManager.h"

#include <glm/gtx/norm.hpp>

#include <QThreadPool>

#include <NumericalConstants.h>

const int MAX_RING_SIZE = 256;

ShapeManager::ShapeManager() {
    _garbageRing.reserve(MAX_RING_SIZE);
    _nextOrphanExpiry = std::chrono::steady_clock::now();
}

ShapeManager::~ShapeManager() {
    // the shapes let go of themselves with the references
    _shapeMap.clear();
    if (_deadWorker) {
        delete _deadWorker;
        _deadWorker = nullptr;
    }
}

const JPH::Shape* ShapeManager::getShape(const ShapeInfo& info) {
    if (info.getType() == SHAPE_TYPE_NONE) {
        return nullptr;
    }
    uint64_t key = info.getHash();
    auto itr = _shapeMap.find(key);
    if (itr != _shapeMap.end()) {
        itr->second.refCount++;
        return itr->second.shape.GetPtr();
    }
    if (info.getType() == SHAPE_TYPE_STATIC_MESH) {
        // mesh shapes are expensive to build (Jolt builds a tree over the triangles), so they go to a worker
        ++_workRequestCount;
        const auto pendingItr = std::find(_pendingMeshShapes.begin(), _pendingMeshShapes.end(), key);
        if (pendingItr == _pendingMeshShapes.end()) {
            _pendingMeshShapes.push_back(key);
            ShapeFactory::Worker* worker = _deadWorker;
            if (!worker) {
                worker = new ShapeFactory::Worker(info);
            } else {
                worker->shapeInfo = info;
                _deadWorker = nullptr;
            }
            worker->setAutoDelete(false);
            QObject::connect(worker, &ShapeFactory::Worker::submitWork, this, &ShapeManager::acceptWork);
            QThreadPool::globalInstance()->start(worker);
        }
        return nullptr;
    }
    ShapeFactory::ShapeRef shape = ShapeFactory::createShapeFromInfo(info);
    if (shape) {
        ShapeReference newRef;
        newRef.refCount = 1;
        newRef.shape = shape;
        newRef.key = key;
        _shapeMap[key] = newRef;
    }
    return shape.GetPtr();
}

const JPH::Shape* ShapeManager::getShapeByKey(uint64_t key) {
    auto itr = _shapeMap.find(key);
    if (itr != _shapeMap.end()) {
        itr->second.refCount++;
        return itr->second.shape.GetPtr();
    }
    return nullptr;
}

bool ShapeManager::hasShapeWithKey(uint64_t key) const {
    return _shapeMap.find(key) != _shapeMap.end();
}

void ShapeManager::addToGarbage(uint64_t key) {
    // look for existing entry in _garbageRing
    int32_t ringSize = (int32_t)(_garbageRing.size());
    for (int32_t i = 0; i < ringSize; ++i) {
        int32_t j = (_ringIndex + ringSize) % ringSize;
        if (_garbageRing[j] == key) {
            // already in list
            return;
        }
    }
    // add to list
    if (ringSize == MAX_RING_SIZE) {
        // remove one
        auto itr = _shapeMap.find(_garbageRing[_ringIndex]);
        if (itr != _shapeMap.end() && itr->second.refCount == 0) {
            _shapeMap.erase(itr);
        }
        _garbageRing[_ringIndex] = key;
        _ringIndex = (_ringIndex + 1) % ringSize;
    } else {
        _garbageRing.push_back(key);
    }
}

bool ShapeManager::releaseShapeByKey(uint64_t key) {
    auto itr = _shapeMap.find(key);
    if (itr != _shapeMap.end()) {
        if (itr->second.refCount > 0) {
            itr->second.refCount--;
            if (itr->second.refCount == 0) {
                addToGarbage(key);
            }
            return true;
        } else {
            // attempt to remove shape that has no refs
            assert(false);
        }
    } else {
        // attempt to remove unmanaged shape
        assert(false);
    }
    return false;
}

bool ShapeManager::releaseShape(const JPH::Shape* shape) {
    for (auto& entry : _shapeMap) {
        if (shape == entry.second.shape.GetPtr()) {
            return releaseShapeByKey(entry.second.key);
        }
    }
    return false;
}

void ShapeManager::collectGarbage() {
    int numShapes = (int32_t)(_garbageRing.size());
    for (int i = 0; i < numShapes; ++i) {
        auto itr = _shapeMap.find(_garbageRing[i]);
        if (itr != _shapeMap.end() && itr->second.refCount == 0) {
            _shapeMap.erase(itr);
        }
    }
    _ringIndex = 0;
    _garbageRing.clear();
}

int ShapeManager::getNumReferences(const ShapeInfo& info) const {
    auto itr = _shapeMap.find(info.getHash());
    if (itr != _shapeMap.end()) {
        return itr->second.refCount;
    }
    return 0;
}

int ShapeManager::getNumReferences(const JPH::Shape* shape) const {
    for (const auto& entry : _shapeMap) {
        if (shape == entry.second.shape.GetPtr()) {
            return entry.second.refCount;
        }
    }
    return 0;
}

bool ShapeManager::hasShape(const JPH::Shape* shape) const {
    for (const auto& entry : _shapeMap) {
        if (shape == entry.second.shape.GetPtr()) {
            return true;
        }
    }
    return false;
}

void ShapeManager::acceptWork(ShapeFactory::Worker* worker) {
    auto itr = std::find(_pendingMeshShapes.begin(), _pendingMeshShapes.end(), worker->shapeInfo.getHash());
    if (itr == _pendingMeshShapes.end()) {
        // the work was canceled; the reference lets the shape go
        worker->shape = nullptr;
    } else {
        // clear pending status
        *itr = _pendingMeshShapes.back();
        _pendingMeshShapes.pop_back();

        // cache the new shape
        if (worker->shape) {
            ShapeReference newRef;
            newRef.refCount = 0;
            newRef.shape = worker->shape;
            newRef.key = worker->shapeInfo.getHash();
            _shapeMap[newRef.key] = newRef;

            // This shape's refCount is zero because an object requested it but then was deleted before
            // the worker finished. If nothing claims it within a second it goes to the garbage.
            const int64_t SHAPE_EXPIRY = USECS_PER_SECOND;
            auto now = std::chrono::steady_clock::now();
            auto newExpiry = now + std::chrono::microseconds(SHAPE_EXPIRY);
            if (_nextOrphanExpiry < now) {
                _nextOrphanExpiry = newExpiry;
                // check for expired orphan shapes
                size_t i = 0;
                while (i < _orphans.size()) {
                    auto expiry = _orphans[i].expiry;
                    if (expiry < now) {
                        auto orphanItr = _shapeMap.find(_orphans[i].key);
                        if (orphanItr != _shapeMap.end() && orphanItr->second.refCount == 0) {
                            addToGarbage(_orphans[i].key);
                        }
                        _orphans[i] = _orphans.back();
                        _orphans.pop_back();
                    } else {
                        if (expiry < _nextOrphanExpiry) {
                            _nextOrphanExpiry = expiry;
                        }
                        ++i;
                    }
                }
            }
            _orphans.push_back(KeyExpiry(newRef.key, newExpiry));
        }
    }

    disconnect(worker, &ShapeFactory::Worker::submitWork, this, &ShapeManager::acceptWork);

    if (_deadWorker) {
        // delete the previous deadWorker manually
        delete _deadWorker;
    }
    // save this dead worker for later
    worker->shapeInfo.clear();
    worker->shape = nullptr;
    _deadWorker = worker;
    ++_workDeliveryCount;
}
