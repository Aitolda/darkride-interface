//
//  RenderableLightEntityItem.cpp
//  interface/src
//
//  Created by Brad Hefta-Gaub on 8/6/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderableLightEntityItem.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <GLMHelpers.h>
#include <PerfStat.h>

using namespace render;
using namespace render::entities;

bool LightEntityRenderer::needsRenderUpdateFromTypedEntity(const TypedEntityPointer& entity) const {
    // userData changes raise no flag of their own: the cookie may have changed with it
    QString userData = entity->getUserData();
    return resultWithReadLock<bool>([&] { return userData != _userData; });
}

void LightEntityRenderer::doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) {
    QString userData = entity->getUserData();
    withWriteLock([&] {
        if (userData == _userData) {
            return;
        }
        _userData = userData;
        QString cookieURL = QJsonDocument::fromJson(userData.toUtf8()).object().value("cookie").toString();
        if (cookieURL != _cookieURL) {
            _cookieURL = cookieURL;
            _cookie = cookieURL.isEmpty() ? NetworkTexturePointer() : DependencyManager::get<TextureCache>()->getTexture(QUrl(cookieURL));
        }
    });
}

void LightEntityRenderer::doRenderUpdateAsynchronousTyped(const TypedEntityPointer& entity) {

    auto& lightPayload = *_lightPayload;
   
    lightPayload.setVisible(_visible);

    auto light = lightPayload.editLight();
    light->setPosition(entity->getWorldPosition());
    light->setOrientation(entity->getWorldOrientation());

    bool success;
    lightPayload.editBound() = entity->getAABox(success);
    if (!success) {
        lightPayload.editBound() = render::Item::Bound();
    }

    glm::vec3 dimensions = entity->getScaledDimensions();
    float largestDiameter = glm::compMax(dimensions);
    light->setMaximumRadius(largestDiameter / 2.0f);

    light->setColor(toGlm(entity->getColor()));

    float intensity = entity->getIntensity();//* entity->getFadingRatio();
    light->setIntensity(intensity);

    light->setFalloffRadius(entity->getFalloffRadius());


    float exponent = entity->getExponent();
    float cutoff = glm::radians(entity->getCutoff());
    if (!entity->getIsSpotlight()) {
        light->setType(graphics::Light::POINT);
    } else {
        light->setType(graphics::Light::SPOT);

        light->setSpotAngle(cutoff);
        light->setSpotExponent(exponent);
    }
    // the entity's Cast Shadows; spot lights' shadows are drawn by RenderLocalShadowTask, as are their cookies
    light->setCastShadows(entity->getCanCastShadow());
    light->setCookie(resultWithReadLock<gpu::TextureSourcePointer>([&] {
        return _cookie ? _cookie->_textureSource : gpu::TextureSourcePointer();
    }));
}

ItemKey LightEntityRenderer::getKey() {
    return payloadGetKey(_lightPayload);
}

Item::Bound LightEntityRenderer::getBound() {
    return payloadGetBound(_lightPayload);
}

void LightEntityRenderer::doRender(RenderArgs* args) {
    // const bool hasChanged = evaluateEntityZoneCullState(_entity);
    _lightPayload->render(args);
}

