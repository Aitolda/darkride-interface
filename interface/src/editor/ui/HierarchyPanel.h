//
//  HierarchyPanel.h
//  interface/src/editor/ui
//
//  The Hierarchy dock: a search field over a tree of the domain's entities.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_HierarchyPanel_h
#define hifi_HierarchyPanel_h

#include <QtWidgets/QDockWidget>

class QLineEdit;
class QTreeView;

class HierarchyPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit HierarchyPanel(QWidget* parent = nullptr);

    QTreeView* treeView() const { return _tree; }
    QLineEdit* searchField() const { return _search; }

private:
    QLineEdit* _search { nullptr };
    QTreeView* _tree { nullptr };
};

#endif // hifi_HierarchyPanel_h
