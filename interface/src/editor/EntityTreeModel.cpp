//
//  EntityTreeModel.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EntityTreeModel.h"

#include <QtCore/QMimeData>

#include <EntityItem.h>
#include <EntityTreeElement.h>
#include <EntityTreeRenderer.h>

#include "Application.h"

namespace {
    const int EDIT_FLUSH_MS = 100;
    const int RESCAN_MS = 3000;
    const QString ID_MIME_TYPE = QStringLiteral("application/x-darkride-entity-ids");   // a drag's ids, one a line

    QList<QUuid> draggedIds(const QMimeData* data) {
        QList<QUuid> ids;
        for (const auto& text : QString::fromUtf8(data->data(ID_MIME_TYPE)).split('\n', Qt::SkipEmptyParts)) {
            QUuid id(text);
            if (!id.isNull()) {
                ids << id;
            }
        }
        return ids;
    }
}

EntityTreeModel::EntityTreeModel(QObject* parent) : QAbstractItemModel(parent), _root(new Node()) {
    _editFlush.setSingleShot(true);
    _editFlush.setInterval(EDIT_FLUSH_MS);
    connect(&_editFlush, &QTimer::timeout, this, &EntityTreeModel::flushEdits);
    _rescan.setInterval(RESCAN_MS);
    connect(&_rescan, &QTimer::timeout, this, &EntityTreeModel::rescan);
}

EntityTreeModel::~EntityTreeModel() {
}

void EntityTreeModel::attach() {
    auto renderer = qApp->getEntities();
    if (!renderer) {
        return;
    }
    auto tree = renderer->getTree();
    if (!tree || tree == _tree) {
        return;
    }
    if (_tree) {
        disconnect(_tree.get(), nullptr, this, nullptr);
    }
    _tree = tree;

    // the tree's signals come from other threads; everything here runs on the model's thread
    connect(_tree.get(), &EntityTree::addingEntity, this, &EntityTreeModel::onEntityAdded, Qt::QueuedConnection);
    connect(_tree.get(), &EntityTree::deletingEntity, this, &EntityTreeModel::onEntityDeleted, Qt::QueuedConnection);
    connect(_tree.get(), &EntityTree::clearingEntities, this, &EntityTreeModel::rebuild, Qt::QueuedConnection);
    connect(_tree.get(), &EntityTree::editingEntityPointer, this, [this](const EntityItemPointer& entity) {
        if (!entity) {
            return;
        }
        QUuid id = entity->getID();
        QMetaObject::invokeMethod(this, [this, id] {
            _pendingEdits.insert(id);
            if (!_editFlush.isActive()) {
                _editFlush.start();
            }
        }, Qt::QueuedConnection);
    }, Qt::DirectConnection);

    rebuild();
    _rescan.start();
}

// --- reading the tree ---------------------------------------------------------------------------

bool EntityTreeModel::readEntity(const QUuid& id, Node& node) const {
    if (!_tree) {
        return false;
    }
    bool found = false;
    _tree->withReadLock([&] {
        auto entity = _tree->findEntityByEntityItemID(id);
        // the Hierarchy is the world: not the scripts' local helpers, not avatar wearables
        if (!entity || !entity->isDomainEntity()) {
            return;
        }
        found = true;
        node.id = id;
        node.name = entity->getName();
        node.type = entity->getType();
        node.locked = entity->getLocked();
        node.visible = entity->getVisible();
        node.created = entity->getCreated();
        node.parentID = entity->getParentID();
    });
    return found;
}

void EntityTreeModel::rebuild() {
    beginResetModel();
    _nodes.clear();
    _pendingEdits.clear();
    _root.reset(new Node());

    QVector<Node*> nodes;
    if (_tree) {
        _tree->withReadLock([&] {
            _tree->recurseTreeWithOperation([&](const OctreeElementPointer& element, void*) {
                auto entityElement = std::static_pointer_cast<EntityTreeElement>(element);
                entityElement->forEachEntity([&](const EntityItemPointer& entity) {
                    if (!entity->isDomainEntity()) {
                        return;
                    }
                    auto node = new Node();
                    node->id = entity->getID();
                    node->name = entity->getName();
                    node->type = entity->getType();
                    node->locked = entity->getLocked();
                    node->visible = entity->getVisible();
                    node->parentID = entity->getParentID();
                    nodes << node;
                });
                return true;
            });
        });
    }
    for (auto node : nodes) {
        _nodes[node->id] = node;
    }
    // parents first, then everything else hangs off its parent or the root
    for (auto node : nodes) {
        Node* parent = parentNodeFor(node->parentID);
        if (parent != _root.get() && isDescendant(parent, node)) {
            parent = _root.get();   // a cycle in the data; keep the tree a tree
        }
        node->parent = parent;
        parent->children.append(node);
    }
    endResetModel();
}

void EntityTreeModel::rescan() {
    if (!_tree) {
        return;
    }
    // everything the tree holds right now, in one pass under the lock
    QHash<QUuid, Node> current;
    _tree->withReadLock([&] {
        _tree->recurseTreeWithOperation([&](const OctreeElementPointer& element, void*) {
            auto entityElement = std::static_pointer_cast<EntityTreeElement>(element);
            entityElement->forEachEntity([&](const EntityItemPointer& entity) {
                if (!entity->isDomainEntity()) {
                    return;
                }
                Node node;
                node.id = entity->getID();
                node.name = entity->getName();
                node.type = entity->getType();
                node.locked = entity->getLocked();
                node.visible = entity->getVisible();
                node.parentID = entity->getParentID();
                current.insert(node.id, node);
            });
            return true;
        });
    });

    // gone
    QList<QUuid> stale;
    for (auto it = _nodes.constBegin(); it != _nodes.constEnd(); ++it) {
        if (!current.contains(it.key())) {
            stale << it.key();
        }
    }
    for (const auto& id : stale) {
        if (auto node = _nodes.value(id)) {
            removeNode(node);
        }
    }
    // new, then changed
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        if (!_nodes.contains(it.key())) {
            auto node = new Node(it.value());
            node->children.clear();
            insertNode(node, parentNodeFor(node->parentID));
        }
    }
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        auto node = _nodes.value(it.key());
        if (!node) {
            continue;
        }
        const Node& fresh = it.value();
        bool changed = node->name != fresh.name || node->locked != fresh.locked || node->visible != fresh.visible
            || node->type != fresh.type;
        if (changed) {
            node->name = fresh.name;
            node->locked = fresh.locked;
            node->visible = fresh.visible;
            node->type = fresh.type;
            QModelIndex index = indexFor(node);
            emit dataChanged(index, index);
        }
        if (node->parentID != fresh.parentID) {
            node->parentID = fresh.parentID;
            moveNode(node, parentNodeFor(fresh.parentID));
        }
    }
}

// --- incremental updates ------------------------------------------------------------------------

void EntityTreeModel::onEntityAdded(const EntityItemID& id) {
    if (_nodes.contains(id)) {
        _pendingEdits.insert(id);
        if (!_editFlush.isActive()) {
            _editFlush.start();
        }
        return;
    }
    auto node = new Node();
    if (!readEntity(id, *node)) {
        delete node;
        return;
    }
    insertNode(node, parentNodeFor(node->parentID));
}

void EntityTreeModel::onEntityDeleted(const EntityItemID& id) {
    _pendingEdits.remove(id);
    if (auto node = _nodes.value(id)) {
        removeNode(node);
    }
}

void EntityTreeModel::flushEdits() {
    QSet<QUuid> pending;
    pending.swap(_pendingEdits);
    for (const auto& id : pending) {
        auto node = _nodes.value(id);
        if (!node) {
            onEntityAdded(id);
            continue;
        }
        updateNode(node);
    }
}

void EntityTreeModel::updateNode(Node* node) {
    Node fresh;
    if (!readEntity(node->id, fresh)) {
        removeNode(node);
        return;
    }
    bool changed = node->name != fresh.name || node->locked != fresh.locked || node->visible != fresh.visible
        || node->type != fresh.type;
    if (changed) {
        node->name = fresh.name;
        node->locked = fresh.locked;
        node->visible = fresh.visible;
        node->type = fresh.type;
        QModelIndex index = indexFor(node);
        emit dataChanged(index, index);
    }
    if (node->parentID != fresh.parentID) {
        node->parentID = fresh.parentID;
        moveNode(node, parentNodeFor(fresh.parentID));
    }
}

void EntityTreeModel::insertNode(Node* node, Node* parent) {
    if (!parent) {
        parent = _root.get();
    }
    int row = parent->children.size();
    beginInsertRows(indexFor(parent), row, row);
    node->parent = parent;
    parent->children.append(node);
    _nodes[node->id] = node;
    endInsertRows();
    adoptOrphans(node);
}

void EntityTreeModel::removeNode(Node* node) {
    if (!node || node == _root.get()) {
        return;
    }
    // the children may well outlive the parent in the world; keep them, at the root for now
    while (!node->children.isEmpty()) {
        moveNode(node->children.first(), _root.get());
    }
    Node* parent = node->parent;
    int row = node->row();
    beginRemoveRows(indexFor(parent), row, row);
    parent->children.removeAt(row);
    _nodes.remove(node->id);
    endRemoveRows();
    delete node;
}

void EntityTreeModel::moveNode(Node* node, Node* newParent) {
    if (!newParent) {
        newParent = _root.get();
    }
    if (newParent == node || isDescendant(newParent, node)) {
        newParent = _root.get();
    }
    Node* oldParent = node->parent;
    if (oldParent == newParent) {
        return;
    }
    int row = node->row();
    int destination = newParent->children.size();
    if (!beginMoveRows(indexFor(oldParent), row, row, indexFor(newParent), destination)) {
        return;
    }
    oldParent->children.removeAt(row);
    node->parent = newParent;
    newParent->children.append(node);
    endMoveRows();
}

void EntityTreeModel::adoptOrphans(Node* parent) {
    // children that arrived before their parent sit at the root until now
    QVector<Node*> orphans;
    for (auto child : _root->children) {
        if (child != parent && child->parentID == parent->id) {
            orphans << child;
        }
    }
    for (auto child : orphans) {
        moveNode(child, parent);
    }
}

EntityTreeModel::Node* EntityTreeModel::parentNodeFor(const QUuid& parentID) const {
    if (parentID.isNull()) {
        return _root.get();
    }
    // an avatar or an entity we do not have is not in the model: hang off the root
    auto node = _nodes.value(parentID);
    return node ? node : _root.get();
}

bool EntityTreeModel::isDescendant(Node* node, Node* ancestor) const {
    for (Node* current = node; current; current = current->parent) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

// --- QAbstractItemModel -------------------------------------------------------------------------

EntityTreeModel::Node* EntityTreeModel::nodeFor(const QModelIndex& index) const {
    if (!index.isValid()) {
        return _root.get();
    }
    return static_cast<Node*>(index.internalPointer());
}

QModelIndex EntityTreeModel::indexFor(Node* node) const {
    if (!node || node == _root.get()) {
        return QModelIndex();
    }
    return createIndex(node->row(), 0, node);
}

QModelIndex EntityTreeModel::indexForId(const QUuid& id) const {
    return indexFor(_nodes.value(id));
}

QUuid EntityTreeModel::idForIndex(const QModelIndex& index) const {
    Node* node = nodeFor(index);
    return (node && node != _root.get()) ? node->id : QUuid();
}

QList<QUuid> EntityTreeModel::allIds() const {
    return _nodes.keys();
}

QModelIndex EntityTreeModel::index(int row, int column, const QModelIndex& parent) const {
    Node* parentNode = nodeFor(parent);
    if (column != 0 || row < 0 || !parentNode || row >= parentNode->children.size()) {
        return QModelIndex();
    }
    return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex EntityTreeModel::parent(const QModelIndex& index) const {
    Node* node = nodeFor(index);
    if (!node || node == _root.get()) {
        return QModelIndex();
    }
    return indexFor(node->parent);
}

int EntityTreeModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) {
        return 0;
    }
    Node* node = nodeFor(parent);
    return node ? node->children.size() : 0;
}

int EntityTreeModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return 1;
}

QVariant EntityTreeModel::data(const QModelIndex& index, int role) const {
    Node* node = nodeFor(index);
    if (!node || node == _root.get()) {
        return QVariant();
    }
    const QString& typeName = EntityTypes::getEntityTypeName(node->type);
    switch (role) {
        case Qt::DisplayRole:
            return node->name.isEmpty() ? typeName : node->name;
        case Qt::EditRole:
            return node->name;
        case Qt::ToolTipRole:
            return typeName + "  " + node->id.toString(QUuid::WithoutBraces);
        case IdRole:
            return node->id;
        case TypeRole:
            return (int)node->type;
        case TypeNameRole:
            return typeName;
        case LockedRole:
            return node->locked;
        case VisibleRole:
            return node->visible;
        case CreatedRole:
            return (qulonglong)node->created;
        default:
            return QVariant();
    }
}

bool EntityTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    Node* node = nodeFor(index);
    if (!node || node == _root.get() || role != Qt::EditRole) {
        return false;
    }
    QString name = value.toString().trimmed();
    if (name == node->name) {
        return false;
    }
    emit renameRequested(node->id, name);
    return true;
}

Qt::ItemFlags EntityTreeModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;   // the empty space below the rows: a drop there is to the top level
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

QStringList EntityTreeModel::mimeTypes() const {
    return { ID_MIME_TYPE };
}

QMimeData* EntityTreeModel::mimeData(const QModelIndexList& indexes) const {
    QStringList ids;
    for (const auto& index : indexes) {
        QUuid id = idForIndex(index);
        if (index.column() == 0 && !id.isNull()) {
            ids << id.toString();
        }
    }
    if (ids.isEmpty()) {
        return nullptr;
    }
    auto data = new QMimeData();
    data->setData(ID_MIME_TYPE, ids.join('\n').toUtf8());
    return data;
}

bool EntityTreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                      const QModelIndex& parent) const {
    Q_UNUSED(row);
    Q_UNUSED(column);
    if (action != Qt::MoveAction || !data || !data->hasFormat(ID_MIME_TYPE)) {
        return false;
    }
    // onto an entity, or onto the empty space; not onto one of the dragged or anything below it
    QUuid target = idForIndex(parent);
    for (const auto& id : draggedIds(data)) {
        if (!target.isNull() && isDescendantOf(target, id)) {
            return false;
        }
    }
    return true;
}

bool EntityTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    if (!canDropMimeData(data, action, row, column, parent)) {
        return false;
    }
    // the editor makes the move (through undo) and the tree follows the entities: nothing is taken out here
    emit reparentRequested(draggedIds(data), idForIndex(parent));
    return true;
}

bool EntityTreeModel::isDescendantOf(const QUuid& id, const QUuid& ancestor) const {
    Node* node = _nodes.value(id);
    Node* ancestorNode = _nodes.value(ancestor);
    return node && ancestorNode && isDescendant(node, ancestorNode);
}

QHash<int, QByteArray> EntityTreeModel::roleNames() const {
    auto roles = QAbstractItemModel::roleNames();
    roles[IdRole] = "id";
    roles[TypeRole] = "type";
    roles[TypeNameRole] = "typeName";
    roles[LockedRole] = "locked";
    roles[VisibleRole] = "visible";
    return roles;
}
