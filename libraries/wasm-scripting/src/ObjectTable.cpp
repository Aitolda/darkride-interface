//
//  ObjectTable.cpp
//  libraries/wasm-scripting/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ObjectTable.h"

#include <QtCore/QObject>

ObjectTable::Handle ObjectTable::handleFor(QObject* object) {
    if (!object) {
        return NULL_HANDLE;
    }
    auto found = _handles.find(object);
    if (found != _handles.end()) {
        // the same address may belong to a new object after the old one died: check the entry is still alive
        auto entry = _entries.find(found.value());
        if (entry != _entries.end() && entry.value().object == object) {
            return found.value();
        }
        if (entry != _entries.end()) {
            _entries.erase(entry);
        }
        _handles.erase(found);
    }
    Handle handle = _next++;
    _entries.insert(handle, Entry { QPointer<QObject>(object), QString::fromUtf8(object->metaObject()->className()) });
    _handles.insert(object, handle);
    return handle;
}

QObject* ObjectTable::object(Handle handle) const {
    auto entry = _entries.find(handle);
    return entry == _entries.end() ? nullptr : entry.value().object.data();
}

QString ObjectTable::className(Handle handle) const {
    auto entry = _entries.find(handle);
    return entry == _entries.end() ? QString() : entry.value().className;
}

bool ObjectTable::contains(Handle handle) const {
    return _entries.contains(handle);
}

void ObjectTable::release(Handle handle) {
    auto entry = _entries.find(handle);
    if (entry == _entries.end()) {
        return;
    }
    if (entry.value().object) {
        _handles.remove(entry.value().object.data());
    }
    _entries.erase(entry);
    for (auto it = _globals.begin(); it != _globals.end();) {
        if (it.value() == handle) {
            it = _globals.erase(it);
        } else {
            ++it;
        }
    }
}

void ObjectTable::registerGlobal(const QString& name, QObject* object) {
    _globals.insert(name, handleFor(object));
}

ObjectTable::Handle ObjectTable::global(const QString& name) const {
    return _globals.value(name, NULL_HANDLE);
}

QStringList ObjectTable::globalNames() const {
    QStringList names = _globals.keys();
    names.sort();
    return names;
}
