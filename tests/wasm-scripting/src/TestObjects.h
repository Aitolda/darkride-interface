//
//  TestObjects.h
//  tests/wasm-scripting/src
//
//  A QObject with the kinds of members the scripting interfaces have: invokables (one overloaded), a slot, a
//  non-public invokable, properties (notifying, constant, enum-typed, vec3-typed), signals with the argument
//  types the relay must carry, and an enum. Shared by the codec, description and relay tests.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TestObjects_h
#define hifi_TestObjects_h

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariant>

#include <RegisteredMetaTypes.h>

class Probe : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY countChanged)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(Mode mode READ mode WRITE setMode)
    Q_PROPERTY(glm::vec3 position READ position WRITE setPosition NOTIFY positionChanged)
public:
    enum Mode { Idle, Running, Stopped = 10 };
    Q_ENUM(Mode)

    explicit Probe(QObject* parent = nullptr) : QObject(parent) {}

    int count() const { return _count; }
    QString label() const { return "probe"; }
    Mode mode() const { return _mode; }
    void setMode(Mode mode) { _mode = mode; }
    glm::vec3 position() const { return _position; }

    Q_INVOKABLE int add(int a, int b) { return a + b; }
    Q_INVOKABLE QString add(const QString& a, const QString& b) { return a + b; }
    Q_INVOKABLE glm::vec3 scale(const glm::vec3& v, float by) { return v * by; }

public slots:
    void setPosition(const glm::vec3& position) {
        _position = position;
        emit positionChanged(position);
    }
    void setCount(int count) {
        _count = count;
        emit countChanged(count);
    }
    void reset() { setCount(0); }
    void send(const QString& name, bool flag) { emit named(name, flag); }
    void carry(const QVariant& value) { emit carried(value); }
    void sendObject(QObject* object) { emit objectSent(object); }
    void ping() { emit bare(); }

signals:
    void countChanged(int count);
    void positionChanged(const glm::vec3& position);
    void named(const QString& name, bool flag);
    void bare();
    void carried(const QVariant& value);
    void objectSent(QObject* object);

protected:
    Q_INVOKABLE void hidden() {}

private:
    int _count { 0 };
    Mode _mode { Idle };
    glm::vec3 _position { 0.0f };
};

/// Another class, for the checks that a handle of one class is not accepted as another.
class Other : public QObject {
    Q_OBJECT
public:
    explicit Other(QObject* parent = nullptr) : QObject(parent) {}
};

#endif // hifi_TestObjects_h
