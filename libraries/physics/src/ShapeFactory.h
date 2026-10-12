//
//  ShapeFactory.h
//  libraries/physics/src
//
//  Created by Andrew Meadows 2014.12.01
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ShapeFactory_h
#define hifi_ShapeFactory_h

#include <glm/glm.hpp>
#include <QObject>
#include <QtCore/QRunnable>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <ShapeInfo.h>

// translates between ShapeInfo and Jolt shapes
namespace ShapeFactory {
    // Jolt shapes are reference counted and immutable; whoever holds a reference keeps the shape alive.
    using ShapeRef = JPH::RefConst<JPH::Shape>;

    // Returns a null reference when the info describes no usable shape.
    ShapeRef createShapeFromInfo(const ShapeInfo& info);

    class Worker : public QObject, public QRunnable {
        Q_OBJECT
    public:
        Worker(const ShapeInfo& info) : shapeInfo(info) {}
        void run() override;
        ShapeInfo shapeInfo;
        ShapeRef shape;
    signals:
        void submitWork(Worker*);
    };
};

#endif // hifi_ShapeFactory_h
