//
//  UserDataEditor.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "UserDataEditor.h"

#include <cmath>
#include <memory>

#include <QtCore/QEvent>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QLocale>
#include <QtCore/QTimer>
#include <QtGui/QDoubleValidator>
#include <QtGui/QFontDatabase>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QTreeWidgetItemIterator>
#include <QtWidgets/QVBoxLayout>

namespace {
    const int PATH_ROLE = Qt::UserRole + 1;
    const int MAX_TREE_HEIGHT = 300;    // then it scrolls
    const int MAX_NAME_WIDTH = 140;     // a long name is cut short (its tooltip has it all), leaving room for the value
    const int MIN_NAME_WIDTH = 110;     // and never so narrow that a name being typed is cut off
    const char* const SHOWN_TEXT = "shownText";   // on a value's field: the text it showed, for Escape
    const int SUMMARY_NAMES = 3;        // a folded object's row names this many of its entries
    const QString NEW_ENTRY = QStringLiteral("newEntry");
    const QColor SUMMARY_COLOR { 0x8f, 0x8f, 0x8f };

    using Path = QVariantList;

    bool isIndex(const QVariant& step) {
        return step.userType() == QMetaType::Int;
    }

    Path child(const Path& path, const QVariant& step) {
        Path result = path;
        result << step;
        return result;
    }

    // the value at the path, undefined when there is none
    QJsonValue valueAt(const QJsonValue& root, const Path& path, int depth = 0) {
        if (depth == path.size()) {
            return root;
        }
        const QVariant& step = path[depth];
        if (isIndex(step)) {
            QJsonArray array = root.toArray();
            int i = step.toInt();
            return i >= 0 && i < array.size() ? valueAt(array[i], path, depth + 1) : QJsonValue(QJsonValue::Undefined);
        }
        QJsonObject object = root.toObject();
        QString key = step.toString();
        return object.contains(key) ? valueAt(object.value(key), path, depth + 1) : QJsonValue(QJsonValue::Undefined);
    }

    // the root with the value at the path put in (an index one past the end appends); undefined takes it out
    QJsonValue withValue(const QJsonValue& root, const Path& path, const QJsonValue& value, int depth = 0) {
        if (depth == path.size()) {
            return value;
        }
        const QVariant& step = path[depth];
        bool last = depth + 1 == path.size();
        if (isIndex(step)) {
            QJsonArray array = root.toArray();
            int i = step.toInt();
            if (i < 0 || i > array.size()) {
                return root;
            }
            if (last && value.isUndefined()) {
                if (i < array.size()) {
                    array.removeAt(i);
                }
                return array;
            }
            QJsonValue item = withValue(i < array.size() ? array[i] : QJsonValue(), path, value, depth + 1);
            if (i < array.size()) {
                array[i] = item;
            } else {
                array.append(item);
            }
            return array;
        }
        QJsonObject object = root.toObject();
        QString key = step.toString();
        if (last && value.isUndefined()) {
            object.remove(key);
            return object;
        }
        object.insert(key, withValue(object.value(key), path, value, depth + 1));
        return object;
    }

    QString pathKey(const Path& path) {
        QStringList parts;
        for (const auto& step : path) {
            parts << (isIndex(step) ? QString("#%1").arg(step.toInt()) : step.toString());
        }
        return parts.join(QChar(0x1f));
    }

    // whole numbers without a point, as people write them
    QString numberText(double number) {
        if (std::isfinite(number) && number == std::floor(number) && std::abs(number) < 1.0e15) {
            return QString::number((qint64)number);
        }
        return QLocale::c().toString(number, 'g', 15);
    }

    QJsonValue defaultValue(QJsonValue::Type type) {
        switch (type) {
            case QJsonValue::Bool: return QJsonValue(false);
            case QJsonValue::Double: return QJsonValue(0.0);
            case QJsonValue::Object: return QJsonValue(QJsonObject());
            case QJsonValue::Array: return QJsonValue(QJsonArray());
            default: return QJsonValue(QString());
        }
    }

    // a value made into another type, keeping what it can (the number in a text, a number's true or false)
    QJsonValue converted(const QJsonValue& value, QJsonValue::Type type) {
        if (value.type() == type) {
            return value;
        }
        switch (type) {
            case QJsonValue::String:
                if (value.isDouble()) {
                    return QJsonValue(numberText(value.toDouble()));
                }
                if (value.isBool()) {
                    return QJsonValue(value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
                }
                return QJsonValue(QString());
            case QJsonValue::Double:
                if (value.isString()) {
                    bool ok = false;
                    double number = QLocale::c().toDouble(value.toString().trimmed(), &ok);
                    return QJsonValue(ok ? number : 0.0);
                }
                return QJsonValue(value.isBool() && value.toBool() ? 1.0 : 0.0);
            case QJsonValue::Bool:
                if (value.isString()) {
                    return QJsonValue(value.toString().trimmed().toLower() == QStringLiteral("true"));
                }
                return QJsonValue(value.isDouble() && value.toDouble() != 0.0);
            default:
                return defaultValue(type);
        }
    }

    // the types + Add, Add Inside and Change To offer, by JSON's names
    struct EntryType {
        const char* label;
        QJsonValue::Type type;
    };
    const EntryType ENTRY_TYPES[] = {
        { "Text", QJsonValue::String },
        { "Number", QJsonValue::Double },
        { "Boolean", QJsonValue::Bool },
        { "Object { }", QJsonValue::Object },
        { "Array [ ]", QJsonValue::Array },
    };
}

UserDataEditor::UserDataEditor(const PropertySpec& spec, QWidget* parent) : PropertyEditor(spec, parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto top = new QHBoxLayout();
    top->setSpacing(4);
    _note = new QLabel(this);
    _note->setObjectName("EditorInspectorNote");
    _addButton = new QToolButton(this);
    _addButton->setObjectName("EditorUserDataAdd");
    _addButton->setText("+ Add");
    _addButton->setToolTip("A new entry");
    _addButton->setPopupMode(QToolButton::InstantPopup);
    auto addMenu = new QMenu(_addButton);
    for (const auto& entryType : ENTRY_TYPES) {
        QJsonValue::Type type = entryType.type;
        addMenu->addAction(entryType.label, this, [this, type] { addEntry(Path(), type); });
    }
    _addButton->setMenu(addMenu);
    _jsonButton = new QToolButton(this);
    _jsonButton->setText("Edit JSON…");
    _jsonButton->setToolTip("All of User Data as text, as scripts read it");
    connect(_jsonButton, &QToolButton::clicked, this, &UserDataEditor::editAsJson);
    top->addWidget(_note, 1);
    top->addWidget(_addButton);
    top->addWidget(_jsonButton);
    layout->addLayout(top);

    _tree = new QTreeWidget(this);
    _tree->setObjectName("EditorUserData");
    _tree->setColumnCount(2);
    _tree->setHeaderHidden(true);
    _tree->setIndentation(14);
    _tree->setSelectionMode(QAbstractItemView::NoSelection);   // the rows hold fields: there is nothing to select
    _tree->setFocusPolicy(Qt::NoFocus);
    _tree->setEditTriggers(QAbstractItemView::DoubleClicked);
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    _tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _tree->header()->setStretchLastSection(true);
    _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _tree->header()->setMinimumSectionSize(MIN_NAME_WIDTH);
    _tree->header()->setMaximumSectionSize(MAX_NAME_WIDTH);
    // as tall as its rows (those of open objects and arrays included), up to a limit
    _tree->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    _tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _tree->setMaximumHeight(MAX_TREE_HEIGHT);
    layout->addWidget(_tree);
    connect(_tree, &QTreeWidget::customContextMenuRequested, this, &UserDataEditor::showMenu);
    connect(_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column == 0 && !_settingItems) {
            renameEntry(item);
        }
    });
    connect(_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        _expanded.insert(pathKey(item->data(0, PATH_ROLE).toList()));
    });
    connect(_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        _expanded.remove(pathKey(item->data(0, PATH_ROLE).toList()));
    });

    _text = new QPlainTextEdit(this);
    _text->setMaximumHeight(110);
    _text->setTabStopDistance(20);
    _text->setReadOnly(spec.readOnly);
    _text->installEventFilter(this);
    layout->addWidget(_text);

    showValue();
}

QWidget* UserDataEditor::createLabel(QWidget* parent) {
    auto label = new QWidget(parent);
    label->setFixedSize(0, 0);
    return label;
}

void UserDataEditor::setValue(const QVariant& value, bool mixed) {
    QString text = mixed ? QString() : value.toString();
    if (mixed == _mixed && text == _shown) {
        return;   // the Inspector refreshes often: nothing has changed
    }
    _updating = true;
    _mixed = mixed;
    _shown = text;
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    _isObject = text.trimmed().isEmpty() || (error.error == QJsonParseError::NoError && document.isObject());
    _object = document.object();
    showValue();
    _updating = false;
}

bool UserDataEditor::isEditing() const {
    if (_menuOrDialogOpen) {
        return true;
    }
    // a value, a name or the text box being typed in
    QWidget* focus = QApplication::focusWidget();
    return focus && isAncestorOf(focus) && (qobject_cast<QLineEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus));
}

void UserDataEditor::revertTyping() {
    // a value being typed goes back to what it showed; a name being typed in place is the tree's own editor, which
    // puts the old name back itself
    auto edit = qobject_cast<QLineEdit*>(QApplication::focusWidget());
    if (edit && isAncestorOf(edit) && edit->property(SHOWN_TEXT).isValid()) {
        edit->setText(edit->property(SHOWN_TEXT).toString());
        edit->setModified(false);
    }
}

void UserDataEditor::setClaimedEntries(const QStringList& entries) {
    if (entries == _claimed) {
        return;
    }
    _claimed = entries;
    if (_isObject && !_mixed) {
        rebuildTree();
    }
}

void UserDataEditor::showValue() {
    bool editable = !_spec.readOnly;
    _addButton->setVisible(_isObject && !_mixed);
    _addButton->setEnabled(editable);
    _jsonButton->setEnabled(editable);
    _text->setVisible(!_isObject && !_mixed);
    if (_mixed) {
        _tree->setVisible(false);
        _note->setText("Different on each entity");
    } else if (!_isObject) {
        _tree->setVisible(false);
        _note->setText("Plain text");
        _text->setPlainText(_shown);
    } else {
        rebuildTree();
    }
}

void UserDataEditor::rebuildTree() {
    _settingItems = true;
    _tree->clear();
    for (const auto& key : _object.keys()) {
        if (!_claimed.contains(key)) {
            addItem(nullptr, key, Path { key }, _object.value(key), true);
        }
    }
    _settingItems = false;
    bool empty = _tree->topLevelItemCount() == 0;
    _note->setText(empty ? QStringLiteral("No entries") : QString());
    _tree->setVisible(!empty);
}

void UserDataEditor::addItem(QTreeWidgetItem* parent, const QString& name, const Path& path, const QJsonValue& value,
                             bool renamable) {
    auto item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(_tree);
    item->setText(0, name);
    item->setToolTip(0, name);
    item->setData(0, PATH_ROLE, path);
    Qt::ItemFlags flags = Qt::ItemIsEnabled;
    if (renamable && !_spec.readOnly) {
        flags |= Qt::ItemIsEditable;
    }
    item->setFlags(flags);
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        QStringList names = object.keys();
        QString summary = names.mid(0, SUMMARY_NAMES).join(", ");
        if (names.size() > SUMMARY_NAMES) {
            summary += QStringLiteral(", …");
        }
        item->setText(1, names.isEmpty() ? QStringLiteral("empty") : summary);
        item->setForeground(1, SUMMARY_COLOR);
        for (const auto& key : names) {
            addItem(item, key, child(path, key), object.value(key), true);
        }
        item->setExpanded(_expanded.contains(pathKey(path)));
    } else if (value.isArray()) {
        QJsonArray array = value.toArray();
        item->setText(1, array.size() == 1 ? QStringLiteral("1 item") : QString("%1 items").arg(array.size()));
        item->setForeground(1, SUMMARY_COLOR);
        for (int i = 0; i < array.size(); ++i) {
            addItem(item, QString::number(i + 1), child(path, i), array[i], false);
        }
        item->setExpanded(_expanded.contains(pathKey(path)));
    } else if (auto field = valueField(path, value)) {
        _tree->setItemWidget(item, 1, field);
    }
}

QWidget* UserDataEditor::valueField(const Path& path, const QJsonValue& value) {
    bool editable = !_spec.readOnly;
    switch (value.type()) {
        case QJsonValue::Bool: {
            auto check = new QCheckBox(_tree);
            check->setChecked(value.toBool());
            check->setEnabled(editable);
            connect(check, &QCheckBox::toggled, this, [this, path](bool on) {
                setEntry(path, QJsonValue(on), false);
            });
            return check;
        }
        case QJsonValue::Double: {
            auto edit = new QLineEdit(numberText(value.toDouble()), _tree);
            edit->setProperty(SHOWN_TEXT, edit->text());
            auto validator = new QDoubleValidator(edit);
            validator->setLocale(QLocale::c());
            edit->setValidator(validator);
            edit->setReadOnly(!editable);
            connect(edit, &QLineEdit::editingFinished, this, [this, path, edit] {
                bool ok = false;
                double number = QLocale::c().toDouble(edit->text(), &ok);
                if (ok && valueAt(_object, path) != QJsonValue(number)) {
                    setEntry(path, QJsonValue(number), false);
                    edit->setProperty(SHOWN_TEXT, edit->text());
                }
            });
            return edit;
        }
        case QJsonValue::String: {
            auto edit = new QLineEdit(value.toString(), _tree);
            edit->setProperty(SHOWN_TEXT, edit->text());
            edit->setReadOnly(!editable);
            connect(edit, &QLineEdit::editingFinished, this, [this, path, edit] {
                if (valueAt(_object, path) != QJsonValue(edit->text())) {
                    setEntry(path, QJsonValue(edit->text()), false);
                    edit->setProperty(SHOWN_TEXT, edit->text());
                }
            });
            return edit;
        }
        case QJsonValue::Null: {
            auto label = new QLabel(QStringLiteral("null"), _tree);
            label->setObjectName("EditorInspectorNote");
            return label;
        }
        default:
            return nullptr;
    }
}

void UserDataEditor::setEntry(const Path& path, const QJsonValue& value, bool rebuild) {
    _object = withValue(_object, path, value).toObject();
    commit();
    if (rebuild) {
        // not from inside the tree's own signals, whose items this replaces
        QTimer::singleShot(0, this, [this] { rebuildTree(); });
    }
}

void UserDataEditor::addEntry(const Path& parentPath, QJsonValue::Type type) {
    QJsonValue parent = parentPath.isEmpty() ? QJsonValue(_object) : valueAt(_object, parentPath);
    Path path = parentPath;
    if (parent.isArray()) {
        path << parent.toArray().size();
    } else {
        QJsonObject object = parent.toObject();
        QString name = NEW_ENTRY;
        for (int n = 2; object.contains(name) || (parentPath.isEmpty() && _claimed.contains(name)); ++n) {
            name = NEW_ENTRY + QString::number(n);
        }
        path << name;
    }
    if (!parentPath.isEmpty()) {
        _expanded.insert(pathKey(parentPath));   // open it, to show what was added
    }
    _object = withValue(_object, path, defaultValue(type)).toObject();
    commit();
    QTimer::singleShot(0, this, [this, path] {
        rebuildTree();
        if (isIndex(path.last())) {
            return;
        }
        // a new entry starts with its name being typed
        for (QTreeWidgetItemIterator it(_tree); *it; ++it) {
            if ((*it)->data(0, PATH_ROLE).toList() == path) {
                _tree->scrollToItem(*it);
                _tree->editItem(*it, 0);
                break;
            }
        }
    });
}

void UserDataEditor::renameEntry(QTreeWidgetItem* item) {
    Path path = item->data(0, PATH_ROLE).toList();
    if (path.isEmpty() || isIndex(path.last())) {
        return;
    }
    QString oldName = path.last().toString();
    QString newName = item->text(0).trimmed();
    Path parentPath = path.mid(0, path.size() - 1);
    QJsonObject parent = (parentPath.isEmpty() ? QJsonValue(_object) : valueAt(_object, parentPath)).toObject();
    if (newName.isEmpty() || newName == oldName || parent.contains(newName) ||
        (parentPath.isEmpty() && _claimed.contains(newName))) {
        // not a name it can take: back to the one it had
        _settingItems = true;
        item->setText(0, oldName);
        _settingItems = false;
        return;
    }
    QJsonValue value = parent.value(oldName);
    parent.remove(oldName);
    parent.insert(newName, value);
    if (_expanded.remove(pathKey(path))) {
        _expanded.insert(pathKey(child(parentPath, newName)));
    }
    _object = parentPath.isEmpty() ? parent : withValue(_object, parentPath, parent).toObject();
    commit();
    QTimer::singleShot(0, this, [this] { rebuildTree(); });
}

void UserDataEditor::showMenu(const QPoint& pos) {
    if (_spec.readOnly || _mixed || !isEnabled()) {
        return;
    }
    QTreeWidgetItem* item = _tree->itemAt(pos);
    QMenu menu;
    if (item) {
        Path path = item->data(0, PATH_ROLE).toList();
        QJsonValue value = valueAt(_object, path);
        if (!isIndex(path.last())) {
            menu.addAction("Rename", this, [this, item] { _tree->editItem(item, 0); });
        }
        if (value.isObject() || value.isArray()) {
            auto inside = menu.addMenu("Add Inside");
            for (const auto& entryType : ENTRY_TYPES) {
                QJsonValue::Type type = entryType.type;
                inside->addAction(entryType.label, this, [this, path, type] { addEntry(path, type); });
            }
        }
        auto change = menu.addMenu("Change To");
        for (const auto& entryType : ENTRY_TYPES) {
            QJsonValue::Type type = entryType.type;
            auto action = change->addAction(entryType.label, this, [this, path, value, type] {
                if (value.type() != type) {
                    setEntry(path, converted(value, type), true);
                }
            });
            action->setCheckable(true);
            action->setChecked(value.type() == type);
        }
        menu.addSeparator();
        menu.addAction("Remove", this, [this, path] { setEntry(path, QJsonValue(QJsonValue::Undefined), true); });
    } else {
        auto add = menu.addMenu("Add");
        for (const auto& entryType : ENTRY_TYPES) {
            QJsonValue::Type type = entryType.type;
            add->addAction(entryType.label, this, [this, type] { addEntry(Path(), type); });
        }
    }
    _menuOrDialogOpen = true;   // the list holds still while the menu is up
    menu.exec(_tree->viewport()->mapToGlobal(pos));
    _menuOrDialogOpen = false;
}

void UserDataEditor::editAsJson() {
    QDialog dialog(this);
    dialog.setWindowTitle("User Data");
    auto layout = new QVBoxLayout(&dialog);
    if (_mixed) {
        auto warning = new QLabel("The selected entities have different User Data: what is written here replaces it on "
                                  "all of them.", &dialog);
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }
    auto text = new QPlainTextEdit(&dialog);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setTabStopDistance(20);
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(_shown.toUtf8(), &error);
    bool json = !_shown.trimmed().isEmpty() && error.error == QJsonParseError::NoError;
    text->setPlainText(json ? QString::fromUtf8(document.toJson(QJsonDocument::Indented)) : _shown);
    layout->addWidget(text);
    auto problem = new QLabel(&dialog);
    problem->setWordWrap(true);
    problem->hide();
    layout->addWidget(problem);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // text that isn't JSON is pointed out first; OK a second time keeps it as plain text, which scripts may want
    auto warnedAbout = std::make_shared<QString>();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, text, problem, warnedAbout] {
        QString typed = text->toPlainText().trimmed();
        QJsonParseError parse;
        QJsonDocument::fromJson(typed.toUtf8(), &parse);
        if (typed.isEmpty() || parse.error == QJsonParseError::NoError || typed == *warnedAbout) {
            dialog.accept();
            return;
        }
        *warnedAbout = typed;
        problem->setText(QString("This isn't valid JSON (%1, at character %2). OK again keeps it as plain text.")
            .arg(parse.errorString()).arg(parse.offset));
        problem->show();
    });
    dialog.resize(520, 420);
    _menuOrDialogOpen = true;
    bool accepted = dialog.exec() == QDialog::Accepted;
    _menuOrDialogOpen = false;
    if (!accepted) {
        return;
    }
    // a JSON object is kept compact, as the list writes it; anything else as it was typed
    QString typed = text->toPlainText().trimmed();
    QJsonDocument parsed = QJsonDocument::fromJson(typed.toUtf8());
    QString value = parsed.isObject() ? QString::fromUtf8(parsed.toJson(QJsonDocument::Compact)) : typed;
    if (value == _shown && !_mixed) {
        return;
    }
    _shown = value;
    _mixed = false;
    _isObject = value.isEmpty() || parsed.isObject();
    _object = parsed.object();
    emit committed(QVariant(_shown));
    showValue();
}

void UserDataEditor::commit() {
    _shown = _object.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(_object).toJson(QJsonDocument::Compact));
    emit committed(QVariant(_shown));
}

bool UserDataEditor::eventFilter(QObject* watched, QEvent* event) {
    if (watched == _text && event->type() == QEvent::FocusOut && !_isObject && !_mixed && !_updating) {
        QString text = _text->toPlainText();
        if (text != _shown) {
            _shown = text;
            emit committed(QVariant(text));
            // text that has become a JSON object shows as the list
            QJsonParseError error;
            QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
            if (text.trimmed().isEmpty() || (error.error == QJsonParseError::NoError && document.isObject())) {
                _isObject = true;
                _object = document.object();
                QTimer::singleShot(0, this, [this] { showValue(); });
            }
        }
    }
    return PropertyEditor::eventFilter(watched, event);
}
