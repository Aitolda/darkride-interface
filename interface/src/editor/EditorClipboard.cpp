//
//  EditorClipboard.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorClipboard.h"

#include "EditorUndo.h"

void EditorClipboard::copy(const QList<QUuid>& ids) {
    _properties.clear();
    _ids.clear();
    _topLevel.clear();

    QList<QUuid> all = EditorUndo::withDescendants(ids);
    QSet<QUuid> inBatch(all.begin(), all.end());
    for (const auto& id : all) {
        EntityItemProperties properties;
        if (!EditorUndo::snapshot(id, properties)) {
            continue;
        }
        _properties << properties;
        _ids << id;
        if (!inBatch.contains(properties.getParentID())) {
            _topLevel.insert(id);
        }
    }
}

QList<QUuid> EditorClipboard::paste(EditorUndo& undo, const QString& text, const glm::vec3& offset) const {
    if (_properties.isEmpty()) {
        return QList<QUuid>();
    }
    QVector<EntityItemProperties> properties = _properties;
    if (offset != glm::vec3(0.0f)) {
        for (int i = 0; i < properties.size(); ++i) {
            if (_topLevel.contains(_ids[i])) {
                properties[i].setPosition(properties[i].getPosition() + offset);
            }
        }
    }
    // the pasted entities are new: give them no old ids to remap, so the originals keep theirs
    QList<QUuid> newIds = undo.pushAdd(text, properties, QList<QUuid>());
    QList<QUuid> topLevel;
    for (int i = 0; i < newIds.size() && i < _ids.size(); ++i) {
        if (_topLevel.contains(_ids[i]) && !newIds[i].isNull()) {
            topLevel << newIds[i];
        }
    }
    return topLevel;
}
