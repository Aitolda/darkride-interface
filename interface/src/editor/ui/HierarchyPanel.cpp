//
//  HierarchyPanel.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "HierarchyPanel.h"

#include <QtCore/QItemSelectionModel>
#include <QtCore/QSortFilterProxyModel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>

#include "../EntityTreeModel.h"

HierarchyPanel::HierarchyPanel(QWidget* parent) : QDockWidget("Hierarchy", parent) {
    setObjectName("EditorHierarchy");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setMinimumWidth(220);

    auto body = new QWidget(this);
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    _search = new QLineEdit(body);
    _search->setPlaceholderText("Search");
    _search->setClearButtonEnabled(true);

    _tree = new QTreeView(body);
    _tree->setHeaderHidden(true);
    _tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _tree->setUniformRowHeights(true);
    _tree->setIndentation(14);
    _tree->setFrameShape(QFrame::NoFrame);
    _tree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    _tree->setExpandsOnDoubleClick(false);

    // the search filters the tree and keeps a match's ancestors; names sort case-blind
    _proxy = new QSortFilterProxyModel(this);
    _proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    _proxy->setFilterRole(Qt::DisplayRole);
    _proxy->setRecursiveFilteringEnabled(true);
    _proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    _proxy->setDynamicSortFilter(true);
    connect(_search, &QLineEdit::textChanged, _proxy, &QSortFilterProxyModel::setFilterFixedString);

    connect(_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        QUuid id = idAt(index);
        if (!id.isNull()) {
            emit frameRequested(id);
        }
    });

    layout->addWidget(_search);
    layout->addWidget(_tree, 1);
    setWidget(body);
}

void HierarchyPanel::setModel(EntityTreeModel* model) {
    _model = model;
    _proxy->setSourceModel(model);
    _tree->setModel(_proxy);
    _proxy->sort(0, Qt::AscendingOrder);
    connect(_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (!_settingSelection) {
            emit selectionChangedByUser(selectedIds());
        }
    });
}

QUuid HierarchyPanel::idAt(const QModelIndex& proxyIndex) const {
    return proxyIndex.data(EntityTreeModel::IdRole).toUuid();
}

QList<QUuid> HierarchyPanel::selectedIds() const {
    QList<QUuid> ids;
    if (!_tree->selectionModel()) {
        return ids;
    }
    for (const auto& index : _tree->selectionModel()->selectedRows()) {
        QUuid id = idAt(index);
        if (!id.isNull()) {
            ids << id;
        }
    }
    return ids;
}

void HierarchyPanel::selectIds(const QList<QUuid>& ids) {
    auto selectionModel = _tree->selectionModel();
    if (!_model || !selectionModel) {
        return;
    }
    _settingSelection = true;
    QItemSelection selection;
    QModelIndex first;
    for (const auto& id : ids) {
        QModelIndex proxyIndex = _proxy->mapFromSource(_model->indexForId(id));
        if (!proxyIndex.isValid()) {
            continue;
        }
        selection.select(proxyIndex, proxyIndex);
        if (!first.isValid()) {
            first = proxyIndex;
        }
    }
    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (first.isValid()) {
        selectionModel->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
        _tree->scrollTo(first);
    }
    _settingSelection = false;
}
