//
//  EditorUndo.h
//  interface/src/editor
//
//  The undo stack for build mode. Nothing edits the world without going through it. A deleted
//  entity comes back with a new id, so the stack keeps a map from old ids to new ones and every
//  command resolves its ids through it.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorUndo_h
#define hifi_EditorUndo_h

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QUuid>
#include <QtCore/QVector>
#include <QtWidgets/QUndoStack>

#include <EntityItemProperties.h>

class EditorUndo : public QObject {
    Q_OBJECT
public:
    explicit EditorUndo(QObject* parent = nullptr);

    QUndoStack* stack() { return &_stack; }

    // the id an entity has now, following re-creations
    QUuid resolve(const QUuid& id) const;
    void remap(const QUuid& from, const QUuid& to);

    // everything the world knows about an entity, for snapshots and re-creation
    static bool snapshot(const QUuid& id, EntityItemProperties& properties);
    // the ids plus every descendant, parents before children, without duplicates
    static QList<QUuid> withDescendants(const QList<QUuid>& ids);
    static bool isLocked(const QUuid& id);

    // creates entities from properties (parents before children; parent ids are resolved and
    // remapped within the batch) and returns the new ids in the same order
    QList<QUuid> createEntities(const QVector<EntityItemProperties>& properties, const QList<QUuid>& oldIds);

    // records a property edit; applies it first unless it already happened (a finished drag)
    void pushPropertyEdit(const QString& text, const QList<QUuid>& ids, const QVector<EntityItemProperties>& before,
        const QVector<EntityItemProperties>& after, bool alreadyApplied);
    // deletes the entities and their descendants now, skipping locked ones; returns what went
    QList<QUuid> pushDelete(const QString& text, const QList<QUuid>& ids);
    // creates entities now and records it; returns the new ids in order
    QList<QUuid> pushAdd(const QString& text, const QVector<EntityItemProperties>& properties, const QList<QUuid>& oldIds);

private:
    QUndoStack _stack;
    QHash<QUuid, QUuid> _remap;
};

#endif // hifi_EditorUndo_h
