#include "include/ui/profile/edit_aivpn.h"
#include "include/configs/outbounds/aivpn.h"
#include <QFormLayout>
#include <QLabel>

EditAivpn::EditAivpn(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    key = new QLineEdit(this);
    key->setPlaceholderText("aivpn://…");
    form->addRow(QStringLiteral("Ключ подключения"), key);
    mask = new QComboBox(this);
    // The original clients' mask list; "auto" lets the server choose.
    for (const auto &item : Configs::aivpn::Masks()) mask->addItem(item == "auto" ? QStringLiteral("Автоматически") : item, item);
    form->addRow(QStringLiteral("Маска трафика"), mask);
    polymorphic = new QCheckBox(QStringLiteral("Полиморфная маска на основе выбранной"), this);
    form->addRow(polymorphic);
    adaptive = new QSpinBox(this);
    adaptive->setRange(0, 3);
    form->addRow(QStringLiteral("Уровень адаптации"), adaptive);
    dns = new QLineEdit(this);
    dns->setPlaceholderText("1.1.1.1,9.9.9.9");
    form->addRow(QStringLiteral("DNS внутри туннеля"), dns);
    auto *note = new QLabel(QStringLiteral("Ключ выдаёт сервер AIVPN. До четырёх DNS-серверов IPv4 через запятую; пусто — как в оригинальном клиенте."), this);
    note->setWordWrap(true);
    form->addRow(note);
}

void EditAivpn::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::aivpn *>(ent->outbound.get());
    if (!o) return;
    key->setText(o->key);
    mask->setCurrentIndex(qMax(0, mask->findData(o->mask)));
    polymorphic->setChecked(o->polymorphic);
    adaptive->setValue(o->adaptiveLevel);
    dns->setText(o->dns);
}

bool EditAivpn::onEnd() {
    auto *o = dynamic_cast<Configs::aivpn *>(ent->outbound.get());
    if (!o) return false;
    Configs::aivpn updated;
    updated.name = o->name;
    updated.key = key->text().trimmed();
    updated.mask = mask->currentData().toString();
    updated.polymorphic = polymorphic->isChecked();
    updated.adaptiveLevel = adaptive->value();
    updated.dns = dns->text().simplified().remove(' ');
    if (const auto error = updated.Validate(); !error.isEmpty()) { MessageBoxWarning("AIVPN", error); return false; }
    return o->ParseFromJson(updated.ExportToJson());
}
