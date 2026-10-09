//
//  HierarchyPanel.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "HierarchyPanel.h"

#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>

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
    _tree->setDragDropMode(QAbstractItemView::InternalMove);
    _tree->setUniformRowHeights(true);
    _tree->setIndentation(14);
    _tree->setFrameShape(QFrame::NoFrame);

    layout->addWidget(_search);
    layout->addWidget(_tree, 1);
    setWidget(body);
}
