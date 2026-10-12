//
//  UserDataEditor.h
//  interface/src/editor/ui
//
//  userData as Blender shows custom properties: one row per entry, each with a field that suits its value (a
//  checkbox, a number, text), objects and arrays folding open as the Hierarchy's tree does. + Add makes an entry;
//  right-click renames one, removes it, changes its type or adds inside an object or array; Edit JSON… opens the
//  whole text, for scripters. userData that isn't a JSON object (a script's own text) shows as plain text. Entries
//  that other Inspector rows show as fields of their own (a light's Cookie) are kept but not listed.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_UserDataEditor_h
#define hifi_UserDataEditor_h

#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QSet>

#include "../JsonOrder.h"
#include "PropertyEditors.h"

class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;

class UserDataEditor : public PropertyEditor {
    Q_OBJECT
public:
    UserDataEditor(const PropertySpec& spec, QWidget* parent = nullptr);
    // the section's header names it: the list takes the whole width, with no label beside it
    QWidget* createLabel(QWidget* parent) override;
    void setValue(const QVariant& value, bool mixed) override;
    bool isEditing() const override;
    void revertTyping() override;
    QVariant currentValue() const override { return QVariant(_shown); }
    bool isMixed() const override { return _mixed; }

    // entries shown as fields of their own elsewhere in the Inspector: kept, but not listed here
    void setClaimedEntries(const QStringList& entries);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;   // the text box commits as it loses the keyboard

private:
    // a place in userData: the keys of objects (QString) and the positions in arrays (int), from the top
    using Path = QVariantList;

    void showValue();
    void rebuildTree();
    void addItem(QTreeWidgetItem* parent, const QString& name, const Path& path, const QJsonValue& value, bool renamable);
    QWidget* valueField(const Path& path, const QJsonValue& value);

    void setEntry(const Path& path, const QJsonValue& value, bool rebuild);
    void addEntry(const Path& parentPath, QJsonValue::Type type);
    QTreeWidgetItem* itemAt(const Path& path) const;
    void reveal(const Path& path);        // scrolls the list and the Inspector to the entry
    void focusValue(const Path& path);    // the keyboard goes to the entry's value, if it has a field
    QScrollArea* outerScrollArea() const; // the Inspector's, which the list sits in
    void renameEntry(QTreeWidgetItem* item);
    void showMenu(const QPoint& pos);
    void editAsJson();
    void commit();

    QLabel* _note { nullptr };            // no entries, different on each entity, or plain text
    QToolButton* _addButton { nullptr };
    QToolButton* _jsonButton { nullptr };
    QTreeWidget* _tree { nullptr };
    QPlainTextEdit* _text { nullptr };    // userData that isn't a JSON object

    QJsonObject _object;                  // all of userData, claimed entries included
    JsonOrder::KeyOrder _order;           // the order of every object's entries, which QJsonObject loses
    bool _isObject { true };
    QString _shown;                       // the userData text last shown or sent
    bool _mixed { false };
    bool _menuOrDialogOpen { false };
    bool _settingItems { false };         // the tree's items being made or put back: not the user renaming
    QStringList _claimed;
    QSet<QString> _expanded;              // the objects and arrays open in the tree, kept across rebuilds
    Path _renamedTo;                      // the entry just renamed, under its new name, until the list is remade
    Path _focusAfterRebuild;              // the entry whose value takes the keyboard once the list is remade
};

#endif // hifi_UserDataEditor_h
