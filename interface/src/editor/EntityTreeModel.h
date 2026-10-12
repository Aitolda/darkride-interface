//
//  EntityTreeModel.h
//  interface/src/editor
//
//  The Hierarchy's model: the domain's entities as a parent/child tree, kept live from the
//  entity tree's signals, with a slow rescan behind them for what the signals miss.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EntityTreeModel_h
#define hifi_EntityTreeModel_h

#include <memory>

#include <QtCore/QAbstractItemModel>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUuid>

#include <EntityItemID.h>
#include <EntityTree.h>
#include <EntityTypes.h>

class EntityTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        TypeNameRole,
        LockedRole,
        VisibleRole
    };

    explicit EntityTreeModel(QObject* parent = nullptr);
    ~EntityTreeModel();

    // follow the client's entity tree (call once it exists)
    void attach();

    QModelIndex indexForId(const QUuid& id) const;
    QUuid idForIndex(const QModelIndex& index) const;
    QList<QUuid> allIds() const;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;
    // rows dragged onto another become its children, onto the empty space top-level entities; the editor makes
    // the move (reparentRequested), and the tree follows the entities
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;
    // the entity is the other, or below it
    bool isDescendantOf(const QUuid& id, const QUuid& ancestor) const;

signals:
    // the user typed a new name in the Hierarchy; the editor applies it (through undo)
    void renameRequested(const QUuid& id, const QString& name);
    // the user dragged entities onto another (or onto the empty space: a null parent); the editor moves them
    void reparentRequested(const QList<QUuid>& ids, const QUuid& parentId);

private slots:
    void onEntityAdded(const EntityItemID& id);
    void onEntityDeleted(const EntityItemID& id);
    void flushEdits();
    void rescan();
    void rebuild();

private:
    struct Node {
        QUuid id;
        QString name;
        EntityTypes::EntityType type { EntityTypes::Unknown };
        bool locked { false };
        bool visible { true };
        QUuid parentID;
        Node* parent { nullptr };
        QVector<Node*> children;

        ~Node() { qDeleteAll(children); }
        int row() const { return parent ? parent->children.indexOf(const_cast<Node*>(this)) : 0; }
    };

    Node* nodeFor(const QModelIndex& index) const;
    QModelIndex indexFor(Node* node) const;
    Node* parentNodeFor(const QUuid& parentID) const;
    bool isDescendant(Node* node, Node* ancestor) const;

    // reads name, type, locked, visible and parent under the tree's read lock; false if gone
    bool readEntity(const QUuid& id, Node& node) const;
    void insertNode(Node* node, Node* parent);
    void removeNode(Node* node);
    void moveNode(Node* node, Node* newParent);
    void adoptOrphans(Node* parent);
    void updateNode(Node* node);

    EntityTreePointer _tree;
    std::unique_ptr<Node> _root;
    QHash<QUuid, Node*> _nodes;
    QSet<QUuid> _pendingEdits;
    QTimer _editFlush;
    QTimer _rescan;
};

#endif // hifi_EntityTreeModel_h
