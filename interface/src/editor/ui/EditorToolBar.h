//
//  EditorToolBar.h
//  interface/src/editor/ui
//
//  The tool strip above the viewport: View / Move / Rotate / Scale, handle space, pivot
//  mode, snapping, the grid, and the way out of build mode.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorToolBar_h
#define hifi_EditorToolBar_h

#include <QtWidgets/QToolBar>

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QToolButton;

class EditorToolBar : public QToolBar {
    Q_OBJECT
public:
    enum class Tool { View, Move, Rotate, Scale };

    explicit EditorToolBar(QWidget* parent = nullptr);

    Tool tool() const { return _tool; }
    void setTool(Tool tool);
    void setGridChecked(bool checked);
    void setLocalSpace(bool local);
    void setPivotMode(bool pivot);
    // the Create button's menu
    void setCreateMenu(QMenu* menu);
    // a short note at the right (an upload's progress); empty clears it
    void setStatus(const QString& text);

signals:
    void toolChanged(Tool tool);
    void localSpaceToggled(bool local);
    void pivotToggled(bool pivot);
    void snapToggled(bool on);
    void gridToggled(bool on);
    void exitRequested();

private:
    QAction* addTool(const QString& text, const QString& shortcutHint, Tool tool);

    Tool _tool { Tool::Move };
    QActionGroup* _tools { nullptr };
    QAction* _spaceAction { nullptr };
    QAction* _pivotAction { nullptr };
    QAction* _snapAction { nullptr };
    QAction* _spacerAction { nullptr };
    QAction* _gridAction { nullptr };
    QToolButton* _createButton { nullptr };
    QLabel* _status { nullptr };
};

#endif // hifi_EditorToolBar_h
