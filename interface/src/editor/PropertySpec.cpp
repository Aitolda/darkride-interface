//
//  PropertySpec.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "PropertySpec.h"

#include <cmath>
#include <functional>

#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRect>
#include <QtGui/QColor>
#include <QtGui/QVector2D>
#include <QtGui/QVector3D>

#include <GLMHelpers.h>
#include <PhysicsCollisionGroups.h>

namespace {
    using EP = EntityItemProperties;

    struct Accessor {
        std::function<QVariant(const EP&)> get;
        std::function<void(EP&, const QVariant&)> set;
    };

    // --- conversions ----------------------------------------------------------------------------

    QColor toColor(const glm::u8vec3& c) { return QColor(c.r, c.g, c.b); }
    glm::u8vec3 fromColor(const QColor& c) { return glm::u8vec3(c.red(), c.green(), c.blue()); }
    QColor toColor(const glm::vec3& c) {
        return QColor(glm::clamp((int)std::lround(c.r), 0, 255), glm::clamp((int)std::lround(c.g), 0, 255),
                      glm::clamp((int)std::lround(c.b), 0, 255));
    }
    glm::vec3 fromFloatColor(const QColor& c) { return glm::vec3(c.red(), c.green(), c.blue()); }
    QVector3D toQ(const glm::vec3& v) { return QVector3D(v.x, v.y, v.z); }
    glm::vec3 fromQ(const QVector3D& v) { return glm::vec3(v.x(), v.y(), v.z()); }
    QVector2D toQ(const glm::vec2& v) { return QVector2D(v.x, v.y); }
    glm::vec2 fromQ(const QVector2D& v) { return glm::vec2(v.x(), v.y()); }
    QVector3D toEuler(const glm::quat& q) { return toQ(glm::degrees(safeEulerAngles(q))); }
    glm::quat fromEuler(const QVector3D& e) { return glm::quat(glm::radians(fromQ(e))); }

    // the key light's direction, as the old panel showed it: elevation and azimuth (Vec3.toPolar)
    QVector2D toPolar(const glm::vec3& v) {
        float radius = glm::length(v);
        if (radius < 1.0e-6f) {
            return QVector2D(0.0f, 0.0f);
        }
        glm::vec3 u = v / radius;
        return QVector2D(glm::degrees(std::asin(glm::clamp(-u.y, -1.0f, 1.0f))), glm::degrees(std::atan2(v.x, v.z)));
    }
    glm::vec3 fromPolar(const QVector2D& p) {
        float elevation = glm::radians(p.x());
        float azimuth = glm::radians(p.y());
        return glm::vec3(std::cos(elevation) * std::sin(azimuth), -std::sin(elevation), std::cos(elevation) * std::cos(azimuth));
    }

    // the collision mask as the comma list the script API uses: "static,dynamic,kinematic,myAvatar,otherAvatar"
    const QVector<QPair<QString, uint16_t>> COLLISION_GROUPS = {
        { "static", USER_COLLISION_GROUP_STATIC },
        { "dynamic", USER_COLLISION_GROUP_DYNAMIC },
        { "kinematic", USER_COLLISION_GROUP_KINEMATIC },
        { "myAvatar", USER_COLLISION_GROUP_MY_AVATAR },
        { "otherAvatar", USER_COLLISION_GROUP_OTHER_AVATAR }
    };
    QString maskToGroups(uint16_t mask) {
        QStringList groups;
        for (const auto& group : COLLISION_GROUPS) {
            if (mask & group.second) {
                groups << group.first;
            }
        }
        return groups.join(",");
    }
    uint16_t groupsToMask(const QString& text) {
        uint16_t mask = 0;
        QStringList groups = text.split(',', QString::SkipEmptyParts);
        for (const auto& group : COLLISION_GROUPS) {
            if (groups.contains(group.first)) {
                mask |= group.second;
            }
        }
        return mask;
    }

    // --- the accessors --------------------------------------------------------------------------

    QHash<QString, Accessor> buildAccessors() {
        QHash<QString, Accessor> a;
        auto add = [&](const char* key, std::function<QVariant(const EP&)> get, std::function<void(EP&, const QVariant&)> set) {
            a.insert(key, Accessor { get, set });
        };

#define P_BOOL(key, N)  add(key, [](const EP& p) { return QVariant(p.get##N()); }, [](EP& p, const QVariant& v) { p.set##N(v.toBool()); })
#define P_FLOAT(key, N) add(key, [](const EP& p) { return QVariant((double)p.get##N()); }, [](EP& p, const QVariant& v) { p.set##N((float)v.toDouble()); })
#define P_DEG(key, N)   add(key, [](const EP& p) { return QVariant((double)glm::degrees(p.get##N())); }, [](EP& p, const QVariant& v) { p.set##N(glm::radians((float)v.toDouble())); })
#define P_INT(key, N, T) add(key, [](const EP& p) { return QVariant((int)p.get##N()); }, [](EP& p, const QVariant& v) { p.set##N((T)v.toInt()); })
#define P_STR(key, N)   add(key, [](const EP& p) { return QVariant(p.get##N()); }, [](EP& p, const QVariant& v) { p.set##N(v.toString()); })
#define P_UUID(key, N)  add(key, [](const EP& p) { return QVariant(p.get##N().toString(QUuid::WithoutBraces)); }, [](EP& p, const QVariant& v) { p.set##N(QUuid(v.toString())); })
#define P_COLOR(key, N) add(key, [](const EP& p) { return QVariant(toColor(p.get##N())); }, [](EP& p, const QVariant& v) { p.set##N(fromColor(v.value<QColor>())); })
#define P_FCOLOR(key, N) add(key, [](const EP& p) { return QVariant(toColor(p.get##N())); }, [](EP& p, const QVariant& v) { p.set##N(fromFloatColor(v.value<QColor>())); })
#define P_VEC2(key, N)  add(key, [](const EP& p) { return QVariant(toQ(p.get##N())); }, [](EP& p, const QVariant& v) { p.set##N(fromQ(v.value<QVector2D>())); })
#define P_VEC3(key, N)  add(key, [](const EP& p) { return QVariant(toQ(p.get##N())); }, [](EP& p, const QVariant& v) { p.set##N(fromQ(v.value<QVector3D>())); })
#define P_EULER(key, N) add(key, [](const EP& p) { return QVariant(toEuler(p.get##N())); }, [](EP& p, const QVariant& v) { p.set##N(fromEuler(v.value<QVector3D>())); })
#define P_RECT(key, N)  add(key, [](const EP& p) { return QVariant(p.get##N()); }, [](EP& p, const QVariant& v) { p.set##N(v.toRect()); })
#define P_ENUM(key, N)  add(key, [](const EP& p) { return QVariant(p.get##N##AsString()); }, [](EP& p, const QVariant& v) { p.set##N##FromString(v.toString()); })
// members of a property group: G is the group's getter
#define G_BOOL(key, G, N)  add(key, [](const EP& p) { return QVariant(p.G().get##N()); }, [](EP& p, const QVariant& v) { p.G().set##N(v.toBool()); })
#define G_FLOAT(key, G, N) add(key, [](const EP& p) { return QVariant((double)p.G().get##N()); }, [](EP& p, const QVariant& v) { p.G().set##N((float)v.toDouble()); })
#define G_STR(key, G, N)   add(key, [](const EP& p) { return QVariant(p.G().get##N()); }, [](EP& p, const QVariant& v) { p.G().set##N(v.toString()); })
#define G_COLOR(key, G, N) add(key, [](const EP& p) { return QVariant(toColor(p.G().get##N())); }, [](EP& p, const QVariant& v) { p.G().set##N(fromColor(v.value<QColor>())); })

        // base
        P_STR("name", Name);
        P_STR("description", Description);
        P_UUID("parentID", ParentID);
        P_INT("parentJointIndex", ParentJointIndex, quint16);
        P_BOOL("locked", Locked);
        P_BOOL("visible", Visible);
        P_ENUM("entityPriority", EntityPriority);
        P_ENUM("renderLayer", RenderLayer);
        P_ENUM("primitiveMode", PrimitiveMode);

        // spatial
        P_VEC3("position", Position);
        P_EULER("rotation", Rotation);
        P_VEC3("dimensions", Dimensions);
        // Scale is dimensions measured in the entity's own size; it is never set directly: the
        // editor turns an edit of it into a dimensions edit, as only it knows each entity's size
        add("scale", [](const EP& p) {
                glm::vec3 natural = PropertySpecs::naturalSize(p);
                if (natural == glm::vec3(0.0f)) {
                    return QVariant();   // a model that has not loaded yet
                }
                return QVariant(toQ(p.getDimensions() / natural));
            }, [](EP&, const QVariant&) {});
        add("type", [](const EP& p) { return QVariant(EntityTypes::getEntityTypeName(p.getType())); },
            [](EP&, const QVariant&) {});
        P_VEC3("localPosition", LocalPosition);
        P_EULER("localRotation", LocalRotation);
        P_VEC3("localDimensions", LocalDimensions);
        P_VEC3("registrationPoint", RegistrationPoint);

        // shape
        P_STR("shape", Shape);
        P_COLOR("color", Color);
        P_FLOAT("alpha", Alpha);

        // text
        P_STR("text", Text);
        P_COLOR("textColor", TextColor);
        P_FLOAT("textAlpha", TextAlpha);
        P_COLOR("backgroundColor", BackgroundColor);
        P_FLOAT("backgroundAlpha", BackgroundAlpha);
        P_FLOAT("lineHeight", LineHeight);
        P_STR("font", Font);
        P_ENUM("textEffect", TextEffect);
        P_COLOR("textEffectColor", TextEffectColor);
        P_FLOAT("textEffectThickness", TextEffectThickness);
        P_ENUM("billboardMode", BillboardMode);
        P_FLOAT("topMargin", TopMargin);
        P_FLOAT("rightMargin", RightMargin);
        P_FLOAT("bottomMargin", BottomMargin);
        P_FLOAT("leftMargin", LeftMargin);
        P_BOOL("unlit", Unlit);

        // zone
        P_ENUM("shapeType", ShapeType);
        P_STR("compoundShapeURL", CompoundShapeURL);
        P_BOOL("flyingAllowed", FlyingAllowed);
        P_BOOL("ghostingAllowed", GhostingAllowed);
        P_STR("filterURL", FilterURL);
        P_ENUM("keyLightMode", KeyLightMode);
        G_COLOR("keyLight.color", getKeyLight, Color);
        G_FLOAT("keyLight.intensity", getKeyLight, Intensity);
        add("keyLight.direction", [](const EP& p) { return QVariant(toPolar(p.getKeyLight().getDirection())); },
            [](EP& p, const QVariant& v) { p.getKeyLight().setDirection(fromPolar(v.value<QVector2D>())); });
        G_BOOL("keyLight.castShadows", getKeyLight, CastShadows);
        G_FLOAT("keyLight.shadowBias", getKeyLight, ShadowBias);
        G_FLOAT("keyLight.shadowMaxDistance", getKeyLight, ShadowMaxDistance);
        P_ENUM("skyboxMode", SkyboxMode);
        G_COLOR("skybox.color", getSkybox, Color);
        G_STR("skybox.url", getSkybox, URL);
        P_ENUM("ambientLightMode", AmbientLightMode);
        G_FLOAT("ambientLight.ambientIntensity", getAmbientLight, AmbientIntensity);
        G_STR("ambientLight.ambientURL", getAmbientLight, AmbientURL);
        P_ENUM("hazeMode", HazeMode);
        G_FLOAT("haze.hazeRange", getHaze, HazeRange);
        G_BOOL("haze.hazeAltitudeEffect", getHaze, HazeAltitudeEffect);
        G_FLOAT("haze.hazeBaseRef", getHaze, HazeBaseRef);
        G_FLOAT("haze.hazeCeiling", getHaze, HazeCeiling);
        G_COLOR("haze.hazeColor", getHaze, HazeColor);
        G_FLOAT("haze.hazeBackgroundBlend", getHaze, HazeBackgroundBlend);
        G_BOOL("haze.hazeEnableGlare", getHaze, HazeEnableGlare);
        G_COLOR("haze.hazeGlareColor", getHaze, HazeGlareColor);
        G_FLOAT("haze.hazeGlareAngle", getHaze, HazeGlareAngle);
        P_ENUM("bloomMode", BloomMode);
        G_FLOAT("bloom.bloomIntensity", getBloom, BloomIntensity);
        G_FLOAT("bloom.bloomThreshold", getBloom, BloomThreshold);
        G_FLOAT("bloom.bloomSize", getBloom, BloomSize);
        P_ENUM("zoneCullingMode", ZoneCullingMode);
        P_ENUM("toneMappingMode", ToneMappingMode);
        G_FLOAT("toneMapping.exposure", getToneMapping, Exposure);
        P_ENUM("avatarPriority", AvatarPriority);

        // model
        P_STR("modelURL", ModelURL);
        G_STR("animation.url", getAnimation, URL);
        G_BOOL("animation.running", getAnimation, Running);
        G_BOOL("animation.loop", getAnimation, Loop);
        G_BOOL("animation.allowTranslation", getAnimation, AllowTranslation);
        G_BOOL("animation.hold", getAnimation, Hold);
        G_FLOAT("animation.currentFrame", getAnimation, CurrentFrame);
        G_FLOAT("animation.firstFrame", getAnimation, FirstFrame);
        G_FLOAT("animation.lastFrame", getAnimation, LastFrame);
        G_FLOAT("animation.fps", getAnimation, FPS);
        P_STR("textures", Textures);
        P_BOOL("groupCulled", GroupCulled);

        // image
        P_STR("imageURL", ImageURL);
        P_BOOL("emissive", Emissive);
        P_RECT("subImage", SubImage);
        P_BOOL("keepAspectRatio", KeepAspectRatio);

        // web
        P_STR("sourceUrl", SourceUrl);
        P_INT("dpi", DPI, uint16_t);
        P_INT("maxFPS", MaxFPS, uint8_t);
        P_ENUM("inputMode", InputMode);
        P_BOOL("showKeyboardFocusHighlight", ShowKeyboardFocusHighlight);
        P_BOOL("transparentBackground", TransparentBackground);
        P_STR("scriptURL", ScriptURL);

        // light
        P_FLOAT("intensity", Intensity);
        P_FLOAT("falloffRadius", FalloffRadius);
        P_BOOL("isSpotlight", IsSpotlight);
        // a light's Type, as Unity has it, and its Range: how far it reaches, half its largest dimension
        // (setting all three is right for a spot light too: the engine shapes it from its length)
        add("lightType", [](const EP& p) { return QVariant(p.getIsSpotlight() ? "spot" : "point"); },
            [](EP& p, const QVariant& v) { p.setIsSpotlight(v.toString() == "spot"); });
        add("range", [](const EP& p) { return QVariant((double)glm::compMax(p.getDimensions()) * 0.5); },
            [](EP& p, const QVariant& v) { p.setDimensions(glm::vec3((float)v.toDouble() * 2.0f)); });
        P_FLOAT("exponent", Exponent);
        P_FLOAT("cutoff", Cutoff);

        // material
        P_STR("materialURL", MaterialURL);
        P_STR("materialData", MaterialData);
        P_STR("parentMaterialName", ParentMaterialName);
        P_INT("priority", Priority, quint16);
        P_ENUM("materialMappingMode", MaterialMappingMode);
        P_VEC2("materialMappingPos", MaterialMappingPos);
        P_VEC2("materialMappingScale", MaterialMappingScale);
        P_FLOAT("materialMappingRot", MaterialMappingRot);
        P_BOOL("materialRepeat", MaterialRepeat);

        // grid
        P_BOOL("followCamera", FollowCamera);
        P_INT("majorGridEvery", MajorGridEvery, uint32_t);
        P_FLOAT("minorGridEvery", MinorGridEvery);

        // particles
        P_BOOL("isEmitting", IsEmitting);
        P_FLOAT("lifespan", Lifespan);
        P_INT("maxParticles", MaxParticles, quint32);
        P_FLOAT("emitRate", EmitRate);
        P_FLOAT("emitSpeed", EmitSpeed);
        P_FLOAT("speedSpread", SpeedSpread);
        P_VEC3("emitDimensions", EmitDimensions);
        P_FLOAT("emitRadiusStart", EmitRadiusStart);
        P_EULER("emitOrientation", EmitOrientation);
        P_BOOL("emitterShouldTrail", EmitterShouldTrail);
        P_FLOAT("radiusStart", RadiusStart);
        P_FLOAT("particleRadius", ParticleRadius);
        P_FLOAT("radiusFinish", RadiusFinish);
        P_FLOAT("radiusSpread", RadiusSpread);
        P_FCOLOR("colorStart", ColorStart);
        P_FCOLOR("colorFinish", ColorFinish);
        P_COLOR("colorSpread", ColorSpread);
        P_FLOAT("alphaStart", AlphaStart);
        P_FLOAT("alphaFinish", AlphaFinish);
        P_FLOAT("alphaSpread", AlphaSpread);
        P_VEC3("emitAcceleration", EmitAcceleration);
        P_VEC3("accelerationSpread", AccelerationSpread);
        P_DEG("spinStart", SpinStart);
        P_DEG("particleSpin", ParticleSpin);
        P_DEG("spinFinish", SpinFinish);
        P_DEG("spinSpread", SpinSpread);
        P_BOOL("rotateWithEntity", RotateWithEntity);
        P_DEG("polarStart", PolarStart);
        P_DEG("polarFinish", PolarFinish);
        P_DEG("azimuthStart", AzimuthStart);
        P_DEG("azimuthFinish", AzimuthFinish);

        // behavior
        G_BOOL("grab.grabbable", getGrab, Grabbable);
        G_BOOL("grab.triggerable", getGrab, Triggerable);
        G_BOOL("grab.grabFollowsController", getGrab, GrabFollowsController);
        P_BOOL("cloneable", Cloneable);
        P_FLOAT("cloneLifetime", CloneLifetime);
        P_FLOAT("cloneLimit", CloneLimit);
        P_BOOL("cloneDynamic", CloneDynamic);
        P_BOOL("cloneAvatarEntity", CloneAvatarEntity);
        P_BOOL("cloneGrabbable", CloneGrabbable);
        P_BOOL("canCastShadow", CanCastShadow);
        P_STR("href", Href);
        P_BOOL("ignorePickIntersection", IgnorePickIntersection);
        P_STR("script", Script);
        P_STR("serverScripts", ServerScripts);
        P_FLOAT("lifetime", Lifetime);
        P_STR("userData", UserData);

        // collision and physics
        P_BOOL("collisionless", Collisionless);
        add("collisionMask", [](const EP& p) { return QVariant(maskToGroups(p.getCollisionMask())); },
            [](EP& p, const QVariant& v) { p.setCollisionMask(groupsToMask(v.toString())); });
        P_STR("collisionSoundURL", CollisionSoundURL);
        P_BOOL("dynamic", Dynamic);
        P_VEC3("localVelocity", LocalVelocity);
        P_FLOAT("damping", Damping);
        add("localAngularVelocity", [](const EP& p) { return QVariant(toQ(glm::degrees(p.getLocalAngularVelocity()))); },
            [](EP& p, const QVariant& v) { p.setLocalAngularVelocity(glm::radians(fromQ(v.value<QVector3D>()))); });
        P_FLOAT("angularDamping", AngularDamping);
        P_FLOAT("restitution", Restitution);
        P_FLOAT("friction", Friction);
        P_FLOAT("density", Density);
        P_VEC3("gravity", Gravity);
        P_VEC3("acceleration", Acceleration);

#undef P_BOOL
#undef P_FLOAT
#undef P_DEG
#undef P_INT
#undef P_STR
#undef P_UUID
#undef P_COLOR
#undef P_FCOLOR
#undef P_VEC2
#undef P_VEC3
#undef P_EULER
#undef P_RECT
#undef P_ENUM
#undef G_BOOL
#undef G_FLOAT
#undef G_STR
#undef G_COLOR

        // entries of userData shown as fields of their own (see PropertySpecs::isUserDataEntry)
        for (const char* entry : { "cookie" }) {
            QString name = entry;
            a.insert(QStringLiteral("userData:") + name, Accessor {
                [name](const EP& p) {
                    return QVariant(QJsonDocument::fromJson(p.getUserData().toUtf8()).object().value(name).toString());
                },
                [name](EP& p, const QVariant& v) {
                    QString text = p.getUserData();
                    QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
                    if (!text.trimmed().isEmpty() && !document.isObject()) {
                        return;   // userData that isn't a set of entries (a script's own text) is left alone
                    }
                    QJsonObject object = document.object();
                    QString value = v.toString();
                    if (value.isEmpty()) {
                        object.remove(name);
                    } else {
                        object.insert(name, value);
                    }
                    p.setUserData(object.isEmpty() ? QString()
                        : QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
                }
            });
        }

        // entries of materialData's materials shown as fields of their own: read from the first material, written to
        // all of them ("materials" is one material or an array of them); a default value is left out of the JSON
        struct MaterialEntry { const char* name; const char* defaultValue; };
        for (const MaterialEntry& entry : { MaterialEntry { "packing", "custom" } }) {
            QString name = entry.name;
            QString defaultValue = entry.defaultValue;
            a.insert(QStringLiteral("materialData:") + name, Accessor {
                [name, defaultValue](const EP& p) {
                    QJsonValue materials = QJsonDocument::fromJson(p.getMaterialData().toUtf8()).object().value("materials");
                    QJsonArray array = materials.toArray();
                    QJsonObject first = materials.isArray() ? (array.isEmpty() ? QJsonObject() : array.first().toObject())
                                                            : materials.toObject();
                    return QVariant(first.value(name).toString(defaultValue));
                },
                [name, defaultValue](EP& p, const QVariant& v) {
                    QString text = p.getMaterialData();
                    QJsonObject document = QJsonDocument::fromJson(text.toUtf8()).object();
                    QJsonValue materials = document.value("materials");
                    if (!text.trimmed().isEmpty() && !materials.isObject() && !materials.isArray()) {
                        return;   // not a set of materials: left alone
                    }
                    QString value = v.toString();
                    auto setEntry = [&](QJsonObject material) {
                        if (value.isEmpty() || value == defaultValue) {
                            material.remove(name);
                        } else {
                            material.insert(name, value);
                        }
                        return material;
                    };
                    if (materials.isArray()) {
                        QJsonArray array = materials.toArray();
                        for (int i = 0; i < array.size(); ++i) {
                            array[i] = setEntry(array[i].toObject());
                        }
                        document.insert("materials", array);
                    } else {
                        document.insert("materials", setEntry(materials.toObject()));
                    }
                    p.setMaterialData(QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact)));
                }
            });
        }
        return a;
    }

    const QHash<QString, Accessor>& accessors() {
        static const QHash<QString, Accessor> table = buildAccessors();
        return table;
    }

    // --- the table ------------------------------------------------------------------------------

    const QStringList MODE_VALUES = { "inherit:Inherit", "disabled:Off", "enabled:On" };
    const QStringList BILLBOARD_VALUES = { "none:None", "yaw:Yaw", "full:Full" };

    PropertySpec spec(const char* key, const char* label, PropertyKind kind, const char* section) {
        PropertySpec s;
        s.key = key;
        s.label = label;
        s.kind = kind;
        s.section = section;
        if (kind == PropertyKind::Int) {
            s.step = 1;
            s.decimals = 0;
        }
        return s;
    }
    PropertySpec& range(PropertySpec& s, double min, double max, double step, int decimals, const char* unit = "") {
        s.min = min;
        s.max = max;
        s.step = step;
        s.decimals = decimals;
        s.unit = unit;
        return s;
    }
    PropertySpec& stepping(PropertySpec& s, double step, int decimals, const char* unit = "") {
        s.step = step;
        s.decimals = decimals;
        s.unit = unit;
        return s;
    }

    QVector<PropertySpec> buildTable() {
        QVector<PropertySpec> t;
        auto add = [&](PropertySpec s) -> PropertySpec& { t.append(s); return t.last(); };
        auto en = [&](const char* key, const char* label, const char* section, const QStringList& values, const char* showWhen = "") {
            PropertySpec s = spec(key, label, PropertyKind::Enum, section);
            s.enumValues = values;
            s.showWhen = showWhen;
            return add(s);
        };
        auto with = [](PropertySpec& s, const char* showWhen) -> PropertySpec& { s.showWhen = showWhen; return s; };
        using K = PropertyKind;

        // base
        add(spec("name", "Name", K::String, "base"));
        add(spec("description", "Description", K::String, "base"));
        add(spec("parentID", "Parent", K::String, "base"));
        add(spec("parentJointIndex", "Parent Joint", K::Int, "base"));
        add(spec("visible", "Visible", K::Bool, "base"));
        add(spec("locked", "Locked", K::Bool, "base"));
        en("entityPriority", "Priority", "base", { "automatic:Automatic", "prioritized:Prioritized", "static:Static" });
        en("renderLayer", "Render Layer", "base", { "world:World", "front:Front", "hud:HUD" });
        en("primitiveMode", "Primitive Mode", "base", { "solid:Solid", "lines:Wireframe" });

        // transform: metres and degrees go without saying here, as in Unity (no unit on the labels)
        stepping(add(spec("position", "Position", K::Vec3, "spatial")), 0.1, 3);
        stepping(add(spec("rotation", "Rotation", K::Euler, "spatial")), 1.0, 2);
        // Unity's order: Position, Rotation, Scale; then the size in metres it amounts to
        with(range(add(spec("scale", "Scale", K::Vec3, "spatial")), 0.0001, DBL_MAX, 0.01, 3), "type not in Light");
        with(range(add(spec("dimensions", "Dimensions", K::Vec3, "spatial")), 0.001, DBL_MAX, 0.01, 3), "type not in Light");
        with(stepping(add(spec("localPosition", "Local Position", K::Vec3, "spatial")), 0.1, 3), "parentID!=");
        with(stepping(add(spec("localRotation", "Local Rotation", K::Euler, "spatial")), 1.0, 2), "parentID!=");
        range(add(spec("registrationPoint", "Pivot", K::Vec3, "spatial")), 0, 1, 0.01, 3);

        // shape
        en("shape", "Shape", "shape", { "Cube:Box", "Sphere:Sphere", "Tetrahedron:Tetrahedron", "Octahedron:Octahedron",
            "Icosahedron:Icosahedron", "Dodecahedron:Dodecahedron", "Hexagon:Hexagon", "Triangle:Triangle", "Octagon:Octagon",
            "Cylinder:Cylinder", "Cone:Cone", "Circle:Circle", "Quad:Quad" });
        add(spec("color", "Color", K::Color, "shape"));
        range(add(spec("alpha", "Alpha", K::Float, "shape")), 0, 1, 0.01, 2);

        // text
        add(spec("text", "Text", K::String, "text"));
        add(spec("textColor", "Text Color", K::Color, "text"));
        range(add(spec("textAlpha", "Text Alpha", K::Float, "text")), 0, 1, 0.01, 2);
        add(spec("backgroundColor", "Background", K::Color, "text"));
        range(add(spec("backgroundAlpha", "Background Alpha", K::Float, "text")), 0, 1, 0.01, 2);
        range(add(spec("lineHeight", "Line Height", K::Float, "text")), 0, DBL_MAX, 0.01, 3, "m");
        add(spec("font", "Font", K::String, "text"));
        en("textEffect", "Effect", "text", { "none:None", "outline:Outline", "outline fill:Outline with fill", "shadow:Shadow" });
        add(spec("textEffectColor", "Effect Color", K::Color, "text"));
        range(add(spec("textEffectThickness", "Effect Thickness", K::Float, "text")), 0, 0.5, 0.01, 2);
        en("billboardMode", "Billboard", "text", BILLBOARD_VALUES);
        stepping(add(spec("topMargin", "Top Margin", K::Float, "text")), 0.01, 2);
        stepping(add(spec("rightMargin", "Right Margin", K::Float, "text")), 0.01, 2);
        stepping(add(spec("bottomMargin", "Bottom Margin", K::Float, "text")), 0.01, 2);
        stepping(add(spec("leftMargin", "Left Margin", K::Float, "text")), 0.01, 2);
        add(spec("unlit", "Unlit", K::Bool, "text"));

        // zone
        en("shapeType", "Shape", "zone", { "box:Box", "sphere:Sphere", "ellipsoid:Ellipsoid", "cylinder-y:Cylinder", "compound:Compound Shape URL" });
        add(spec("compoundShapeURL", "Compound Shape", K::Url, "zone"));
        add(spec("flyingAllowed", "Flying Allowed", K::Bool, "zone"));
        add(spec("ghostingAllowed", "Ghosting Allowed", K::Bool, "zone"));
        add(spec("filterURL", "Filter", K::Url, "zone"));
        en("keyLightMode", "Key Light", "zone", MODE_VALUES);
        with(add(spec("keyLight.color", "Key Light Color", K::Color, "zone")), "keyLightMode=enabled");
        with(range(add(spec("keyLight.intensity", "Light Intensity", K::Float, "zone")), 0, 40, 0.01, 2), "keyLightMode=enabled");
        with(stepping(add(spec("keyLight.direction", "Light Angle", K::Polar, "zone")), 0.5, 1, "°"), "keyLightMode=enabled");
        with(add(spec("keyLight.castShadows", "Cast Shadows", K::Bool, "zone")), "keyLightMode=enabled");
        with(range(add(spec("keyLight.shadowBias", "Shadow Bias", K::Float, "zone")), 0, 1, 0.01, 2), "keyLightMode=enabled");
        with(range(add(spec("keyLight.shadowMaxDistance", "Shadow Distance", K::Float, "zone")), 1, 250, 0.1, 1, "m"), "keyLightMode=enabled");
        en("skyboxMode", "Skybox", "zone", MODE_VALUES);
        with(add(spec("skybox.color", "Skybox Color", K::Color, "zone")), "skyboxMode=enabled");
        with(add(spec("skybox.url", "Skybox Source", K::Url, "zone")), "skyboxMode=enabled");
        en("ambientLightMode", "Ambient Light", "zone", MODE_VALUES);
        with(range(add(spec("ambientLight.ambientIntensity", "Ambient Intensity", K::Float, "zone")), 0, 200, 0.1, 2), "ambientLightMode=enabled");
        with(add(spec("ambientLight.ambientURL", "Ambient Source", K::Url, "zone")), "ambientLightMode=enabled");
        en("hazeMode", "Haze", "zone", MODE_VALUES);
        with(range(add(spec("haze.hazeRange", "Haze Range", K::Float, "zone")), 1, 10000, 1, 0, "m"), "hazeMode=enabled");
        with(add(spec("haze.hazeAltitudeEffect", "Use Altitude", K::Bool, "zone")), "hazeMode=enabled");
        with(range(add(spec("haze.hazeBaseRef", "Haze Base", K::Float, "zone")), -16000, 16000, 1, 0, "m"), "hazeMode=enabled");
        with(range(add(spec("haze.hazeCeiling", "Haze Ceiling", K::Float, "zone")), -16000, 16000, 1, 0, "m"), "hazeMode=enabled");
        with(add(spec("haze.hazeColor", "Haze Color", K::Color, "zone")), "hazeMode=enabled");
        with(range(add(spec("haze.hazeBackgroundBlend", "Background Blend", K::Float, "zone")), 0, 1, 0.001, 3), "hazeMode=enabled");
        with(add(spec("haze.hazeEnableGlare", "Glare", K::Bool, "zone")), "hazeMode=enabled");
        with(add(spec("haze.hazeGlareColor", "Glare Color", K::Color, "zone")), "hazeMode=enabled");
        with(range(add(spec("haze.hazeGlareAngle", "Glare Angle", K::Float, "zone")), 0, 180, 1, 0, "°"), "hazeMode=enabled");
        en("bloomMode", "Bloom", "zone", MODE_VALUES);
        with(range(add(spec("bloom.bloomIntensity", "Bloom Intensity", K::Float, "zone")), 0, 1, 0.001, 3), "bloomMode=enabled");
        with(range(add(spec("bloom.bloomThreshold", "Bloom Threshold", K::Float, "zone")), 0, 1, 0.001, 3), "bloomMode=enabled");
        with(range(add(spec("bloom.bloomSize", "Bloom Size", K::Float, "zone")), 0, 2, 0.001, 3), "bloomMode=enabled");
        en("zoneCullingMode", "Zone Culling", "zone", { "inherit:Inherit", "outside:Cull Outside", "disabled:Disabled" });
        en("toneMappingMode", "Tone Mapping", "zone", { "inherit:Inherit", "rgb:RGB", "srgb:sRGB", "reinhard:Reinhard", "filmic:Filmic" });
        with(range(add(spec("toneMapping.exposure", "Exposure", K::Float, "zone")), -4, 4, 0.25, 2), "toneMappingMode in rgb,srgb,reinhard,filmic");
        en("avatarPriority", "Avatar Priority", "zone", { "inherit:Inherit", "crowd:Crowd", "hero:Hero" });

        // model
        add(spec("modelURL", "Model", K::Url, "model"));
        en("shapeType", "Collision Shape", "model", { "none:No Collision", "box:Box", "sphere:Sphere", "compound:Compound",
            "simple-hull:Basic, whole model", "simple-compound:Good, sub-meshes", "static-mesh:Exact, not dynamic" });
        add(spec("compoundShapeURL", "Compound Shape", K::Url, "model"));
        add(spec("animation.url", "Animation", K::Url, "model"));
        add(spec("animation.running", "Play Automatically", K::Bool, "model"));
        add(spec("animation.loop", "Loop", K::Bool, "model"));
        add(spec("animation.allowTranslation", "Allow Translation", K::Bool, "model"));
        add(spec("animation.hold", "Hold", K::Bool, "model"));
        stepping(add(spec("animation.currentFrame", "Frame", K::Float, "model")), 1, 1);
        stepping(add(spec("animation.firstFrame", "First Frame", K::Float, "model")), 1, 0);
        stepping(add(spec("animation.lastFrame", "Last Frame", K::Float, "model")), 1, 0);
        stepping(add(spec("animation.fps", "FPS", K::Float, "model")), 1, 1);
        add(spec("textures", "Textures", K::Json, "model"));
        add(spec("groupCulled", "Group Culled", K::Bool, "model"));

        // image
        add(spec("imageURL", "Image", K::Url, "image"));
        add(spec("color", "Color", K::Color, "image"));
        range(add(spec("alpha", "Alpha", K::Float, "image")), 0, 1, 0.01, 2);
        add(spec("emissive", "Emissive", K::Bool, "image"));
        add(spec("subImage", "Sub Image", K::Rect, "image"));
        en("billboardMode", "Billboard", "image", BILLBOARD_VALUES);
        add(spec("keepAspectRatio", "Keep Aspect Ratio", K::Bool, "image"));

        // web
        add(spec("sourceUrl", "Source", K::Url, "web"));
        add(spec("dpi", "Resolution", K::Int, "web"));
        add(spec("color", "Color", K::Color, "web"));
        range(add(spec("alpha", "Alpha", K::Float, "web")), 0, 1, 0.001, 3);
        add(spec("maxFPS", "Max FPS", K::Int, "web"));
        en("billboardMode", "Billboard", "web", BILLBOARD_VALUES);
        en("inputMode", "Input", "web", { "touch:Touch", "mouse:Mouse", "none:None" });
        add(spec("showKeyboardFocusHighlight", "Focus Highlight", K::Bool, "web"));
        add(spec("transparentBackground", "Transparent", K::Bool, "web"));
        add(spec("scriptURL", "Script URL", K::Url, "web"));

        // light
        // Unity's rows, the numbers together: Type, Color, Range, Intensity. Range is where the light ends (what its
        // sphere or cone shows) and sets its dimensions, which are not shown for a light; Falloff Radius is how fast
        // it dims within that (a quarter as bright that far out), and an edit of Range scales it along
        en("lightType", "Type", "light", { "point:Point", "spot:Spot" });
        add(spec("color", "Color", K::Color, "light"));
        range(add(spec("range", "Range", K::Float, "light")), 0.01, 5000, 0.1, 2, "m");
        range(add(spec("intensity", "Intensity", K::Float, "light")), 0, 10000, 0.1, 2);
        range(add(spec("falloffRadius", "Falloff Radius", K::Float, "light")), 0, 10000, 0.1, 2, "m");
        with(range(add(spec("exponent", "Spot Exponent", K::Float, "light")), 0, DBL_MAX, 0.01, 2), "isSpotlight=true");
        with(range(add(spec("cutoff", "Spot Cutoff", K::Float, "light")), 0, 90, 0.5, 1, "°"), "isSpotlight=true");
        // a picture the light shines through, as a slide in a projector (Unity's cookie); kept in userData
        with(add(spec("userData:cookie", "Cookie", K::Image, "light")), "isSpotlight=true");

        // material
        add(spec("materialURL", "Material URL", K::Url, "material"));
        // how occlusion, roughness and metallic are packed into pictures, for a material whose data is its own
        en("materialData:packing", "Packing", "material", { "unity:Unity (Metallic + Smoothness)",
            "orm:ORM (glTF, Unreal)", "custom:Custom" }, "materialURL=materialData");
        add(spec("materialData", "Material Data", K::Json, "material"));
        add(spec("parentMaterialName", "Target", K::String, "material"));
        add(spec("priority", "Priority", K::Int, "material"));
        en("materialMappingMode", "Mapping", "material", { "uv:UV space", "projected:3D projected" });
        range(add(spec("materialMappingPos", "Position", K::Vec2, "material")), 0, 1, 0.1, 3);
        range(add(spec("materialMappingScale", "Scale", K::Vec2, "material")), 0, DBL_MAX, 0.1, 3);
        stepping(add(spec("materialMappingRot", "Rotation", K::Float, "material")), 1, 1, "°");
        add(spec("materialRepeat", "Repeat", K::Bool, "material"));

        // grid
        add(spec("color", "Color", K::Color, "grid"));
        add(spec("followCamera", "Follow Camera", K::Bool, "grid"));
        add(spec("majorGridEvery", "Major Every", K::Int, "grid"));
        range(add(spec("minorGridEvery", "Minor Every", K::Float, "grid")), 0, DBL_MAX, 0.01, 2);

        // particles
        add(spec("isEmitting", "Emit", K::Bool, "particles"));
        range(add(spec("lifespan", "Lifespan", K::Float, "particles")), 0, 86400, 0.01, 2, "s");
        range(add(spec("maxParticles", "Max Particles", K::Int, "particles")), 1, 100000, 1, 0);
        add(spec("textures", "Texture", K::Url, "particles"));
        range(add(spec("emitRate", "Emit Rate", K::Float, "particles_emit")), 0, 100000, 1, 1);
        range(add(spec("emitSpeed", "Emit Speed", K::Float, "particles_emit")), -1000, 1000, 0.1, 2);
        range(add(spec("speedSpread", "Speed Spread", K::Float, "particles_emit")), -1000, 1000, 0.1, 2);
        en("shapeType", "Shape", "particles_emit", { "box:Box", "ellipsoid:Ellipsoid", "cylinder-y:Cylinder", "circle:Circle", "plane:Plane", "compound:Compound Shape URL" });
        add(spec("compoundShapeURL", "Compound Shape", K::Url, "particles_emit"));
        range(add(spec("emitDimensions", "Emit Dimensions", K::Vec3, "particles_emit")), 0, 32768, 0.01, 2);
        range(add(spec("emitRadiusStart", "Emit Radius Start", K::Float, "particles_emit")), 0, 1, 0.001, 3);
        stepping(add(spec("emitOrientation", "Emit Orientation", K::Euler, "particles_emit")), 1, 1, "°");
        add(spec("emitterShouldTrail", "Trails", K::Bool, "particles_emit"));
        range(add(spec("radiusStart", "Size Start", K::Float, "particles_size")), 0, 32768, 0.01, 2);
        range(add(spec("particleRadius", "Size Middle", K::Float, "particles_size")), 0, 32768, 0.01, 2);
        range(add(spec("radiusFinish", "Size Finish", K::Float, "particles_size")), 0, 32768, 0.01, 2);
        range(add(spec("radiusSpread", "Size Spread", K::Float, "particles_size")), 0, 32768, 0.01, 2);
        add(spec("colorStart", "Color Start", K::FloatColor, "particles_color"));
        add(spec("color", "Color Middle", K::Color, "particles_color"));
        add(spec("colorFinish", "Color Finish", K::FloatColor, "particles_color"));
        add(spec("colorSpread", "Color Spread", K::Color, "particles_color"));
        range(add(spec("alphaStart", "Alpha Start", K::Float, "particles_alpha")), 0, 1, 0.001, 3);
        range(add(spec("alpha", "Alpha Middle", K::Float, "particles_alpha")), 0, 1, 0.001, 3);
        range(add(spec("alphaFinish", "Alpha Finish", K::Float, "particles_alpha")), 0, 1, 0.001, 3);
        range(add(spec("alphaSpread", "Alpha Spread", K::Float, "particles_alpha")), 0, 1, 0.001, 3);
        range(add(spec("emitAcceleration", "Acceleration", K::Vec3, "particles_acceleration")), -100, 100, 0.01, 2);
        range(add(spec("accelerationSpread", "Acceleration Spread", K::Vec3, "particles_acceleration")), 0, 100, 0.01, 2);
        range(add(spec("spinStart", "Spin Start", K::Degrees, "particles_spin")), -360, 360, 1, 1, "°");
        range(add(spec("particleSpin", "Spin Middle", K::Degrees, "particles_spin")), -360, 360, 1, 1, "°");
        range(add(spec("spinFinish", "Spin Finish", K::Degrees, "particles_spin")), -360, 360, 1, 1, "°");
        range(add(spec("spinSpread", "Spin Spread", K::Degrees, "particles_spin")), -360, 360, 1, 1, "°");
        add(spec("rotateWithEntity", "Rotate with Entity", K::Bool, "particles_spin"));
        range(add(spec("polarStart", "Polar Start", K::Degrees, "particles_constraints")), 0, 180, 1, 1, "°");
        range(add(spec("polarFinish", "Polar Finish", K::Degrees, "particles_constraints")), 0, 180, 1, 1, "°");
        range(add(spec("azimuthStart", "Azimuth Start", K::Degrees, "particles_constraints")), -180, 180, 1, 1, "°");
        range(add(spec("azimuthFinish", "Azimuth Finish", K::Degrees, "particles_constraints")), -180, 180, 1, 1, "°");

        // behavior
        add(spec("grab.grabbable", "Grabbable", K::Bool, "behavior"));
        add(spec("grab.triggerable", "Triggerable", K::Bool, "behavior"));
        add(spec("grab.grabFollowsController", "Follows Controller", K::Bool, "behavior"));
        add(spec("cloneable", "Cloneable", K::Bool, "behavior"));
        with(range(add(spec("cloneLifetime", "Clone Lifetime", K::Float, "behavior")), -1, DBL_MAX, 1, 0, "s"), "cloneable=true");
        with(range(add(spec("cloneLimit", "Clone Limit", K::Float, "behavior")), 0, DBL_MAX, 1, 0), "cloneable=true");
        with(add(spec("cloneDynamic", "Clone Dynamic", K::Bool, "behavior")), "cloneable=true");
        with(add(spec("cloneAvatarEntity", "Clone Avatar Entity", K::Bool, "behavior")), "cloneable=true");
        with(add(spec("cloneGrabbable", "Clone Grabbable", K::Bool, "behavior")), "cloneable=true");
        // whether the object casts a shadow from the sun; lights in this engine cast none, so on a
        // light (or a zone, or a material) the box would only mislead
        // spot lights cast shadows; point lights don't yet, so they don't offer it
        with(add(spec("canCastShadow", "Cast Shadows", K::Bool, "behavior")), "type not in Light,Zone,Material or lightType=spot");
        add(spec("href", "Link", K::Url, "behavior"));
        add(spec("ignorePickIntersection", "Ignore Picking", K::Bool, "behavior"));
        add(spec("script", "Script", K::Url, "behavior"));
        add(spec("serverScripts", "Server Script", K::Url, "behavior"));
        stepping(add(spec("lifetime", "Lifetime", K::Float, "behavior")), 1, 0, "s");
        add(spec("userData", "User Data", K::UserData, "userData"));

        // collision
        add(spec("collisionless", "Collisionless", K::Bool, "collision"));
        with(add(spec("collisionMask", "Collides With", K::CollisionMask, "collision")), "collisionless=false");
        with(add(spec("collisionSoundURL", "Collision Sound", K::Url, "collision")), "collisionless=false");
        add(spec("dynamic", "Dynamic", K::Bool, "collision"));

        // physics
        stepping(add(spec("localVelocity", "Velocity", K::Vec3, "physics")), 0.01, 3, "m/s");
        range(add(spec("damping", "Damping", K::Float, "physics")), 0, 1, 0.001, 3);
        stepping(add(spec("localAngularVelocity", "Angular Velocity", K::Vec3, "physics")), 1, 2, "°/s");
        range(add(spec("angularDamping", "Angular Damping", K::Float, "physics")), 0, 1, 0.001, 3);
        range(add(spec("restitution", "Bounciness", K::Float, "physics")), 0, 0.99, 0.01, 2);
        range(add(spec("friction", "Friction", K::Float, "physics")), 0, 10, 0.01, 2);
        range(add(spec("density", "Density", K::Float, "physics")), 100, 10000, 1, 0);
        stepping(add(spec("gravity", "Gravity", K::Vec3, "physics")), 0.1, 2, "m/s²");
        stepping(add(spec("acceleration", "Acceleration", K::Vec3, "physics")), 0.1, 2, "m/s²");

        return t;
    }

    const QVector<PropertySection> SECTIONS = {
        { "base", "", false },
        { "spatial", "Transform", false },
        { "shape", "Shape", false },
        { "text", "Text", false },
        { "zone", "Zone", false },
        { "model", "Model", false },
        { "image", "Image", false },
        { "web", "Web", false },
        { "light", "Light", false },
        { "material", "Material", false },
        { "grid", "Grid", false },
        { "particles", "Particles", false },
        { "particles_emit", "Emit", true },
        { "particles_size", "Size", true },
        { "particles_color", "Color", true },
        { "particles_alpha", "Alpha", true },
        { "particles_acceleration", "Acceleration", true },
        { "particles_spin", "Spin", true },
        { "particles_constraints", "Constraints", true },
        { "behavior", "Behavior", false },
        { "collision", "Collision", false },
        { "physics", "Physics", false },
        { "userData", "User Data", false }
    };
}

namespace PropertySpecs {

const QVector<PropertySpec>& all() {
    static const QVector<PropertySpec> table = buildTable();
    return table;
}

QVector<const PropertySpec*> forSection(const QString& section) {
    QVector<const PropertySpec*> result;
    for (const auto& s : all()) {
        if (s.section == section) {
            result << &s;
        }
    }
    return result;
}

const PropertySpec* find(const QString& key) {
    for (const auto& s : all()) {
        if (s.key == key) {
            return &s;
        }
    }
    return nullptr;
}

const PropertySection* section(const QString& id) {
    for (const auto& s : SECTIONS) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

// the sections an entity type shows, User Data aside
static QStringList typeSections(EntityTypes::EntityType type, bool multiple) {
    if (multiple) {
        return { "base", "spatial", "behavior", "collision", "physics" };
    }
    switch (type) {
        case EntityTypes::Box:
        case EntityTypes::Sphere:
        case EntityTypes::Shape:
            return { "base", "spatial", "shape", "behavior", "collision", "physics" };
        case EntityTypes::Text:
            return { "base", "spatial", "text", "behavior", "collision", "physics" };
        case EntityTypes::Zone:
            return { "base", "spatial", "zone", "behavior", "physics" };
        case EntityTypes::Model:
            return { "base", "spatial", "model", "behavior", "collision", "physics" };
        case EntityTypes::Image:
            return { "base", "spatial", "image", "behavior", "collision", "physics" };
        case EntityTypes::Web:
            return { "base", "spatial", "web", "behavior", "collision", "physics" };
        case EntityTypes::Light:
            return { "base", "spatial", "light", "behavior", "collision", "physics" };
        case EntityTypes::Material:
            return { "base", "spatial", "material", "behavior" };
        case EntityTypes::ParticleEffect:
            return { "base", "spatial", "particles", "particles_emit", "particles_size", "particles_color", "particles_alpha",
                     "particles_acceleration", "particles_spin", "particles_constraints", "behavior", "physics" };
        case EntityTypes::Grid:
            return { "base", "spatial", "grid", "behavior", "physics" };
        default:
            return { "base", "spatial", "behavior", "collision", "physics" };
    }
}

QStringList sectionsForType(EntityTypes::EntityType type, bool multiple) {
    // every entity has User Data, last, as Unity puts what a project adds below an object's own components
    return typeSections(type, multiple) << QStringLiteral("userData");
}

QVariant get(const QString& key, const EntityItemProperties& properties) {
    auto it = accessors().constFind(key);
    return it == accessors().constEnd() ? QVariant() : it->get(properties);
}

bool set(const QString& key, EntityItemProperties& properties, const QVariant& value) {
    auto it = accessors().constFind(key);
    if (it == accessors().constEnd()) {
        return false;
    }
    it->set(properties, value);
    return true;
}

bool isEntry(const QString& key) {
    return key.contains(':');
}

bool isUserDataEntry(const QString& key) {
    return key.startsWith(QStringLiteral("userData:"));
}

bool takesEntry(const QString& key, const EntityItemProperties& properties) {
    if (isUserDataEntry(key)) {
        QString text = properties.getUserData();
        return text.trimmed().isEmpty() || QJsonDocument::fromJson(text.toUtf8()).isObject();
    }
    if (key.startsWith(QStringLiteral("materialData:"))) {
        QString text = properties.getMaterialData();
        QJsonValue materials = QJsonDocument::fromJson(text.toUtf8()).object().value("materials");
        return text.trimmed().isEmpty() || materials.isObject() || materials.isArray();
    }
    return true;
}

QString storedKey(const QString& key) {
    if (key == QStringLiteral("scale")) {
        return QStringLiteral("dimensions");
    }
    return isEntry(key) ? key.left(key.indexOf(':')) : key;
}


glm::vec3 naturalSize(const EntityItemProperties& properties) {
    if (properties.getType() != EntityTypes::Model) {
        return glm::vec3(1.0f);
    }
    glm::vec3 natural = properties.getNaturalDimensions();
    if (!glm::any(glm::greaterThan(natural, glm::vec3(1.0e-6f)))) {
        return glm::vec3(0.0f);   // not loaded
    }
    // a flat model (a plane) has no thickness of its own: that axis counts in metres
    for (int i = 0; i < 3; ++i) {
        if (natural[i] <= 1.0e-6f) {
            natural[i] = 1.0f;
        }
    }
    return natural;
}

}
