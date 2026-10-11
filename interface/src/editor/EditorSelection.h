//
//  EditorSelection.h
//  interface/src/editor
//
//  The one place selection lives in build mode. The Hierarchy, the Inspector, the gizmo and
//  the outline all follow it.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorSelection_h
#define hifi_EditorSelection_h

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUuid>

#include <glm/glm.hpp>

#include <AABox.h>

class EditorSelection : public QObject {
    Q_OBJECT
public:
    explicit EditorSelection(QObject* parent = nullptr);

    const QList<QUuid>& ids() const { return _ids; }
    int count() const { return _ids.size(); }
    bool isEmpty() const { return _ids.isEmpty(); }
    bool contains(const QUuid& id) const { return _ids.contains(id); }
    QUuid primary() const { return _ids.isEmpty() ? QUuid() : _ids.last(); }

    // the selected entities that have no selected ancestor: the ones a move or a copy applies to
    QList<QUuid> topLevel() const;
    // the world bounds of everything selected; false when nothing selected is in the tree
    bool bounds(AABox& bounds) const;
    glm::vec3 center() const;

    void set(const QList<QUuid>& ids);
    void add(const QUuid& id);
    void remove(const QUuid& id);
    void toggle(const QUuid& id);
    void clear();
    // drops ids that are no longer in the tree
    void prune();

    void setHighlightEnabled(bool enabled);

signals:
    void changed();

private:
    void applyHighlight();
    void pushHighlight();

    QList<QUuid> _ids;
    bool _highlightEnabled { false };
    // an entity made a moment ago (a paste, a duplicate, a new one) has nothing drawn yet, and the outline is
    // made of what is drawn: the list is sent again shortly, until all of them show
    QTimer _highlightRetry;
    int _highlightRetries { 0 };
};

#endif // hifi_EditorSelection_h
