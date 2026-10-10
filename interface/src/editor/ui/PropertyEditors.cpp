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
#include <QtCore/QRegularExpression>
#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QVector2D>
#include <QtGui/QVector3D>
#include <QtWidgets/QApplication>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMenu>
#include <QtWidgets/QVBoxLayout>

#include <glm/glm.hpp>

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

    // a number as people write it: up to four decimals, no trailing zeros
    QString formatNumber(double value) {
        QString text = QString::number(value, 'f', 4);
        if (text.contains('.')) {
            while (text.endsWith('0')) {
                text.chop(1);
            }
            if (text.endsWith('.')) {
                text.chop(1);
            }
        }
        return text == "-0" ? "0" : text;
    }

    // every number in a piece of text, in order: "1, 2, 3", "(1 2 3)" and {"x":1,"y":2,"z":3} all work
    QVector<double> numbersIn(const QString& text) {
        static const QRegularExpression NUMBER("[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?");
        QVector<double> numbers;
        auto matches = NUMBER.globalMatch(text);
        while (matches.hasNext()) {
            numbers << matches.next().captured(0).toDouble();
        }
        return numbers;
    }
}

// --- clipboard values ---------------------------------------------------------------------------

QString PropertyEditor::valueToText(const PropertySpec& spec, const QVariant& value) {
    switch (spec.kind) {
        case PropertyKind::Vec2:
        case PropertyKind::Polar: {
            QVector2D v = value.value<QVector2D>();
            return formatNumber(v.x()) + ", " + formatNumber(v.y());
        }
        case PropertyKind::Vec3:
        case PropertyKind::Euler: {
            QVector3D v = value.value<QVector3D>();
            return formatNumber(v.x()) + ", " + formatNumber(v.y()) + ", " + formatNumber(v.z());
        }
        case PropertyKind::Rect: {
            QRect r = value.toRect();
            return QString("%1, %2, %3, %4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        }
        case PropertyKind::Color:
        case PropertyKind::FloatColor:
            return value.value<QColor>().name().toUpper();
        case PropertyKind::Bool:
            return value.toBool() ? "true" : "false";
        case PropertyKind::Int:
        case PropertyKind::Float:
        case PropertyKind::Degrees:
            return formatNumber(value.toDouble());
        default:
            return value.toString();
    }
}

bool PropertyEditor::textToValue(const PropertySpec& spec, const QString& text, QVariant& value) {
    QString trimmed = text.trimmed();
    QVector<double> numbers = numbersIn(trimmed);
    switch (spec.kind) {
        case PropertyKind::Vec2:
        case PropertyKind::Polar:
            if (numbers.size() < 2) {
                return false;
            }
            value = QVariant(QVector2D((float)numbers[0], (float)numbers[1]));
            return true;
        case PropertyKind::Vec3:
        case PropertyKind::Euler:
            if (numbers.size() < 3) {
                return false;
            }
            value = QVariant(QVector3D((float)numbers[0], (float)numbers[1], (float)numbers[2]));
            return true;
        case PropertyKind::Rect:
            if (numbers.size() < 4) {
                return false;
            }
            value = QVariant(QRect((int)numbers[0], (int)numbers[1], (int)numbers[2], (int)numbers[3]));
            return true;
        case PropertyKind::Color:
        case PropertyKind::FloatColor: {
            static const QRegularExpression HEX("^#?([0-9a-fA-F]{6})$");
            auto hex = HEX.match(trimmed);
            if (hex.hasMatch()) {
                value = QVariant(QColor("#" + hex.captured(1)));
                return true;
            }
            if (numbers.size() == 3) {
                value = QVariant(QColor(glm::clamp((int)numbers[0], 0, 255), glm::clamp((int)numbers[1], 0, 255),
                                        glm::clamp((int)numbers[2], 0, 255)));
                return true;
            }
            return false;
        }
        case PropertyKind::Bool: {
            QString word = trimmed.toLower();
            if (word == "true" || word == "yes" || word == "on" || word == "1") {
                value = QVariant(true);
                return true;
            }
            if (word == "false" || word == "no" || word == "off" || word == "0") {
                value = QVariant(false);
                return true;
            }
            return false;
        }
        case PropertyKind::Int:
        case PropertyKind::Float:
        case PropertyKind::Degrees: {
            bool ok = false;
            double number = trimmed.toDouble(&ok);
            if (!ok) {
                return false;
            }
            value = QVariant(glm::clamp(number, spec.min, spec.max));
            return true;
        }
        case PropertyKind::Enum:
            for (const auto& entry : spec.enumValues) {
                int colon = entry.indexOf(':');
                QString entryValue = colon < 0 ? entry : entry.left(colon);
                QString entryLabel = colon < 0 ? entry : entry.mid(colon + 1);
                if (trimmed.compare(entryValue, Qt::CaseInsensitive) == 0
                    || trimmed.compare(entryLabel, Qt::CaseInsensitive) == 0) {
                    value = QVariant(entryValue);
                    return true;
                }
            }
            return false;
        case PropertyKind::Json: {
            QJsonParseError error;
            QJsonDocument::fromJson(trimmed.toUtf8(), &error);
            if (!trimmed.isEmpty() && error.error != QJsonParseError::NoError) {
                return false;
            }
            value = QVariant(trimmed);
            return true;
        }
        default:
            value = QVariant(trimmed);
            return true;
    }
}

void PropertyEditor::showValueMenu(const QPoint& globalPos) {
    QMenu menu;
    QAction* copy = menu.addAction("Copy " + _spec.label);
    copy->setEnabled(!isMixed());
    QVariant pasted;
    bool canPaste = !_spec.readOnly && isEnabled()
        && textToValue(_spec, QApplication::clipboard()->text(), pasted);
    QAction* paste = menu.addAction("Paste " + _spec.label);
    paste->setEnabled(canPaste);
    QAction* chosen = menu.exec(globalPos);
    if (chosen == copy) {
        QApplication::clipboard()->setText(valueToText(_spec, currentValue()));
    } else if (chosen == paste && canPaste) {
        setValue(pasted, false);
        emit committed(pasted);
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

QSize ScrubSpinBox::sizeHint() const {
    QSize size = QDoubleSpinBox::sizeHint();
    size.setWidth(72);
    return size;
}

QSize ScrubSpinBox::minimumSizeHint() const {
    QSize size = QDoubleSpinBox::minimumSizeHint();
    size.setWidth(44);
    return size;
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
    // vector rows carry their unit on the label, since their fields are too small for a suffix
    bool vector = _spec.kind == PropertyKind::Vec2 || _spec.kind == PropertyKind::Vec3 || _spec.kind == PropertyKind::Euler
        || _spec.kind == PropertyKind::Polar || _spec.kind == PropertyKind::Rect;
    QString text = (vector && !_spec.unit.isEmpty()) ? _spec.label + " (" + _spec.unit + ")" : _spec.label;
    return new QLabel(text, parent);
}

PropertyEditor* PropertyEditor::create(const PropertySpec& spec, QWidget* parent) {
    switch (spec.kind) {
        case PropertyKind::Bool: return new BoolEditor(spec, parent);
        case PropertyKind::Int:
        case PropertyKind::Float:
        case PropertyKind::Degrees: {
            auto editor = new NumberEditor(spec, parent);
            editor->setMaximumWidth(130);   // a lone number does not need the whole row
            return editor;
        }
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

NumberEditor::NumberEditor(const PropertySpec& spec, QWidget* parent, bool component) : PropertyEditor(spec, parent), _component(component) {
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _spin = new ScrubSpinBox(this);
    _spin->setDecimals(spec.kind == PropertyKind::Int ? 0 : spec.decimals);
    _spin->setRange(spec.min <= -HUGE_RANGE ? -HUGE_RANGE : spec.min, spec.max >= HUGE_RANGE ? HUGE_RANGE : spec.max);
    _spin->setSingleStep(spec.step);
    if (!spec.unit.isEmpty() && !_component) {
        _spin->setSuffix(" " + spec.unit);
    }
    _spin->setEnabled(!spec.readOnly);
    _spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
        // a read-only field (the world is not editable, or the property never is) does not scrub
        if (!isEnabled() || !_spin->isEnabled()) {
            return;
        }
        _scrubbing = true;
        _scrubStart = _spin->value();
        _scrubAccumulated = _scrubStart;
        emit editBegan();
    });
    connect(label, &ScrubLabel::scrubbed, this, [this](int deltaPixels, Qt::KeyboardModifiers modifiers) {
        if (!_scrubbing) {
            return;
        }
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
        if (!_scrubbing) {
            return;
        }
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
        component.decimals = std::min(spec.decimals, 3);
        auto editor = new NumberEditor(component, this, true);
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

bool VectorEditor::isMixed() const {
    for (auto component : _components) {
        if (component->isMixed()) {
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
    _mixed = mixed;
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
    _mixed = mixed;
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
    connect(_button, &QToolButton::clicked, this, &ColorEditor::openPicker);
    updateSwatch();
}

void ColorEditor::openPicker() {
    if (_dialog) {
        _dialog->raise();
        _dialog->activateWindow();
        return;
    }
    // Qt's own picker rather than macOS's colour panel: the native panel's OK button did not
    // register on this macOS, so every pick came back as a cancel and nothing changed
    auto dialog = new QColorDialog(_mixed ? QColor(Qt::white) : _color, this);
    dialog->setOption(QColorDialog::DontUseNativeDialog, true);
    dialog->setWindowTitle(_spec.label);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // the selection cannot change under an open picker
    dialog->setWindowModality(Qt::ApplicationModal);
    _dialog = dialog;
    _original = _color;
    _originalMixed = _mixed;
    emit editBegan();
    connect(dialog, &QColorDialog::currentColorChanged, this, [this](const QColor& color) {
        if (!color.isValid()) {
            return;
        }
        _color = color;
        _mixed = false;
        updateSwatch();
        emit liveValue(QVariant(color));
    });
    connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
        if (result == QDialog::Accepted) {
            QColor chosen = dialog->selectedColor();
            if (chosen.isValid()) {
                _color = chosen;
                _mixed = false;
            }
            updateSwatch();
            emit committed(QVariant(_color));
        } else {
            _color = _original;
            _mixed = _originalMixed;
            updateSwatch();
            emit editCancelled();
        }
    });
    dialog->show();
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
    if (_dialog) {
        return;   // the picker is open: what it shows is the truth until it closes
    }
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
    _mixed = mixed;
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
