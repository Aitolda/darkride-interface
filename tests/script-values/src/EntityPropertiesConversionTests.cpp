//
//  EntityPropertiesConversionTests.cpp
//  tests/script-values/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EntityPropertiesConversionTests.h"

#include <QtCore/QCborValue>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtScript/QScriptEngine>
#include <QtScript/QScriptValue>
#include <QtScript/QScriptValueIterator>

#include <EntityItemProperties.h>
#include <EntityTypes.h>
#include <PlainScriptValue.h>
#include <SharedUtil.h>
#include <VariantMapToScriptValue.h>

#include "SameVariant.h"

QTEST_MAIN(EntityPropertiesConversionTests)

namespace {

// The names of an object's own properties in the order QtScript lists them
QStringList scriptNames(const QScriptValue& value) {
    QStringList names;
    QScriptValueIterator it(value);
    while (it.hasNext()) {
        it.next();
        names.append(it.name());
    }
    return names;
}

// Whether the two values have the same properties in the same order, nested values included (an array's
// elements by index; the values themselves are compared through their variants)
bool sameShape(const QScriptValue& script, const PlainScriptValue& plain, QString& where) {
    if (script.isArray()) {
        if (!plain.isArray()) {
            where = "an array on QtScript only";
            return false;
        }
        const int length = script.property("length").toInt32();
        if (plain.property("length").toInt32() != length) {
            where = QString("array length %1 vs %2").arg(length).arg(plain.property("length").toInt32());
            return false;
        }
        for (int i = 0; i < length; ++i) {
            if (!sameShape(script.property(i), plain.property(i), where)) {
                where = QString("[%1] ").arg(i) + where;
                return false;
            }
        }
        return true;
    }
    if (script.isObject()) {
        if (!plain.isObject() || plain.isArray()) {
            where = "an object on QtScript only";
            return false;
        }
        const QStringList a = scriptNames(script);
        const QStringList b = plain.propertyNames();
        if (a != b) {
            where = "order " + a.join(",") + " vs " + b.join(",");
            return false;
        }
        for (const QString& name : a) {
            if (!sameShape(script.property(name), plain.property(name), where)) {
                where = name + ": " + where;
                return false;
            }
        }
        return true;
    }
    return true;
}

// age and ageAsText come from the clock at the moment of the conversion, not from the properties
void stripClock(QVariantMap& map) {
    map.remove("age");
    map.remove("ageAsText");
}

// The properties written both ways: the same variant (type and value) and the same shape (order)
bool sameWriting(const EntityItemProperties& properties, bool skipDefaults, bool allowUnknownCreateTime, bool strictSemantics,
                 EntityPsuedoPropertyFlags flags, QString& where) {
    QScriptEngine engine;
    PlainScriptEngine plainEngine;
    const QScriptValue script = properties.copyToScriptValue(&engine, skipDefaults, allowUnknownCreateTime, strictSemantics, flags);
    const PlainScriptValue plain = properties.copyToScriptValue(&plainEngine, skipDefaults, allowUnknownCreateTime, strictSemantics, flags);
    QVariantMap a = script.toVariant().toMap();
    QVariantMap b = plain.toVariant().toMap();
    if (a.contains("age") != b.contains("age") || a.contains("ageAsText") != b.contains("ageAsText")) {
        where = "age present on one side only";
        return false;
    }
    stripClock(a);
    stripClock(b);
    if (!sameVariant(a, b, where)) {
        where = "value: " + where;
        return false;
    }
    if (!sameShape(script, plain, where)) {
        where = "shape: " + where;
        return false;
    }
    if (a.isEmpty()) {
        where = "nothing written";
        return false;
    }
    return true;
}

bool sameWritingEveryWay(const EntityItemProperties& properties, QString& where) {
    EntityPsuedoPropertyFlags pseudo;
    pseudo.set(EntityPsuedoPropertyFlag::FlagsActive);
    pseudo.set(EntityPsuedoPropertyFlag::ID);
    pseudo.set(EntityPsuedoPropertyFlag::Type);
    pseudo.set(EntityPsuedoPropertyFlag::BoundingBox);
    pseudo.set(EntityPsuedoPropertyFlag::RenderInfo);
    pseudo.set(EntityPsuedoPropertyFlag::FaceCamera);
    EntityItemProperties desired = properties;
    EntityPropertyFlags flags;
    flags << PROP_POSITION;
    flags << PROP_NAME;
    flags << PROP_COLOR;
    desired.setDesiredProperties(flags);
    struct Way {
        const char* name;
        const EntityItemProperties& properties;
        bool skipDefaults;
        bool strictSemantics;
        EntityPsuedoPropertyFlags flags;
    };
    const Way ways[] = {
        { "everything", properties, false, false, EntityPsuedoPropertyFlags() },
        { "non-defaults", properties, true, false, EntityPsuedoPropertyFlags() },
        { "strict", properties, false, true, EntityPsuedoPropertyFlags() },
        { "desired", desired, false, false, EntityPsuedoPropertyFlags() },
        { "desired, strict", desired, false, true, EntityPsuedoPropertyFlags() },
        { "pseudo-properties", properties, false, false, pseudo },
        { "desired with pseudo-properties", desired, false, false, pseudo },
    };
    for (const Way& way : ways) {
        if (!sameWriting(way.properties, way.skipDefaults, true, way.strictSemantics, way.flags, where)) {
            where = QString(way.name) + ": " + where;
            return false;
        }
    }
    return true;
}

// The same input read both ways, compared as QtScript writes the properties read
bool sameReading(const QString& json, bool honorReadOnly, QString& where) {
    QScriptEngine engine;
    const QScriptValue scriptInput = engine.evaluate("(" + json + ")");
    if (engine.hasUncaughtException()) {
        where = "the input does not evaluate: " + engine.uncaughtException().toString();
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        where = "the input is not JSON: " + error.errorString();
        return false;
    }
    const PlainScriptValue plainInput = PlainScriptValue::fromCbor(QCborValue::fromJsonValue(document.object()));

    EntityItemProperties fromScript;
    EntityItemProperties fromPlain;
    fromScript.copyFromScriptValue(scriptInput, honorReadOnly);
    fromPlain.copyFromScriptValue(plainInput, honorReadOnly);
    fromPlain.setLastEdited(fromScript.getLastEdited());  // the clock, not the conversion

    QVariantMap a = fromScript.copyToScriptValue(&engine, false, true).toVariant().toMap();
    QVariantMap b = fromPlain.copyToScriptValue(&engine, false, true).toVariant().toMap();
    stripClock(a);
    stripClock(b);
    if (!sameVariant(a, b, where)) {
        return false;
    }
    // and the plain reading agrees with itself through the plain writing
    PlainScriptEngine plainEngine;
    QVariantMap c = fromPlain.copyToScriptValue(&plainEngine, false, true).toVariant().toMap();
    stripClock(c);
    if (!sameVariant(a, c, where)) {
        where = "plain writing of the plain reading: " + where;
        return false;
    }
    return true;
}

// A map read as the callers outside scripting read one (a world file's entity, avatar-entity data, an avatar
// bookmark, an overlay's properties): through variantMapToScriptValue on QtScript and fromVariant on the plain
// path, compared as sameReading compares
bool sameVariantReading(const QVariantMap& map, bool honorReadOnly, QString& where) {
    QScriptEngine engine;
    QVariantMap input = map;
    const QScriptValue scriptInput = variantMapToScriptValue(input, engine);
    const PlainScriptValue plainInput = PlainScriptValue::fromVariant(map);

    EntityItemProperties fromScript;
    EntityItemProperties fromPlain;
    fromScript.copyFromScriptValue(scriptInput, honorReadOnly);
    fromPlain.copyFromScriptValue(plainInput, honorReadOnly);
    fromPlain.setLastEdited(fromScript.getLastEdited());

    QVariantMap a = fromScript.copyToScriptValue(&engine, false, true).toVariant().toMap();
    QVariantMap b = fromPlain.copyToScriptValue(&engine, false, true).toVariant().toMap();
    stripClock(a);
    stripClock(b);
    if (!sameVariant(a, b, where)) {
        return false;
    }
    PlainScriptEngine plainEngine;
    QVariantMap c = fromPlain.copyToScriptValue(&plainEngine, false, true).toVariant().toMap();
    stripClock(c);
    if (!sameVariant(a, c, where)) {
        where = "plain writing of the plain reading: " + where;
        return false;
    }
    return true;
}

EntityItemProperties sampleProperties(EntityTypes::EntityType type) {
    EntityItemProperties properties;
    properties.setType(type);
    properties.setCreated(usecTimestampNow() - USECS_PER_SECOND);
    properties.setPosition(glm::vec3(1.5f, -2.0f, 3.25f));
    properties.setRotation(glm::quat(0.5f, 0.5f, 0.5f, 0.5f));
    properties.setDimensions(glm::vec3(0.5f, 1.0f, 2.0f));
    properties.setColor(glm::u8vec3(255, 128, 0));
    properties.setName("sample");
    properties.setScript("https://example.invalid/script.js");
    properties.setUserData("{\"grabbable\": true}");
    properties.setParentID(QUuid("{1a2b3c4d-0000-0000-0000-000000000001}"));
    properties.setLifetime(60.0f);
    properties.setCollisionless(true);
    properties.setDynamic(true);
    properties.setModelURL("https://example.invalid/model.glb");
    properties.setImageURL("https://example.invalid/image.png");
    properties.setShapeType(SHAPE_TYPE_BOX);
    properties.setBillboardMode(BillboardMode::YAW);
    properties.setActionData(QByteArray("action bytes"));
    properties.setLimitedRun(7);
    properties.getKeyLight().setColor(glm::u8vec3(10, 20, 30));
    properties.getKeyLight().setIntensity(0.75f);
    properties.getGrab().setGrabbable(false);
    properties.getAnimation().setURL("https://example.invalid/animation.fbx");
    properties.getAnimation().setFPS(24.0f);
    properties.getPulse().setMin(0.25f);
    return properties;
}

}  // namespace

void EntityPropertiesConversionTests::testEveryTypeWritten() {
    for (int type = EntityTypes::Box; type < EntityTypes::NUM_TYPES; ++type) {
        if (type == EntityTypes::RemovedPolyVox) {
            continue;
        }
        EntityItemProperties properties;
        properties.setType(static_cast<EntityTypes::EntityType>(type));
        properties.setCreated(usecTimestampNow());
        QString where;
        QVERIFY2(sameWritingEveryWay(properties, where),
                 qPrintable(EntityTypes::getEntityTypeName(static_cast<EntityTypes::EntityType>(type)) + ": " + where));
        // with no creation time, nothing is written unless allowed
        EntityItemProperties fresh;
        fresh.setType(static_cast<EntityTypes::EntityType>(type));
        QScriptEngine engine;
        PlainScriptEngine plainEngine;
        QVERIFY(fresh.copyToScriptValue(&engine, false).toVariant().toMap().isEmpty());
        QVERIFY(fresh.copyToScriptValue(&plainEngine, false).toVariant().toMap().isEmpty());
    }
}

void EntityPropertiesConversionTests::testValuesWritten() {
    for (EntityTypes::EntityType type : { EntityTypes::Model, EntityTypes::Image, EntityTypes::Zone, EntityTypes::Grid, EntityTypes::Text,
                                          EntityTypes::Web, EntityTypes::ParticleEffect, EntityTypes::PolyLine, EntityTypes::Light,
                                          EntityTypes::Material, EntityTypes::Shape, EntityTypes::Gizmo, EntityTypes::Terrain,
                                          EntityTypes::Track }) {
        QString where;
        QVERIFY2(sameWritingEveryWay(sampleProperties(type), where), qPrintable(EntityTypes::getEntityTypeName(type) + ": " + where));
    }
    // what a module sees: the keys in QtScript's order, not sorted
    PlainScriptEngine plainEngine;
    const QStringList names = sampleProperties(EntityTypes::Model).copyToScriptValue(&plainEngine, false).propertyNames();
    QVERIFY(names.indexOf("id") < names.indexOf("type") && names.indexOf("type") < names.indexOf("name"));
    QVERIFY(names.indexOf("name") < names.indexOf("position") && names.indexOf("position") < names.indexOf("modelURL"));
    QStringList sorted = names;
    sorted.sort();
    QVERIFY(names != sorted);
}

void EntityPropertiesConversionTests::testReadingEdgeCases() {
    const QStringList inputs {
        // vectors: partial, a number, a colour name, an array, aliases
        "{\"type\": \"Box\", \"position\": {\"x\": 1}}",
        "{\"type\": \"Box\", \"position\": 5}",
        "{\"type\": \"Box\", \"dimensions\": [1, 2, 3]}",
        "{\"type\": \"Box\", \"dimensions\": [1, 2]}",
        "{\"type\": \"Box\", \"color\": \"red\"}",
        "{\"type\": \"Box\", \"color\": {\"r\": 1, \"g\": 2, \"b\": 3}}",
        "{\"type\": \"Box\", \"color\": [10, 20, 30]}",
        "{\"type\": \"Box\", \"color\": 7}",
        "{\"type\": \"Zone\", \"keyLight\": {\"color\": \"blue\", \"direction\": {\"x\": \"NaN\", \"y\": 1, \"z\": 0}}}",
        // quaternions: whole, partial, with a NaN
        "{\"type\": \"Box\", \"rotation\": {\"x\": 0, \"y\": 0.7071, \"z\": 0, \"w\": 0.7071}}",
        "{\"type\": \"Box\", \"rotation\": {\"x\": 1, \"y\": 1}}",
        "{\"type\": \"Box\", \"rotation\": {\"x\": \"NaN\", \"y\": 0, \"z\": 0, \"w\": 1}}",
        // the legacy names
        "{\"type\": \"Model\", \"animationURL\": \"https://example.invalid/a.fbx\", \"animationFPS\": 12}",
        "{\"type\": \"Box\", \"ignoreForCollisions\": true, \"collisionsWillMove\": true}",
        "{\"type\": \"Image\", \"textures\": \"{\\\"tex.picture\\\": \\\"https://example.invalid/p.png\\\"}\"}",
        "{\"type\": \"Image\", \"textures\": \"{\\\"tex.picture\\\": \\\"https://example.invalid/p.png\\\"}\", \"imageURL\": \"https://example.invalid/q.png\"}",
        "{\"type\": \"Text\", \"faceCamera\": true}",
        "{\"type\": \"Text\", \"isFacingAvatar\": true}",
        "{\"type\": \"Text\", \"faceCamera\": true, \"isFacingAvatar\": true}",
        "{\"type\": \"Text\", \"faceCamera\": true, \"billboardMode\": \"full\"}",
        "{\"type\": \"PolyLine\", \"faceCamera\": true}",
        // enumerations by name, good and bad
        "{\"type\": \"Shape\", \"shapeType\": \"sphere\", \"shape\": \"Cylinder\", \"billboardMode\": \"yaw\"}",
        "{\"type\": \"Shape\", \"shapeType\": \"nonsense\", \"billboardMode\": 3}",
        "{\"type\": \"Zone\", \"keyLightMode\": \"enabled\", \"shapeType\": \"box\"}",
        // numbers as text, out of range, and the unsigned readers
        "{\"type\": \"Box\", \"parentJointIndex\": \"7\", \"limitedRun\": \"4294967295\", \"editionNumber\": \"12.5\"}",
        "{\"type\": \"Box\", \"parentJointIndex\": 70000, \"limitedRun\": -1, \"lifetime\": \"abc\", \"alpha\": \"0.5\"}",
        "{\"type\": \"Box\", \"created\": 1700000000000000, \"scriptTimestamp\": \"42\"}",
        // bytes as base64, an id with and without braces
        "{\"type\": \"Box\", \"actionData\": \"YWN0aW9u\", \"parentID\": \"{1a2b3c4d-0000-0000-0000-000000000001}\"}",
        "{\"type\": \"Box\", \"parentID\": \"1a2b3c4d-0000-0000-0000-000000000002\", \"owningAvatarID\": \"not an id\"}",
        // nulls, undefined through a missing value, booleans as other things
        "{\"type\": \"Box\", \"name\": null, \"position\": null, \"visible\": null, \"color\": null, \"userData\": null}",
        "{\"type\": \"Box\", \"visible\": \"false\", \"locked\": 0, \"dynamic\": \"yes\", \"collisionless\": 1}",
        // groups: a group as a whole, a group's value of the wrong kind
        "{\"type\": \"Zone\", \"haze\": {\"hazeRange\": 100, \"hazeColor\": {\"red\": 1, \"green\": 2, \"blue\": 3}}, \"bloom\": 5}",
        "{\"type\": \"Box\", \"grab\": {\"grabbable\": false, \"equippableLeftPosition\": {\"x\": 1, \"y\": 2, \"z\": 3}}, \"pulse\": {\"min\": 0.1, \"colorMode\": \"in\"}}",
        // the type missing, unknown, or changed
        "{\"position\": {\"x\": 1, \"y\": 2, \"z\": 3}}",
        "{\"type\": \"Nonsense\", \"name\": \"x\"}",
        "{\"type\": \"Model\", \"modelURL\": \"https://example.invalid/m.glb\", \"jointRotations\": [{\"x\": 0, \"y\": 0, \"z\": 0, \"w\": 1}, {}], \"jointRotationsSet\": [true, false]}",
        // lists of numbers and strings
        "{\"type\": \"PolyLine\", \"linePoints\": [{\"x\": 0, \"y\": 0, \"z\": 0}, {\"x\": 1, \"y\": 1, \"z\": 1}], \"strokeWidths\": [0.1, \"0.2\", null], \"strokeColors\": [{\"x\": 1, \"y\": 0, \"z\": 0}]}",
        "{\"type\": \"ParticleEffect\", \"emitRate\": 10, \"emitOrientation\": {\"x\": 0, \"y\": 0, \"z\": 0, \"w\": 1}, \"particleRadius\": \"0.5\"}",
        "{}"
    };
    for (const QString& input : inputs) {
        QString where;
        QVERIFY2(sameReading(input, true, where), qPrintable(input + ": " + where));
        QVERIFY2(sameReading(input, false, where), qPrintable(input + " (read-only ignored): " + where));
    }
}

void EntityPropertiesConversionTests::testReadingWorldEntities() {
    const QString path = QFileInfo(__FILE__).dir().filePath("../../../tests-manual/wasm-scripting/world.json");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable("no test world at " + path));
    const QJsonArray entities = QJsonDocument::fromJson(file.readAll()).object().value("Entities").toArray();
    QVERIFY(!entities.isEmpty());
    for (const QJsonValue& entity : entities) {
        const QString json = QString::fromUtf8(QJsonDocument(entity.toObject()).toJson(QJsonDocument::Compact));
        QString where;
        QVERIFY2(sameReading(json, true, where), qPrintable(json.left(80) + ": " + where));
    }
}

void EntityPropertiesConversionTests::testReadingVariantMaps() {
    // what an avatar bookmark stores and reads back, and what the overlay conversion starts from: a full
    // property map as toVariant() gives it, for every entity type
    for (int type = EntityTypes::Box; type < EntityTypes::NUM_TYPES; ++type) {
        QScriptEngine engine;
        const QVariantMap written = sampleProperties((EntityTypes::EntityType)type).copyToScriptValue(&engine, false).toVariant().toMap();
        const QString name = EntityTypes::getEntityTypeName((EntityTypes::EntityType)type);
        QString where;
        QVERIFY2(sameVariantReading(written, false, where), qPrintable(name + ": " + where));
        QVERIFY2(sameVariantReading(written, true, where), qPrintable(name + " (read-only honoured): " + where));
    }

    // an overlay's properties as the Overlays API gets them from a script: ints and doubles, maps for the vectors
    // and the colour, lists, a null, text
    QVariantMap overlay;
    overlay["type"] = "Shape";
    overlay["shape"] = "Cube";
    overlay["name"] = "an overlay";
    overlay["position"] = QVariantMap({ { "x", 1 }, { "y", 2.5 }, { "z", -3 } });
    overlay["dimensions"] = QVariantMap({ { "x", 1 }, { "y", 1 }, { "z", 0.25 } });
    overlay["rotation"] = QVariantMap({ { "x", 0 }, { "y", 0.7071 }, { "z", 0 }, { "w", 0.7071 } });
    overlay["color"] = QVariantMap({ { "red", 255 }, { "green", 128 }, { "blue", 0 } });
    overlay["alpha"] = 0.5;
    overlay["visible"] = true;
    overlay["lifetime"] = -1;
    overlay["parentID"] = "{1a2b3c4d-0000-0000-0000-000000000001}";
    overlay["parentJointIndex"] = 65535;
    overlay["renderLayer"] = "hud";
    overlay["primitiveMode"] = "lines";
    overlay["ignorePickIntersection"] = false;
    overlay["linePoints"] = QVariantList({ QVariantMap({ { "x", 0 }, { "y", 0 }, { "z", 0 } }), QVariantMap({ { "x", 1 }, { "y", 1 }, { "z", 1 } }) });
    overlay["strokeWidths"] = QVariantList({ 0.1, 0.2 });
    overlay["textures"] = "{\"tex\": \"https://example.invalid/t.png\"}";
    overlay["userData"] = QVariant();
    overlay["url"] = QUrl("https://example.invalid/model.glb");
    QString where;
    QVERIFY2(sameVariantReading(overlay, true, where), qPrintable("overlay: " + where));
    overlay["type"] = "PolyLine";
    QVERIFY2(sameVariantReading(overlay, true, where), qPrintable("polyline overlay: " + where));

    // the world file's entities as EntityTree::readFromMap gets them: QJsonDocument's variants, every number a double
    const QString path = QFileInfo(__FILE__).dir().filePath("../../../tests-manual/wasm-scripting/world.json");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable("no test world at " + path));
    const QVariantList entities = QJsonDocument::fromJson(file.readAll()).toVariant().toMap().value("Entities").toList();
    QVERIFY(!entities.isEmpty());
    for (const QVariant& entity : entities) {
        const QVariantMap map = entity.toMap();
        QVERIFY2(sameVariantReading(map, true, where), qPrintable(map.value("id").toString() + ": " + where));
        QVERIFY2(sameVariantReading(map, false, where), qPrintable(map.value("id").toString() + " (read-only ignored): " + where));
    }
}

void EntityPropertiesConversionTests::testRoundTrip() {
    // what QtScript writes for an entity with values, read back both ways, as a script's full property map is
    for (EntityTypes::EntityType type : { EntityTypes::Model, EntityTypes::Image, EntityTypes::Zone, EntityTypes::Text, EntityTypes::Shape }) {
        QScriptEngine engine;
        const QScriptValue written = sampleProperties(type).copyToScriptValue(&engine, false);
        const QString json = QString::fromUtf8(QJsonDocument::fromVariant(written.toVariant()).toJson(QJsonDocument::Compact));
        QString where;
        QVERIFY2(sameReading(json, true, where), qPrintable(EntityTypes::getEntityTypeName(type) + ": " + where));
        QVERIFY2(sameReading(json, false, where), qPrintable(EntityTypes::getEntityTypeName(type) + " (read-only ignored): " + where));
    }
    // what a module writes, read the plain way, is what QtScript reads from the same object
    PlainScriptEngine plainEngine;
    const PlainScriptValue plain = sampleProperties(EntityTypes::Model).copyToScriptValue(&plainEngine, false);
    const PlainScriptValue back = PlainScriptValue::fromCbor(plain.toCbor());
    EntityItemProperties fromPlain;
    fromPlain.copyFromScriptValue(back, true);
    QScriptEngine engine;
    EntityItemProperties fromScript;
    fromScript.copyFromScriptValue(engine.toScriptValue(plain.toVariant()), true);
    fromPlain.setLastEdited(fromScript.getLastEdited());
    QVariantMap a = fromScript.copyToScriptValue(&engine, false, true).toVariant().toMap();
    QVariantMap b = fromPlain.copyToScriptValue(&engine, false, true).toVariant().toMap();
    stripClock(a);
    stripClock(b);
    QString where;
    QVERIFY2(sameVariant(a, b, where), qPrintable(where));
    QVERIFY(a.value("name").toString() == "sample" && a.value("modelURL").toString() == "https://example.invalid/model.glb");
}
