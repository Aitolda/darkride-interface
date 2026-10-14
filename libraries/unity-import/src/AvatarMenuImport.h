//
//  AvatarMenuImport.h
//  libraries/unity-import/src
//
//  Created on 2026-10-14 for Darkride's import of avatars from Unity packages.
//
//  Distributed under the MIT License.
//  See the accompanying file LICENSE-MIT or https://opensource.org/licenses/MIT
//

#ifndef hifi_AvatarMenuImport_h
#define hifi_AvatarMenuImport_h

#include <functional>

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <hfm/HFM.h>

namespace unity {
class Package;
}

namespace avatarimport {

// An avatar's menu (and the parameters the menu sets) and the effects layer's state
// machines those parameters drive, as data for the engine (avatars-renderer's AvatarMenu runs them). The .fst
// line is "avatarMenu = <this object as JSON>":
//   {"version": 1, "parameters": [...], "menu": {...}, "objects": [...], "layers": [...]}
// - parameters: {"name", "type" ("bool", "int", "float"), "default", "saved", "synced", "builtin"}; the
//   built-in parameters the layers read (GestureLeft, IsLocal, ...) are marked builtin.
// - menu: {"controls": [{"name", "type" ("button", "toggle", "submenu", "radial", "twoAxis", "fourAxis"),
//   "parameter", "value", "icon" (a file beside the .fst), "controls" (a submenu's), "subParameters"}]}.
// - objects: what the layers switch on and off: {"path" (Unity's), "parts" (the model's mesh parts under it, which
//   the engine hides), "springBones" (roots of spring bones on it)}; "active" its state before any layer writes.
// - layers: {"name", "default" (state index), "entry" and "anyState" (transitions), "states": [{"name",
//   "writeDefaults", "effect", "transitions"}]}; a transition is {"to" (state index, -1 for the exit), "conditions":
//   [[parameter, mode, threshold]] with mode "if", "ifNot", "greater", "less", "equals", "notEqual", "exitTime" (null
//   or the normalized time), "duration" (seconds)}. An effect is what the state's clip sets at its first frame:
//   {"active": [[object, bool]], "blendshapes": {name: 0 to 1}, "joints": {name: {"translation", "rotation",
//   "scale"}}} (only the parts it animates, in the engine's frame).
// - base: each property's value before any layer writes (for states that write defaults).
// Joints are named as the avatar names them (after jointMap); blend shapes as the engine keeps the model's own.
struct AvatarMenuData {
    QJsonObject json;
    QSet<int> controlledParts;   // mesh parts the layers show and hide: the .fst must not hide them at load
    int layers { 0 };
    int states { 0 };
    int controls { 0 };
    QStringList notes;
};

struct AvatarMenuInputs {
    const hfm::Model* model { nullptr };
    QStringList paths;                             // the model's joints by Unity path (unity::jointPaths)
    QHash<QString, QString> skeletonNames;         // the file's joint name -> the avatar's (jointMap)
    float metersPerUnit { 1.0f };
    std::function<QString(const QString& textureGuid)> copyIcon;   // copies a menu icon beside the .fst
    std::function<bool(int joint)> prefabActive;                   // an object's state as the prefab leaves it
    std::function<QJsonObject(int joint)> prefabPose;              // a joint's pose as the prefab leaves it
    QHash<QString, float> prefabBlendshapes;                       // shape keys the prefab sets, 0 to 1
};

AvatarMenuData avatarMenuData(const unity::Package& package, const QVariantMap& descriptor, const AvatarMenuInputs& in);

}

#endif // hifi_AvatarMenuImport_h
