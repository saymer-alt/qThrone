#include "include/configs/outbounds/aivpn.h"
#include "include/global/Utils.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

namespace Configs {
namespace {
// aivpn-client's own reading of a key: aivpn:// and base64url (no padding) of a JSON object with "s" and "k".
QJsonObject keyPayload(const QString &key) {
    auto body = key.trimmed();
    if (!body.startsWith("aivpn://", Qt::CaseInsensitive)) return {};
    body = body.mid(8);
    const auto bytes = QByteArray::fromBase64(body.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const auto json = QJsonDocument::fromJson(bytes).object();
    if (json["s"].toString().isEmpty() || json["k"].toString().isEmpty()) return {};
    return json;
}
}

const QStringList &aivpn::Masks() {
    static const QStringList masks{"auto", "webrtc_zoom_v3", "quic_https_v2", "webrtc_yandex_telemost_v1", "webrtc_vk_teams_v1",
                                   "webrtc_sberjazz_v1", "telegram_calls_v1", "whatsapp_voip_v1", "vk_video_v1", "sber_salute_v1", "avito_api_v1"};
    return masks;
}

bool aivpn::ParseFromLink(const QString &link) {
    const auto payload = keyPayload(link);
    if (payload.isEmpty()) return false;
    key = link.trimmed();
    // "s" is host:port, or [IPv6]:port.
    const auto address = payload["s"].toString();
    const int colon = address.lastIndexOf(':');
    server = colon > 0 ? address.left(colon) : address;
    if (server.startsWith('[') && server.endsWith(']')) server = server.mid(1, server.size() - 2);
    server_port = colon > 0 ? address.mid(colon + 1).toInt() : 443;
    if (name.isEmpty()) name = server;
    return Validate().isEmpty();
}

bool aivpn::ParseFromJson(const QJsonObject &o) {
    if (o["type"].toString() != "aivpn") return false;
    name = o["name"].toString();
    mask = o["mask"].toString("auto");
    polymorphic = o["polymorphic"].toBool();
    adaptiveLevel = o["adaptive_level"].toInt();
    dns = o["dns"].toString();
    return ParseFromLink(o["key"].toString());
}

QJsonObject aivpn::ExportToJson() {
    QJsonObject o{{"type", "aivpn"}, {"key", key}};
    if (!name.isEmpty()) o["name"] = name;
    if (mask != "auto") o["mask"] = mask;
    if (polymorphic) o["polymorphic"] = true;
    if (adaptiveLevel > 0) o["adaptive_level"] = adaptiveLevel;
    if (!dns.trimmed().isEmpty()) o["dns"] = dns.trimmed();
    return o;
}

QJsonObject aivpn::ExportIdentity() { auto o = ExportToJson(); o.remove("name"); return o; }

QString aivpn::DisplayAddress() { return ::DisplayAddress(server, server_port); }

QString aivpn::Validate() const {
    if (keyPayload(key).isEmpty()) return QStringLiteral("AIVPN: вставьте ключ подключения aivpn://…");
    if (!Masks().contains(mask)) return QStringLiteral("AIVPN: неизвестная маска %1").arg(mask);
    if (adaptiveLevel < 0 || adaptiveLevel > 3) return QStringLiteral("AIVPN: уровень адаптации от 0 до 3");
    // --proxy-dns: up to four IPv4 addresses.
    const auto servers = dns.split(',', Qt::SkipEmptyParts);
    if (servers.size() > 4) return QStringLiteral("AIVPN: не больше четырёх DNS-серверов");
    for (const auto &item : servers)
        if (QHostAddress(item.trimmed()).protocol() != QAbstractSocket::IPv4Protocol)
            return QStringLiteral("AIVPN: DNS-серверы задаются адресами IPv4");
    return {};
}

QString aivpn::Prepare() {
    if (const auto error = Validate(); !error.isEmpty()) return error;
    QString executable = "aivpn-client";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    extraCorePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!QFileInfo(extraCorePath).isExecutable()) return QStringLiteral("AIVPN: клиент не входит в эту сборку");
    // The SOCKS listener and the client's own admin port: each instance needs ports of its own.
    const auto ports = MkManyPorts(2);
    if (ports.size() < 2) return QStringLiteral("AIVPN: не удалось открыть локальный порт");
    socksPort = ports[0];
    adminPort = ports[1];
    bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // backend, peer and device_id are read by RawlineCore: its startup budget and one probe per server session.
    const auto payload = keyPayload(key);
    QJsonObject config{{"backend", "aivpn"},
                       {"peer", payload["s"].toString()},
                       {"device_id", QString::fromLatin1(QCryptographicHash::hash(payload["k"].toString().toUtf8(), QCryptographicHash::Sha256).toHex())},
                       {"key", key.trimmed()},
                       {"socks", "127.0.0.1:" + QString::number(socksPort)},
                       {"admin", "127.0.0.1:" + QString::number(adminPort)},
                       {"socks_user", bridgeAuth},
                       {"socks_pass", bridgeAuth},
                       {"mask", mask},
                       {"polymorphic", polymorphic},
                       {"adaptive_level", adaptiveLevel}};
    QJsonArray servers;
    for (const auto &item : dns.split(',', Qt::SkipEmptyParts)) servers << item.trimmed();
    if (!servers.isEmpty()) config["dns"] = servers;
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    extraCoreArgs = "--rawline-config %s";
    noLogs = true;
    return {};
}

BuildResult aivpn::Build() {
    if (socksPort == 0 || bridgeAuth.isEmpty()) return {{}, "AIVPN bridge has not been prepared"};
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort}, {"version", "5"},
                        {"username", bridgeAuth}, {"password", bridgeAuth}}, {}};
}
}
