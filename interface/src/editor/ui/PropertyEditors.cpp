//
//  PropertyEditors.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "PropertyEditors.h"

#include <cmath>

#include <QtCore/QEvent>
#include <QtCore/QJsonDocument>
#include <QtCore/QRect>
#include <QtGui/QColor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QVector2D>
#include <QtGui/QVector3D>
#include <QtWidgets/QApplication>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>

namespace {
    const int SCRUB_SLOP_PIXELS = 3;
    const double SCRUB_FINE = 0.1;      // Alt
    const double SCRUB_COARSE = 10.0;   // Shift
    const double HUGE_RANGE = 1.0e9;
    const QString MIXED = QStringLiteral("—");

    const char* const COLLISION_GROUPS[] = { "static", "dynamic", "kinematic", "myAvatar", "otherAvatar" };
    const char* const COLLISION_LABELS[] = { "Static", "Dynamic", "Kinematic", "My Avatar", "Other Avatars" };

    // the component labels for each vector kind
    QStringList componentLabels(PropertyKind kind) {
        switch (kind) {
            case PropertyKind::Vec2: return { "X", "Y" };
            case PropertyKind::Polar: return { "Elevation", "Azimuth" };
            case PropertyKind::Rect: return { "X", "Y", "W", "H" };
            default: return { "X", "Y", "Z" };
        }
    }
}

// --- ScrubLabel ---------------------------------------------------------------------------------

ScrubLabel::ScrubLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setCursor(Qt::SizeHorCursor);
}

void ScrubLabel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        _pressed = true;
        _scrubbing = false;
        _pressX = _lastX = event->globalX();
    }
    QLabel::mousePressEvent(event);
}

void ScrubLabel::mouseMoveEvent(QMouseEvent* event) {
    if (!_pressed) {
        QLabel::mouseMoveEvent(event);
        return;
    }
    int x = event->globalX();
    if (!_scrubbing) {
        if (std::abs(x - _pressX) < SCRUB_SLOP_PIXELS) {
            return;
        }
        _scrubbing = true;
        _lastX = x;
        emit scrubBegan();
    }
    if (x != _lastX) {
        emit scrubbed(x - _lastX, event->modifiers());
        _lastX = x;
    }
}

void ScrubLabel::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && _pressed) {
        _pressed = false;
        if (_scrubbing) {
            _scrubbing = false;
            emit scrubEnded();
        }
    }
    QLabel::mouseReleaseEvent(event);
}

// --- ScrubSpinBox -------------------------------------------------------------------------------

ScrubSpinBox::ScrubSpinBox(QWidget* parent) : QDoubleSpinBox(parent) {
    setKeyboardTracking(false);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setAlignment(Qt::AlignRight);
}

void ScrubSpinBox::showMixed() {
    _mixed = true;
    lineEdit()->setText(MIXED);
}

void ScrubSpinBox::focusInEvent(QFocusEvent* event) {
    if (_mixed) {
        _mixed = false;
        lineEdit()->setText(textFromValue(value()));
    }
    QDoubleSpinBox::focusInEvent(event);
}

// --- PropertyEditor -----------------------------------------------------------------------------

PropertyEditor::PropertyEditor(const PropertySpec& spec, QWidget* parent) : QWidget(parent), _spec(spec) {
}

QWidget* PropertyEditor::createLabel(QWidget* parent) {
    return new QLabel(_spec.label, parent);
}

PropertyEditor* PropertyEditor::create(const PropertySpec& spec, QWidget* parent) {
    switch (spec.kind) {
        case PropertyKind::Bool: return new BoolEditor(spec, parent);
        case PropertyKind::Int:
        case PropertyKind::Float:
        case PropertyKind::Degrees: return new NumberEditor(spec, parent);
        case PropertyKind::String:
        case PropertyKind::Url: return new TextEditor(spec, parent);
        case PropertyKind::Json: return new JsonEditor(spec, parent);
        case PropertyKind::Color:
        case PropertyKind::FloatColor: return new ColorEditor(spec, parent);
        case PropertyKind::Vec2:
        case PropertyKind::Vec3:
        case PropertyKind::Euler:
        case PropertyKind::Polar:
        case PropertyKind::Rect: return new VectorEditor(spec, parent);
        case PropertyKind::Enum: return new EnumEditor(spec, parent);
        case PropertyKind::CollisionMask: return new CollisionMaskEditor(spec, parent);
    }
    return new TextEditor(spec, parent);
}

// --- NumberEditor -------------------------------------------------------------------------------

NumberEditor::NumberEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _spin = new ScrubSpinBox(this);
    _spin->setDecimals(spec.kind == PropertyKind::Int ? 0 : spec.decimals);
    _spin->setRange(spec.min <= -HUGE_RANGE ? -HUGE_RANGE : spec.min, spec.max >= HUGE_RANGE ? HUGE_RANGE : spec.max);
    _spin->setSingleStep(spec.step);
    if (!spec.unit.isEmpty()) {
        _spin->setSuffix(" " + spec.unit);
    }
    _spin->setEnabled(!spec.readOnly);
    layout->addWidget(_spin);
    connect(_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (!_updating && !_scrubbing) {
            emit committed(QVariant(value));
        }
    });
}

QWidget* NumberEditor::createLabel(QWidget* parent) {
    auto label = new ScrubLabel(_spec.label, parent);
    attachScrubLabel(label);
    return label;
}

void NumberEditor::attachScrubLabel(ScrubLabel* label) {
    connect(label, &ScrubLabel::scrubBegan, this, [this] {
        _scrubbing = true;
        _scrubStart = _spin->value();
        _scrubAccumulated = _scrubStart;
        emit editBegan();
    });
    connect(label, &ScrubLabel::scrubbed, this, [this](int deltaPixels, Qt::KeyboardModifiers modifiers) {
        double step = _spec.step;
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            step *= SCRUB_COARSE;
        } else if (modifiers.testFlag(Qt::AltModifier)) {
            step *= SCRUB_FINE;
        }
        _scrubAccumulated += deltaPixels * step;
        _updating = true;
        _spin->setValue(_scrubAccumulated);
        _updating = false;
        emit liveValue(QVariant(_spin->value()));
    });
    connect(label, &ScrubLabel::scrubEnded, this, [this] {
        _scrubbing = false;
        emit committed(QVariant(_spin->value()));
    });
}

void NumberEditor::setValue(const QVariant& value, bool mixed) {
    _updating = true;
    _spin->setValue(value.toDouble());
    if (mixed) {
        _spin->showMixed();
    }
    _updating = false;
}

bool NumberEditor::isEditing() const {
    return _scrubbing || _spin->hasFocus();
}

// --- VectorEditor -------------------------------------------------------------------------------

VectorEditor::VectorEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    QStringList labels = componentLabels(spec.kind);
    for (const auto& text : labels) {
        PropertySpec component = spec;
        component.label = text;
        component.kind = spec.kind == PropertyKind::Rect ? PropertyKind::Int : PropertyKind::Float;
        auto editor = new NumberEditor(component, this);
        auto label = new ScrubLabel(text, this);
        label->setObjectName("EditorComponentLabel");
        editor->attachScrubLabel(label);
        layout->addWidget(label);
        layout->addWidget(editor, 1);
        connect(editor, &PropertyEditor::editBegan, this, &PropertyEditor::editBegan);
        connect(editor, &PropertyEditor::liveValue, this, [this] { emit liveValue(currentValue()); });
        connect(editor, &PropertyEditor::committed, this, [this] { emit committed(currentValue()); });
        _components << editor;
    }
}

QVariant VectorEditor::currentValue() const {
    switch (_spec.kind) {
        case PropertyKind::Vec2:
        case PropertyKind::Polar:
            return QVariant(QVector2D((float)_components[0]->value(), (float)_components[1]->value()));
        case PropertyKind::Rect:
            return QVariant(QRect((int)_components[0]->value(), (int)_components[1]->value(),
                                  (int)_components[2]->value(), (int)_components[3]->value()));
        default:
            return QVariant(QVector3D((float)_components[0]->value(), (float)_components[1]->value(), (float)_components[2]->value()));
    }
}

void VectorEditor::setValue(const QVariant& value, bool mixed) {
    QVector<double> parts;
    switch (_spec.kind) {
        case PropertyKind::Vec2:
        case PropertyKind::Polar: {
            QVector2D v = value.value<QVector2D>();
            parts = { v.x(), v.y() };
            break;
        }
        case PropertyKind::Rect: {
            QRect r = value.toRect();
            parts = { (double)r.x(), (double)r.y(), (double)r.width(), (double)r.height() };
            break;
        }
        default: {
            QVector3D v = value.value<QVector3D>();
            parts = { v.x(), v.y(), v.z() };
            break;
        }
    }
    for (int i = 0; i < _components.size() && i < parts.size(); ++i) {
        if (!_components[i]->isEditing()) {
            _components[i]->setValue(QVariant(parts[i]), mixed);
        }
    }
}

bool VectorEditor::isEditing() const {
    for (auto component : _components) {
        if (component->isEditing()) {
            return true;
        }
    }
    return false;
}

// --- BoolEditor ---------------------------------------------------------------------------------

BoolEditor::BoolEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _check = new QCheckBox(this);
    _check->setEnabled(!spec.readOnly);
    layout->addWidget(_check);
    layout->addStretch(1);
    connect(_check, &QCheckBox::clicked, this, [this](bool checked) {
        _check->setTristate(false);
        emit committed(QVariant(checked));
    });
}

void BoolEditor::setValue(const QVariant& value, bool mixed) {
    _updating = true;
    if (mixed) {
        _check->setTristate(true);
        _check->setCheckState(Qt::PartiallyChecked);
    } else {
        _check->setTristate(false);
        _check->setChecked(value.toBool());
    }
    _updating = false;
}

// --- TextEditor ---------------------------------------------------------------------------------

TextEditor::TextEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _edit = new QLineEdit(this);
    _edit->setReadOnly(spec.readOnly);
    if (spec.kind == PropertyKind::Url) {
        _edit->setPlaceholderText("URL");
    }
    layout->addWidget(_edit);
    connect(_edit, &QLineEdit::editingFinished, this, [this] {
        if (!_updating && _edit->text() != _shown) {
            _shown = _edit->text();
            emit committed(QVariant(_shown));
        }
    });
}

void TextEditor::setValue(const QVariant& value, bool mixed) {
    _updating = true;
    _shown = mixed ? QString() : value.toString();
    _edit->setText(_shown);
    _edit->setPlaceholderText(mixed ? MIXED : (_spec.kind == PropertyKind::Url ? "URL" : QString()));
    _updating = false;
}

bool TextEditor::isEditing() const {
    return _edit->hasFocus();
}

// --- JsonEditor ---------------------------------------------------------------------------------

JsonEditor::JsonEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _edit = new QPlainTextEdit(this);
    _edit->setMaximumHeight(110);
    _edit->setTabStopDistance(20);
    _edit->setReadOnly(spec.readOnly);
    _edit->installEventFilter(this);
    layout->addWidget(_edit);
}

bool JsonEditor::eventFilter(QObject* watched, QEvent* event) {
    if (watched == _edit && event->type() == QEvent::FocusOut) {
        commitIfChanged();
    }
    return PropertyEditor::eventFilter(watched, event);
}

void JsonEditor::commitIfChanged() {
    QString text = _edit->toPlainText();
    if (_updating || text == _shown) {
        return;
    }
    QString trimmed = text.trimmed();
    if (!trimmed.isEmpty()) {
        QJsonParseError error;
        QJsonDocument::fromJson(trimmed.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            _edit->setStyleSheet("border: 1px solid #c0392b;");
            return;   // keep the text so it can be fixed; nothing is sent
        }
    }
    _edit->setStyleSheet(QString());
    _shown = text;
    emit committed(QVariant(text));
}

void JsonEditor::setValue(const QVariant& value, bool mixed) {
    _updating = true;
    _shown = mixed ? QString() : value.toString();
    _edit->setPlainText(_shown);
    _edit->setPlaceholderText(mixed ? MIXED : QString());
    _edit->setStyleSheet(QString());
    _updating = false;
}

bool JsonEditor::isEditing() const {
    return _edit->hasFocus();
}

// --- ColorEditor --------------------------------------------------------------------------------

ColorEditor::ColorEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _button = new QToolButton(this);
    _button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _button->setAutoRaise(false);
    _button->setEnabled(!spec.readOnly);
    _button->setMinimumWidth(120);
    layout->addWidget(_button);
    layout->addStretch(1);
    connect(_button, &QToolButton::clicked, this, [this] {
        QColor chosen = QColorDialog::getColor(_mixed ? Qt::white : _color, this, _spec.label);
        if (chosen.isValid()) {
            _color = chosen;
            _mixed = false;
            updateSwatch();
            emit committed(QVariant(chosen));
        }
    });
    updateSwatch();
}

void ColorEditor::updateSwatch() {
    QPixmap swatch(18, 18);
    swatch.fill(_mixed ? QColor("#666666") : _color);
    QPainter painter(&swatch);
    painter.setPen(QColor("#1b1b1b"));
    painter.drawRect(0, 0, 17, 17);
    painter.end();
    _button->setIcon(QIcon(swatch));
    _button->setText(_mixed ? MIXED : _color.name().toUpper());
}

void ColorEditor::setValue(const QVariant& value, bool mixed) {
    _color = value.value<QColor>();
    _mixed = mixed;
    updateSwatch();
}

// --- EnumEditor ---------------------------------------------------------------------------------

EnumEditor::EnumEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _combo = new QComboBox(this);
    _combo->setEnabled(!spec.readOnly);
    for (const auto& entry : spec.enumValues) {
        int colon = entry.indexOf(':');
        QString value = colon < 0 ? entry : entry.left(colon);
        QString label = colon < 0 ? entry : entry.mid(colon + 1);
        _combo->addItem(label, value);
    }
    layout->addWidget(_combo, 1);
    connect(_combo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (!_updating && index >= 0) {
            emit committed(_combo->itemData(index));
        }
    });
}

void EnumEditor::setValue(const QVariant& value, bool mixed) {
    _updating = true;
    if (mixed) {
        _combo->setCurrentIndex(-1);
    } else {
        QString text = value.toString();
        int index = _combo->findData(text);
        if (index < 0) {
            index = _combo->findData(text, Qt::UserRole, Qt::MatchFixedString);   // case-blind
        }
        _combo->setCurrentIndex(index);
    }
    _updating = false;
}

// --- CollisionMaskEditor ------------------------------------------------------------------------

CollisionMaskEditor::CollisionMaskEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    for (int i = 0; i < 5; ++i) {
        auto check = new QCheckBox(COLLISION_LABELS[i], this);
        layout->addWidget(check);
        connect(check, &QCheckBox::clicked, this, [this] { emit committed(QVariant(currentMask())); });
        _checks << check;
    }
}

QString CollisionMaskEditor::currentMask() const {
    QStringList groups;
    for (int i = 0; i < _checks.size(); ++i) {
        if (_checks[i]->isChecked()) {
            groups << COLLISION_GROUPS[i];
        }
    }
    return groups.join(",");
}

void CollisionMaskEditor::setValue(const QVariant& value, bool mixed) {
    _updating = true;
    QStringList groups = value.toString().split(',', QString::SkipEmptyParts);
    for (int i = 0; i < _checks.size(); ++i) {
        _checks[i]->setTristate(mixed);
        if (mixed) {
            _checks[i]->setCheckState(Qt::PartiallyChecked);
        } else {
            _checks[i]->setChecked(groups.contains(COLLISION_GROUPS[i]));
        }
    }
    _updating = false;
}
