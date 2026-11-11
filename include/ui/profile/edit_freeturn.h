#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QWidget>
#include "profile_editor.h"
class EditFreeTurn : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditFreeTurn(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QComboBox *hashSource, *provider, *tunnelMode, *relayTransport, *obfProfile, *dnsMode;
    QPlainTextEdit *wgConfig;
    QFormLayout *form;
    // One call per field, as the other relays' hashes; a profile with more calls gets more fields.
    QList<QLineEdit *> hashes;
    void setHashCount(int count);
    void updateSource();
    QLineEdit *obfKey, *clientId, *dnsServers, *turnHost;
    QSpinBox *threads, *streamsPerCred, *obfTiming;
    QCheckBox *bond, *manualCaptcha;
};
