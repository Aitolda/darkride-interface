//
//  WasmClientTypeConversions.cpp
//  interface/src/scripting
//
//  The WebAssembly script engine's conversions (ValueCodec, libraries/wasm-scripting) for the client's own types
//  that have only QtScript conversions, registered when the program starts, as
//  libraries/script-engine/src/WasmTypeConversions.cpp does for the library types. Each gives a module the
//  shape the QtScript conversion gives a script, member for member and in the same order, and reads one back
//  as the QtScript conversion reads it; the QtScript original is named beside each.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QtCore/QCborArray>
#include <QtCore/QCborMap>

#include <BoxBase.h>
#include <RegisteredMetaTypes.h>
#include <ValueCodec.h>

#include "AccountServicesScriptingInterface.h"
#include "ui/InteractiveWindow.h"
#include "ui/overlays/Overlays.h"

namespace {

QVariant variantOf(const ValueCodec& codec, const QCborMap& object, const char* name) {
    return codec.fromCbor(object.value(QLatin1String(name)));
}

template <typename T>
T typed(const ValueCodec& codec, const QCborValue& value) {
    return codec.fromCbor(value, qMetaTypeId<T>()).template value<T>();
}

// RayToOverlayIntersectionResultToScriptValue, ...FromScriptValue (ui/overlays/Overlays.cpp)
QCborValue encodeRayToOverlayIntersectionResult(const ValueCodec& codec, const QVariant& value) {
    const auto result = value.value<RayToOverlayIntersectionResult>();
    QCborMap map;
    map.insert(QStringLiteral("intersects"), result.intersects);
    map.insert(QStringLiteral("overlayID"), result.overlayID.isNull() ? QCborValue(nullptr) : QCborValue(result.overlayID.toString()));
    map.insert(QStringLiteral("distance"), double(result.distance));
    map.insert(QStringLiteral("face"), boxFaceToString(result.face));
    map.insert(QStringLiteral("intersection"), codec.toCbor(QVariant::fromValue(result.intersection)));
    map.insert(QStringLiteral("surfaceNormal"), codec.toCbor(QVariant::fromValue(result.surfaceNormal)));
    map.insert(QStringLiteral("extraInfo"), codec.toCbor(result.extraInfo));
    return map;
}

bool decodeRayToOverlayIntersectionResult(const ValueCodec& codec, const QCborValue& value, QVariant& out) {
    const QCborMap object = value.toMap();
    RayToOverlayIntersectionResult result;
    result.intersects = variantOf(codec, object, "intersects").toBool();
    result.overlayID = typed<QUuid>(codec, object.value(QLatin1String("overlayID")));
    result.distance = variantOf(codec, object, "distance").toFloat();
    result.face = boxFaceFromString(variantOf(codec, object, "face").toString());
    if (object.contains(QLatin1String("intersection"))) {
        result.intersection = typed<glm::vec3>(codec, object.value(QLatin1String("intersection")));
    }
    if (object.contains(QLatin1String("surfaceNormal"))) {
        result.surfaceNormal = typed<glm::vec3>(codec, object.value(QLatin1String("surfaceNormal")));
    }
    result.extraInfo = variantOf(codec, object, "extraInfo").toMap();
    out = QVariant::fromValue(result);
    return true;
}

// DownloadInfoResultToScriptValue, ...FromScriptValue (scripting/AccountServicesScriptingInterface.cpp)
QCborValue encodeDownloadInfoResult(const ValueCodec&, const QVariant& value) {
    const auto result = value.value<DownloadInfoResult>();
    QCborArray downloading;
    for (float percent : result.downloading) {
        downloading.append(double(percent));
    }
    QCborMap map;
    map.insert(QStringLiteral("downloading"), downloading);
    map.insert(QStringLiteral("pending"), double(result.pending));
    return map;
}

bool decodeDownloadInfoResult(const ValueCodec& codec, const QCborValue& value, QVariant& out) {
    const QCborMap object = value.toMap();
    DownloadInfoResult result;
    for (const QVariant& percent : variantOf(codec, object, "downloading").toList()) {
        result.downloading.append(percent.toFloat());
    }
    result.pending = variantOf(codec, object, "pending").toFloat();
    out = QVariant::fromValue(result);
    return true;
}

// interactiveWindowPointerToScriptValue gives the window to the script with ScriptOwnership (ui/InteractiveWindow.cpp):
// the engine's object table owns it. A window the script let go of stays open until the script stops (QtScript's
// collector closed it at a time of its own; a script that opens a window and keeps no reference means it to stay).
QCborValue encodeInteractiveWindow(const ValueCodec& codec, const QVariant& value) {
    return codec.toCborOwned(value.value<InteractiveWindowPointer>(), [](QObject*) { return true; });
}

bool decodeInteractiveWindow(const ValueCodec& codec, const QCborValue& value, QVariant& out) {
    out = QVariant::fromValue(qobject_cast<InteractiveWindowPointer>(codec.fromCbor(value).value<QObject*>()));
    return true;
}

[[maybe_unused]] const bool registered = [] {
    ValueCodec::registerType(qMetaTypeId<RayToOverlayIntersectionResult>(), &encodeRayToOverlayIntersectionResult,
                             &decodeRayToOverlayIntersectionResult);
    ValueCodec::registerType(qMetaTypeId<DownloadInfoResult>(), &encodeDownloadInfoResult, &decodeDownloadInfoResult);
    ValueCodec::registerType(qMetaTypeId<InteractiveWindowPointer>(), &encodeInteractiveWindow, &decodeInteractiveWindow);
    return true;
}();

} // namespace
