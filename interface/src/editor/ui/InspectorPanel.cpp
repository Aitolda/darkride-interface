//
//  InspectorPanel.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "InspectorPanel.h"

#include <QtCore/QUuid>
#include <QtGui/QResizeEvent>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include "../PropertySpec.h"
#include "PropertyEditors.h"

namespace {
    const QString NULL_UUID = QUuid().toString(QUuid::WithoutBraces);

    // the value of a property as text, for the show-when rules
    QString valueText(const QVariant& value) {
        if (value.type() == QVariant::Bool) {
            return value.toBool() ? "true" : "false";
        }
        return value.toString();
    }
}

InspectorPanel::InspectorPanel(QWidget* parent) : QDockWidget("Inspector", parent) {
    setObjectName("EditorInspector");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable);   // fixed in place, see HierarchyPanel
    setMinimumWidth(340);   // room for a label and three vector fields

    auto body = new QWidget(this);
    auto bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    _banner = new QLabel(body);
    _banner->setObjectName("EditorReadOnlyBanner");
    _banner->setWordWrap(true);
    _banner->hide();
    bodyLayout->addWidget(_banner);

    _scroll = new QScrollArea(body);
    _scroll->setWidgetResizable(true);
    _scroll->setFrameShape(QFrame::NoFrame);
    _scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyLayout->addWidget(_scroll, 1);

    _empty = new QWidget(_scroll);
    auto emptyLayout = new QVBoxLayout(_empty);
    _emptyLabel = new QLabel("Nothing selected", _empty);
    _emptyLabel->setAlignment(Qt::AlignCenter);
    _emptyLabel->setObjectName("EditorInspectorEmpty");
    emptyLayout->addWidget(_emptyLabel);

    _content = new QWidget(_scroll);
    _contentLayout = new QVBoxLayout(_content);
    _contentLayout->setContentsMargins(6, 6, 18, 6);   // the overlay scroll bar draws over the right edge
    _contentLayout->setSpacing(4);
    _title = new QLabel(_content);
    _title->setObjectName("EditorInspectorTitle");
    _title->setWordWrap(true);
    _contentLayout->addWidget(_title);
    _contentLayout->addStretch(1);

    _scroll->setWidget(_empty);
    _scroll->viewport()->installEventFilter(this);
    setWidget(body);
}

void InspectorPanel::setReadOnly(bool readOnly, const QString& reason) {
    _readOnly = readOnly;
    _banner->setText(reason);
    _banner->setVisible(readOnly);
    applyReadOnly();
}

void InspectorPanel::applyReadOnly() {
    // the fields grey out; the labels stay live so Copy still works on them (they do not scrub)
    for (auto& row : _rows) {
        row.editor->setEnabled(!_readOnly);
    }
}

bool InspectorPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == _scroll->viewport() && event->type() == QEvent::Resize) {
        int width = _scroll->viewport()->width();
        if (width > 0) {
            _content->setFixedWidth(width);
            _empty->setFixedWidth(width);
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void InspectorPanel::setSummary(const QString& text) {
    _emptyLabel->setText(text);
    if (_scroll->widget() != _empty) {
        _scroll->takeWidget();
        _scroll->setWidget(_empty);
    }
    _layoutKey.clear();
}

void InspectorPanel::showEntities(const QVector<EntityItemProperties>& snapshots, EntityTypes::EntityType type,
                                  bool multiple, const QString& title) {
    if (snapshots.isEmpty()) {
        setSummary("Nothing selected");
        return;
    }
    QString key = QString("%1/%2").arg((int)type).arg(multiple ? 1 : 0);
    if (key != _layoutKey) {
        _layoutKey = key;
        rebuild(PropertySpecs::sectionsForType(type, multiple));
    }
    _title->setText(title);
    if (_scroll->widget() != _content) {
        _scroll->takeWidget();
        _scroll->setWidget(_content);
    }
    applyValues(snapshots, true);
}

void InspectorPanel::refreshValues(const QVector<EntityItemProperties>& snapshots) {
    if (_scroll->widget() == _content && !snapshots.isEmpty()) {
        applyValues(snapshots, false);
    }
}

void InspectorPanel::rebuild(const QStringList& sections) {
    // everything but the title and the stretch goes
    for (auto& row : _rows) {
        delete row.label;
        delete row.editor;
    }
    _rows.clear();
    while (_contentLayout->count() > 2) {
        auto item = _contentLayout->takeAt(1);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    int position = 1;
    for (const auto& sectionId : sections) {
        if (auto widget = buildSection(sectionId, _content)) {
            _contentLayout->insertWidget(position++, widget);
        }
    }
    applyReadOnly();
}

QWidget* InspectorPanel::buildSection(const QString& sectionId, QWidget* parent) {
    auto specs = PropertySpecs::forSection(sectionId);
    auto section = PropertySpecs::section(sectionId);
    if (specs.isEmpty() || !section) {
        return nullptr;
    }
    auto frame = new QWidget(parent);
    frame->setObjectName(section->minor ? "EditorInspectorMinorSection" : "EditorInspectorSection");
    auto frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->setSpacing(0);

    auto body = new QWidget(frame);
    body->setObjectName("EditorInspectorBody");
    auto grid = new QGridLayout(body);
    grid->setContentsMargins(8, 4, 4, 6);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);

    // sections other than the top one fold
    if (!section->label.isEmpty()) {
        auto header = new QToolButton(frame);
        header->setObjectName("EditorInspectorHeader");
        header->setText(section->label);
        header->setCheckable(true);
        header->setChecked(!_collapsed.value(sectionId, false));
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setArrowType(header->isChecked() ? Qt::DownArrow : Qt::RightArrow);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        body->setVisible(header->isChecked());
        connect(header, &QToolButton::toggled, this, [this, sectionId, header, body](bool open) {
            header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
            body->setVisible(open);
            _collapsed[sectionId] = !open;
        });
        frameLayout->addWidget(header);
    }
    frameLayout->addWidget(body);

    int rowIndex = 0;
    for (auto spec : specs) {
        auto editor = PropertyEditor::create(*spec, body);
        auto label = editor->createLabel(body);
        label->setObjectName("EditorInspectorLabel");
        bool tall = spec->kind == PropertyKind::Json || spec->kind == PropertyKind::CollisionMask;
        grid->addWidget(label, rowIndex, 0, Qt::AlignLeft | (tall ? Qt::AlignTop : Qt::AlignVCenter));
        grid->addWidget(editor, rowIndex, 1);
        ++rowIndex;

        QString key = spec->key;
        connect(editor, &PropertyEditor::editBegan, this, [this, key] { emit editBegan(key); });
        connect(editor, &PropertyEditor::liveValue, this, [this, key](const QVariant& v) { emit liveEdit(key, v); });
        connect(editor, &PropertyEditor::committed, this, [this, key](const QVariant& v) { emit committed(key, v); });
        connect(editor, &PropertyEditor::editCancelled, this, [this, key] { emit editCancelled(key); });
        // right-click on a label: Copy and Paste the row's value, as Unity does
        label->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(label, &QWidget::customContextMenuRequested, editor, [editor, label](const QPoint& pos) {
            editor->showValueMenu(label->mapToGlobal(pos));
        });
        _rows.insert(key, Row { spec, label, editor });
    }
    return frame;
}

void InspectorPanel::applyValues(const QVector<EntityItemProperties>& snapshots, bool force) {
    for (auto it = _rows.begin(); it != _rows.end(); ++it) {
        Row& row = it.value();
        if (!force && row.editor->isEditing()) {
            continue;
        }
        QVariant first = PropertySpecs::get(it.key(), snapshots.first());
        // a value not known yet (a model's scale before the model has loaded) shows as "—"
        bool mixed = !first.isValid();
        for (int i = 1; i < snapshots.size() && !mixed; ++i) {
            mixed = PropertySpecs::get(it.key(), snapshots[i]) != first;
        }
        row.editor->setValue(first, mixed);
    }
    applyVisibility(snapshots.first());
}

bool InspectorPanel::conditionHolds(const QString& showWhen, const EntityItemProperties& first) const {
    if (showWhen.isEmpty()) {
        return true;
    }
    int orAt = showWhen.indexOf(" or ");
    if (orAt > 0) {
        return conditionHolds(showWhen.left(orAt), first) || conditionHolds(showWhen.mid(orAt + 4), first);
    }
    int notIn = showWhen.indexOf(" not in ");
    if (notIn > 0) {
        QString value = valueText(PropertySpecs::get(showWhen.left(notIn), first));
        return !showWhen.mid(notIn + 8).split(',').contains(value);
    }
    int in = showWhen.indexOf(" in ");
    if (in > 0) {
        QString value = valueText(PropertySpecs::get(showWhen.left(in), first));
        return showWhen.mid(in + 4).split(',').contains(value);
    }
    int notEqual = showWhen.indexOf("!=");
    if (notEqual > 0) {
        QString value = valueText(PropertySpecs::get(showWhen.left(notEqual), first));
        QString expected = showWhen.mid(notEqual + 2);
        if (expected.isEmpty()) {
            return !value.isEmpty() && value != NULL_UUID;   // "has a value"
        }
        return value != expected;
    }
    int equal = showWhen.indexOf('=');
    if (equal > 0) {
        QString value = valueText(PropertySpecs::get(showWhen.left(equal), first));
        return value == showWhen.mid(equal + 1);
    }
    return true;
}

void InspectorPanel::applyVisibility(const EntityItemProperties& first) {
    for (auto it = _rows.begin(); it != _rows.end(); ++it) {
        bool shown = conditionHolds(it->spec->showWhen, first);
        it->label->setVisible(shown);
        it->editor->setVisible(shown);
    }
}
