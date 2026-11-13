#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QWidget>
#include "profile_editor.h"
class QFormLayout;

// DNSTT, VayDNS, SlipStream, MasterDNS, CottenDNS and Soar: one editor, fields by transport.
class EditDnsTunnel : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditDnsTunnel(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QString kind;
    QFormLayout *form;
    QLineEdit *domain, *pubkey, *mkey, *idleTimeout, *keepalive, *user, *password, *soarCountry;
    QPlainTextEdit *cert, *resolvers, *privateKey, *masterOptions;
    QComboBox *encryption, *cottenPreset, *recordType, *congestion, *backend, *method, *soarTransport;
    QCheckBox *dnsttCompat, *authoritative, *soarDiscovery;
    QSpinBox *clientIdSize, *keepAliveInterval;
    QLabel *note;
    // MasterDnsVPN client keys with a field of their own; an empty field keeps the engine default.
    QMap<QString, QWidget *> masterFields;
    void setRowVisible(QWidget *field, bool visible);
    void updateFields();
};
