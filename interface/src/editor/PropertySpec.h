//
//  PropertySpec.h
//  interface/src/editor
//
//  The property table the Inspector is generated from: every editable entity property with its
//  kind, label, section, range and enum values, which entity types show which sections, and the
//  accessors that read and write a property by key on an EntityItemProperties. One table, so a
//  property cannot be editable in one place and not another.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_PropertySpec_h
#define hifi_PropertySpec_h

#include <cfloat>

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtCore/QVector>

#include <EntityItemProperties.h>
#include <EntityTypes.h>

// How a property is edited. The QVariant carried for each kind:
//   Bool: bool  Int, Float: double  Degrees: double degrees (stored in radians)
//   String, Url, Json, Enum (the value name), CollisionMask (the comma list): QString
//   Color, FloatColor: QColor  Vec2, Polar (elevation and azimuth in degrees): QVector2D
//   Vec3, Euler (degrees): QVector3D  Rect: QRect
enum class PropertyKind {
    Bool,
    Int,
    Float,
    Degrees,
    String,
    Url,
    Json,
    Color,
    FloatColor,
    Vec2,
    Vec3,
    Euler,
    Polar,
    Rect,
    Enum,
    CollisionMask
};

struct PropertySpec {
    QString key;            // the property path, e.g. "keyLight.color"
    QString label;
    PropertyKind kind;
    QString section;
    double min { -DBL_MAX };
    double max { DBL_MAX };
    double step { 0.1 };
    int decimals { 2 };
    QString unit;
    QStringList enumValues; // "value:Label" entries
    QString showWhen;       // "key=value", "key!=value", "key in a,b,c", or empty
    bool readOnly { false };
};

struct PropertySection {
    QString id;
    QString label;
    bool minor { false };   // a sub-section (the particle groups)
};

namespace PropertySpecs {
    const QVector<PropertySpec>& all();
    QVector<const PropertySpec*> forSection(const QString& section);
    const PropertySpec* find(const QString& key);
    const PropertySection* section(const QString& id);
    // the sections an entity type shows, in order; `multiple` for a mixed selection
    QStringList sectionsForType(EntityTypes::EntityType type, bool multiple);

    // the property's value, as the kind's QVariant; invalid when the key is unknown
    QVariant get(const QString& key, const EntityItemProperties& properties);
    // sets the property (and only it) on the properties; false when the key is unknown
    bool set(const QString& key, EntityItemProperties& properties, const QVariant& value);

    // an entity's own size, what Scale 1 means: a model's natural dimensions (zero until the model
    // has loaded), 1 m for everything else, as for Unity's primitives
    glm::vec3 naturalSize(const EntityItemProperties& properties);
}

#endif // hifi_PropertySpec_h
