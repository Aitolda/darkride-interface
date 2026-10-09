//
//  EditorUndo.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorUndo.h"

#include <QtCore/QSet>
#include <QtWidgets/QUndoCommand>

#include <EntityItem.h>
#include <EntityScriptingInterface.h>
#include <EntityTree.h>
#include <EntityTreeRenderer.h>
#include <SharedUtil.h>

#include "Application.h"

namespace {
    const int MAX_REMAP_DEPTH = 64;

    EntityTreePointer entityTree() {
        auto renderer = qApp->getEntities();
        return renderer ? renderer->getTree() : nullptr;
    }

    void applyProperties(EditorUndo* undo, const QList<QUuid>& ids, const QVector<EntityItemProperties>& properties) {
        auto entities = DependencyManager::get<EntityScriptingInterface>();
        for (int i = 0; i < ids.size() && i < properties.size(); ++i) {
            entities->editEntity(undo->resolve(ids[i]), properties[i]);
        }
    }

    void deleteEntities(EditorUndo* undo, const QList<QUuid>& ids) {
        auto entities = DependencyManager::get<EntityScriptingInterface>();
        for (const auto& id : ids) {
            entities->deleteEntity(undo->resolve(id));
        }
    }

    // A change to some properties of some entities, with the way back.
    class PropertyEditCommand : public QUndoCommand {
    public:
        PropertyEditCommand(EditorUndo* undo, const QString& text, const QList<QUuid>& ids,
            const QVector<EntityItemProperties>& before, const QVector<EntityItemProperties>& after, bool alreadyApplied) :
            QUndoCommand(text), _undo(undo), _ids(ids), _before(before), _after(after), _skipFirstRedo(alreadyApplied) {
        }
        void redo() override {
            if (_skipFirstRedo) {
                _skipFirstRedo = false;
                return;
            }
            applyProperties(_undo, _ids, _after);
        }
        void undo() override {
            applyProperties(_undo, _ids, _before);
        }
    private:
        EditorUndo* _undo;
        QList<QUuid> _ids;
        QVector<EntityItemProperties> _before;
        QVector<EntityItemProperties> _after;
        bool _skipFirstRedo;
    };

    // Entities that were deleted; undo brings them back (with new ids, which the stack remaps).
    class DeleteEntitiesCommand : public QUndoCommand {
    public:
        DeleteEntitiesCommand(EditorUndo* undo, const QString& text, const QList<QUuid>& ids,
            const QVector<EntityItemProperties>& properties) :
            QUndoCommand(text), _undo(undo), _ids(ids), _properties(properties) {
        }
        void redo() override {
            deleteEntities(_undo, _ids);
        }
        void undo() override {
            _undo->createEntities(_properties, _ids);
        }
    private:
        EditorUndo* _undo;
        QList<QUuid> _ids;
        QVector<EntityItemProperties> _properties;
    };

    // Entities that were created (a paste, a duplicate); undo deletes them.
    class AddEntitiesCommand : public QUndoCommand {
    public:
        AddEntitiesCommand(EditorUndo* undo, const QString& text, const QList<QUuid>& ids,
            const QVector<EntityItemProperties>& properties) :
            QUndoCommand(text), _undo(undo), _ids(ids), _properties(properties) {
        }
        void redo() override {
            if (_skipFirstRedo) {
                _skipFirstRedo = false;
                return;
            }
            _undo->createEntities(_properties, _ids);
        }
        void undo() override {
            deleteEntities(_undo, _ids);
        }
    private:
        EditorUndo* _undo;
        QList<QUuid> _ids;
        QVector<EntityItemProperties> _properties;
        bool _skipFirstRedo { true };
    };
}

EditorUndo::EditorUndo(QObject* parent) : QObject(parent) {
}

QUuid EditorUndo::resolve(const QUuid& id) const {
    QUuid current = id;
    for (int depth = 0; depth < MAX_REMAP_DEPTH; ++depth) {
        auto it = _remap.constFind(current);
        if (it == _remap.constEnd()) {
            break;
        }
        current = it.value();
    }
    return current;
}

void EditorUndo::remap(const QUuid& from, const QUuid& to) {
    if (!from.isNull() && !to.isNull() && from != to) {
        _remap[from] = to;
    }
}

bool EditorUndo::snapshot(const QUuid& id, EntityItemProperties& properties) {
    auto tree = entityTree();
    if (!tree) {
        return false;
    }
    bool found = false;
    tree->withReadLock([&] {
        auto entity = tree->findEntityByEntityItemID(id);
        if (entity) {
            properties = entity->getProperties();
            found = true;
        }
    });
    return found;
}

QList<QUuid> EditorUndo::withDescendants(const QList<QUuid>& ids) {
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    QList<QUuid> result;
    QSet<QUuid> seen;
    QList<QUuid> queue = ids;
    while (!queue.isEmpty()) {
        QUuid id = queue.takeFirst();
        if (id.isNull() || seen.contains(id)) {
            continue;
        }
        seen.insert(id);
        result << id;
        for (const auto& child : entities->getChildrenIDs(id)) {
            queue << child;
        }
    }
    return result;
}

bool EditorUndo::isLocked(const QUuid& id) {
    auto tree = entityTree();
    if (!tree) {
        return false;
    }
    bool locked = false;
    tree->withReadLock([&] {
        auto entity = tree->findEntityByEntityItemID(id);
        locked = entity && entity->getLocked();
    });
    return locked;
}

QList<QUuid> EditorUndo::createEntities(const QVector<EntityItemProperties>& properties, const QList<QUuid>& oldIds) {
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    QHash<QUuid, QUuid> batch;   // old id to new id, within this batch
    QList<QUuid> newIds;
    for (int i = 0; i < properties.size(); ++i) {
        EntityItemProperties copy = properties[i];
        QUuid parentID = copy.getParentID();
        if (!parentID.isNull()) {
            copy.setParentID(batch.contains(parentID) ? batch.value(parentID) : resolve(parentID));
        }
        // a new entity: a fresh creation time (with the old one, a finite lifetime would already be
        // over and the server would delete it on arrival), no stale edit stamp, no inherited physics
        copy.setCreated(UNKNOWN_CREATED_TIME);
        copy.setLastEdited(usecTimestampNow());
        copy.setActionData(QByteArray());
        copy.setSimulationOwner(QUuid(), 0);
        QUuid newId = entities->addEntity(copy);
        newIds << newId;
        QUuid oldId = i < oldIds.size() ? oldIds[i] : QUuid();
        if (!oldId.isNull() && !newId.isNull()) {
            batch[oldId] = newId;
            remap(oldId, newId);
        }
    }
    return newIds;
}

void EditorUndo::pushPropertyEdit(const QString& text, const QList<QUuid>& ids, const QVector<EntityItemProperties>& before,
    const QVector<EntityItemProperties>& after, bool alreadyApplied) {
    if (ids.isEmpty()) {
        return;
    }
    _stack.push(new PropertyEditCommand(this, text, ids, before, after, alreadyApplied));
}

QList<QUuid> EditorUndo::pushDelete(const QString& text, const QList<QUuid>& ids) {
    QList<QUuid> all = withDescendants(ids);
    QList<QUuid> captured;
    QVector<EntityItemProperties> properties;
    for (const auto& id : all) {
        if (isLocked(id)) {
            continue;
        }
        EntityItemProperties snapshotProperties;
        if (snapshot(id, snapshotProperties)) {
            captured << id;
            properties << snapshotProperties;
        }
    }
    if (captured.isEmpty()) {
        return captured;
    }
    _stack.push(new DeleteEntitiesCommand(this, text, captured, properties));
    return captured;
}

QList<QUuid> EditorUndo::pushAdd(const QString& text, const QVector<EntityItemProperties>& properties, const QList<QUuid>& oldIds) {
    if (properties.isEmpty()) {
        return QList<QUuid>();
    }
    QList<QUuid> newIds = createEntities(properties, oldIds);
    // the command keeps the ids the entities were captured with, so a later re-creation remaps them
    QList<QUuid> recordIds;
    for (int i = 0; i < newIds.size(); ++i) {
        QUuid oldId = i < oldIds.size() ? oldIds[i] : QUuid();
        recordIds << (oldId.isNull() ? newIds[i] : oldId);
    }
    _stack.push(new AddEntitiesCommand(this, text, recordIds, properties));
    return newIds;
}
