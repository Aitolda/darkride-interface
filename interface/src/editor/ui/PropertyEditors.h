//
//  PropertyEditors.h
//  interface/src/editor/ui
//
//  The Inspector's field widgets, one per property kind: numbers with Unity's drag-to-scrub
//  labels, vectors, colours, enums, booleans, text, JSON and the collision mask.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_PropertyEditors_h
#define hifi_PropertyEditors_h

#include <QtCore/QVariant>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>

#include "../PropertySpec.h"

// A label that scrubs a value when dragged sideways, as Unity's numeric labels do.
class ScrubLabel : public QLabel {
    Q_OBJECT
public:
    explicit ScrubLabel(const QString& text, QWidget* parent = nullptr);
signals:
    void scrubBegan();
    void scrubbed(int deltaPixels, Qt::KeyboardModifiers modifiers);
    void scrubEnded();
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    bool _pressed { false };
    bool _scrubbing { false };
    int _lastX { 0 };
    int _pressX { 0 };
};

// A spin box that can show "—" for a mixed selection without losing its value.
class ScrubSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit ScrubSpinBox(QWidget* parent = nullptr);
    void showMixed();
    bool isMixedShown() const { return _mixed; }
protected:
    void focusInEvent(QFocusEvent* event) override;
private:
    bool _mixed { false };
};

// One editable property. The label is part of it: numeric kinds get a scrubbing label.
class PropertyEditor : public QWidget {
    Q_OBJECT
public:
    explicit PropertyEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    const PropertySpec& spec() const { return _spec; }

    // the label to put beside the editor; owned by the caller's layout
    virtual QWidget* createLabel(QWidget* parent);
    // show a value; mixed when the selected entities disagree
    virtual void setValue(const QVariant& value, bool mixed) = 0;
    // the user is in the middle of it: do not refresh the value under them
    virtual bool isEditing() const { return false; }

    static PropertyEditor* create(const PropertySpec& spec, QWidget* parent);

signals:
    void editBegan();                      // a scrub started: what is shown now is the undo point
    void liveValue(const QVariant& value); // while scrubbing
    void committed(const QVariant& value); // the edit is done

protected:
    PropertySpec _spec;
    bool _updating { false };              // setValue in progress: widget signals are not the user's
};

// Int, Float, Degrees
class NumberEditor : public PropertyEditor {
    Q_OBJECT
public:
    NumberEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    QWidget* createLabel(QWidget* parent) override;
    void setValue(const QVariant& value, bool mixed) override;
    bool isEditing() const override;
    double value() const { return _spin->value(); }
    // connects a scrub label to this editor's value
    void attachScrubLabel(ScrubLabel* label);
private:
    ScrubSpinBox* _spin { nullptr };
    bool _scrubbing { false };
    double _scrubStart { 0.0 };
    double _scrubAccumulated { 0.0 };
};

// Vec2, Vec3, Euler, Polar, Rect: a row of numbers, each with its own scrub label
class VectorEditor : public PropertyEditor {
    Q_OBJECT
public:
    VectorEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    void setValue(const QVariant& value, bool mixed) override;
    bool isEditing() const override;
private:
    QVariant currentValue() const;
    QVector<NumberEditor*> _components;
};

class BoolEditor : public PropertyEditor {
    Q_OBJECT
public:
    BoolEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    void setValue(const QVariant& value, bool mixed) override;
private:
    QCheckBox* _check { nullptr };
};

// String, Url
class TextEditor : public PropertyEditor {
    Q_OBJECT
public:
    TextEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    void setValue(const QVariant& value, bool mixed) override;
    bool isEditing() const override;
private:
    QLineEdit* _edit { nullptr };
    QString _shown;
};

class JsonEditor : public PropertyEditor {
    Q_OBJECT
public:
    JsonEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    void setValue(const QVariant& value, bool mixed) override;
    bool isEditing() const override;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void commitIfChanged();
    QPlainTextEdit* _edit { nullptr };
    QString _shown;
};

// Color, FloatColor
class ColorEditor : public PropertyEditor {
    Q_OBJECT
public:
    ColorEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    void setValue(const QVariant& value, bool mixed) override;
private:
    void updateSwatch();
    QToolButton* _button { nullptr };
    QColor _color;
    bool _mixed { false };
};

class EnumEditor : public PropertyEditor {
    Q_OBJECT
public:
    EnumEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    void setValue(const QVariant& value, bool mixed) override;
private:
    QComboBox* _combo { nullptr };
};

class CollisionMaskEditor : public PropertyEditor {
    Q_OBJECT
public:
    CollisionMaskEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    void setValue(const QVariant& value, bool mixed) override;
private:
    QString currentMask() const;
    QVector<QCheckBox*> _checks;
};

#endif // hifi_PropertyEditors_h
