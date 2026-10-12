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

#include <functional>

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

    // everything the world knows about an entity, the way scripts see it: position and rotation in
    // the world (edits take them so), the parent-relative ones in localPosition and localRotation.
    // Nothing is marked as set; read values from it, or use capture() to make the entity again.
    static bool snapshot(const QUuid& id, EntityItemProperties& properties);
    static EntityItemProperties snapshotOf(const EntityItemPointer& entity);
    // a snapshot with every property marked as set, to make the entity again (a copy, or one that
    // was deleted): an add applies and sends only the marked properties
    static bool capture(const QUuid& id, EntityItemProperties& properties);
    // the ids plus every descendant, parents before children, without duplicates
    static QList<QUuid> withDescendants(const QList<QUuid>& ids);
    static bool isLocked(const QUuid& id);

    // creates entities from properties (parents before children) and returns the new ids in the same
    // order. sourceIds name what each entry was captured from: a parent among them is replaced by the
    // entity made for it, so a copied family stays together. With remapSources, the stack also resolves
    // each source id to its new entity from now on (deleted entities coming back; not a paste, whose
    // originals remain).
    QList<QUuid> createEntities(const QVector<EntityItemProperties>& properties, const QList<QUuid>& sourceIds,
        bool remapSources);

    // records a property edit; applies it first unless it already happened (a finished drag)
    void pushPropertyEdit(const QString& text, const QList<QUuid>& ids, const QVector<EntityItemProperties>& before,
        const QVector<EntityItemProperties>& after, bool alreadyApplied);
    // deletes the entities and their descendants now, skipping locked ones; returns what went
    QList<QUuid> pushDelete(const QString& text, const QList<QUuid>& ids);
    // creates entities now and records it; returns the new ids in order. sourceIds: as for
    // createEntities (a paste gives the originals; a brand new entity gives none)
    QList<QUuid> pushAdd(const QString& text, const QVector<EntityItemProperties>& properties, const QList<QUuid>& sourceIds);
    // a change made to a created entity since, which a redo of its creation should make too (a new model's own
    // size, once it has loaded): the dimensions and position in `changes` replace the recorded ones
    void amendCreated(const QUuid& id, const EntityItemProperties& changes);

    // the selection is part of the history, as in Unity: a change of it is a step (pushed once it has happened),
    // undone and redone through the editor, the ids following entities made again
    void setSelectionApplier(std::function<void(const QList<QUuid>&)> applier) { _applySelection = applier; }
    void pushSelection(const QList<QUuid>& before, const QList<QUuid>& after);
    void applySelection(const QList<QUuid>& ids);

private:
    QUndoStack _stack;
    QHash<QUuid, QUuid> _remap;
    std::function<void(const QList<QUuid>&)> _applySelection;
};

#endif // hifi_EditorUndo_h
