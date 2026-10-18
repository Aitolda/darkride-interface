//
//  ImpostorBaker.h
//  interface/src/editor
//
//  Bakes a model entity's impostor (IMPOSTORS.md 2.2): six orthographic frames of the entity's models (its own and its
//  descendants') drawn by the render engine's bake task, composed into two 3 x 2 atlases (albedo + coverage; object-
//  space normal + depth), written to the local impostors folder and to My Files, and recorded in the entity's
//  userData as the "impostor" entry the card renderer reads. Scripts reach it as Impostors.bake(id, size).
//
//  Created by the editor session on 2026-10-18.
//

#ifndef hifi_ImpostorBaker_h
#define hifi_ImpostorBaker_h

#include <memory>
#include <vector>

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUuid>

#include <DependencyManager.h>
#include <ImpostorBake.h>
#include <Model.h>

class EditorUpload;

class ImpostorBaker : public QObject, public Dependency {
    Q_OBJECT
    SINGLETON_DEPENDENCY

public:
    ImpostorBaker();

    // bakes the entity, size x size pixels a frame (128 to 1024); queued behind a bake under way; baked() says how it went
    Q_INVOKABLE void bake(const QUuid& entityID, int size = 256);
    Q_INVOKABLE bool isBusy() const { return _job != nullptr || !_pending.empty(); }
    // where the atlases are written locally
    Q_INVOKABLE QString folder() const;

signals:
    // message: the albedo atlas's URL when ok, the reason otherwise
    void baked(const QUuid& entityID, bool ok, const QString& message);

private:
    struct Job {
        QUuid id;
        int size { 256 };
        std::vector<ModelPointer> models;
        std::vector<ImpostorBake::FramePointer> frames;
        glm::vec3 center;       // the bounding sphere, in the entity's own frame
        float radius { 0.0f };
        QString stem;           // <name>-<id8>: the entity's files, whatever their bake time
        QString name;           // the stem, the bake time and ".impostor"
        QString albedoUrl;
        QString normalUrl;
        int uploadsLeft { 0 };
        QString uploadError;
        QElapsedTimer clock;
    };

    void start(const QUuid& entityID, int size);
    void poll();
    void compose();
    void upload(const QByteArray& png, const QString& fileName, QString& urlOut);
    void writeEntry();
    void untag();
    void finish(bool ok, const QString& message);

    std::unique_ptr<Job> _job;
    std::vector<std::pair<QUuid, int>> _pending;
    QTimer _timer;
};

#endif // hifi_ImpostorBaker_h
