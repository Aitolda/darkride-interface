//
//  InspectorPanel.h
//  interface/src/editor/ui
//
//  The Inspector dock: the selected entities' properties in sections, generated from the
//  property table. Multi-selection shows "—" where values differ and applies an edit to all.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_InspectorPanel_h
#define hifi_InspectorPanel_h

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtCore/QVector>
#include <QtWidgets/QDockWidget>

#include <EntityItemProperties.h>
#include <EntityTypes.h>

class QLabel;
class QScrollArea;
class QVBoxLayout;
class QWidget;
class PropertyEditor;
struct PropertySpec;

class InspectorPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    // nothing selected: a note instead of fields
    void setSummary(const QString& text);
    // the selection: one snapshot per entity, the common type (or multiple), and the title line
    void showEntities(const QVector<EntityItemProperties>& snapshots, EntityTypes::EntityType type, bool multiple,
        const QString& title);
    // fresh values for the same selection; fields being edited are left alone
    void refreshValues(const QVector<EntityItemProperties>& snapshots);

signals:
    void editBegan(const QString& key);
    void liveEdit(const QString& key, const QVariant& value);
    void committed(const QString& key, const QVariant& value);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Row {
        const PropertySpec* spec { nullptr };
        QWidget* label { nullptr };
        PropertyEditor* editor { nullptr };
    };

    void rebuild(const QStringList& sections);
    void applyValues(const QVector<EntityItemProperties>& snapshots, bool force);
    void applyVisibility(const EntityItemProperties& first);
    bool conditionHolds(const QString& showWhen, const EntityItemProperties& first) const;
    QWidget* buildSection(const QString& sectionId, QWidget* parent);

    QScrollArea* _scroll { nullptr };
    QWidget* _empty { nullptr };
    QLabel* _emptyLabel { nullptr };
    QWidget* _content { nullptr };
    QVBoxLayout* _contentLayout { nullptr };
    QLabel* _title { nullptr };
    QString _layoutKey;
    QHash<QString, Row> _rows;
    QHash<QString, bool> _collapsed;
};

#endif // hifi_InspectorPanel_h
