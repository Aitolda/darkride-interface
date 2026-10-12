//
//  JoltUtilTests.h
//  tests/physics/src
//
//  Round trips between glm and Jolt vectors, quaternions and matrices through JoltUtil.h.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_JoltUtilTests_h
#define hifi_JoltUtilTests_h

#include <QtTest/QtTest>

class JoltUtilTests : public QObject {
    Q_OBJECT

private slots:
    void fromJoltToGLM();
    void fromGLMToJolt();
    void realVectorRoundTrip();
    void clampLengthTest();
};

#endif // hifi_JoltUtilTests_h
