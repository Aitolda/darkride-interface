//
//  HierarchyPanel.h
//  interface/src/editor/ui
//
//  The Hierarchy dock: a search field over the tree of the domain's entities.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_HierarchyPanel_h
#define hifi_HierarchyPanel_h

#include <QtCore/QList>
#include <QtCore/QUuid>
#include <QtWidgets/QDockWidget>

class QItemSelection;
class QLineEdit;
class QModelIndex;
class QSortFilterProxyModel;
class QTreeView;
class EntityTreeModel;

class HierarchyPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit HierarchyPanel(QWidget* parent = nullptr);

    void setModel(EntityTreeModel* model);
    // shows these as selected without reporting it back as the user's doing
    void selectIds(const QList<QUuid>& ids);

    QTreeView* treeView() const { return _tree; }
    QLineEdit* searchField() const { return _search; }

signals:
    void selectionChangedByUser(const QList<QUuid>& ids);
    void frameRequested(const QUuid& id);

private:
    QList<QUuid> selectedIds() const;
    QUuid idAt(const QModelIndex& proxyIndex) const;

    QLineEdit* _search { nullptr };
    QTreeView* _tree { nullptr };
    QSortFilterProxyModel* _proxy { nullptr };
    EntityTreeModel* _model { nullptr };
    bool _settingSelection { false };
};

#endif // hifi_HierarchyPanel_h
