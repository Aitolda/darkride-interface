//
//  ImpostorBaker.cpp
//  interface/src/editor
//
//  Created by the editor session on 2026-10-18. See ImpostorBaker.h and IMPOSTORS.md 2.2.
//

#include "ImpostorBaker.h"

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QThread>
#include <QtGui/QPainter>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <AbstractViewStateInterface.h>
#include <EntityScriptingInterface.h>
#include <EntityTreeRenderer.h>
#include <PathUtils.h>
#include <RenderableModelEntityItem.h>

#include "EditorCreate.h"
#include "ui/UserFiles.h"
#include "../InterfaceLogging.h"

namespace {
    const int POLL_MS = 30;
    const int TIMEOUT_MS = 15000;
    const int SETTLE_FRAMES = 3;      // the new item keys take a frame or two to reach the scene
    const float CAMERA_DISTANCE = 2.0f; // in radii, from the centre: any distance beyond the sphere does for an orthographic view

    // the six views, in the entity's own frame: where the camera stands (its backward axis) and its up
    struct View { glm::vec3 backward; glm::vec3 up; };
    const View VIEWS[6] = {
        { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) },     // +X
        { glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) },    // -X
        { glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f) },    // +Y, from above, -Z up the picture
        { glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) },    // -Y, from below
        { glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f) },     // +Z
        { glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f) }     // -Z
    };
}

ImpostorBaker::ImpostorBaker() {
    _timer.setInterval(POLL_MS);
    connect(&_timer, &QTimer::timeout, this, &ImpostorBaker::poll);
}

QString ImpostorBaker::folder() const {
    return PathUtils::getAppLocalDataPath() + "impostors/";
}

void ImpostorBaker::bake(const QUuid& entityID, int size) {
    // scripts call from their own thread; the bake, its timer and the scene's models belong to the main thread
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, entityID, size] { bake(entityID, size); }, Qt::QueuedConnection);
        return;
    }
    size = glm::clamp(size, 128, 1024);
    if (_job) {
        _pending.push_back({ entityID, size });
        return;
    }
    start(entityID, size);
}

void ImpostorBaker::start(const QUuid& entityID, int size) {
    _job = std::make_unique<Job>();
    _job->id = entityID;
    _job->size = size;
    _job->clock.start();

    auto entityTreeRenderer = DependencyManager::get<EntityTreeRenderer>();
    auto tree = entityTreeRenderer ? entityTreeRenderer->getTree() : nullptr;
    EntityItemPointer entity = tree ? tree->findEntityByID(entityID) : nullptr;
    if (!entity) {
        finish(false, "no such entity");
        return;
    }

    // the models: the entity's own and its descendants', loaded
    auto addModel = [&](const SpatiallyNestablePointer& nestable) {
        auto item = std::dynamic_pointer_cast<EntityItem>(nestable);
        if (!item || item->getType() != EntityTypes::Model) {
            return;
        }
        auto renderer = std::dynamic_pointer_cast<render::entities::ModelEntityRenderer>(entityTreeRenderer->renderableForEntityId(item->getID()));
        ModelPointer model = renderer ? renderer->getModelForBake() : nullptr;
        if (model && model->isLoaded()) {
            _job->models.push_back(model);
        } else {
            qCDebug(interfaceapp) << "impostor bake skips" << item->getName() << item->getID() << (renderer ? (model ? "model not loaded yet" : "no model") : "no renderer");
        }
    };
    addModel(entity);
    entity->forEachDescendant(addModel);
    {
        QStringList seen;
        entity->forEachDescendant([&](const SpatiallyNestablePointer& nestable) {
            auto item = std::dynamic_pointer_cast<EntityItem>(nestable);
            seen << (item ? item->getName() + "/" + EntityTypes::getEntityTypeName(item->getType()) : QString("?"));
        });
        qCDebug(interfaceapp) << "impostor bake sees" << seen.size() << "descendants of" << entity->getName() << ":" << seen.join(", ");
    }
    if (_job->models.empty()) {
        finish(false, "no loaded model on the entity or under it");
        return;
    }

    // the bounding sphere in world space, then in the entity's frame
    AABox box;
    bool first = true;
    for (const auto& model : _job->models) {
        AABox bound = model->getRenderableMeshBound();
        if (first) {
            box = bound;
            first = false;
        } else {
            box += bound;
        }
    }
    // the sphere around the union of the models' world boxes: never too small, generous for a flat thing seen
    // diagonally (a tighter sphere from the vertices is a later refinement)
    glm::vec3 center = box.calcCenter();
    float radius = 0.5f * glm::length(box.getDimensions());
    if (radius <= 0.0f) {
        finish(false, "the model has no size");
        return;
    }
    glm::vec3 position = entity->getWorldPosition();
    bool success = false;
    glm::quat rotation = entity->getWorldOrientation(success);
    _job->center = glm::inverse(rotation) * (center - position);
    _job->radius = radius;
    QString name = entity->getName().isEmpty() ? QString("entity") : entity->getName();
    _job->name = EditorUpload::safeFileName(name) + "-" + entityID.toString().mid(1, 8) + ".impostor";

    // the six frames
    const float distance = CAMERA_DISTANCE * radius;
    for (int i = 0; i < 6; i++) {
        auto frame = std::make_shared<ImpostorBake::Frame>();
        glm::vec3 backward = rotation * VIEWS[i].backward;
        glm::vec3 up = rotation * VIEWS[i].up;
        glm::vec3 right = glm::cross(up, backward);
        glm::quat orientation = glm::quat_cast(glm::mat3(right, up, backward));
        frame->nearClip = distance - radius;
        frame->farClip = distance + radius;
        frame->projection = glm::ortho(-radius, radius, -radius, radius, frame->nearClip, frame->farClip);
        frame->frustum.setPosition(center + backward * distance);
        frame->frustum.setOrientation(orientation);
        frame->frustum.setProjection(frame->projection);
        frame->frustum.calculate();
        frame->worldToObject = glm::mat4_cast(glm::inverse(rotation));
        frame->size = size;
        frame->albedoFramebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create("impostorBakeAlbedo", gpu::Element::COLOR_RGBA_32, size, size));
        frame->normalFramebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create("impostorBakeNormal", gpu::Element::COLOR_RGBA_32, size, size));
        frame->albedo = QImage(size, size, QImage::Format_ARGB32);
        frame->normal = QImage(size, size, QImage::Format_ARGB32);
        _job->frames.push_back(frame);
    }

    // the subject's items carry the bake tag until the frames are drawn
    auto scene = AbstractViewStateInterface::instance()->getMain3DScene();
    for (const auto& model : _job->models) {
        model->setTagMask((uint8_t)model->getTagMask() | ImpostorBake::TAG, scene);
    }
    ImpostorBake::instance().enqueue(_job->frames, SETTLE_FRAMES);
    qCDebug(interfaceapp) << "impostor bake started" << entityID << name << "radius" << radius << "centre" << _job->center.x << _job->center.y << _job->center.z
                          << "models" << _job->models.size() << "size" << size;
    _timer.start();
}

void ImpostorBaker::poll() {
    if (!_job) {
        _timer.stop();
        return;
    }
    bool done = true;
    for (const auto& frame : _job->frames) {
        if (frame->state != ImpostorBake::Frame::DONE) {
            done = false;
            break;
        }
    }
    if (!done) {
        if (_job->clock.elapsed() > TIMEOUT_MS) {
            finish(false, "timed out waiting for the frames");
        }
        return;
    }
    _timer.stop();
    untag();
    compose();
}

void ImpostorBaker::untag() {
    if (!_job) {
        return;
    }
    auto scene = AbstractViewStateInterface::instance()->getMain3DScene();
    for (const auto& model : _job->models) {
        model->setTagMask((uint8_t)model->getTagMask() & ~ImpostorBake::TAG, scene);
    }
    _job->models.clear();
}

void ImpostorBaker::compose() {
    const int size = _job->size;
    QImage albedo(3 * size, 2 * size, QImage::Format_ARGB32);
    QImage normal(3 * size, 2 * size, QImage::Format_ARGB32);
    albedo.fill(Qt::transparent);
    normal.fill(QColor(128, 128, 255, 255));
    {
        QPainter albedoPainter(&albedo);
        QPainter normalPainter(&normal);
        albedoPainter.setCompositionMode(QPainter::CompositionMode_Source);
        normalPainter.setCompositionMode(QPainter::CompositionMode_Source);
        for (int i = 0; i < 6; i++) {
            const auto& frame = _job->frames[i];
            // glReadPixels gives the bottom row first
            albedoPainter.drawImage((i % 3) * size, (i / 3) * size, frame->albedo.mirrored(false, true));
            normalPainter.drawImage((i % 3) * size, (i / 3) * size, frame->normal.mirrored(false, true));
        }
    }
    _job->frames.clear();

    QDir().mkpath(folder());
    QString albedoPath = folder() + _job->name + ".albedo.png";
    QString normalPath = folder() + _job->name + ".normal.png";
    QByteArray albedoPng;
    QByteArray normalPng;
    {
        QBuffer buffer(&albedoPng);
        buffer.open(QIODevice::WriteOnly);
        albedo.save(&buffer, "PNG");
    }
    {
        QBuffer buffer(&normalPng);
        buffer.open(QIODevice::WriteOnly);
        normal.save(&buffer, "PNG");
    }
    QFile albedoFile(albedoPath);
    QFile normalFile(normalPath);
    if (!albedoFile.open(QIODevice::WriteOnly) || albedoFile.write(albedoPng) != albedoPng.size() ||
        !normalFile.open(QIODevice::WriteOnly) || normalFile.write(normalPng) != normalPng.size()) {
        finish(false, "could not write " + albedoPath);
        return;
    }
    albedoFile.close();
    normalFile.close();
    _job->albedoUrl = QUrl::fromLocalFile(albedoPath).toString();
    _job->normalUrl = QUrl::fromLocalFile(normalPath).toString();
    qCDebug(interfaceapp) << "impostor atlases written" << albedoPath << "in" << _job->clock.elapsed() << "ms";

    // to My Files when logged in; the entry takes the local files meanwhile
    if (UserFiles::isLoggedIn()) {
        _job->uploadsLeft = 2;
        upload(albedoPng, _job->name + ".albedo.png", _job->albedoUrl);
        upload(normalPng, _job->name + ".normal.png", _job->normalUrl);
    } else {
        writeEntry();
    }
}

void ImpostorBaker::upload(const QByteArray& png, const QString& fileName, QString& urlOut) {
    auto uploader = new EditorUpload(this);
    QString* target = &urlOut;
    connect(uploader, &EditorUpload::finished, this, [this, uploader, target](const QString& url) {
        uploader->deleteLater();
        if (_job) {
            *target = url;
            if (--_job->uploadsLeft == 0) {
                writeEntry();
            }
        }
    });
    connect(uploader, &EditorUpload::failed, this, [this, uploader](const QString& error) {
        uploader->deleteLater();
        if (_job) {
            _job->uploadError = error;
            if (--_job->uploadsLeft == 0) {
                writeEntry();
            }
        }
    });
    QString error;
    if (!uploader->startData(png, fileName, "/impostors", "image/png", error)) {
        uploader->deleteLater();
        _job->uploadError = error;
        if (--_job->uploadsLeft == 0) {
            writeEntry();
        }
    }
}

void ImpostorBaker::writeEntry() {
    auto entityTreeRenderer = DependencyManager::get<EntityTreeRenderer>();
    auto tree = entityTreeRenderer ? entityTreeRenderer->getTree() : nullptr;
    EntityItemPointer entity = tree ? tree->findEntityByID(_job->id) : nullptr;
    if (!entity) {
        finish(false, "the entity went away");
        return;
    }
    QJsonObject userData = QJsonDocument::fromJson(entity->getUserData().toUtf8()).object();
    QJsonObject entry;
    entry["albedo"] = _job->albedoUrl;
    entry["normal"] = _job->normalUrl;
    entry["frames"] = "cube6";
    entry["size"] = _job->size;
    entry["radius"] = (double)_job->radius;
    entry["center"] = QJsonArray({ (double)_job->center.x, (double)_job->center.y, (double)_job->center.z });
    entry["roughness"] = 0.7;
    entry["metallic"] = 0.0;
    entry["baked"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    userData["impostor"] = entry;

    EntityItemProperties properties;
    properties.setUserData(QString::fromUtf8(QJsonDocument(userData).toJson(QJsonDocument::Compact)));
    DependencyManager::get<EntityScriptingInterface>()->editEntity(_job->id, properties);
    QString message = _job->albedoUrl;
    if (!_job->uploadError.isEmpty()) {
        message += " (upload failed: " + _job->uploadError + ")";
    }
    finish(true, message);
}

void ImpostorBaker::finish(bool ok, const QString& message) {
    _timer.stop();
    untag();
    QUuid id = _job ? _job->id : QUuid();
    _job.reset();
    if (ok) {
        qCDebug(interfaceapp) << "impostor baked" << id << message;
    } else {
        qCWarning(interfaceapp) << "impostor bake failed" << id << message;
    }
    emit baked(id, ok, message);
    if (!_pending.empty()) {
        auto next = _pending.front();
        _pending.erase(_pending.begin());
        start(next.first, next.second);
    }
}
