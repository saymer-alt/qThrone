#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QWidget>
#include "profile_editor.h"

class EditAivpn : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditAivpn(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QLineEdit *key, *dns;
    QComboBox *mask;
    QCheckBox *polymorphic;
    QSpinBox *adaptive;
};
