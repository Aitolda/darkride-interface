//
//  ValueCodec.h
//  libraries/wasm-scripting/src
//
//  Values between the host and a module, as CBOR (RFC 8949). The host side is QVariant: the codec maps the
//  QVariant type system to the plain CBOR data model (integers, floats, booleans, null, undefined, text, bytes,
//  arrays, maps; no tags), in the shapes scripts see today: vec3 as {x, y, z}, quat as {x, y, z, w}, mat4 as
//  the {r0c0 ... r3c3} map, a colour as {red, green, blue}, a QUuid as its text with braces (null for a null
//  uuid), a QUrl as text, a QObject as a handle record {"@object": handle, "@class": name}, a guest function
//  as {"@callback": id}. Decoding is driven by the target type when one is known (a method's parameter type),
//  with the coercions QtScript applies today: a number for a vec3, an array of three for a vec3, a colour name
//  for a vec3, an integer or its key for an enum.
//
//  Types the codec does not know are looked up in a registry (registerType), then in the codec's fallback (the
//  engine hands it QtScript's own conversions during the transition), and otherwise encode as undefined.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ValueCodec_h
#define hifi_ValueCodec_h

#include <cstdint>
#include <functional>

#include <QtCore/QByteArray>
#include <QtCore/QCborValue>
#include <QtCore/QList>
#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QVariant>

class ObjectTable;

/// A function in the guest, held by the id the guest gave it. Crosses as {"@callback": id}; the engine calls
/// it back through the module's on_callback export.
struct ScriptCallback {
    int32_t id { 0 };
    bool isValid() const { return id != 0; }
    bool operator==(const ScriptCallback& other) const { return id == other.id; }
};
Q_DECLARE_METATYPE(ScriptCallback)

class ValueCodec {
public:
    static const QString OBJECT_KEY;    // "@object"
    static const QString CLASS_KEY;     // "@class"
    static const QString CALLBACK_KEY;  // "@callback"

    using Encoder = std::function<QCborValue(const ValueCodec& codec, const QVariant& value)>;
    /// Fills `out` from `value` for the registered type; false when the value has no sensible conversion.
    using Decoder = std::function<bool(const ValueCodec& codec, const QCborValue& value, QVariant& out)>;

    /// Process-wide: a conversion pair for a metatype the codec does not know by itself. Later registrations
    /// replace earlier ones. Safe to call from any thread, before or after codecs exist.
    static void registerType(int typeId, Encoder encoder, Decoder decoder);
    static bool hasRegisteredType(int typeId);

    /// The fallback for the types neither the codec nor the registry knows; the engine supplies it.
    using EncodeFallback = std::function<bool(const QVariant& value, QCborValue& out)>;
    using DecodeFallback = std::function<bool(const QCborValue& value, int targetType, QVariant& out)>;

    ValueCodec();
    explicit ValueCodec(ObjectTable* objects);

    void setObjectTable(ObjectTable* objects) { _objects = objects; }
    ObjectTable* objectTable() const { return _objects; }
    void setFallback(EncodeFallback encode, DecodeFallback decode);

    QCborValue toCbor(const QVariant& value) const;
    /// With a target type: the value converted to it, with QtScript's coercions; `ok` says whether a
    /// conversion existed. Without one (QMetaType::UnknownType): the natural QVariant for the CBOR item.
    QVariant fromCbor(const QCborValue& value, int targetType, bool* ok) const;
    QVariant fromCbor(const QCborValue& value) const;
    QVariant fromCbor(const QCborValue& value, int targetType) const;

    QByteArray encode(const QVariant& value) const;
    /// False with the error when the bytes are not well-formed CBOR.
    bool decode(const QByteArray& bytes, QVariant& out, QString& error) const;
    bool decode(const QByteArray& bytes, int targetType, QVariant& out, QString& error) const;
    /// A CBOR array of arguments decoded to the parameter types (extra arguments are kept as plain values,
    /// missing ones are invalid QVariants, as a JavaScript call with fewer arguments would give).
    bool decodeArguments(const QByteArray& bytes, const QList<int>& types, QVariantList& out, QString& error) const;

    /// True for the types the codec converts itself (not counting the registry and the fallback).
    static bool isBuiltinType(int typeId);

private:
    QCborValue encodeObject(QObject* object) const;
    QCborValue encodeSequence(const QVariant& value) const;
    QCborValue encodeAssociative(const QVariant& value) const;
    QVariant decodeTyped(const QCborValue& value, int targetType, bool& ok) const;
    QVariant decodeNumber(const QCborValue& value, int targetType, bool& ok) const;
    QVariant decodeEnum(const QCborValue& value, int targetType, bool& ok) const;
    QVariant decodeObjectPointer(const QCborValue& value, int targetType, bool& ok) const;

    ObjectTable* _objects { nullptr };
    EncodeFallback _encodeFallback;
    DecodeFallback _decodeFallback;
};

#endif // hifi_ValueCodec_h
