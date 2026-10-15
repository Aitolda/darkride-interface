//
//  JavaScriptGuestTests.cpp
//  tests/wasm-scripting/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "JavaScriptGuestTests.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include <HostInterface.h>
#include <ObjectTable.h>
#include <SignalRelay.h>
#include <ValueCodec.h>
#include <WasmRuntime.h>

#include "TestObjects.h"

QTEST_MAIN(JavaScriptGuestTests)

namespace {

QString guestModulePath() {
    QString fromEnvironment = qEnvironmentVariable("DARKRIDE_JS_WASM");
    if (!fromEnvironment.isEmpty()) {
        return fromEnvironment;
    }
    // this source file is tests/wasm-scripting/src/...; the module is built beside the tools
    return QFileInfo(QString(__FILE__)).dir().absoluteFilePath("../../../tools/wasm-js/build/darkride-js.wasm");
}

struct Guest {
    std::shared_ptr<WasmRuntime> runtime { std::make_shared<WasmRuntime>() };
    std::shared_ptr<WasmModule> module;
    std::unique_ptr<WasmInstance> instance;
    ObjectTable objects;
    ValueCodec codec { &objects };
    SignalRelay relay;
    std::unique_ptr<HostInterface> host;
    FakeScript script;
    Probe probe;
    QStringList logged;        // "<level> <text>"
    QStringList errors;        // level 3 only
    QStringList failures;
    QVariantList carried;      // what the script handed to Probe.carry

    bool start(const QString& source, QString& error) {
        QFile file(guestModulePath());
        if (!file.open(QIODevice::ReadOnly)) {
            error = "no guest module at " + guestModulePath();
            return false;
        }
        module = runtime->compile(file.readAll(), error);
        if (!module) {
            return false;
        }
        WasmInstance::Limits limits;
        limits.callTimeout = std::chrono::milliseconds(10000);
        instance = std::make_unique<WasmInstance>(module, limits);
        host = std::make_unique<HostInterface>(*instance, objects, codec, relay);
        host->setLogger([this](int level, const QString& text) {
            logged << QString::number(level) + " " + text;
            if (level == HostInterface::Error) {
                errors << text;
            }
        });
        host->setFailureHandler([this](const QString& entry, const QString& failure) { failures << entry + ": " + failure; });
        host->install();
        instance->enableWasi();
        objects.registerGlobal("Script", &script);
        objects.registerGlobal("Probe", &probe);
        QObject::connect(&probe, &Probe::carried, [this](const QVariant& value) { carried << value; });
        script.source = source;
        if (!instance->instantiate(error)) {
            return false;
        }
        std::vector<WasmValue> results;
        if (instance->hasExport("_initialize") && !instance->call("_initialize", {}, results, error)) {
            return false;
        }
        return host->callScriptStart(error);
    }

    void stop() {
        QString error;
        if (host) {
            host->callScriptStop(error);
        }
    }
};

#define START_OR_SKIP(guest, source)                                                        \
    {                                                                                      \
        if (!QFileInfo::exists(guestModulePath())) {                                       \
            QSKIP("darkride-js.wasm is not built (tools/wasm-js/build.sh)");               \
        }                                                                                  \
        QString error;                                                                     \
        QVERIFY2(guest.start(source, error), qPrintable(error + " " + guest.errors.join(" | "))); \
    }

} // namespace

void JavaScriptGuestTests::testCallsPropertiesAndValues() {
    Guest guest;
    const QString source = R"(
        var out = [];
        out.push(Probe.add(2, 3));
        out.push(Probe.add("a", "b"));
        Probe.count = 7;
        out.push(Probe.count);
        out.push(Probe.scale({ x: 1, y: 2, z: 3 }, 2).y);
        out.push(Probe.self() === Probe);
        out.push(Probe.label);
        Probe.position = { x: 1.5, y: 2.5, z: -3 };
        out.push(Probe.position.z);
        out.push(Probe.join(["a", "b"], "c").join("+"));
        out.push(String(Probe.uuidOf("{6ba7b810-9dad-11d1-80b4-00c04fd430c8}")));
        out.push(Probe.countOf(Probe));
        out.push(PickType.Ray);
        out.push(typeof Probe.hidden);
        out.push(Object.prototype.toString.call(Probe.echo(new Uint8Array([1, 2, 3]).buffer)));
        out.push("héllo ✓");
        print("printed", 1, { a: 1 });
        Probe.carry(Probe.echo({ nested: [1, "two", null, { three: true }], n: 2.5 }));
        Probe.carry(out);
    )";
    START_OR_SKIP(guest, source);
    QVERIFY2(guest.errors.isEmpty(), qPrintable(guest.errors.join(" | ")));
    QCOMPARE(guest.carried.size(), 2);
    QVariantMap echoed = guest.carried[0].toMap();
    QCOMPARE(echoed["n"].toDouble(), 2.5);
    QVariantList nested = echoed["nested"].toList();
    QCOMPARE(nested.size(), 4);
    QCOMPARE(nested[1].toString(), QString("two"));
    QCOMPARE(nested[3].toMap()["three"].toBool(), true);
    QVariantList out = guest.carried[1].toList();
    QCOMPARE(out.size(), 14);
    QCOMPARE(out[0].toInt(), 5);
    QCOMPARE(out[1].toString(), QString("ab"));
    QCOMPARE(out[2].toInt(), 7);
    QCOMPARE(out[3].toDouble(), 4.0);
    QCOMPARE(out[4].toBool(), true);
    QCOMPARE(out[5].toString(), guest.probe.label());
    QCOMPARE(out[6].toDouble(), -3.0);
    QCOMPARE(out[7].toString(), QString("a+b+c"));
    QCOMPARE(out[8].toString(), QString("{6ba7b810-9dad-11d1-80b4-00c04fd430c8}"));
    QCOMPARE(out[9].toInt(), 7);
    QCOMPARE(out[10].toInt(), 1);
    QCOMPARE(out[11].toString(), QString("undefined"));
    QCOMPARE(out[12].toString(), QString("[object ArrayBuffer]"));
    QCOMPARE(out[13].toString(), QString::fromUtf8("héllo ✓"));
    QVERIFY(guest.logged.contains("0 printed 1 [object Object]"));
    guest.stop();
}

void JavaScriptGuestTests::testSignalsCallbacksAndTimers() {
    Guest guest;
    const QString source = R"(
        var seen = [];
        function onCount(c) { seen.push("count " + c); }
        Probe.countChanged.connect(onCount);
        Probe.named.connect(function (name, flag) { seen.push(name + ":" + flag); });
        Probe.count = 3;                       // emitted inside the set: delivered re-entrantly
        Probe.send("x", true);
        Probe.countChanged.disconnect(onCount);
        Probe.count = 4;                       // not seen
        var t = Script.setTimeout(function () { seen.push("timer"); Probe.carry(seen); }, 10);
        var cleared = Script.setTimeout(function () { seen.push("never"); }, 10);
        Script.clearTimeout(cleared);
        Script.update.connect(function (dt) { seen.push("update " + dt); });
        var holder = { value: 0, method: function (c) { this.value = c; seen.push("this " + this.value); } };
        Probe.countChanged.connect(holder, holder.method);
        Probe.count = 5;
        Script.scriptEnding.connect(function () { Probe.carry("ending"); });
        Probe.carry(typeof t);
    )";
    START_OR_SKIP(guest, source);
    QVERIFY2(guest.errors.isEmpty(), qPrintable(guest.errors.join(" | ")));
    QCOMPARE(guest.carried.size(), 1);
    QCOMPARE(guest.carried[0].toString(), QString("number"));
    // the timer the script set: fire it as the engine would
    QCOMPARE(guest.script.timers.size(), 1);
    QString error;
    emit guest.script.update(0.5f);
    QVERIFY(guest.host->deliverCallback(guest.script.timers.begin().value().id, QVariantList(), error));
    QCOMPARE(guest.carried.size(), 2);
    QStringList seen = guest.carried[1].toStringList();
    QCOMPARE(seen, QStringList({ "count 3", "x:true", "this 5", "update 0.5", "timer" }));
    emit guest.script.scriptEnding();
    QCOMPARE(guest.carried.size(), 3);
    QCOMPARE(guest.carried[2].toString(), QString("ending"));
    QVERIFY2(guest.failures.isEmpty(), qPrintable(guest.failures.join(" | ")));
    guest.stop();
}

void JavaScriptGuestTests::testIncludeAndRequire() {
    Guest guest;
    guest.script.files["file:///test/lib/helper.js"] = "var helperLoaded = (typeof helperLoaded === 'number' ? helperLoaded : 0) + 1; var helperPath = Script.resolvePath('data.json');";
    guest.script.files["file:///test/lib/mod.js"] = "module.exports = { answer: 42, dir: __dirname, again: require('./other.js').twice };";
    guest.script.files["file:///test/lib/other.js"] = "exports.twice = require('./mod.js') ? 2 : 0; exports.self = module.filename;";
    guest.script.files["file:///test/lib/settings.json"] = "{ \"volume\": 0.5 }";
    const QString source = R"(
        Script.include("lib/helper.js");
        Script.include(["lib/helper.js"]);     // a second include of the same file is skipped
        var m = Script.require("./lib/mod.js");
        var j = Script.require("./lib/settings.json");
        var missing = null;
        try { Script.require("./lib/missing.js"); } catch (e) { missing = e.message; }
        Probe.carry([helperLoaded, helperPath, m.answer, m.dir, m.again, j.volume, missing, Script.resolvePath("x.js"),
                     Object.keys(Script.require.cache).length]);
    )";
    START_OR_SKIP(guest, source);
    QVERIFY2(guest.errors.isEmpty(), qPrintable(guest.errors.join(" | ")));
    QCOMPARE(guest.carried.size(), 1);
    QVariantList out = guest.carried[0].toList();
    QCOMPARE(out[0].toInt(), 1);
    QCOMPARE(out[1].toString(), QString("file:///test/lib/data.json"));  // resolved against the included file
    QCOMPARE(out[2].toInt(), 42);
    QCOMPARE(out[3].toString(), QString("file:///test/lib"));
    QCOMPARE(out[4].toInt(), 2);                                          // the cycle: other saw mod's partial exports
    QCOMPARE(out[5].toDouble(), 0.5);
    QVERIFY(out[6].toString().contains("missing.js"));
    QCOMPARE(out[7].toString(), QString("file:///test/x.js"));
    QCOMPARE(out[8].toInt(), 3);
    guest.stop();
}

void JavaScriptGuestTests::testErrorsAreReportedNotFatal() {
    Guest guest;
    const QString source = R"(
        var messages = [];
        try { Probe.add(); } catch (e) { messages.push(e.name + "|" + e.message); }
        try { Probe.label = "x"; messages.push("silent"); } catch (e) { messages.push("threw"); }
        try { Probe.nothing(); } catch (e) { messages.push(e instanceof TypeError); }
        Probe.bare.connect(function () { throw new Error("in a handler"); });
        Probe.ping();
        messages.push("after the handler");
        Probe.carry(messages);
        throw new Error("at the top level");
    )";
    START_OR_SKIP(guest, source);
    QCOMPARE(guest.carried.size(), 1);
    QVariantList messages = guest.carried[0].toList();
    QCOMPARE(messages.size(), 4);
    QVERIFY2(messages[0].toString().contains("add"), qPrintable(messages[0].toString()));
    QCOMPARE(messages[1].toString(), QString("silent"));
    QCOMPARE(messages[2].toBool(), true);
    QCOMPARE(messages[3].toString(), QString("after the handler"));
    QCOMPARE(guest.errors.size(), 2);
    QVERIFY2(guest.errors[0].contains("in a handler"), qPrintable(guest.errors[0]));
    QVERIFY2(guest.errors[1].contains("at the top level") && guest.errors[1].contains("main.js"), qPrintable(guest.errors[1]));
    QVERIFY(guest.failures.isEmpty());
    guest.stop();
}

void JavaScriptGuestTests::testApiDump() {
    Guest guest;
    const QString source = R"(Script.dumpApi("/nowhere/api.json");)";
    START_OR_SKIP(guest, source);
    QVERIFY2(guest.errors.isEmpty(), qPrintable(guest.errors.join(" | ")));
    QCOMPARE(guest.script.dumpPath, QString("/nowhere/api.json"));
    QVariantMap probe = guest.script.dumped["Probe"].toMap();
    QCOMPARE(probe["kind"].toString(), QString("qobject"));
    QCOMPARE(probe["meta"].toMap()["class"].toString(), QString("Probe"));
    QCOMPARE(guest.script.dumped["XMLHttpRequest"].toMap()["kind"].toString(), QString("function"));
    QCOMPARE(guest.script.dumped["Math"].toMap()["kind"].toString(), QString("object"));
    QCOMPARE(guest.script.dumped["PickType"].toMap()["members"].toMap()["Ray"].toString(), QString("number"));
    guest.stop();
}

// Tens of thousands of calls with maps crossing both ways, as a script's hot loop makes them: the guest's heap
// must stay sound and its memory bounded.
void JavaScriptGuestTests::testManyCalls() {
    Guest guest;
    const QString source = R"(
        var s = 0, i, m;
        for (i = 0; i < 30000; i++) {
            m = Probe.echo({ position: { x: i, y: 2.5, z: -3 }, name: "box-" + i, list: [1, 2, 3], nested: { deep: { er: true } } });
            s += m.position.y + m.list.length;
            Probe.count = i;
            s += Probe.count;
            Probe.position = m.position;
            s += Probe.describeSelf().count;
        }
        Probe.carry([s, Probe.position.x]);
    )";
    START_OR_SKIP(guest, source);
    QVERIFY2(guest.errors.isEmpty(), qPrintable(guest.errors.join(" | ")));
    QVERIFY2(guest.failures.isEmpty(), qPrintable(guest.failures.join(" | ")));
    QCOMPARE(guest.carried.size(), 1);
    QVariantList out = guest.carried[0].toList();
    QCOMPARE(out[1].toDouble(), 29999.0);
    guest.stop();
}

#include "moc_TestObjects.cpp"
