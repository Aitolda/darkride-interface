//
//  ContactInfo.h
//  libraries/physics/src
//
//  Created by Andrew Meadows 2015.01.20
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ContactEvent_h
#define hifi_ContactEvent_h

#include <stdint.h>

#include <glm/glm.hpp>

#include "RegisteredMetaTypes.h"

// One touching pair, as last reported by the engine, with the step bookkeeping that turns a stream of
// contact reports into START, CONTINUE and END events.
class ContactInfo {
public:
    // positionOnB and normalOnB are in the simulation frame; the normal points from B toward A, as Bullet's
    // did, so every consumer keeps its sign conventions. distance is negative when penetrating.
    void update(uint32_t currentStep, const glm::vec3& positionOnB, const glm::vec3& normalOnB, float distance);
    ContactEventType computeType(uint32_t thisStep);

    const glm::vec3& getPositionWorldOnB() const { return positionWorldOnB; }
    glm::vec3 getPositionWorldOnA() const { return positionWorldOnB + normalWorldOnB * distance; }
    bool readyForContinue(uint32_t thisStep);
    uint32_t getLastStep() const { return _lastStep; }

    glm::vec3 positionWorldOnB { 0.0f };
    glm::vec3 normalWorldOnB { 0.0f, 1.0f, 0.0f };
    float distance { 0.0f };

private:
    uint32_t _lastStep { 0 };
    uint32_t _continueExpiry { 0 };
};

#endif // hifi_ContactEvent_h
