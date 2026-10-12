//
//  AvatarImporter.h
//  interface/src/avatar
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_AvatarImporter_h
#define hifi_AvatarImporter_h

#include <QString>

// File > Import Avatar. An .fst is worn as it is. A character file (FBX, DAE, OBJ, glTF and the rest the client reads)
// gets the standard skeleton from libraries/auto-rigging, after asking when the file already has a rig, and an
// offer to fix a height that looks like a units mistake. The result is saved under the app data folder's
// Avatars/<name>/, and the client offers to wear it. Saving to My Files is not here yet: until then only this
// client sees an imported avatar.
class AvatarImporter {
public:
    static void importFile(const QString& path);
};

#endif // hifi_AvatarImporter_h
