//
//  AvatarWriter.cpp
//  libraries/auto-rigging/src
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "AvatarWriter.h"

#include <algorithm>
#include <map>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QVector>

#include <FBX.h>
#include <FBXWriter.h>
#include <GLMHelpers.h>
#include <hfm/HFM.h>

using namespace autorig;

namespace {

const float CENTIMETRES_PER_METRE = 100.0f;
// The names of the two UV layers: the reader treats "map1" as the first set and any other name as the second.
const QByteArray FIRST_UV_SET = "map1";
const QByteArray SECOND_UV_SET = "UVSet1";

// FBX object names are "name\0\1Class".
QByteArray objectName(const QString& name, const char* kind) {
    return name.toUtf8() + QByteArray("\x00\x01", 2) + QByteArray(kind);
}

FBXNode makeNode(const QByteArray& name, const QVariantList& properties = QVariantList(),
                 const FBXNodeList& children = FBXNodeList()) {
    FBXNode node;
    node.name = name;
    node.properties = properties;
    node.children = children;
    return node;
}

// One "P" entry of a Properties70 block: name, type, label, flags, values.
FBXNode property(const char* name, const char* type, const char* label, const char* flags, const QVariantList& values) {
    QVariantList properties { QByteArray(name), QByteArray(type), QByteArray(label), QByteArray(flags) };
    properties.append(values);
    return makeNode("P", properties);
}

QVariantList vector3(const glm::vec3& v) {
    return { (double)v.x, (double)v.y, (double)v.z };
}

QVariant doubles(const QVector<double>& values) {
    return QVariant::fromValue(values);
}

QVariant ints(const QVector<qint32>& values) {
    return QVariant::fromValue(values);
}

// glm matrices are stored column by column, as FBX expects.
QVector<double> matrixValues(const glm::mat4& m) {
    QVector<double> values;
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            values.append(m[column][row]);
        }
    }
    return values;
}

FBXNode transformModel(qint64 id, const QString& name, const char* kind, const glm::vec3& translation,
                       const glm::vec3& rotationDegrees) {
    FBXNodeList properties {
        property("RotationActive", "bool", "", "", { 1 }),
        property("InheritType", "enum", "", "", { 1 }),
        property("ScalingMax", "Vector3D", "Vector", "", { 0.0, 0.0, 0.0 }),
        property("DefaultAttributeIndex", "int", "Integer", "", { 0 }),
        property("Lcl Translation", "Lcl Translation", "", "A+", vector3(translation)),
        property("Lcl Rotation", "Lcl Rotation", "", "A+", vector3(rotationDegrees)),
        property("Lcl Scaling", "Lcl Scaling", "", "A", { 1.0, 1.0, 1.0 }),
    };
    return makeNode("Model", { id, objectName(name, "Model"), QByteArray(kind) },
                    { makeNode("Version", { 232 }), makeNode("Properties70", {}, properties),
                      makeNode("Shading", { true }), makeNode("Culling", { QByteArray("CullingOff") }) });
}

FBXNode layerElement(const char* type) {
    return makeNode("LayerElement", {}, { makeNode("Type", { QByteArray(type) }), makeNode("TypedIndex", { 0 }) });
}

}

WrittenAvatar autorig::writeAvatar(const hfm::Model& model, const Body& body, const Result& rig, const QString& sourcePath,
                                   const QString& folder, const QString& name) {
    WrittenAvatar written;
    if (!rig.ok || rig.joints.empty()) {
        written.error = "the character is not rigged";
        return written;
    }
    if (rig.weights.size() != body.vertices.size()) {
        written.error = "the skin weights do not match the body";
        return written;
    }
    QDir out(folder);
    if (!out.exists() && !QDir().mkpath(folder)) {
        written.error = "cannot make the folder " + folder;
        return written;
    }
    const QDir sourceFolder = QFileInfo(sourcePath).absoluteDir();

    qint64 nextId = 1000000;
    auto newId = [&]() {
        return nextId++;
    };
    FBXNodeList objects;
    FBXNodeList connections;
    std::map<QByteArray, int> counts;  // for the Definitions block
    auto add = [&](const FBXNode& object) {
        objects << object;
        counts[object.name]++;
    };
    auto connect = [&](qint64 child, qint64 parent) {
        connections << makeNode("C", { QByteArray("OO"), child, parent });
    };
    auto connectTo = [&](qint64 child, qint64 parent, const char* slot) {
        connections << makeNode("C", { QByteArray("OP"), child, parent, QByteArray(slot) });
    };

    // the skeleton: each joint's place and axes relative to its parent, in centimetres and degrees
    const int jointCount = (int)rig.joints.size();
    std::vector<qint64> jointIds(jointCount);
    std::vector<glm::mat4> bind(jointCount);
    for (int j = 0; j < jointCount; j++) {
        bind[j] = glm::translate(glm::mat4(), rig.joints[j].position * CENTIMETRES_PER_METRE) *
                  glm::mat4_cast(rig.joints[j].rotation);
        jointIds[j] = newId();
    }
    for (int j = 0; j < jointCount; j++) {
        const RiggedJoint& joint = rig.joints[j];
        const int parent = joint.parent;
        const glm::mat4 local = parent >= 0 ? glm::inverse(bind[parent]) * bind[j] : bind[j];
        const glm::quat rotation =
            glm::normalize(parent >= 0 ? glm::inverse(rig.joints[parent].rotation) * joint.rotation : joint.rotation);
        const qint64 attribute = newId();
        add(makeNode("NodeAttribute", { attribute, objectName(joint.name, "NodeAttribute"), QByteArray("LimbNode") },
                     { makeNode("TypeFlags", { QByteArray("Skeleton") }) }));
        add(transformModel(jointIds[j], joint.name, "LimbNode", extractTranslation(local),
                           glm::degrees(safeEulerAngles(rotation))));
        connect(attribute, jointIds[j]);
        connect(jointIds[j], parent >= 0 ? jointIds[parent] : 0);
    }

    // textures: copied beside the FBX, found the way game rips need (relative, by file name, any case)
    std::map<QString, QString> placed;
    auto placeTexture = [&](const hfm::Texture& texture) -> QString {
        QString path = QString::fromUtf8(texture.filename);
        path.replace('\\', '/');
        auto done = placed.find(path);
        if (done != placed.end()) {
            return done->second;
        }
        QString fileName = QFileInfo(path).fileName();
        if (!texture.content.isEmpty()) {
            if (fileName.isEmpty()) {
                fileName = QString("texture%1.png").arg(placed.size());
            }
            QFile file(out.filePath(fileName));
            if (!file.open(QIODevice::WriteOnly) || file.write(texture.content) != texture.content.size()) {
                return QString();
            }
        } else {
            QString found;
            for (const QString& candidate : { path, sourceFolder.filePath(path), sourceFolder.filePath(fileName) }) {
                if (!candidate.isEmpty() && QFileInfo(candidate).isFile()) {
                    found = candidate;
                    break;
                }
            }
            if (found.isEmpty() && !fileName.isEmpty()) {
                for (const QString& entry : sourceFolder.entryList(QDir::Files)) {
                    if (entry.compare(fileName, Qt::CaseInsensitive) == 0) {
                        found = sourceFolder.filePath(entry);
                        break;
                    }
                }
            }
            if (found.isEmpty()) {
                written.notes << "texture not found: " + path;
                placed[path] = QString();
                return QString();
            }
            fileName = QFileInfo(found).fileName();
            const QString target = out.filePath(fileName);
            if (QFileInfo(found).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
                QFile::remove(target);
                QFile::copy(found, target);
            }
        }
        placed[path] = fileName;
        return fileName;
    };
    auto addTexture = [&](const hfm::Texture& texture, qint64 material, const char* slot) {
        if (texture.filename.isEmpty() && texture.content.isEmpty()) {
            return;
        }
        const QString fileName = placeTexture(texture);
        if (fileName.isEmpty()) {
            return;
        }
        const qint64 id = newId();
        // the reader gives a texture the mesh's UV layer of the same name (FBXSerializer::matchTextureUVSetToAttributeChannel)
        const QByteArray uvSet = texture.texcoordSet == 1 ? SECOND_UV_SET : FIRST_UV_SET;
        add(makeNode("Texture", { id, objectName(fileName, "Texture"), QByteArray("") },
                     { makeNode("Type", { QByteArray("TextureVideoClip") }), makeNode("Version", { 202 }),
                       makeNode("TextureName", { objectName(fileName, "Texture") }),
                       makeNode("Properties70", {}, { property("UVSet", "KString", "", "", { uvSet }) }),
                       makeNode("FileName", { fileName.toUtf8() }), makeNode("RelativeFilename", { fileName.toUtf8() }),
                       makeNode("ModelUVTranslation", { 0.0, 0.0 }), makeNode("ModelUVScaling", { 1.0, 1.0 }),
                       makeNode("Texture_Alpha_Source", { QByteArray("None") }),
                       makeNode("Cropping", { 0, 0, 0, 0 }) }));
        connectTo(id, material, slot);
    };

    // materials, each written once, with the textures the engine reads back
    std::map<uint32_t, qint64> materialIds;
    auto materialFor = [&](uint32_t index) -> qint64 {
        auto done = materialIds.find(index);
        if (done != materialIds.end()) {
            return done->second;
        }
        const hfm::Material fallback;
        const hfm::Material& material = index < model.materials.size() ? model.materials[index] : fallback;
        const QString materialName = material.name.isEmpty() ? QString("material%1").arg(index) : material.name;
        const qint64 id = newId();
        add(makeNode("Material", { id, objectName(materialName, "Material"), QByteArray("") },
                     { makeNode("Version", { 102 }), makeNode("ShadingModel", { QByteArray("phong") }),
                       makeNode("MultiLayer", { 0 }),
                       makeNode("Properties70", {},
                                { property("DiffuseColor", "Color", "", "A", vector3(material.diffuseColor)),
                                  property("DiffuseFactor", "Number", "", "A", { 1.0 }),
                                  property("SpecularColor", "Color", "", "A", vector3(material.specularColor)),
                                  property("ShininessExponent", "Number", "", "A", { (double)material.shininess }),
                                  property("EmissiveColor", "Color", "", "A", vector3(material.emissiveColor)),
                                  property("Opacity", "Number", "", "A", { (double)material.opacity }) }) }));
        addTexture(material.albedoTexture, id, "DiffuseColor");
        addTexture(material.normalTexture, id, "NormalMap");
        addTexture(material.opacityTexture, id, "TransparentColor");
        addTexture(material.specularTexture, id, "SpecularColor");
        addTexture(material.glossTexture, id, "ShininessExponent");
        addTexture(material.emissiveTexture, id, "EmissiveColor");
        addTexture(material.roughnessTexture, id, "tex_roughness_map");
        addTexture(material.metallicTexture, id, "tex_metallic_map");
        materialIds[index] = id;
        return id;
    };

    // the meshes, in centimetres, each with its materials and its skin
    std::vector<qint64> meshIds;
    int pieceCount = 0;
    for (const Body::Piece& piece : body.pieces) {
        const hfm::Mesh& mesh = model.meshes[piece.mesh];
        const glm::mat3 normalTransform = glm::mat3(body.adjustment * piece.transform);
        QVector<double> vertices;
        vertices.reserve((int)piece.vertexCount * 3);
        for (uint32_t v = 0; v < piece.vertexCount; v++) {
            const glm::vec3 p = body.vertices[piece.firstVertex + v] * CENTIMETRES_PER_METRE;
            vertices << p.x << p.y << p.z;
        }
        QVector<qint32> polygons;
        QVector<qint32> polygonMaterials;
        QVector<qint32> corners;  // the vertex at each polygon corner
        std::vector<uint32_t> pieceMaterials;
        for (uint32_t shapeIndex : piece.shapes) {
            const hfm::Shape& shape = model.shapes[shapeIndex];
            const hfm::MeshPart& part = mesh.parts[shape.meshPart];
            auto known = std::find(pieceMaterials.begin(), pieceMaterials.end(), shape.material);
            const int localMaterial = (int)(known - pieceMaterials.begin());
            if (known == pieceMaterials.end()) {
                pieceMaterials.push_back(shape.material);
            }
            for (const QVector<int>* indices : { &part.triangleIndices, &part.quadTrianglesIndices }) {
                for (int i = 0; i + 2 < indices->size(); i += 3) {
                    const int a = indices->at(i), b = indices->at(i + 1), c = indices->at(i + 2);
                    if (a < 0 || b < 0 || c < 0 || a >= (int)piece.vertexCount || b >= (int)piece.vertexCount ||
                        c >= (int)piece.vertexCount) {
                        continue;
                    }
                    polygons << a << b << (-c - 1);
                    corners << a << b << c;
                    polygonMaterials << localMaterial;
                }
            }
        }
        if (polygons.isEmpty()) {
            continue;
        }
        FBXNodeList geometry { makeNode("Vertices", { doubles(vertices) }), makeNode("PolygonVertexIndex", { ints(polygons) }),
                               makeNode("GeometryVersion", { 124 }) };
        FBXNodeList layer { makeNode("Version", { 100 }) };
        if (mesh.normals.size() == mesh.vertices.size()) {
            QVector<double> normals;
            normals.reserve(corners.size() * 3);
            for (qint32 corner : corners) {
                const glm::vec3 n = glm::normalize(normalTransform * mesh.normals[corner]);
                normals << n.x << n.y << n.z;
            }
            geometry << makeNode("LayerElementNormal", { 0 },
                                 { makeNode("Version", { 102 }), makeNode("Name", { QByteArray("") }),
                                   makeNode("MappingInformationType", { QByteArray("ByPolygonVertex") }),
                                   makeNode("ReferenceInformationType", { QByteArray("Direct") }),
                                   makeNode("Normals", { doubles(normals) }) });
            layer << layerElement("LayerElementNormal");
        }
        if (mesh.texCoords.size() == mesh.vertices.size()) {
            QVector<double> uvs;
            uvs.reserve(mesh.texCoords.size() * 2);
            for (const glm::vec2& uv : mesh.texCoords) {
                uvs << uv.x << uv.y;
            }
            geometry << makeNode("LayerElementUV", { 0 },
                                 { makeNode("Version", { 101 }), makeNode("Name", { FIRST_UV_SET }),
                                   makeNode("MappingInformationType", { QByteArray("ByPolygonVertex") }),
                                   makeNode("ReferenceInformationType", { QByteArray("IndexToDirect") }),
                                   makeNode("UV", { doubles(uvs) }), makeNode("UVIndex", { ints(corners) }) });
            layer << layerElement("LayerElementUV");
        }
        // a second UV layer: game rips often keep their textures on it (Chloe's all use "UVChannel_1")
        FBXNodeList secondLayer;
        if (mesh.texCoords1.size() == mesh.vertices.size()) {
            QVector<double> uvs;
            uvs.reserve(mesh.texCoords1.size() * 2);
            for (const glm::vec2& uv : mesh.texCoords1) {
                uvs << uv.x << uv.y;
            }
            geometry << makeNode("LayerElementUV", { 1 },
                                 { makeNode("Version", { 101 }), makeNode("Name", { SECOND_UV_SET }),
                                   makeNode("MappingInformationType", { QByteArray("ByPolygonVertex") }),
                                   makeNode("ReferenceInformationType", { QByteArray("IndexToDirect") }),
                                   makeNode("UV", { doubles(uvs) }), makeNode("UVIndex", { ints(corners) }) });
            secondLayer << makeNode("Version", { 100 })
                        << makeNode("LayerElement", {}, { makeNode("Type", { QByteArray("LayerElementUV") }),
                                                         makeNode("TypedIndex", { 1 }) });
        }
        geometry << makeNode("LayerElementMaterial", { 0 },
                             { makeNode("Version", { 101 }), makeNode("Name", { QByteArray("") }),
                               makeNode("MappingInformationType", { QByteArray("ByPolygon") }),
                               makeNode("ReferenceInformationType", { QByteArray("IndexToDirect") }),
                               makeNode("Materials", { ints(polygonMaterials) }) });
        layer << layerElement("LayerElementMaterial");
        geometry << makeNode("Layer", { 0 }, layer);
        if (!secondLayer.isEmpty()) {
            geometry << makeNode("Layer", { 1 }, secondLayer);
        }

        QString meshName = model.getModelNameOfMesh((int)piece.mesh);
        if (meshName.isEmpty()) {
            meshName = QString("body%1").arg(pieceCount);
        }
        const qint64 geometryId = newId();
        const qint64 meshId = newId();
        add(makeNode("Geometry", { geometryId, objectName(meshName, "Geometry"), QByteArray("Mesh") }, geometry));
        add(transformModel(meshId, meshName, "Mesh", glm::vec3(0.0f), glm::vec3(0.0f)));
        connect(geometryId, meshId);
        connect(meshId, 0);
        for (uint32_t material : pieceMaterials) {
            connect(materialFor(material), meshId);  // the polygons' material numbers count these in this order
        }
        meshIds.push_back(meshId);

        // the skin: a cluster for each joint that moves part of this mesh
        std::map<int, std::pair<QVector<qint32>, QVector<double>>> clusters;
        for (uint32_t v = 0; v < piece.vertexCount; v++) {
            const VertexWeights& weights = rig.weights[piece.firstVertex + v];
            for (int k = 0; k < 4; k++) {
                if (weights.weights[k] > 0.0f) {
                    auto& cluster = clusters[weights.joints[k]];
                    cluster.first << (qint32)v;
                    cluster.second << (double)weights.weights[k];
                }
            }
        }
        const qint64 skinId = newId();
        add(makeNode("Deformer", { skinId, objectName("Skin " + meshName, "Deformer"), QByteArray("Skin") },
                     { makeNode("Version", { 101 }), makeNode("Link_DeformAcuracy", { 50.0 }) }));
        connect(skinId, geometryId);
        for (const auto& cluster : clusters) {
            const int j = cluster.first;
            const qint64 clusterId = newId();
            add(makeNode("Deformer",
                         { clusterId, objectName("Cluster " + rig.joints[j].name, "SubDeformer"), QByteArray("Cluster") },
                         { makeNode("Version", { 100 }), makeNode("UserData", { QByteArray(""), QByteArray("") }),
                           makeNode("Indexes", { ints(cluster.second.first) }),
                           makeNode("Weights", { doubles(cluster.second.second) }),
                           makeNode("Transform", { doubles(matrixValues(glm::mat4())) }),
                           makeNode("TransformLink", { doubles(matrixValues(bind[j])) }) }));
            connect(clusterId, skinId);
            connect(jointIds[j], clusterId);
        }
        pieceCount++;
    }

    // the bind pose: every joint and mesh as placed
    FBXNodeList pose { makeNode("Type", { QByteArray("BindPose") }), makeNode("Version", { 100 }),
                       makeNode("NbPoseNodes", { jointCount + (int)meshIds.size() }) };
    for (int j = 0; j < jointCount; j++) {
        pose << makeNode("PoseNode", {}, { makeNode("Node", { jointIds[j] }), makeNode("Matrix", { doubles(matrixValues(bind[j])) }) });
    }
    for (qint64 meshId : meshIds) {
        pose << makeNode("PoseNode", {}, { makeNode("Node", { meshId }), makeNode("Matrix", { doubles(matrixValues(glm::mat4())) }) });
    }
    add(makeNode("Pose", { newId(), objectName("BindPose", "Pose"), QByteArray("BindPose") }, pose));

    FBXNodeList definitions { makeNode("Version", { 100 }), makeNode("Count", { (int)objects.size() + 1 }),
                              makeNode("ObjectType", { QByteArray("GlobalSettings") }, { makeNode("Count", { 1 }) }) };
    for (const auto& count : counts) {
        definitions << makeNode("ObjectType", { count.first }, { makeNode("Count", { count.second }) });
    }
    FBXNode root;
    root.children << makeNode("FBXHeaderExtension", {},
                              { makeNode("FBXHeaderVersion", { 1003 }), makeNode("FBXVersion", { 7400 }),
                                makeNode("Creator", { QByteArray("Darkride auto-rigger") }) });
    root.children << makeNode("GlobalSettings", {},
                              { makeNode("Version", { 1000 }),
                                makeNode("Properties70", {},
                                         { property("UpAxis", "int", "Integer", "", { 1 }),
                                           property("UpAxisSign", "int", "Integer", "", { 1 }),
                                           property("FrontAxis", "int", "Integer", "", { 2 }),
                                           property("FrontAxisSign", "int", "Integer", "", { 1 }),
                                           property("CoordAxis", "int", "Integer", "", { 0 }),
                                           property("CoordAxisSign", "int", "Integer", "", { 1 }),
                                           property("OriginalUpAxis", "int", "Integer", "", { 1 }),
                                           property("OriginalUpAxisSign", "int", "Integer", "", { 1 }),
                                           property("UnitScaleFactor", "double", "Number", "", { 1.0 }),
                                           property("OriginalUnitScaleFactor", "double", "Number", "", { 1.0 }) }) });
    root.children << makeNode("Definitions", {}, definitions);
    root.children << makeNode("Objects", {}, objects);
    root.children << makeNode("Connections", {}, connections);

    written.fbxPath = out.filePath(name + ".fbx");
    QFile fbx(written.fbxPath);
    const QByteArray data = FBXWriter::encodeFBX(root);
    if (!fbx.open(QIODevice::WriteOnly) || fbx.write(data) != data.size()) {
        written.error = "cannot write " + written.fbxPath;
        return written;
    }
    fbx.close();

    written.fstPath = out.filePath(name + ".fst");
    if (!writeFst(written.fstPath, name, name + ".fbx")) {
        written.error = "cannot write " + written.fstPath;
        return written;
    }

    written.notes << QString("wrote %1: %2 joints, %3 meshes, %4 materials, %5 textures, %6 KB")
                         .arg(QFileInfo(written.fbxPath).fileName()).arg(jointCount).arg(meshIds.size())
                         .arg(materialIds.size()).arg(counts["Texture"]).arg(data.size() / 1024);
    written.ok = true;
    return written;
}

bool autorig::writeFst(const QString& fstPath, const QString& name, const QString& modelFilename) {
    QFile fst(fstPath);
    if (!fst.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream text(&fst);
    text << "name = " << name << "\n"
         << "type = body+head\n"
         << "scale = 1\n"
         << "filename = " << modelFilename << "\n"
         << "joint = jointRoot = Hips\n"
         << "joint = jointLean = Spine\n"
         << "joint = jointNeck = Neck\n"
         << "joint = jointHead = Head\n"
         << "joint = jointEyeLeft = LeftEye\n"
         << "joint = jointEyeRight = RightEye\n"
         << "joint = jointLeftHand = LeftHand\n"
         << "joint = jointRightHand = RightHand\n";
    return text.status() == QTextStream::Ok;
}
