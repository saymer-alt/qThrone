#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QStringList>

namespace Configs {
// An AIVPN profile: the original aivpn-client (infosave2007/aivpn) in its SOCKS5 proxy mode, behind the per-launch
// credentials Rawline's patch adds. The connection key (aivpn://…) holds the server, its keys and the VPN address.
class aivpn : public extracore {
public:
    aivpn() { server_port = 443; socksPort = 0; }
    // The masks aivpn-client accepts by name (assets/masks); "auto" leaves the choice to the server.
    static const QStringList &Masks();
    QString key, mask = "auto", dns;
    bool polymorphic = false;
    int adaptiveLevel = 0;
    bool ParseFromLink(const QString &link) override;
    bool ParseFromJson(const QJsonObject &object) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override { return key; }
    QString DisplayType() override { return "AIVPN"; }
    QString DisplayAddress() override;
    SecurityInfo GetSecurity() override { return {"AIVPN", "Noise", SecurityLevel::Secure}; }
    QString Validate() const;
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { key.clear(); }
private:
    QString bridgeAuth;
    int adminPort = 0;
};
}
