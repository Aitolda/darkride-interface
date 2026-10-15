//
//  ObjectTable.h
//  libraries/wasm-scripting/src
//
//  The objects a script engine has handed to a module, by handle. A handle is a small integer the guest keeps;
//  the host keeps the QObject behind it with a QPointer, so an object deleted on the host side becomes a dead
//  handle (object() returns null) rather than a dangling pointer. The registered globals (Entities, MyAvatar,
//  Script...) are the named entries; every other entry is an object a method returned or a signal carried.
//
//  Used from one thread, the engine's.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ObjectTable_h
#define hifi_ObjectTable_h

#include <cstdint>

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>

class ObjectTable {
public:
    using Handle = int32_t;
    static constexpr Handle NULL_HANDLE = 0;

    /// The handle of an object, allocated on first sight; NULL_HANDLE for null.
    Handle handleFor(QObject* object);
    /// The object behind a handle; null when the handle is unknown, released, or the object was deleted.
    QObject* object(Handle handle) const;
    /// The class name the object had when it was entered (readable after the object is gone).
    QString className(Handle handle) const;
    bool contains(Handle handle) const;
    /// Forgets a handle. The object itself is not touched.
    void release(Handle handle);

    /// A named global, as registerGlobalObject makes one. The name is bound to the object's handle.
    void registerGlobal(const QString& name, QObject* object);
    Handle global(const QString& name) const;
    QStringList globalNames() const;

    int size() const { return _entries.size(); }

private:
    struct Entry {
        QPointer<QObject> object;
        QString className;
    };
    QHash<Handle, Entry> _entries;
    QHash<QObject*, Handle> _handles;
    QHash<QString, Handle> _globals;
    Handle _next { 1 };
};

#endif // hifi_ObjectTable_h
