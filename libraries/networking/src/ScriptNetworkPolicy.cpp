//
//  ScriptNetworkPolicy.cpp
//
//  Created by scripting on 2026-10-30.
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "ScriptNetworkPolicy.h"

#include <cstring>

#include <QtCore/QStringList>
#include <QtNetwork/QHostInfo>

#include <DependencyManager.h>

#include "NodeList.h"

namespace {

const QStringList WEB_SCHEMES { "http", "https", "ws", "wss" };
const QString ASSET_SCHEME = "atp";

bool inIPv4Block(quint32 address, quint32 block, int bits) {
    const quint32 mask = bits == 0 ? 0 : (0xffffffffu << (32 - bits));
    return (address & mask) == block;
}

bool isPrivateIPv4(quint32 address) {
    return inIPv4Block(address, 0x00000000u, 8)      // 0.0.0.0/8: this network
        || inIPv4Block(address, 0x0A000000u, 8)      // 10/8
        || inIPv4Block(address, 0x64400000u, 10)     // 100.64/10: carrier-grade NAT, shared address space
        || inIPv4Block(address, 0x7F000000u, 8)      // 127/8: loopback
        || inIPv4Block(address, 0xA9FE0000u, 16)     // 169.254/16: link-local (and the cloud metadata address)
        || inIPv4Block(address, 0xAC100000u, 12)     // 172.16/12
        || inIPv4Block(address, 0xC0000000u, 24)     // 192.0.0/24: protocol assignments
        || inIPv4Block(address, 0xC0A80000u, 16)     // 192.168/16
        || inIPv4Block(address, 0xC6120000u, 15)     // 198.18/15: benchmarking
        || inIPv4Block(address, 0xE0000000u, 4)      // 224/4: multicast
        || inIPv4Block(address, 0xF0000000u, 4);     // 240/4: reserved, and the broadcast address
}

}  // namespace

bool ScriptNetworkPolicy::isPrivateAddress(const QHostAddress& address) {
    if (address.isNull()) {
        return true;
    }
    bool isIPv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIPv4);   // an IPv4-mapped IPv6 address converts too
    if (isIPv4) {
        return isPrivateIPv4(ipv4);
    }
    if (address.protocol() != QAbstractSocket::IPv6Protocol) {
        return true;
    }
    if (address == QHostAddress(QHostAddress::AnyIPv6) || address.isLoopback() || address.isLinkLocal() ||
        address.isSiteLocal() || address.isUniqueLocalUnicast() || address.isMulticast()) {
        return true;
    }
    // an IPv4 address carried in IPv6 by a translator (64:ff9b::/96) or the deprecated compatible form (::/96)
    const Q_IPV6ADDR bytes = address.toIPv6Address();
    static const quint8 NAT64[12] = { 0x00, 0x64, 0xff, 0x9b, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const quint8 COMPATIBLE[12] = { 0 };
    if (memcmp(bytes.c, NAT64, 12) == 0 || memcmp(bytes.c, COMPATIBLE, 12) == 0) {
        const quint32 carried = (quint32(bytes.c[12]) << 24) | (quint32(bytes.c[13]) << 16) |
                                (quint32(bytes.c[14]) << 8) | quint32(bytes.c[15]);
        return isPrivateIPv4(carried);
    }
    return false;
}

bool ScriptNetworkPolicy::isLocalHostName(const QString& hostName) {
    const QString host = hostName.toLower();
    if (host.isEmpty() || host == "localhost") {
        return true;
    }
    if (!host.contains('.')) {
        return true;    // a name on the local network (printer, nas, router)
    }
    static const QStringList LOCAL_SUFFIXES { ".localhost", ".local", ".lan", ".home", ".internal", ".home.arpa",
                                              ".in-addr.arpa", ".ip6.arpa" };
    for (const QString& suffix : LOCAL_SUFFIXES) {
        if (host.endsWith(suffix)) {
            return true;
        }
    }
    return false;
}

bool ScriptNetworkPolicy::isLocalOrigin(const QUrl& scriptUrl) {
    if (scriptUrl.isEmpty()) {
        return false;   // unknown: held to the rule
    }
    const QString scheme = scriptUrl.scheme().toLower();
    if (scheme.isEmpty() || scheme == "file" || scheme == "qrc") {
        return true;
    }
    if (!WEB_SCHEMES.contains(scheme)) {
        return false;   // atp (the world's asset server), about (an entities engine itself): judged by the world
    }
    const QString host = scriptUrl.host().toLower();
    const QHostAddress address(host);
    return address.isNull() ? isLocalHostName(host) : isPrivateAddress(address);
}

bool ScriptNetworkPolicy::isWorldLocal() {
    auto nodeList = DependencyManager::get<NodeList>();
    if (!nodeList) {
        return false;
    }
    const DomainHandler& domain = nodeList->getDomainHandler();
    const QHostAddress& ip = domain.getIP();
    if (!ip.isNull()) {
        return isPrivateAddress(ip);
    }
    const QString host = domain.getHostname().toLower();
    if (host.isEmpty()) {
        return domain.isServerless();   // a world loaded from a file on this machine
    }
    const QHostAddress address(host);
    return address.isNull() ? isLocalHostName(host) : isPrivateAddress(address);
}

bool ScriptNetworkPolicy::isStrict() {
    static const bool strict = qEnvironmentVariable("DARKRIDE_SCRIPT_NETWORK_STRICT") == "1";
    return strict;
}

bool ScriptNetworkPolicy::applies(const QUrl& origin) {
    if (isStrict()) {
        return true;
    }
    return !isLocalOrigin(origin) && !isWorldLocal();
}

ScriptNetworkPolicy::Decision ScriptNetworkPolicy::check(const QUrl& target, const QUrl& origin) {
    Decision decision;
    if (!applies(origin)) {
        return decision;
    }
    const QString scheme = target.scheme().toLower();
    if (scheme == ASSET_SCHEME) {
        return decision;    // the world's own asset server, reached through the world's connection
    }
    if (!WEB_SCHEMES.contains(scheme)) {
        decision.allowed = false;
        decision.reason = scheme.isEmpty() ? "not a web address" : "not a web address (" + scheme + ")";
        return decision;
    }
    const QString host = target.host().toLower();
    if (host.isEmpty()) {
        decision.allowed = false;
        decision.reason = "no host in the address";
        return decision;
    }
    const QHostAddress address(host);
    if (!address.isNull()) {
        if (isPrivateAddress(address)) {
            decision.allowed = false;
            decision.reason = "an address on this machine or its network";
        }
        return decision;
    }
    if (isLocalHostName(host)) {
        decision.allowed = false;
        decision.reason = "a name for this machine or its network";
        return decision;
    }
    decision.allowed = false;
    decision.needsLookup = true;
    return decision;
}

ScriptNetworkPolicy::Decision ScriptNetworkPolicy::checkAddresses(const QList<QHostAddress>& addresses) {
    Decision decision;
    if (addresses.isEmpty()) {
        decision.allowed = false;
        decision.reason = "the name has no address";
        return decision;
    }
    for (const QHostAddress& address : addresses) {
        if (isPrivateAddress(address)) {
            decision.allowed = false;
            decision.reason = "the name leads to an address on this machine or its network (" + address.toString() + ")";
            return decision;
        }
    }
    return decision;
}

void ScriptNetworkPolicy::decide(const QUrl& target, const QUrl& origin, QObject* context,
                                 std::function<void(const Decision&)> callback) {
    const Decision first = check(target, origin);
    if (!first.needsLookup) {
        callback(first);
        return;
    }
    QHostInfo::lookupHost(target.host(), context, [callback](const QHostInfo& info) {
        if (info.error() != QHostInfo::NoError) {
            Decision decision;
            decision.allowed = false;
            decision.reason = "the name could not be looked up (" + info.errorString() + ")";
            callback(decision);
            return;
        }
        callback(checkAddresses(info.addresses()));
    });
}

QString ScriptNetworkPolicy::refusalLine(const QUrl& origin, const QUrl& target, const QString& reason) {
    return QString("%1 may not reach %2: %3. A script from the network uses web addresses to public hosts only; "
                   "a script loaded from this machine, or a world served from it or its network, keeps every address.")
        .arg(origin.isEmpty() ? "a script" : origin.toDisplayString(), target.toDisplayString(), reason);
}
