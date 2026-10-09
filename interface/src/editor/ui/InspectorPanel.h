//
//  InspectorPanel.h
//  interface/src/editor/ui
//
//  The Inspector dock: the selected entities' properties, in sections.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_InspectorPanel_h
#define hifi_InspectorPanel_h

#include <QtWidgets/QDockWidget>

class QLabel;
class QScrollArea;

class InspectorPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    // the panel shows this widget, or the "Nothing selected" note when it is null
    void setContent(QWidget* content);
    // the note shown while there is no content: what is selected, in a line or two
    void setSummary(const QString& text);

private:
    QScrollArea* _scroll { nullptr };
    QLabel* _empty { nullptr };
};

#endif // hifi_InspectorPanel_h
