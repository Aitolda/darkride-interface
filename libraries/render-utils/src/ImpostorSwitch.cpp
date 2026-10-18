//
//  ImpostorSwitch.cpp
//  libraries/render-utils/src
//
//  Created by the editor session on 2026-10-18. See ImpostorSwitch.h and IMPOSTORS.md 2.4.
//

#include "ImpostorSwitch.h"

#include <ViewFrustum.h>

namespace {
    // the hysteresis band of the main view, on the squared half-tangent: the card comes at a fifth under the angle's
    // measure and goes at a fifth over it, about a tenth of the distance either way
    const float BAND = 0.2f;
}

bool ImpostorSwitch::showsCard(const RenderArgs* args) {
    if (!_cardReady || _radius <= 0.0f || !args || !args->hasViewFrustum()) {
        return false;
    }
    if (args->_renderMode == RenderArgs::RenderMode::SHADOW_RENDER_MODE) {
        return false;
    }
    const float thresholdSq = args->_impostorAngleHalfTanSq;
    if (thresholdSq <= 0.0f) {
        return false;
    }
    // LODManager::shouldRender's test: the bound's half size squared against the angle's half tangent squared times
    // the squared distance; the card shows where the model would be too small for the impostor angle
    glm::vec3 toCamera = args->getViewFrustum().getPosition() - _center;
    float distanceSq = glm::dot(toCamera, toCamera);
    float radiusSq = _radius * _radius;
    if (args->_renderMode != RenderArgs::RenderMode::DEFAULT_RENDER_MODE) {
        return radiusSq < thresholdSq * distanceSq;
    }
    // the main view: every item of a frame reads the same state, and the band's two tests agree with each other for
    // either state, so the model's parts and the card never both draw or both stay away
    if (_mainViewShowsCard) {
        if (radiusSq > thresholdSq * (1.0f + BAND) * distanceSq) {
            _mainViewShowsCard = false;
        }
    } else {
        if (radiusSq < thresholdSq * (1.0f - BAND) * distanceSq) {
            _mainViewShowsCard = true;
        }
    }
    return _mainViewShowsCard;
}
