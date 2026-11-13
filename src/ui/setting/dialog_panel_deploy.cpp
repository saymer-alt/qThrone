#include "include/ui/setting/dialog_panel_deploy.h"
#include "include/global/Utils.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace {
struct Choice {
    QString value, title, text;
};
const QList<Choice> &roleChoices() {
    static const QList<Choice> list{
        {"combined", "Панель и нода", "Веб-панель и VPN-протоколы на одном VPS. Подходит, если сервер один."},
        {"panel", "Только панель", "Пользователи, подписки и управление нодами. VPN-протоколы на этом VPS не ставятся, ноды подключаются из панели."},
        {"node", "Только нода", "VPN-протоколы без веб-панели. Нода подключается к панели на другом VPS по SSH."},
    };
    return list;
}
const QList<Choice> &accessChoices() {
    static const QList<Choice> list{
        {"nginx", "Nginx", "Панель и подписки по HTTPS на порту 443 с сертификатом Let's Encrypt. Если Nginx на VPS уже работает, панель добавится отдельным сайтом, остальные сайты не меняются."},
        {"caddy", "Caddy", "Панель и подписки по HTTPS на порту 443 с сертификатом Let's Encrypt. Если Caddy на VPS уже работает, панель добавится отдельным сайтом."},
        {"local", "Локальный порт", "Панель слушает только 127.0.0.1 на VPS и открывается из Rawline через SSH. Домен не нужен, порты 80 и 443 остаются свободными. Подписки по ссылке в этом режиме не работают, конфиги копируются из панели вручную. Самый совместимый режим."},
    };
    return list;
}
struct Protocol {
    QString id, title;
    int group; // 0: VPN, 1: DNS tunnel, 2: other
};
const QList<Protocol> &protocolList() {
    static const QList<Protocol> list{
        {"wdtt", "WDTT", 0}, {"qwdtt", "qWDTT", 0}, {"wdtt-plus", "WDTT Plus", 0}, {"csqtt-2.1.9", "CSQTT 2.1.9", 0},
        {"csqtt-2.5.0", "CSQTT 2.5.0", 0}, {"csqtt-2.5.3-community", "CSQTT 2.5.3 (community)", 0}, {"freeturn", "FreeTurn", 0},
        {"dnstt", "DNSTT", 1}, {"vaydns", "VayDNS", 1}, {"slipstream", "Slipstream", 1}, {"masterdns", "MasterDNS / StormDNS", 1},
        {"cottendns", "CottenDNS", 1}, {"soar", "Soar", 1}, {"openflux", "OpenFlux Direct", 2},
    };
    return list;
}
QString roleTitle(const QString &role) {
    for (const auto &choice : roleChoices())
        if (choice.value == role) return choice.title;
    return QStringLiteral("Ничего не установлено");
}
QString accessTitle(const QString &mode) {
    return mode == "nginx" ? QStringLiteral("HTTPS через Nginx") : mode == "caddy" ? QStringLiteral("HTTPS через Caddy") : QStringLiteral("Локальный порт через SSH");
}
QStringList split(const QString &value) { return value.split(',', Qt::SkipEmptyParts); }
bool hasDns(const QStringList &names) {
    for (const auto &p : protocolList())
        if (p.group == 1 && names.contains(p.id)) return true;
    return false;
}
QString protocolTitles(const QStringList &names) {
    QStringList titles;
    for (const auto &p : protocolList())
        if (names.contains(p.id)) titles.append(p.title);
    return titles.isEmpty() ? QStringLiteral("нет") : titles.join(", ");
}
const QRegularExpression &hostname() {
    static const QRegularExpression pattern(R"(^([a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z][a-z0-9-]{0,61}[a-z0-9]$)");
    return pattern;
}
QLabel *note(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
QString randomSecret(int length) {
    static const QString alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    QString out;
    for (int i = 0; i < length; ++i) out += alphabet[QRandomGenerator::system()->bounded(alphabet.size())];
    return out;
}

// SSH forwards to panels on a local port; each lives until Rawline closes or the user closes it.
QMap<QString, QPointer<RelayTask>> &tunnels() {
    static QMap<QString, QPointer<RelayTask>> map;
    return map;
}
QMap<QString, int> &tunnelPorts() {
    static QMap<QString, int> map;
    return map;
}

class PanelDeploy : public QDialog {
    enum Page { Connect, Role, Protocols, Access, Domain, Dns, Check, Progress, Server };
    QJsonObject server;
    std::function<void(const QJsonObject &)> save;
    RelayTask *task;
    QStackedWidget *pages;
    QLabel *heading, *subheading, *error;
    QPushButton *back, *next;
    QList<int> history;
    bool busy = false;
    // What the wizard installs: a full install, or the missing part of an installed VPS.
    QString goal;
    // Connect
    QMap<QString, QLineEdit *> fields;
    QComboBox *auth;
    QLabel *keyState;
    QList<QWidget *> passwordRows, keyRows;
    QFormLayout *connectForm;
    // Role, access
    QButtonGroup *roleGroup, *accessGroup;
    // Protocols
    QMap<QString, QCheckBox *> protocolBoxes;
    // Domain and DNS
    QLineEdit *domain, *email, *zone, *ns;
    QLabel *domainHelp, *dnsHelp;
    // Check
    QPlainTextEdit *checkText;
    QPushButton *recheck;
    bool checkPassed = false;
    // Progress
    QLabel *step;
    QProgressBar *progress;
    QPlainTextEdit *console;
    QPushButton *stop;
    // Server
    QWidget *serverBody;

    QString value(const QString &key) const { return server[key].toString(); }
    void set(const QString &key, const QString &text) { server[key] = text; }
    void store() {
        if (save) save(server);
    }
    QWidget *page(const QString &) {
        auto *widget = new QWidget(pages);
        auto *layout = new QVBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        pages->addWidget(widget);
        return widget;
    }
    QButtonGroup *choices(QWidget *parent, const QList<Choice> &list, const QString &current) {
        auto *group = new QButtonGroup(parent);
        auto *layout = qobject_cast<QVBoxLayout *>(parent->layout());
        for (int i = 0; i < list.size(); ++i) {
            auto *button = new QRadioButton(list[i].title, parent);
            auto font = button->font();
            font.setBold(true);
            button->setFont(font);
            button->setProperty("value", list[i].value);
            button->setChecked(list[i].value == current);
            group->addButton(button, i);
            layout->addWidget(button);
            auto *text = note(list[i].text, parent);
            // Under the button's title, as the settings dialogs align a hint with its checkbox.
            text->setContentsMargins(button->style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, button) +
                                         button->style()->pixelMetric(QStyle::PM_RadioButtonLabelSpacing, nullptr, button),
                                     0, 0, 0);
            text->setForegroundRole(QPalette::PlaceholderText);
            layout->addWidget(text);
        }
        if (!group->checkedButton()) group->buttons().first()->setChecked(true);
        return group;
    }
    static QString chosen(QButtonGroup *group) { return group->checkedButton() ? group->checkedButton()->property("value").toString() : QString(); }

    void go(int index) {
        static const QStringList titles{"Подключение к VPS", "Что установить", "Протоколы", "Доступ к панели", "Домен панели", "DNS-туннели", "Проверка совместимости", "Установка", "Сервер"};
        static const QStringList texts{
            "Rawline подключится к VPS по SSH и установит WDTT Panel официальным скриптом. Лучше взять новый VPS без других VPN и панелей: так не будет конфликтов портов и сетевых правил.",
            "Панель управляет пользователями, подписками и нодами. Нода запускает VPN-протоколы. Недостающий компонент можно добавить позже.",
            "Каждый протокол получает свой инбаунд. Остальные можно добавить позже в самой панели: ядра всех протоколов на ноде собираются сразу.",
            "Как открывать панель и откуда клиенты получают подписки.",
            "Сертификат Let's Encrypt выпускается на этот домен, поэтому он должен уже указывать на VPS.",
            "DNS-туннели работают через делегированную зону, которую обслуживает этот VPS.",
            "Скрипт панели проверяет VPS, ничего не меняя: порты, сетевые права, подсети, прокси, DNS-записи и место на диске.",
            "Сборка ядер на VPS занимает от 5 до 40 минут в зависимости от процессора. Окно можно не держать открытым на переднем плане.",
            QString()};
        if (pages->currentIndex() != index && pages->currentIndex() != Progress) history.append(pages->currentIndex());
        pages->setCurrentIndex(index);
        heading->setText(index == Server ? (value("name").isEmpty() ? value("host") : value("name")) : titles[index]);
        subheading->setText(index == Server ? value("host") : texts[index]);
        error->clear();
        // Fields keep what the user typed; an empty one takes the stored value (the VPS's own zone when a node is added).
        if (index == Domain) {
            if (domain->text().isEmpty()) domain->setText(value("domain"));
            refreshDomainHelp();
        }
        if (index == Dns) {
            if (zone->text().isEmpty()) zone->setText(value("dnsZone"));
            if (ns->text().isEmpty()) ns->setText(value("dnsNs"));
            refreshDnsHelp();
        }
        if (index == Server) {
            history.clear();
            buildServer();
        }
        if (index == Check) runCheck();
        updateButtons();
    }
    void updateButtons() {
        const int index = pages->currentIndex();
        back->setVisible(index != Server && !(index == Progress));
        back->setEnabled(!busy && !history.isEmpty());
        next->setVisible(index != Server && index != Progress);
        next->setText(index == Check ? QStringLiteral("Установить") : QStringLiteral("Далее"));
        next->setEnabled(!busy && (index != Check || checkPassed));
        stop->setEnabled(busy);
        recheck->setEnabled(!busy);
        if (index == Connect) for (auto *edit : fields) edit->setEnabled(!busy);
    }
    void goBack() {
        if (busy || history.isEmpty()) return;
        const int previous = history.takeLast();
        pages->setCurrentIndex(previous);
        go(previous);
    }
    void fail(const QString &text) {
        error->setText(text);
    }

    QJsonObject request() const {
        QJsonObject s;
        for (const auto &key : {"host", "sshPort", "user", "auth", "sshPassword", "key", "keyPassword", "sudoPassword", "hostKey", "role",
                                "protocols", "access", "domain", "email", "dnsZone", "dnsNs", "panelPort", "panelUser", "panelPassword"})
            s[key] = server[key].toString();
        return s;
    }
    // One SSH action of the panel's installer; [done] runs after it ends, with the error text or empty.
    void call(const QString &action, const std::function<void(const QJsonValue &)> &result, const std::function<void(const QString &)> &done = {},
              const QString &id = {}) {
        busy = true;
        updateButtons();
        auto failure = std::make_shared<QString>();
        task->start({{"operation", "panel"}, {"value", action}, {"id", id}, {"server", request()}}, [=, this](const QJsonObject &event) {
            const auto type = event["type"].toString();
            const auto text = event["text"].toString();
            if (type == "trust") {
                task->approve(QMessageBox::question(this, "Ключ SSH сервера",
                                                    "Первое подключение к " + value("host") + ".\n\nОтпечаток ключа:\n" + text +
                                                        "\n\nСверьте его с ключом VPS у провайдера. Доверять этому ключу?") == QMessageBox::Yes);
            } else if (type == "host-key") {
                set("hostKey", text);
            } else if (type == "status") {
                applyStatus(event["value"].toObject());
            } else if (type == "result") {
                if (result) result(event["value"]);
            } else if (type == "progress") {
                step->setText(text);
                if (event["value"].isDouble()) progress->setValue(event["value"].toInt());
            } else if (type == "log") {
                console->moveCursor(QTextCursor::End);
                console->insertPlainText(text);
            } else if (type == "error") {
                *failure = text;
            } else if (type == "finished") {
                busy = false;
                updateButtons();
                if (done) done(*failure);
                else if (!failure->isEmpty()) fail(*failure);
            }
        });
    }
    void applyStatus(const QJsonObject &s) {
        QStringList names;
        for (const auto &name : s["protocols"].toArray()) names.append(name.toString());
        set("installedRole", s["role"].toString());
        set("mode", s["mode"].toString());
        set("installedDomain", s["domain"].toString());
        set("installedProtocols", names.join(','));
        set("installedZone", s["dns_zone"].toString());
        set("installedNs", s["dns_ns"].toString());
        set("publicIp", s["public_ip"].toString());
        set("panelUrl", s["panel_url"].toString());
        set("panelPort", QString::number(s["panel_port"].toInt()));
        set("panelPath", s["panel_path"].toString());
        set("panelUser", s["username"].toString());
        set("panelPassword", s["password"].toString());
        set("version", s["version"].toString());
        store();
    }

    // Screens
    void buildConnect() {
        auto *widget = page("connect");
        auto *form = connectForm = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        qobject_cast<QVBoxLayout *>(widget->layout())->addLayout(form);
        const auto add = [&](const QString &key, const QString &label, bool secret = false) {
            auto *edit = new QLineEdit(value(key), widget);
            if (secret) edit->setEchoMode(QLineEdit::Password);
            fields[key] = edit;
            form->addRow(label, edit);
            return edit;
        };
        add("name", "Название")->setPlaceholderText("Например, Амстердам");
        add("host", "IP или домен");
        add("sshPort", "Порт SSH");
        add("user", "Логин SSH");
        auth = new QComboBox(widget);
        auth->addItem("Пароль", "password");
        auth->addItem("SSH-ключ", "key");
        auth->setCurrentIndex(value("auth") == "key" ? 1 : 0);
        form->addRow("Вход", auth);
        passwordRows.append(add("sshPassword", "Пароль SSH", true));
        auto *keyRow = new QWidget(widget);
        auto *keyLine = new QHBoxLayout(keyRow);
        keyLine->setContentsMargins(0, 0, 0, 0);
        keyState = new QLabel(value("key").isEmpty() ? "Ключ не выбран" : "Ключ: " + value("keyName"), keyRow);
        auto *pick = new QPushButton("Выбрать файл", keyRow);
        keyLine->addWidget(keyState, 1);
        keyLine->addWidget(pick);
        form->addRow("SSH-ключ", keyRow);
        keyRows.append(keyRow);
        keyRows.append(add("keyPassword", "Пароль ключа", true));
        keyRows.append(add("sudoPassword", "Пароль sudo", true));
        fields["sudoPassword"]->setPlaceholderText("Если логин не root и sudo спрашивает пароль");
        connect(pick, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, "SSH-ключ");
            if (path.isEmpty()) return;
            const auto text = ReadFileText(path);
            if (text.isEmpty() || text.size() > 65536) {
                fail("Не удалось прочитать ключ");
                return;
            }
            set("key", text);
            set("keyName", QFileInfo(path).fileName());
            keyState->setText("Ключ: " + value("keyName"));
        });
        const auto visibility = [this] {
            const bool key = auth->currentData() == "key";
            for (auto *row : passwordRows) {
                row->setVisible(!key);
                connectForm->labelForField(row)->setVisible(!key);
            }
            for (auto *row : keyRows) {
                row->setVisible(key);
                connectForm->labelForField(row)->setVisible(key);
            }
        };
        connect(auth, &QComboBox::currentIndexChanged, this, visibility);
        visibility();
        qobject_cast<QVBoxLayout *>(widget->layout())->addStretch();
    }
    void connectNext() {
        for (auto it = fields.begin(); it != fields.end(); ++it) set(it.key(), it.value()->text().trimmed());
        set("auth", auth->currentData().toString());
        if (value("auth") == "key") set("sshPassword", "");
        else set("sudoPassword", "");
        if (!QRegularExpression("^[A-Za-z0-9.:_-]+$").match(value("host")).hasMatch()) return fail("Укажите IP или домен VPS без схемы и порта");
        if (!QRegularExpression("^[A-Za-z0-9_.-]+$").match(value("user")).hasMatch()) return fail("Укажите логин SSH");
        const int port = value("sshPort").toInt();
        if (port < 1 || port > 65535) return fail("Порт SSH: от 1 до 65535");
        if (value("auth") == "key" ? value("key").isEmpty() : value("sshPassword").isEmpty()) return fail(value("auth") == "key" ? "Выберите SSH-ключ" : "Введите пароль SSH");
        // A changed address is another server: its old key and state no longer apply.
        if (value("host") != value("connectedHost") || value("sshPort") != value("connectedPort")) {
            set("hostKey", "");
            set("installedRole", "");
        }
        step->setText("Подключение");
        call("status", {}, [this](const QString &failure) {
            if (!failure.isEmpty()) return fail(failure);
            set("connectedHost", value("host"));
            set("connectedPort", value("sshPort"));
            store();
            if (!value("installedRole").isEmpty()) return go(Server);
            goal = "install";
            go(Role);
        });
    }
    void buildRole() {
        auto *widget = page("role");
        roleGroup = choices(widget, roleChoices(), value("role").isEmpty() ? "combined" : value("role"));
        qobject_cast<QVBoxLayout *>(widget->layout())->addStretch();
    }
    void buildProtocols() {
        auto *widget = page("protocols");
        auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());
        const QStringList selected = value("protocols").isEmpty() ? QStringList{"wdtt", "qwdtt", "wdtt-plus", "csqtt-2.1.9", "csqtt-2.5.0", "csqtt-2.5.3-community", "freeturn"} : split(value("protocols"));
        const QStringList groups{"VPN через звонки", "DNS-туннели (нужна делегированная зона)", "Прямое подключение"};
        for (int g = 0; g < groups.size(); ++g) {
            auto *box = new QGroupBox(groups[g], widget);
            auto *grid = new QVBoxLayout(box);
            for (const auto &p : protocolList()) {
                if (p.group != g) continue;
                auto *check = new QCheckBox(p.title, box);
                check->setChecked(selected.contains(p.id));
                protocolBoxes[p.id] = check;
                grid->addWidget(check);
            }
            layout->addWidget(box);
        }
        layout->addStretch();
    }
    QStringList checkedProtocols() const {
        QStringList names;
        for (const auto &p : protocolList())
            if (protocolBoxes[p.id]->isChecked()) names.append(p.id);
        return names;
    }
    void buildAccess() {
        auto *widget = page("access");
        accessGroup = choices(widget, accessChoices(), value("access").isEmpty() ? "nginx" : value("access"));
        qobject_cast<QVBoxLayout *>(widget->layout())->addStretch();
    }
    void buildDomain() {
        auto *widget = page("domain");
        auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());
        auto *form = new QFormLayout;
        domain = new QLineEdit(value("domain"), widget);
        domain->setPlaceholderText("panel.example.org");
        email = new QLineEdit(value("email"), widget);
        email->setPlaceholderText("Необязательно: письма о сроке сертификата");
        form->addRow("Домен", domain);
        form->addRow("Email", email);
        layout->addLayout(form);
        domainHelp = note({}, widget);
        layout->addWidget(domainHelp);
        connect(domain, &QLineEdit::textChanged, this, [this] { refreshDomainHelp(); });
        layout->addStretch();
    }
    void refreshDomainHelp() {
        const auto name = domain->text().trimmed().isEmpty() ? QStringLiteral("panel.example.org") : domain->text().trimmed().toLower();
        const auto ip = value("publicIp").isEmpty() ? QStringLiteral("IP VPS") : value("publicIp");
        domainHelp->setText("Создайте у регистратора домена A-запись:\n" + name + "  A  " + ip +
                            "\n\nЕсли домен в Cloudflare, выключите проксирование (серое облако): сертификат выпускается напрямую на VPS. "
                            "Новая запись обычно начинает работать за несколько минут.");
    }
    void buildDns() {
        auto *widget = page("dns");
        auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());
        auto *form = new QFormLayout;
        zone = new QLineEdit(value("dnsZone"), widget);
        zone->setPlaceholderText("t.example.org");
        ns = new QLineEdit(value("dnsNs"), widget);
        ns->setPlaceholderText("ns.example.org");
        form->addRow("Зона туннелей", zone);
        form->addRow("NS-сервер", ns);
        layout->addLayout(form);
        dnsHelp = note({}, widget);
        dnsHelp->setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        layout->addWidget(dnsHelp);
        connect(zone, &QLineEdit::textChanged, this, [this] { refreshDnsHelp(); });
        connect(ns, &QLineEdit::textChanged, this, [this] { refreshDnsHelp(); });
        layout->addStretch();
    }
    void refreshDnsHelp() {
        const auto z = zone->text().trimmed().isEmpty() ? QStringLiteral("t.example.org") : zone->text().trimmed().toLower();
        const auto n = ns->text().trimmed().isEmpty() ? QStringLiteral("ns.example.org") : ns->text().trimmed().toLower();
        const auto ip = value("publicIp").isEmpty() ? QStringLiteral("IP VPS") : value("publicIp");
        dnsHelp->setText("Создайте у регистратора две записи:\n" + n + "  A  " + ip + "\n" + z + "  NS  " + n +
                         "\n\nNS-сервер должен быть вне зоны, CDN и проксирование для обеих записей выключите. "
                         "На VPS должны быть свободны порты 53 TCP и UDP. Для Slipstream зона не длиннее 18 символов, для Soar не длиннее 26.");
    }
    void buildCheck() {
        auto *widget = page("check");
        auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());
        checkText = new QPlainTextEdit(widget);
        checkText->setReadOnly(true);
        checkText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(checkText, 1);
        recheck = new QPushButton("Проверить снова", widget);
        layout->addWidget(recheck, 0, Qt::AlignLeft);
        connect(recheck, &QPushButton::clicked, this, [this] { runCheck(); });
    }
    void buildProgress() {
        auto *widget = page("progress");
        auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());
        step = new QLabel(widget);
        layout->addWidget(step);
        progress = new QProgressBar(widget);
        progress->setRange(0, 100);
        layout->addWidget(progress);
        console = new QPlainTextEdit(widget);
        console->setReadOnly(true);
        console->setMaximumBlockCount(6000);
        console->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(console, 1);
        stop = new QPushButton("Остановить", widget);
        layout->addWidget(stop, 0, Qt::AlignLeft);
        connect(stop, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, "Остановить", "Остановить операцию? Уже выполненные изменения на VPS сохранятся, установку можно запустить снова.") == QMessageBox::Yes)
                task->cancel();
        });
    }

    // Steps
    void roleNext() {
        set("role", chosen(roleGroup));
        if (value("role") != "panel") return go(Protocols);
        set("protocols", "");
        go(Access);
    }
    void protocolsNext() {
        const auto names = checkedProtocols();
        if (names.isEmpty()) return fail("Выберите хотя бы один протокол");
        set("protocols", names.join(','));
        // A node added to a panel keeps the panel's access and domain.
        if (goal == "add-node") return go(hasDns(names) ? Dns : Check);
        // A node alone is reached by the panel over SSH: it needs no public address of its own.
        if (value("role") == "node") {
            set("access", "local");
            set("domain", "");
            return afterDomain();
        }
        go(Access);
    }
    void accessNext() {
        set("access", chosen(accessGroup));
        if (value("access") != "local") return go(Domain);
        set("domain", "");
        afterDomain();
    }
    void afterDomain() { go(value("role") != "panel" && hasDns(split(value("protocols"))) ? Dns : Check); }
    // The name must resolve to this VPS from the VPS itself, as the installer's own check sees it.
    void resolveThen(const QString &name, const QString &label, const std::function<void()> &then) {
        call(
            "resolve",
            [=, this](const QJsonValue &result) {
                QStringList found;
                for (const auto &ip : result.toArray()) found.append(ip.toString());
                if (found.isEmpty()) return fail(label + " " + name + " пока не находится в DNS. Проверьте запись и повторите через несколько минут.");
                if (!value("publicIp").isEmpty() && !found.contains(value("publicIp")))
                    return fail(label + " " + name + " указывает на " + found.join(", ") + ", а IP этого VPS " + value("publicIp") + ".");
                then();
            },
            [this](const QString &failure) { if (!failure.isEmpty()) fail(failure); }, name);
    }
    void domainNext() {
        const auto name = domain->text().trimmed().toLower();
        if (!hostname().match(name).hasMatch()) return fail("Укажите домен без схемы, порта и пути");
        const auto mail = email->text().trimmed();
        if (!mail.isEmpty() && !QRegularExpression(R"(^[^\s@'"\\]+@[^\s@'"\\]+\.[^\s@'"\\]+$)").match(mail).hasMatch()) return fail("Некорректный email");
        set("domain", name);
        set("email", mail);
        resolveThen(name, "Домен", [this] { afterDomain(); });
    }
    void dnsNext() {
        const auto z = zone->text().trimmed().toLower(), n = ns->text().trimmed().toLower();
        if (!hostname().match(z).hasMatch() || !hostname().match(n).hasMatch()) return fail("Укажите зону и NS-сервер, например t.example.org и ns.example.org");
        if (n.endsWith("." + z)) return fail("NS-сервер должен быть вне зоны " + z);
        const auto names = split(value("protocols"));
        if (names.contains("slipstream") && z.size() > 18) return fail("Для Slipstream зона не длиннее 18 символов");
        if (names.contains("soar") && z.size() > 26) return fail("Для Soar зона не длиннее 26 символов");
        set("dnsZone", z);
        set("dnsNs", n);
        resolveThen(n, "NS-сервер", [this] { go(Check); });
    }
    void runCheck() {
        checkPassed = false;
        checkText->setPlainText("Проверка…");
        updateButtons();
        call(
            "check",
            [this](const QJsonValue &result) {
                const auto object = result.toObject();
                checkPassed = object["ok"].toBool();
                const auto lines = object["text"].toString().split('\n');
                QStringList shown;
                for (const auto &line : lines)
                    if (!line.startsWith("==> ")) shown.append(line);
                checkText->setPlainText(shown.join('\n').trimmed());
            },
            [this](const QString &failure) {
                if (!failure.isEmpty()) checkText->setPlainText(failure);
                updateButtons();
            });
    }
    void install() {
        console->clear();
        progress->setValue(0);
        step->setText("Установка");
        go(Progress);
        call("install", {}, [this](const QString &failure) {
            if (!failure.isEmpty()) {
                step->setText(failure);
                console->appendPlainText("\n" + failure);
                fail(failure);
                // The page stays for the log; the server page shows what is installed now.
                auto *open = new QPushButton("К серверу", pages->widget(Progress));
                qobject_cast<QVBoxLayout *>(pages->widget(Progress)->layout())->addWidget(open, 0, Qt::AlignLeft);
                connect(open, &QPushButton::clicked, this, [this, open] { open->deleteLater(); go(Server); });
                return;
            }
            go(Server);
        });
    }
    void forward() {
        error->clear();
        switch (pages->currentIndex()) {
        case Connect: return connectNext();
        case Role: return roleNext();
        case Protocols: return protocolsNext();
        case Access: return accessNext();
        case Domain: return domainNext();
        case Dns: return dnsNext();
        case Check: return install();
        default: return;
        }
    }

    // The server's page: what is installed, the panel's address and login, adding and removing parts.
    void startAdd(const QString &part) {
        // The installer keeps the other part's settings: the node's protocols, the panel's access and domain.
        goal = part == "node" ? "add-node" : "add-panel";
        set("role", part);
        set("access", value("mode").isEmpty() ? "local" : value("mode"));
        set("domain", value("installedDomain"));
        set("protocols", value("installedProtocols"));
        set("dnsZone", value("installedZone"));
        set("dnsNs", value("installedNs"));
        if (part == "node") {
            set("protocols", "");
            for (auto *box : protocolBoxes) box->setChecked(false);
            go(Protocols);
        } else
            go(Access);
    }
    void uninstall(const QString &target) {
        const QString what = target == "all" ? "всю установку: панель, ноду, пользователей и сертификаты панели" : target == "panel" ? "панель. Нода, пользователи и настройки останутся" : "ноду и её протоколы. Панель, пользователи и настройки останутся";
        if (QMessageBox::question(this, "Удаление с VPS", "Удалить с " + value("host") + " " + what + "?") != QMessageBox::Yes) return;
        console->clear();
        progress->setValue(0);
        step->setText("Удаление");
        go(Progress);
        call("uninstall-" + target, {}, [this](const QString &failure) {
            go(Server);
            if (!failure.isEmpty()) fail(failure);
        });
    }
    void changeLogin() {
        QDialog dialog(this);
        dialog.setWindowTitle("Логин и пароль панели");
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout;
        auto *user = new QLineEdit(value("panelUser"), &dialog);
        auto *password = new QLineEdit(randomSecret(20), &dialog);
        form->addRow("Логин", user);
        form->addRow("Пароль", password);
        layout->addLayout(form);
        auto *generate = new QPushButton("Создать пароль", &dialog);
        layout->addWidget(generate, 0, Qt::AlignLeft);
        connect(generate, &QPushButton::clicked, &dialog, [password] { password->setText(randomSecret(20)); });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(420, dialog.sizeHint().height());
        if (dialog.exec() != QDialog::Accepted) return;
        const auto newUser = user->text(), newPassword = password->text();
        if (newUser.trimmed() != newUser || newPassword.trimmed() != newPassword || newUser.isEmpty() || newPassword.isEmpty() || newUser.size() > 256 || newPassword.size() > 256)
            return fail("Логин и пароль: без пробелов по краям, до 256 символов");
        const auto oldUser = value("panelUser"), oldPassword = value("panelPassword");
        set("panelUser", newUser);
        set("panelPassword", newPassword);
        call("credentials", {}, [=, this](const QString &failure) {
            if (!failure.isEmpty()) {
                set("panelUser", oldUser);
                set("panelPassword", oldPassword);
                fail(failure);
            }
            buildServer();
        });
    }
    void openPanel() {
        if (value("mode") != "local") {
            QDesktopServices::openUrl(QUrl(value("panelUrl")));
            return;
        }
        const auto id = value("id");
        const auto open = [path = value("panelPath")](int port) { QDesktopServices::openUrl(QUrl(QString("http://127.0.0.1:%1%2").arg(port).arg(path))); };
        if (tunnels().value(id) && tunnelPorts().contains(id)) return open(tunnelPorts()[id]);
        auto *tunnel = new RelayTask(qApp);
        tunnels()[id] = tunnel;
        QPointer<PanelDeploy> self(this);
        tunnel->start({{"operation", "panel"}, {"value", "tunnel"}, {"server", request()}}, [=](const QJsonObject &event) {
            const auto type = event["type"].toString();
            if (type == "trust") tunnel->approve(false);
            if (type == "result") {
                tunnelPorts()[id] = event["value"].toObject()["port"].toInt();
                open(tunnelPorts()[id]);
            }
            if (type == "error" && self) self->fail(event["text"].toString());
            if (type == "finished") {
                tunnels().remove(id);
                tunnelPorts().remove(id);
                tunnel->deleteLater();
            }
            if ((type == "result" || type == "finished") && self && self->pages->currentIndex() == Server) self->buildServer();
        });
    }
    QWidget *secretRow(QWidget *parent, const QString &text, bool secret) {
        auto *row = new QWidget(parent);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(text, row);
        edit->setReadOnly(true);
        if (secret) edit->setEchoMode(QLineEdit::Password);
        line->addWidget(edit, 1);
        if (secret) {
            auto *eye = new QPushButton("Показать", row);
            eye->setCheckable(true);
            connect(eye, &QPushButton::toggled, row, [edit, eye](bool on) {
                edit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
                eye->setText(on ? "Скрыть" : "Показать");
            });
            line->addWidget(eye);
        }
        auto *copy = new QPushButton("Копировать", row);
        connect(copy, &QPushButton::clicked, row, [text] { QApplication::clipboard()->setText(text); });
        line->addWidget(copy);
        return row;
    }
    void buildServer() {
        // A button of the old page may be the caller: it goes once the event is handled.
        serverBody->hide();
        serverBody->deleteLater();
        serverBody = new QWidget(pages->widget(Server));
        pages->widget(Server)->layout()->addWidget(serverBody);
        auto *layout = new QVBoxLayout(serverBody);
        layout->setContentsMargins(0, 0, 0, 0);
        const auto role = value("installedRole");
        heading->setText(value("name").isEmpty() ? value("host") : value("name"));
        subheading->setText(value("host") + (value("version").isEmpty() ? QString() : "   WDTT Panel " + value("version")));
        auto *summary = new QGroupBox("Установлено", serverBody);
        auto *summaryForm = new QFormLayout(summary);
        summaryForm->addRow("Компоненты", new QLabel(roleTitle(role), summary));
        if (role == "combined" || role == "node") {
            auto *list = note(protocolTitles(split(value("installedProtocols"))), summary);
            summaryForm->addRow("Протоколы", list);
        }
        if (!role.isEmpty()) summaryForm->addRow("Доступ", new QLabel(accessTitle(value("mode")) + (value("installedDomain").isEmpty() ? QString() : ", " + value("installedDomain")), summary));
        layout->addWidget(summary);
        if (role == "combined" || role == "panel") {
            auto *panel = new QGroupBox("Панель", serverBody);
            auto *form = new QFormLayout(panel);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            if (value("mode") == "local") {
                const int port = tunnelPorts().value(value("id"));
                form->addRow(note(port ? QString("Панель открыта через SSH на http://127.0.0.1:%1%2").arg(port).arg(value("panelPath"))
                                       : QStringLiteral("Панель доступна только с VPS. Rawline откроет её через SSH на этом компьютере, не меняя маршруты и TUN."),
                                  panel));
            } else
                form->addRow("Адрес", secretRow(panel, value("panelUrl"), false));
            form->addRow("Логин", secretRow(panel, value("panelUser"), false));
            form->addRow("Пароль", secretRow(panel, value("panelPassword"), true));
            auto *actions = new QHBoxLayout;
            auto *open = new QPushButton("Открыть панель", panel);
            auto *change = new QPushButton("Сменить логин и пароль", panel);
            actions->addWidget(open);
            actions->addWidget(change);
            if (value("mode") == "local" && tunnels().value(value("id"))) {
                auto *close = new QPushButton("Закрыть доступ", panel);
                actions->addWidget(close);
                connect(close, &QPushButton::clicked, this, [this] { if (auto task = tunnels().value(value("id"))) task->cancel(); });
            }
            actions->addStretch();
            form->addRow(actions);
            connect(open, &QPushButton::clicked, this, [this] { openPanel(); });
            connect(change, &QPushButton::clicked, this, [this] { changeLogin(); });
            for (auto *button : {open, change}) button->setEnabled(!busy);
            layout->addWidget(panel);
        }
        if (role.isEmpty()) layout->addWidget(note("На этом VPS панель и нода не установлены.", serverBody));
        auto *actions = new QHBoxLayout;
        if (role.isEmpty()) {
            auto *installButton = new QPushButton("Установить", serverBody);
            connect(installButton, &QPushButton::clicked, this, [this] { goal = "install"; go(Role); });
            actions->addWidget(installButton);
        }
        if (role == "panel" || role == "node") {
            auto *add = new QPushButton(role == "panel" ? "Добавить ноду" : "Добавить панель", serverBody);
            connect(add, &QPushButton::clicked, this, [this, role] { startAdd(role == "panel" ? "node" : "panel"); });
            actions->addWidget(add);
        }
        if (!role.isEmpty()) {
            auto *remove = new QToolButton(serverBody);
            remove->setText("Удалить");
            remove->setPopupMode(QToolButton::MenuButtonPopup);
            remove->setToolButtonStyle(Qt::ToolButtonTextOnly);
            auto *menu = new QMenu(remove);
            const auto item = [this, menu](const QString &title, const QString &target) {
                connect(menu->addAction(title), &QAction::triggered, this, [this, target] { uninstall(target); });
            };
            item("Всё", "all");
            if (role == "combined") {
                item("Только панель", "panel");
                item("Только ноду", "node");
            }
            remove->setMenu(menu);
            connect(remove, &QToolButton::clicked, this, [this] { uninstall("all"); });
            actions->addWidget(remove);
        }
        auto *refresh = new QPushButton("Обновить", serverBody);
        connect(refresh, &QPushButton::clicked, this, [this] {
            call("status", {}, [this](const QString &failure) {
                if (!failure.isEmpty()) fail(failure);
                buildServer();
            });
        });
        actions->addWidget(refresh);
        auto *ssh = new QPushButton("Подключение", serverBody);
        connect(ssh, &QPushButton::clicked, this, [this] { go(Connect); history.clear(); updateButtons(); });
        actions->addWidget(ssh);
        actions->addStretch();
        for (int i = 0; i < actions->count(); ++i)
            if (auto *w = actions->itemAt(i)->widget()) w->setEnabled(!busy);
        layout->addLayout(actions);
        layout->addStretch();
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (task->running()) {
            if (QMessageBox::question(this, "Операция выполняется", "Остановить операцию на VPS? Уже выполненные изменения сохранятся.") != QMessageBox::Yes) {
                event->ignore();
                return;
            }
            task->cancel();
        }
        QDialog::closeEvent(event);
    }

  public:
    PanelDeploy(QJsonObject initial, std::function<void(const QJsonObject &)> callback, QWidget *parent)
        : QDialog(parent), server(std::move(initial)), save(std::move(callback)) {
        setWindowTitle("Деплой WDTT Panel");
        resize(760, 650);
        task = new RelayTask(this);
        auto *outer = new QVBoxLayout(this);
        heading = new QLabel(this);
        auto font = heading->font();
        if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * 1.2);
        font.setBold(true);
        heading->setFont(font);
        outer->addWidget(heading);
        subheading = note({}, this);
        subheading->setForegroundRole(QPalette::PlaceholderText);
        outer->addWidget(subheading);
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        pages = new QStackedWidget(scroll);
        scroll->setWidget(pages);
        outer->addWidget(scroll, 1);
        error = note({}, this);
        error->setStyleSheet("color: #d64545;");
        outer->addWidget(error);
        auto *buttons = new QHBoxLayout;
        back = new QPushButton("Назад", this);
        next = new QPushButton("Далее", this);
        next->setDefault(true);
        buttons->addWidget(back);
        buttons->addStretch();
        buttons->addWidget(next);
        outer->addLayout(buttons);
        connect(back, &QPushButton::clicked, this, [this] { goBack(); });
        connect(next, &QPushButton::clicked, this, [this] { forward(); });
        buildConnect();
        buildRole();
        buildProtocols();
        buildAccess();
        buildDomain();
        buildDns();
        buildCheck();
        buildProgress();
        auto *serverPage = page("server");
        serverBody = new QWidget(serverPage);
        serverPage->layout()->addWidget(serverBody);
        go(value("connectedHost").isEmpty() ? Connect : Server);
    }
};
} // namespace

QJsonObject NewPanelServer() {
    return {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"kind", "panel"}, {"name", ""}, {"user", "root"}, {"auth", "password"}, {"sshPort", "22"}};
}

QDialog *OpenPanelDeploy(QWidget *parent, const QJsonObject &server, const std::function<void(const QJsonObject &)> &save) {
    auto *dialog = new PanelDeploy(server, save, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    return dialog;
}
