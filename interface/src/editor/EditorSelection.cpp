//
//  EditorSelection.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorSelection.h"

#include <QtCore/QSet>
#include <QtCore/QVariantMap>

#include <EntityItem.h>
#include <EntityTree.h>
#include <EntityTreeRenderer.h>

#include "Application.h"
#include "scripting/SelectionScriptingInterface.h"

namespace {
    const QString HIGHLIGHT_LIST = "editorSelection";
    const QString ITEM_TYPE = "entity";
    const int MAX_PARENT_DEPTH = 64;
    const int HIGHLIGHT_RETRY_MS = 100;
    const int HIGHLIGHT_RETRY_LIMIT = 50;   // 5 s

    QVariantMap color(int red, int green, int blue) {
        QVariantMap map;
        map["red"] = red;
        map["green"] = green;
        map["blue"] = blue;
        return map;
    }

    // Unity's orange outline, seen through walls at a lower strength
    QVariantMap highlightStyle() {
        QVariantMap style;
        style["outlineUnoccludedColor"] = color(255, 128, 0);
        style["outlineUnoccludedAlpha"] = 1.0f;
        style["outlineOccludedColor"] = color(255, 128, 0);
        style["outlineOccludedAlpha"] = 0.35f;
        style["fillUnoccludedAlpha"] = 0.0f;
        style["fillOccludedAlpha"] = 0.0f;
        style["outlineWidth"] = 3;
        style["isOutlineSmooth"] = true;
        return style;
    }

    EntityTreePointer entityTree() {
        auto renderer = qApp->getEntities();
        return renderer ? renderer->getTree() : nullptr;
    }
}

EditorSelection::EditorSelection(QObject* parent) : QObject(parent) {
    _highlightRetry.setSingleShot(true);
    _highlightRetry.setInterval(HIGHLIGHT_RETRY_MS);
    connect(&_highlightRetry, &QTimer::timeout, this, &EditorSelection::pushHighlight);
}

QList<QUuid> EditorSelection::topLevel() const {
    auto tree = entityTree();
    if (!tree) {
        return _ids;
    }
    QSet<QUuid> selected(_ids.begin(), _ids.end());
    QList<QUuid> result;
    tree->withReadLock([&] {
        for (const auto& id : _ids) {
            bool hasSelectedAncestor = false;
            auto entity = tree->findEntityByEntityItemID(id);
            QUuid parentID = entity ? entity->getParentID() : QUuid();
            for (int depth = 0; !parentID.isNull() && depth < MAX_PARENT_DEPTH; ++depth) {
                if (selected.contains(parentID)) {
                    hasSelectedAncestor = true;
                    break;
                }
                auto parent = tree->findEntityByEntityItemID(parentID);
                parentID = parent ? parent->getParentID() : QUuid();
            }
            if (!hasSelectedAncestor) {
                result << id;
            }
        }
    });
    return result;
}

bool EditorSelection::bounds(AABox& bounds) const {
    auto tree = entityTree();
    if (!tree || _ids.isEmpty()) {
        return false;
    }
    bool any = false;
    tree->withReadLock([&] {
        for (const auto& id : _ids) {
            auto entity = tree->findEntityByEntityItemID(id);
            if (!entity) {
                continue;
            }
            bool success = false;
            AABox box = entity->getAABox(success);
            if (!success) {
                continue;
            }
            if (any) {
                bounds += box;
            } else {
                bounds = box;
                any = true;
            }
        }
    });
    return any;
}

glm::vec3 EditorSelection::center() const {
    AABox box;
    return bounds(box) ? box.calcCenter() : glm::vec3(0.0f);
}

void EditorSelection::set(const QList<QUuid>& ids) {
    QList<QUuid> unique;
    for (const auto& id : ids) {
        if (!id.isNull() && !unique.contains(id)) {
            unique << id;
        }
    }
    if (unique == _ids) {
        return;
    }
    _ids = unique;
    applyHighlight();
    emit changed();
}

void EditorSelection::add(const QUuid& id) {
    if (id.isNull() || _ids.contains(id)) {
        return;
    }
    _ids << id;
    applyHighlight();
    emit changed();
}

void EditorSelection::remove(const QUuid& id) {
    if (_ids.removeAll(id) > 0) {
        applyHighlight();
        emit changed();
    }
}

void EditorSelection::toggle(const QUuid& id) {
    if (_ids.contains(id)) {
        remove(id);
    } else {
        add(id);
    }
}

void EditorSelection::clear() {
    if (_ids.isEmpty()) {
        return;
    }
    _ids.clear();
    applyHighlight();
    emit changed();
}

void EditorSelection::prune() {
    auto tree = entityTree();
    if (!tree || _ids.isEmpty()) {
        return;
    }
    QList<QUuid> kept;
    tree->withReadLock([&] {
        for (const auto& id : _ids) {
            if (tree->findEntityByEntityItemID(id)) {
                kept << id;
            }
        }
    });
    if (kept.size() != _ids.size()) {
        _ids = kept;
        applyHighlight();
        emit changed();
    }
}

void EditorSelection::setHighlightEnabled(bool enabled) {
    if (enabled == _highlightEnabled) {
        return;
    }
    _highlightEnabled = enabled;
    auto selection = DependencyManager::get<SelectionScriptingInterface>();
    if (!selection) {
        return;
    }
    if (enabled) {
        selection->enableListHighlight(HIGHLIGHT_LIST, highlightStyle());
        applyHighlight();
    } else {
        selection->clearSelectedItemsList(HIGHLIGHT_LIST);
        selection->disableListHighlight(HIGHLIGHT_LIST);
    }
}

void EditorSelection::applyHighlight() {
    _highlightRetries = 0;
    pushHighlight();
}

void EditorSelection::pushHighlight() {
    if (!_highlightEnabled) {
        return;
    }
    auto selection = DependencyManager::get<SelectionScriptingInterface>();
    if (!selection) {
        return;
    }
    // the list holds what each entity draws, looked up as it is filled: one with nothing drawn yet is left out
    auto renderer = qApp->getEntities();
    bool waiting = false;
    selection->clearSelectedItemsList(HIGHLIGHT_LIST);
    for (const auto& id : _ids) {
        selection->addToSelectedItemsList(HIGHLIGHT_LIST, ITEM_TYPE, id);
        waiting |= renderer && !render::Item::isValidID(renderer->renderableIdForEntityId(id));
    }
    if (waiting && _highlightRetries++ < HIGHLIGHT_RETRY_LIMIT) {
        _highlightRetry.start();
    }
}
