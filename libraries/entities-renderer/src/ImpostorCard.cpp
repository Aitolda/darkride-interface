//
//  ImpostorCard.cpp
//  libraries/entities-renderer/src
//
//  Created by the editor session on 2026-10-18. See ImpostorCard.h and IMPOSTORS.md 2.3 and 2.4.
//

#include "ImpostorCard.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <glm/gtx/quaternion.hpp>

#include <DeferredLightingEffect.h>
#include <DependencyManager.h>
#include <StencilMaskPass.h>
#include <render/ShapePipeline.h>
#include <shaders/Shaders.h>

#include "entities-renderer/ShaderConstants.h"

using namespace render;

namespace {
    struct Params {
        glm::mat4 worldToObject;
        glm::mat4 objectToWorld;
        glm::vec4 centerRadius;
        glm::vec4 material;   // roughness, metallic, the frame size in texels, unused
    };

    uint8_t CUSTOM_PIPELINE_NUMBER = 0;
    std::map<bool, ShapePipelinePointer> _pipelines;   // by forward
    gpu::Stream::FormatPointer _quadFormat;
    gpu::BufferPointer _quadBuffer;

    // The main pass asks for the card's pipeline through its custom shape key; the card is no shadow caster yet.
    ShapePipelinePointer impostorPipelineFactory(const ShapePlumber& plumber, const ShapeKey& key, RenderArgs* args) {
        if (_pipelines.empty()) {
            namespace program = shader::entities_renderer::program;
            for (int forward = 0; forward < 2; ++forward) {
                auto state = std::make_shared<gpu::State>();
                state->setCullMode(gpu::State::CULL_NONE);   // the square faces the camera by construction
                state->setDepthTest(true, true, gpu::GREATER_EQUAL);
                PrepareStencil::testMaskDrawShape(*state);
                auto shaderProgram = gpu::Shader::createProgram(forward ? program::impostor_forward : program::impostor);
                // the forward variant lights in place, so it binds the frame's lighting as the lit pipelines'
                // batch setter does (RenderPipelines' lightBatchSetter): the key light, the ambient with its skybox
                // and probe volume, the lights, clusters, probes and shadows; a pipeline without a setter would see
                // whatever the pipeline before it left bound, or nothing when it comes first in the pass
                ShapePipeline::BatchSetter batchSetter = nullptr;
                if (forward) {
                    batchSetter = [](const ShapePipeline&, gpu::Batch& batch, RenderArgs* args) {
                        auto deferredLightingEffect = DependencyManager::get<DeferredLightingEffect>();
                        deferredLightingEffect->setupKeyLightBatch(args, batch);
                        deferredLightingEffect->setupFrameLightingBatch(batch, args);
                    };
                }
                _pipelines[forward != 0] =
                    std::make_shared<ShapePipeline>(gpu::Pipeline::create(shaderProgram, state), nullptr, batchSetter, nullptr);
            }
        }
        return _pipelines[args->_renderMethod == Args::RenderMethod::FORWARD];
    }

    void initImpostorPipelines() {
        static std::once_flag once;
        std::call_once(once, [] {
            CUSTOM_PIPELINE_NUMBER = ShapePipeline::registerCustomShapePipelineFactory(impostorPipelineFactory);
            // the unit square, a triangle strip of two
            const glm::vec3 corners[4] = {
                glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)
            };
            _quadBuffer = std::make_shared<gpu::Buffer>(sizeof(corners), (const gpu::Byte*)corners);
            _quadFormat = std::make_shared<gpu::Stream::Format>();
            _quadFormat->setAttribute(gpu::Stream::POSITION, 0, gpu::Element(gpu::VEC3, gpu::FLOAT, gpu::XYZ), 0);
        });
    }
}

ImpostorEntry ImpostorEntry::parse(const QString& userData) {
    ImpostorEntry entry;
    if (userData.isEmpty() || !userData.contains(QStringLiteral("impostor"))) {
        return entry;
    }
    QJsonValue value = QJsonDocument::fromJson(userData.toUtf8()).object().value("impostor");
    if (!value.isObject()) {
        return entry;
    }
    QJsonObject object = value.toObject();
    entry.spec = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    entry.albedoUrl = object.value("albedo").toString();
    entry.normalUrl = object.value("normal").toString();
    QString frames = object.value("frames").toString("cube6");
    entry.size = object.value("size").toInt(256);
    entry.radius = (float)object.value("radius").toDouble(0.0);
    QJsonArray center = object.value("center").toArray();
    if (center.size() == 3) {
        entry.center = glm::vec3((float)center[0].toDouble(), (float)center[1].toDouble(), (float)center[2].toDouble());
    }
    entry.roughness = (float)object.value("roughness").toDouble(0.7);
    entry.metallic = (float)object.value("metallic").toDouble(0.0);
    entry.valid = frames == "cube6" && !entry.albedoUrl.isEmpty() && !entry.normalUrl.isEmpty() && entry.radius > 0.0f;
    return entry;
}

ImpostorCard::ImpostorCard() : _switch(std::make_shared<ImpostorSwitch>()) {
    initImpostorPipelines();
    _params = std::make_shared<gpu::Buffer>(sizeof(Params), nullptr);
}

void ImpostorCard::setEntry(const ImpostorEntry& entry) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (entry.spec == _pending.entry.spec && entry.valid == _pending.entry.valid) {
        return;
    }
    _pending.entry = entry;
    // the albedo as a colour texture (sRGB, mips); the normal and depth as four linear channels, uncompressed, which
    // is what a lightmap's direction map is (a normal map's two compressed channels would lose the depth)
    auto textureCache = DependencyManager::get<TextureCache>();
    _albedo = entry.valid ? textureCache->getTexture(QUrl(entry.albedoUrl), image::TextureUsage::ALBEDO_TEXTURE) : nullptr;
    _normal = entry.valid ? textureCache->getTexture(QUrl(entry.normalUrl), image::TextureUsage::LIGHTMAP_DIRECTION_TEXTURE) : nullptr;
}

void ImpostorCard::setFrame(const glm::vec3& positionWS, const glm::quat& rotationWS) {
    std::lock_guard<std::mutex> lock(_mutex);
    _pending.position = positionWS;
    _pending.rotation = rotationWS;
}

void ImpostorCard::setVisible(bool visible) {
    std::lock_guard<std::mutex> lock(_mutex);
    _pending.visible = visible;
}

void ImpostorCard::setKeyBits(render::hifi::Tag tagMask, render::hifi::Layer layer, bool subMetaCulled) {
    std::lock_guard<std::mutex> lock(_mutex);
    _pending.tagMask = tagMask;
    _pending.layer = layer;
    _pending.subMetaCulled = subMetaCulled;
}

bool ImpostorCard::waitingForTextures() const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto waiting = [](const NetworkTexturePointer& texture) { return texture && !texture->isLoaded() && !texture->isFailed(); };
    return waiting(_albedo) || waiting(_normal);
}

void ImpostorCard::update() {
    NetworkTexturePointer albedo;
    NetworkTexturePointer normal;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _live = _pending;
        albedo = _albedo;
        normal = _normal;
    }
    const auto& entry = _live.entry;
    _albedoTexture = (albedo && albedo->isLoaded()) ? albedo->getGPUTexture() : nullptr;
    _normalTexture = (normal && normal->isLoaded()) ? normal->getGPUTexture() : nullptr;
    _ready = entry.valid && _albedoTexture && _normalTexture;

    _centerWS = _live.position + _live.rotation * entry.center;
    float radius = entry.valid ? entry.radius : 0.0f;
    _bound = AABox(_centerWS - glm::vec3(radius), glm::vec3(2.0f * radius));
    _switch->setSphere(_centerWS, radius);
    _switch->setCardReady(_ready && _live.visible);

    if (_ready) {
        Params params;
        params.worldToObject = glm::mat4_cast(glm::inverse(_live.rotation));
        params.objectToWorld = glm::mat4_cast(_live.rotation);
        params.centerRadius = glm::vec4(_centerWS, radius);
        // the atlas is 3 x 2 frames: a frame's side is half the atlas's height
        float frameSize = (float)(_albedoTexture->getHeight() / 2);
        params.material = glm::vec4(entry.roughness, entry.metallic, frameSize, (float)_debugMode);
        _params->setSubData(0, params);
    }
}

ItemKey ImpostorCard::getKey() {
    ItemKey::Builder builder = ItemKey::Builder().withTypeShape().withTagBits(_live.tagMask).withLayer(_live.layer);
    if (_live.subMetaCulled) {
        builder.withSubMetaCulled();
    }
    if (!_live.visible || !_ready) {
        builder.withInvisible();
    }
    return builder.build();
}

ShapeKey ImpostorCard::getShapeKey() {
    return ShapeKey::Builder().withCustom(CUSTOM_PIPELINE_NUMBER).build();
}

Item::Bound ImpostorCard::getBound() {
    return _bound;
}

void ImpostorCard::render(RenderArgs* args) {
    if (!_ready || !_live.visible || !args || !args->_batch || !_switch->showsCard(args)) {
        return;
    }
    const float radius = _live.entry.radius;
    const glm::vec3 cameraWS = args->getViewFrustum().getPosition();
    glm::vec3 toCamera = cameraWS - _centerWS;
    float distance = glm::length(toCamera);
    // the square faces the camera; its half side is the radius, and a little more up close, where the sphere's
    // silhouette is wider than its section through the centre (twice the radius at most, inside the sphere)
    glm::quat facing = (distance > 1.0e-5f) ? glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), toCamera / distance) : glm::quat();
    float halfSide = 2.0f * radius;
    if (distance > radius) {
        float ratio = radius / distance;
        halfSide = std::min(radius / sqrtf(1.0f - ratio * ratio), 2.0f * radius);
    }
    Transform transform;
    transform.setTranslation(_centerWS);
    transform.setRotation(facing);
    transform.setScale(glm::vec3(halfSide));

    if (args->_impostorDebug != _debugMode) {
        // the shader's debug view, in the parameters' spare slot
        _debugMode = args->_impostorDebug;
        glm::vec4 material(_live.entry.roughness, _live.entry.metallic, (float)(_albedoTexture->getHeight() / 2), (float)_debugMode);
        _params->setSubData(offsetof(Params, material), sizeof(material), reinterpret_cast<const gpu::Byte*>(&material));
    }

    gpu::Batch& batch = *args->_batch;
    batch.setModelTransform(transform);
    batch.setInputFormat(_quadFormat);
    batch.setInputBuffer(0, _quadBuffer, 0, sizeof(glm::vec3));
    batch.setUniformBuffer(ENTITIES_BUFFER_IMPOSTOR, _params, 0, sizeof(Params));
    batch.setResourceTexture(ENTITIES_TEXTURE_IMPOSTOR_ALBEDO, _albedoTexture);
    batch.setResourceTexture(ENTITIES_TEXTURE_IMPOSTOR_NORMAL, _normalTexture);
    batch.draw(gpu::TRIANGLE_STRIP, 4, 0);
    batch.setResourceTexture(ENTITIES_TEXTURE_IMPOSTOR_ALBEDO, nullptr);
    batch.setResourceTexture(ENTITIES_TEXTURE_IMPOSTOR_NORMAL, nullptr);
    args->_details._trianglesRendered += 2;
}
