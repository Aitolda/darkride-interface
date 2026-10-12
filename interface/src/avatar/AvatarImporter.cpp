//
//  AvatarImporter.cpp
//  interface/src/avatar
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "AvatarImporter.h"

#include <functional>
#include <map>
#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegExp>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <AssimpSerializer.h>
#include <AutoRig.h>
#include <AvatarWriter.h>
#include <FBXSerializer.h>
#include <GLMHelpers.h>
#include <GLTFSerializer.h>
#include <OBJSerializer.h>
#include <OffscreenUi.h>
#include <PathUtils.h>
#include <StandardSkeleton.h>

#include "Application.h"
#include "AvatarManager.h"
#include "MyAvatar.h"

namespace {

const char* TITLE = "Import Avatar";
const float USUAL_HEIGHT = 1.70f;

// The joints a rig needs before Darkride's animations can drive it.
const char* const NEEDED_JOINTS[] = { "Hips", "Spine", "Spine1", "Neck", "Head",
                                      "LeftArm", "LeftForeArm", "LeftHand", "RightArm", "RightForeArm", "RightHand",
                                      "LeftUpLeg", "LeftLeg", "LeftFoot", "RightUpLeg", "RightLeg", "RightFoot" };

enum class RigKind { None, Standard, Other };

struct Import {
    QString path;
    QString name;
    HFMModel::Pointer model;
    autorig::Body body;
    RigKind rig { RigKind::None };
    QString why;  // for RigKind::Other: what keeps the animations from driving it
};
using ImportPointer = std::shared_ptr<Import>;

HFMModel::Pointer readModel(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    const QByteArray data = file.readAll();
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
    const QVariantHash mapping;
    if (suffix == "fbx") {
        return FBXSerializer().read(data, mapping, url);
    }
    if (suffix == "gltf" || suffix == "glb") {
        return GLTFSerializer().read(data, mapping, url);
    }
    if (suffix == "obj") {
        return OBJSerializer().read(data, mapping, url);
    }
    return AssimpSerializer().read(data, mapping, url);
}

// None: no skin. Standard: the needed joints, each with Mixamo's axes (its Y axis along its bone), so Darkride's
// animations drive it as it is. Other: a skeleton of its own that the animations would drive wrongly (AnimClip.cpp
// copies each joint's absolute rotation onto the avatar).
RigKind classify(const HFMModel& model, QString& why) {
    if (model.skinDeformers.empty()) {
        return RigKind::None;
    }
    std::map<QString, int> byName;
    for (int j = 0; j < (int)model.joints.size(); j++) {
        byName.emplace(model.joints[j].name.section(':', -1).toLower(), j);
    }
    const autorig::StandardSkeleton& standard = autorig::StandardSkeleton::get();
    for (const char* name : NEEDED_JOINTS) {
        auto found = byName.find(QString(name).toLower());
        if (found == byName.end()) {
            why = QString("it has no %1 joint").arg(name);
            return RigKind::Other;
        }
        const int child = standard.boneChild(standard.indexOf(name));
        auto childFound = child >= 0 ? byName.find(QString(standard.joint(child).name).toLower()) : byName.end();
        if (childFound == byName.end()) {
            continue;
        }
        const glm::mat4& joint = model.joints[found->second].globalTransform;
        const glm::vec3 bone = extractTranslation(model.joints[childFound->second].globalTransform) - extractTranslation(joint);
        const glm::vec3 axis = glm::mat3(joint) * glm::vec3(0.0f, 1.0f, 0.0f);
        if (glm::length(bone) > 0.0f && glm::length(axis) > 0.0f &&
            glm::dot(glm::normalize(axis), glm::normalize(bone)) < 0.9f) {
            why = QString("its %1 joint is not turned the way Darkride's animations expect").arg(name);
            return RigKind::Other;
        }
    }
    return RigKind::Standard;
}

QString avatarName(const QString& path) {
    QString name = QFileInfo(path).completeBaseName();
    name.replace(QRegExp("[^A-Za-z0-9_-]+"), "_");
    return name.isEmpty() ? QString("avatar") : name;
}

// A new folder for each import (name, name-2, name-3...), so the client never shows a cached copy of an earlier one.
QString avatarFolder(const QString& name) {
    const QString base = PathUtils::getAppDataPath() + "Avatars/" + name;
    QString folder = base;
    for (int n = 2; QDir(folder).exists(); n++) {
        folder = base + "-" + QString::number(n);
    }
    return folder;
}

void ask(const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
         const std::function<void(QMessageBox::StandardButton)>& then) {
    ModalDialogListener* dialog = OffscreenUi::asyncQuestion(TITLE, text, buttons, defaultButton);
    QObject::connect(dialog, &ModalDialogListener::response, qApp, [dialog, then](QVariant answer) {
        QObject::disconnect(dialog, &ModalDialogListener::response, qApp, nullptr);
        then(static_cast<QMessageBox::StandardButton>(answer.toInt()));
    });
}

void wear(const QString& fstPath, const QString& name) {
    const QString url = QUrl::fromLocalFile(fstPath).toString();
    DependencyManager::get<AvatarManager>()->getMyAvatar()->useFullAvatarURL(url, name);
    emit qApp->fullAvatarURLChanged(url, name);
}

void offerToWear(const QString& fstPath, const QString& name, const QString& what) {
    ask(what + "\n\nWear it now? Only you will see it until it is in My Files.", QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes, [fstPath, name](QMessageBox::StandardButton answer) {
            if (answer == QMessageBox::Yes) {
                wear(fstPath, name);
            }
        });
}

// The rigging and saving happen off the main thread; the answer comes back to it.
void autoRig(const ImportPointer& import, float scale) {
    QtConcurrent::run([import, scale]() {
        autorig::Body body = import->body;
        if (scale != 1.0f) {
            body.scale(scale);
        }
        const autorig::Result result = autorig::rig(body);
        autorig::WrittenAvatar written;
        if (result.ok) {
            written = autorig::writeAvatar(*import->model, body, result, import->path, avatarFolder(import->name), import->name);
        }
        QMetaObject::invokeMethod(qApp, [import, result, written]() {
            if (!result.ok) {
                OffscreenUi::asyncWarning(TITLE, "Could not rig " + import->name + ": " + result.error + ".");
            } else if (!written.ok) {
                OffscreenUi::asyncWarning(TITLE, "Could not save " + import->name + ": " + written.error + ".");
            } else {
                offerToWear(written.fstPath, import->name,
                            import->name + " is rigged with Darkride's standard skeleton and saved in " +
                                QFileInfo(written.fstPath).absolutePath() + ".");
            }
        }, Qt::QueuedConnection);
    });
}

void checkHeightThenRig(const ImportPointer& import) {
    const float height = import->body.maximum.y - import->body.minimum.y;
    if (height > 2.4f || height < 0.6f) {
        ask(QString("%1 is %2 m tall, which looks like a units mistake in its file.\n\nYes: make it %3 m tall.\nNo: keep "
                    "it %2 m tall.")
                .arg(import->name).arg(height, 0, 'f', 2).arg(USUAL_HEIGHT, 0, 'f', 2),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes,
            [import, height](QMessageBox::StandardButton answer) {
                if (answer == QMessageBox::Yes) {
                    autoRig(import, USUAL_HEIGHT / height);
                } else if (answer == QMessageBox::No) {
                    autoRig(import, 1.0f);
                }
            });
    } else {
        autoRig(import, 1.0f);
    }
}

void decide(const ImportPointer& import) {
    if (!import->model || import->body.triangles.empty()) {
        OffscreenUi::asyncWarning(TITLE, "Could not read " + QFileInfo(import->path).fileName() + " as a character.");
        return;
    }
    switch (import->rig) {
        case RigKind::Standard:
            ask(import->name + " already has Darkride's standard skeleton.\n\nYes: wear it with its own rig.\nNo: replace "
                "its rig with an automatic one.",
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes,
                [import](QMessageBox::StandardButton answer) {
                    if (answer == QMessageBox::Yes) {
                        const QString folder = avatarFolder(import->name);
                        QDir().mkpath(folder);
                        const QString fstPath = QDir(folder).filePath(import->name + ".fst");
                        if (autorig::writeFst(fstPath, import->name, QUrl::fromLocalFile(import->path).toString())) {
                            wear(fstPath, import->name);
                        } else {
                            OffscreenUi::asyncWarning(TITLE, "Could not write " + fstPath + ".");
                        }
                    } else if (answer == QMessageBox::No) {
                        checkHeightThenRig(import);
                    }
                });
            break;
        case RigKind::Other:
            ask(import->name + " has a skeleton of its own, but " + import->why +
                    ", so Darkride's animations would move it wrongly.\n\nAuto rig it? Its skeleton and skin weights are "
                    "replaced with Darkride's standard skeleton. The file itself is not changed.",
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes, [import](QMessageBox::StandardButton answer) {
                    if (answer == QMessageBox::Yes) {
                        checkHeightThenRig(import);
                    }
                });
            break;
        case RigKind::None:
            ask(import->name + " has no skeleton yet.\n\nAuto rig it with Darkride's standard skeleton?",
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes, [import](QMessageBox::StandardButton answer) {
                    if (answer == QMessageBox::Yes) {
                        checkHeightThenRig(import);
                    }
                });
            break;
    }
}

}

void AvatarImporter::importFile(const QString& path) {
    const QString name = avatarName(path);
    if (QFileInfo(path).suffix().toLower() == "fst") {
        ask("Wear " + name + "? Only you will see it until it is in My Files.", QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes, [path, name](QMessageBox::StandardButton answer) {
                if (answer == QMessageBox::Yes) {
                    wear(path, name);
                }
            });
        return;
    }
    auto import = std::make_shared<Import>();
    import->path = path;
    import->name = name;
    QtConcurrent::run([import]() {
        import->model = readModel(import->path);
        if (import->model) {
            import->body = autorig::gatherBody(*import->model);
            import->rig = classify(*import->model, import->why);
        }
        QMetaObject::invokeMethod(qApp, [import]() {
            decide(import);
        }, Qt::QueuedConnection);
    });
}
