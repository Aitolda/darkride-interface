//
//  AutoRigApp.cpp
//  tools/auto-rig/src
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "AutoRigApp.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegExp>
#include <QUrl>

#include <AssimpSerializer.h>
#include <FBX.h>
#include <FBXSerializer.h>
#include <GLMHelpers.h>
#include <GLTFSerializer.h>
#include <OBJSerializer.h>

#include <AutoRig.h>
#include <AvatarWriter.h>
#include <StandardSkeleton.h>

#include "RigCheck.h"
#include "RigPreview.h"

static HFMModel::Pointer readModel(const QString& path) {
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

// The answer key's joints under their standard names: matched without case, after any prefix ("mixamorig:Hips").
static std::map<QString, glm::vec3> keyJoints(const HFMModel& key) {
    const autorig::StandardSkeleton& standard = autorig::StandardSkeleton::get();
    std::map<QString, glm::vec3> joints;
    for (const HFMJoint& joint : key.joints) {
        const QString name = joint.name.section(':', -1).toLower();
        for (int i = 0; i < standard.size(); i++) {
            const QString standardName = standard.joint(i).name;
            if (standardName.toLower() == name && !joints.count(standardName)) {
                joints[standardName] = extractTranslation(joint.globalTransform);
            }
        }
    }
    return joints;
}


// Prints an FBX file's node tree as the engine's parser sees it: each node's name, properties and child count.
static void dumpNode(const FBXNode& node, int depth, int maxDepth, int maxChildren) {
    QStringList properties;
    for (int i = 0; i < node.properties.size() && i < 8; i++) {
        const QVariant& p = node.properties.at(i);
        const QString type = p.typeName();
        if (p.type() == QVariant::ByteArray) {
            QString text = QString::fromLatin1(p.toByteArray().left(60));
            text.replace(QChar('\0'), "\\0").replace(QChar('\1'), "\\1");
            properties << "\"" + text + "\"";
        } else if (type.startsWith("QVector")) {
            int size = 0;
            if (type == "QVector<double>") size = p.value<QVector<double>>().size();
            else if (type == "QVector<float>") size = p.value<QVector<float>>().size();
            else if (type == "QVector<int>") size = p.value<QVector<int>>().size();
            else if (type == "QVector<qlonglong>") size = p.value<QVector<qlonglong>>().size();
            properties << QString("%1[%2]").arg(type).arg(size);
        } else {
            properties << QString("%1:%2").arg(type).arg(p.toString().left(40));
        }
    }
    fprintf(stdout, "%*s%s %s%s\n", depth * 2, "", node.name.constData(), qPrintable(properties.join(", ")),
            node.children.isEmpty() ? "" : qPrintable(QString(" {%1}").arg(node.children.size())));
    if (depth >= maxDepth) {
        return;
    }
    int shown = 0;
    QMap<QByteArray, int> skipped;
    for (const FBXNode& child : node.children) {
        if (shown < maxChildren) {
            dumpNode(child, depth + 1, maxDepth, maxChildren);
            shown++;
        } else {
            skipped[child.name]++;
        }
    }
    for (auto it = skipped.begin(); it != skipped.end(); ++it) {
        fprintf(stdout, "%*s... %d more %s\n", (depth + 1) * 2, "", it.value(), it.key().constData());
    }
}

AutoRigApp::AutoRigApp(int argc, char* argv[]) : QCoreApplication(argc, argv) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Darkride auto-rigger: places the standard skeleton in a character");
    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption inputOption("i", "the character (FBX, glTF, OBJ, or anything Assimp reads)", "file");
    const QCommandLineOption keyOption("key", "a rigged file to measure against (default: the input's own rig)", "file");
    const QCommandLineOption turnOption("turn-around", "the character faces -Z: turn it to face +Z");
    const QCommandLineOption jsonOption("json", "write the joints and the answer key as JSON", "file");
    const QCommandLineOption outputOption("o", "write the rigged avatar (FBX, textures, .fst) into this folder", "folder");
    const QCommandLineOption nameOption("name", "the avatar's name (default: the input file's)", "name");
    const QCommandLineOption previewOption("preview", "draw the rigged character into a PNG", "file");
    const QCommandLineOption animationOption("animation", "with --preview: also pose it by an animation", "fbx");
    const QCommandLineOption frameOption("frame", "with --animation: which frame (default 30)", "number");
    const QCommandLineOption heightOption("height", "scale the character to this height first (for files in the wrong units)",
                                          "metres");
    const QCommandLineOption profilesOption("profiles", "print the measurements along each arm");
    const QCommandLineOption scaleOption("scale", "scale the character by a factor first (0.3937 for inches read as cm)",
                                         "factor");
    const QCommandLineOption jointsOnlyOption("joints-only", "place the joints and skip the skin weights");
    const QCommandLineOption dumpOption("dump", "print an FBX file's node tree and stop", "file");
    const QCommandLineOption depthOption("depth", "with --dump: how deep (default 3)", "levels");
    const QCommandLineOption childrenOption("children", "with --dump: children shown per node (default 12)", "count");
    const QCommandLineOption falloffOption("falloff", "skin weights: how fast a bone's influence fades (0 to 1)", "a");
    const QCommandLineOption smoothingOption("smoothing", "skin weights: passes of smoothing over the mesh", "passes");
    parser.addOption(inputOption);
    parser.addOption(keyOption);
    parser.addOption(turnOption);
    parser.addOption(jsonOption);
    parser.addOption(outputOption);
    parser.addOption(nameOption);
    parser.addOption(previewOption);
    parser.addOption(animationOption);
    parser.addOption(frameOption);
    parser.addOption(heightOption);
    parser.addOption(profilesOption);
    parser.addOption(scaleOption);
    parser.addOption(jointsOnlyOption);
    parser.addOption(dumpOption);
    parser.addOption(depthOption);
    parser.addOption(childrenOption);
    parser.addOption(falloffOption);
    parser.addOption(smoothingOption);
    if (parser.parse(QCoreApplication::arguments()) && parser.isSet(dumpOption)) {
        QFile file(parser.value(dumpOption));
        if (!file.open(QIODevice::ReadOnly)) {
            fprintf(stderr, "could not read %s\n", qPrintable(parser.value(dumpOption)));
            _returnCode = 2;
            return;
        }
        const FBXNode root = FBXSerializer::parseFBX(&file);
        dumpNode(root, 0, parser.isSet(depthOption) ? parser.value(depthOption).toInt() : 3,
                 parser.isSet(childrenOption) ? parser.value(childrenOption).toInt() : 12);
        return;
    }
    if (!parser.parse(QCoreApplication::arguments()) || !parser.isSet(inputOption)) {
        parser.showHelp(1);
    }
    if (parser.isSet(helpOption)) {
        parser.showHelp();
        return;
    }

    const QString inputPath = parser.value(inputOption);
    HFMModel::Pointer model = readModel(inputPath);
    if (!model) {
        fprintf(stderr, "could not read %s\n", qPrintable(inputPath));
        _returnCode = 2;
        return;
    }
    autorig::Body body = autorig::gatherBody(*model);
    float scale = parser.isSet(scaleOption) ? parser.value(scaleOption).toFloat() : 1.0f;
    if (parser.isSet(heightOption) && body.maximum.y > body.minimum.y) {
        scale = parser.value(heightOption).toFloat() / (body.maximum.y - body.minimum.y);
    }
    if (scale != 1.0f) {
        body.scale(scale);
        fprintf(stdout, "scaled by %.4f\n", scale);
    }
    fprintf(stdout, "%s: %zu vertices, %zu triangles, from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f) cm\n",
            qPrintable(QFileInfo(inputPath).fileName()), body.vertices.size(), body.triangles.size(),
            body.minimum.x * 100.0f, body.minimum.y * 100.0f, body.minimum.z * 100.0f, body.maximum.x * 100.0f,
            body.maximum.y * 100.0f, body.maximum.z * 100.0f);

    const bool turned = parser.isSet(turnOption);
    if (turned) {
        body.turnAround();
    }
    autorig::Options options;
    options.profiles = parser.isSet(profilesOption);
    options.skin = !parser.isSet(jointsOnlyOption);
    if (parser.isSet(falloffOption)) {
        options.falloff = parser.value(falloffOption).toFloat();
    }
    if (parser.isSet(smoothingOption)) {
        options.smoothing = parser.value(smoothingOption).toInt();
    }
    const autorig::Result result = autorig::rig(body, options);
    for (const QString& note : result.notes) {
        fprintf(stdout, "  %s\n", qPrintable(note));
    }
    for (const QString& line : result.profiles) {
        fprintf(stdout, "    %s\n", qPrintable(line));
    }
    if (!result.ok) {
        fprintf(stderr, "not rigged: %s\n", qPrintable(result.error));
        _returnCode = 3;
        return;
    }

    HFMModel::Pointer key = parser.isSet(keyOption) ? readModel(parser.value(keyOption)) : model;
    std::map<QString, glm::vec3> answers = key ? keyJoints(*key) : std::map<QString, glm::vec3>();
    for (auto& answer : answers) {
        answer.second *= scale;
    }

    // the joints, and how far each is from the answer key's
    fprintf(stdout, "\n%-20s %26s %26s %8s\n", "joint", "placed (cm)", "answer key (cm)", "off (cm)");
    std::vector<std::pair<float, QString>> body_errors, finger_errors;
    QJsonArray jsonJoints;
    for (const autorig::RiggedJoint& joint : result.joints) {
        const glm::vec3 p = joint.position * 100.0f;
        QJsonObject entry {
            { "name", joint.name },
            { "parent", joint.parent },
            { "position", QJsonArray { p.x, p.y, p.z } },
            { "rotation", QJsonArray { joint.rotation.x, joint.rotation.y, joint.rotation.z, joint.rotation.w } },
        };
        auto found = answers.find(joint.name);
        if (found != answers.end()) {
            const glm::vec3 k = found->second * 100.0f;
            const float off = glm::distance(p, k);
            fprintf(stdout, "%-20s %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f\n", qPrintable(joint.name), p.x, p.y, p.z,
                    k.x, k.y, k.z, off);
            entry["key"] = QJsonArray { k.x, k.y, k.z };
            const bool finger = joint.name.contains("Hand") && joint.name != "LeftHand" && joint.name != "RightHand";
            (finger ? finger_errors : body_errors).push_back({ off, joint.name });
        } else {
            fprintf(stdout, "%-20s %8.1f %8.1f %8.1f %26s\n", qPrintable(joint.name), p.x, p.y, p.z, "-");
        }
        jsonJoints.append(entry);
    }
    auto summarize = [](const char* label, std::vector<std::pair<float, QString>> errors) {
        if (errors.empty()) {
            return;
        }
        float sum = 0.0f;
        for (const auto& e : errors) {
            sum += e.first;
        }
        std::sort(errors.begin(), errors.end(), [](const std::pair<float, QString>& a, const std::pair<float, QString>& b) {
            return a.first > b.first;
        });
        QStringList worst;
        for (size_t i = 0; i < std::min<size_t>(5, errors.size()); i++) {
            worst << QString("%1 %2").arg(errors[i].second).arg(errors[i].first, 0, 'f', 1);
        }
        fprintf(stdout, "%s: %zu joints against the key, mean %.1f cm off; worst %s\n", label, errors.size(),
                sum / errors.size(), qPrintable(worst.join(", ")));
    };
    fprintf(stdout, "\n");
    summarize("body", body_errors);
    summarize("fingers", finger_errors);
    if (key && options.skin && !turned) {
        const KeyRig keyRig = readKeyRig(*key, scale);
        compareWeights(result, keyRig);
        compareDeformation(body, result, keyRig);
    }

    if (parser.isSet(outputOption)) {
        if (!options.skin) {
            fprintf(stderr, "writing an avatar needs the skin weights: drop --joints-only\n");
            _returnCode = 4;
            return;
        }
        QString name = parser.isSet(nameOption) ? parser.value(nameOption) : QFileInfo(inputPath).completeBaseName();
        name.replace(QRegExp("[^A-Za-z0-9_-]+"), "_");
        const autorig::WrittenAvatar written =
            autorig::writeAvatar(*model, body, result, inputPath, parser.value(outputOption), name);
        for (const QString& note : written.notes) {
            fprintf(stdout, "%s\n", qPrintable(note));
        }
        if (!written.ok) {
            fprintf(stderr, "not written: %s\n", qPrintable(written.error));
            _returnCode = 5;
            return;
        }
        if (!checkWritten(written.fbxPath, body, result)) {
            _returnCode = 6;
        }
    }
    if (parser.isSet(previewOption) && options.skin) {
        HFMModel::Pointer animation = parser.isSet(animationOption) ? readModel(parser.value(animationOption)) : nullptr;
        const int frame = parser.isSet(frameOption) ? parser.value(frameOption).toInt() : 30;
        if (renderPreview(body, result, animation.get(), frame, parser.value(previewOption))) {
            fprintf(stdout, "preview: %s%s\n", qPrintable(parser.value(previewOption)),
                    animation ? qPrintable(QString(" (frame %1 of %2)").arg(frame).arg(animation->animationFrames.size())) : "");
        } else {
            fprintf(stdout, "preview: not drawn\n");
        }
    }
    if (parser.isSet(jsonOption)) {
        QFile out(parser.value(jsonOption));
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(QJsonObject { { "joints", jsonJoints } }).toJson());
        }
    }
}
