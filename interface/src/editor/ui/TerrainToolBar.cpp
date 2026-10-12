//
//  TerrainToolBar.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TerrainToolBar.h"

#include <cmath>
#include <memory>

#include <QtWidgets/QAction>
#include <QtWidgets/QActionGroup>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpinBox>

#include "PropertyEditors.h"

namespace {
    const float MIN_RADIUS = 0.1f;      // metres
    const float MAX_RADIUS = 100.0f;
}

TerrainToolBar::TerrainToolBar(QWidget* parent) : QToolBar("Terrain", parent) {
    setObjectName("EditorTerrainBar");
    setMovable(false);
    setFloatable(false);
    setAllowedAreas(Qt::TopToolBarArea);
    setToolButtonStyle(Qt::ToolButtonTextOnly);

    _modes = new QActionGroup(this);
    _modes->setExclusive(true);
    addMode("Raise", "Raise the ground under the brush; hold Shift to lower it", terrain::SculptMode::Raise)->setChecked(true);
    addMode("Lower", "Lower the ground under the brush; hold Shift to raise it", terrain::SculptMode::Lower);
    addMode("Smooth", "Smooth the ground under the brush", terrain::SculptMode::Smooth);
    addMode("Flatten", "Pull the ground to the height where the stroke began", terrain::SculptMode::Flatten);
    addMode("Dig", "Carve the brush's sphere out of the ground; hold Shift to fill instead", terrain::SculptMode::Dig);
    addMode("Fill", "Add the brush's sphere to the ground; hold Shift to dig instead", terrain::SculptMode::Fill);
    addMode("Paint", "Set the material of the ground under the brush", terrain::SculptMode::Paint);
    addSeparator();

    auto addField = [this](const QString& label, QAbstractSpinBox* box, double scrubStep, bool proportional, const QString& toolTip) {
        auto name = new ScrubLabel(label, this);
        name->setObjectName("EditorFieldName");
        name->setToolTip("Drag sideways to change it (Shift: faster, Alt: finer)");
        addWidget(name);
        box->setToolTip(toolTip);
        box->setFocusPolicy(Qt::ClickFocus);   // typing elsewhere never lands here; a click does
        addWidget(box);
        attachScrub(name, box, scrubStep, proportional);
    };
    _size = new QDoubleSpinBox(this);
    _size->setRange(MIN_RADIUS, MAX_RADIUS);
    _size->setDecimals(1);
    _size->setSingleStep(0.5);
    _size->setSuffix(" m");
    _size->setValue(3.0);
    addField("Size", _size, 0.01, true, "The brush's radius ([ and ] change it)");

    _strength = new QDoubleSpinBox(this);
    _strength->setRange(1.0, 100.0);
    _strength->setDecimals(0);
    _strength->setSingleStep(5.0);
    _strength->setSuffix(" %");
    _strength->setValue(50.0);
    addField("Strength", _strength, 0.5, false, "How fast Raise, Lower, Smooth and Flatten work (Dig and Fill take the whole sphere)");

    _hardness = new QDoubleSpinBox(this);
    _hardness->setRange(0.0, 100.0);
    _hardness->setDecimals(0);
    _hardness->setSingleStep(5.0);
    _hardness->setSuffix(" %");
    _hardness->setValue(50.0);
    addField("Hardness", _hardness, 0.5, false, "The part of the brush with its full effect, before the soft edge");

    _material = new QSpinBox(this);
    _material->setRange(0, 7);
    _material->setValue(0);
    addField("Material", _material, 0.05, false, "The material new ground gets, and Paint paints");

    for (auto box : { _size, _strength, _hardness }) {
        connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TerrainToolBar::brushChanged);
    }
    connect(_material, QOverload<int>::of(&QSpinBox::valueChanged), this, &TerrainToolBar::brushChanged);

    auto hint = new QLabel("Drag on the ground to sculpt it  ·  Shift inverts  ·  Esc cancels the stroke", this);
    hint->setObjectName("EditorHint");
    addWidget(hint);
}

void TerrainToolBar::attachScrub(ScrubLabel* label, QAbstractSpinBox* box, double step, bool proportional) {
    const double COARSE = 10.0;
    const double FINE = 0.1;
    auto doubleBox = qobject_cast<QDoubleSpinBox*>(box);
    auto intBox = qobject_cast<QSpinBox*>(box);
    auto accumulated = std::make_shared<double>(0.0);   // the value as dragged, before the field rounds it
    connect(label, &ScrubLabel::scrubBegan, this, [=] {
        box->interpretText();   // anything half-typed counts first
        *accumulated = doubleBox ? doubleBox->value() : (double)intBox->value();
    });
    connect(label, &ScrubLabel::scrubbed, this, [=](int deltaPixels, Qt::KeyboardModifiers modifiers) {
        double perPixel = step;
        if (proportional) {
            perPixel *= std::max(std::abs(*accumulated), 0.5);
        }
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            perPixel *= COARSE;
        } else if (modifiers.testFlag(Qt::AltModifier)) {
            perPixel *= FINE;
        }
        if (doubleBox) {
            *accumulated = qBound(doubleBox->minimum(), *accumulated + deltaPixels * perPixel, doubleBox->maximum());
            doubleBox->setValue(*accumulated);
        } else if (intBox) {
            *accumulated = qBound((double)intBox->minimum(), *accumulated + deltaPixels * perPixel, (double)intBox->maximum());
            intBox->setValue((int)std::lround(*accumulated));
        }
    });
}

QAction* TerrainToolBar::addMode(const QString& text, const QString& toolTip, terrain::SculptMode mode) {
    auto action = addAction(text);
    action->setCheckable(true);
    action->setToolTip(toolTip);
    action->setData((int)mode);
    _modes->addAction(action);
    connect(action, &QAction::triggered, this, &TerrainToolBar::brushChanged);
    return action;
}

EditorTerrainBrush TerrainToolBar::brush() const {
    EditorTerrainBrush brush;
    if (auto checked = _modes->checkedAction()) {
        brush.mode = (terrain::SculptMode)checked->data().toInt();
    }
    brush.radius = (float)_size->value();
    brush.strength = (float)_strength->value() / 100.0f;
    brush.hardness = (float)_hardness->value() / 100.0f;
    brush.material = _material->value();
    return brush;
}

void TerrainToolBar::setMode(terrain::SculptMode mode) {
    for (auto action : _modes->actions()) {
        if ((terrain::SculptMode)action->data().toInt() == mode) {
            action->setChecked(true);
            emit brushChanged();
            return;
        }
    }
}

void TerrainToolBar::setRadius(float metres) {
    _size->setValue(glm::clamp(metres, MIN_RADIUS, MAX_RADIUS));   // valueChanged announces it
}
