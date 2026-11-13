#include "include/ui/setting/dialog_vk_relay.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/csqtt.h"
#include "include/configs/outbounds/wdttplus.h"
#include "include/database/ProfilesRepo.h"
#include "include/configs/outbounds/freeturn.h"
#include "include/configs/outbounds/mrtt.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/profile/edit_qwdtt.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ShareBlocks.h"
#include "include/ui/setting/dialog_panel_deploy.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMouseEvent>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMap>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QStackedWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QAbstractButton>
#include <QPainter>
#include <QShowEvent>
#include <QHideEvent>
#include <QUuid>
#include <QVBoxLayout>


RelayTask::RelayTask(QObject *parent) : QObject(parent) {
    connect(&process, &QProcess::started, this, [this] { process.write(requestBytes); });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        pending += process.readAllStandardOutput();
        while (pending.contains('\n')) {
            const int end = pending.indexOf('\n');
            const auto line = pending.left(end);
            pending.remove(0, end + 1);
            const auto event = QJsonDocument::fromJson(line).object();
            if (event["type"] == "vk-confirm") {
                approve(QMessageBox::question(qobject_cast<QWidget *>(this->parent()), "Подтверждение VK",
                                               event["text"].toString(), QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No) == QMessageBox::Yes);
                continue;
            }
            if (event["type"] == "error")
                failed = true;
            if (!event.isEmpty() && handler)
                handler(event);
        }
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        failed = true;
        if (handler)
            handler({{"type", "error"}, {"text", QStringLiteral("Не удалось запустить помощник VK-релея")}});
        if (error == QProcess::FailedToStart) {
            queue.clear();
            if (handler)
                handler({{"type", "finished"}});
        }
    });
    connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        finishing = true;
        if (!failed && (code != 0 || status != QProcess::NormalExit) && handler)
            handler({{"type", "error"}, {"text", QStringLiteral("Операция прервана")}});
        if (handler)
            handler({{"type", "finished"}});
        finishing = false;
        if (!queue.isEmpty()) {
            const auto next = queue.dequeue();
            QTimer::singleShot(0, this, [this, next] { start(next.first, next.second); });
        }
    });
}
void RelayTask::start(const QJsonObject &request, Handler callback) {
    if (running() || finishing) {
        queue.enqueue({request, std::move(callback)});
        return;
    }
    handler = std::move(callback);
    ++generation;
    pending.clear();
    failed = false;
    process.setProgram(Configs::FindCoreRealPath());
    process.setArguments({"--relay"});
    requestBytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    process.start();
}
void RelayTask::approve(bool accepted) { process.write(accepted ? "approve\n" : "reject\n"); }
void RelayTask::cancel() {
    queue.clear();
    if (running()) {
        process.write("cancel\n");
        const auto cancelledGeneration = generation;
        QTimer::singleShot(6000, &process, [this, cancelledGeneration] {
            if (generation == cancelledGeneration && running())
                process.kill();
        });
    }
}
RelayTask::~RelayTask() {
    process.disconnect(this);
    if (running()) {
        process.write("cancel\n");
        if (!process.waitForFinished(1500)) {
            process.kill();
            process.waitForFinished(1000);
        }
    }
}
bool RelayTask::running() const { return process.state() != QProcess::NotRunning; }
static QJsonValue relayAccountValue(const QString &operation, int count, QString *error) {
    QProcess process;
    process.start(Configs::FindCoreRealPath(), {"--relay"});
    if (!process.waitForStarted(5000)) {
        *error = QStringLiteral("Не удалось запустить помощник VK-релея");
        return {};
    }
    process.write(
        QJsonDocument(QJsonObject{{"operation", operation}, {"count", count}}).toJson(QJsonDocument::Compact) + '\n');
    QJsonValue result;
    QByteArray pending;
    QElapsedTimer elapsed;
    elapsed.start();
    while (true) {
        process.waitForReadyRead(100);
        pending += process.readAllStandardOutput();
        while (pending.contains('\n')) {
            const auto end = pending.indexOf('\n');
            const auto event = QJsonDocument::fromJson(pending.left(end)).object();
            pending.remove(0, end + 1);
            if (event["type"] == "vk-confirm") {
                bool accepted = false;
                const auto confirm = [&] {
                    accepted = QMessageBox::question(GetMessageBoxParent(), "Подтверждение VK",
                                                      event["text"].toString(), QMessageBox::Yes | QMessageBox::No,
                                                      QMessageBox::No) == QMessageBox::Yes;
                };
                auto *app = QCoreApplication::instance();
                if (app && app->thread() == QThread::currentThread())
                    confirm();
                else if (app)
                    QMetaObject::invokeMethod(app, confirm, Qt::BlockingQueuedConnection);
                process.write(accepted ? "approve\n" : "reject\n");
                // The browser OAuth flow has its own five-minute timeout.
                elapsed.restart();
            }
            if (event["type"] == "result") {
                result = event["value"];
            }
            if (event["type"] == "error")
                *error = event["text"].toString();
        }
        if (process.state() == QProcess::NotRunning)
            break;
        if (elapsed.elapsed() > 360000) {
            process.kill();
            process.waitForFinished(5000);
            *error = QStringLiteral("VK: превышено время ожидания");
            return {};
        }
    }
    if ((result.isNull() || result.isUndefined()) && error->isEmpty())
        *error = QStringLiteral("VK не вернул хеши");
    return result;
}
// MaxTransportRiskDialog of the original app.
bool ConfirmMaxRisk(QWidget *parent) {
    auto *settings = Configs::dataManager->settingsRepo.get();
    if (settings->mrtt_max_risk_acknowledged) return true;
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Внимание!"));
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *never = new QCheckBox(QStringLiteral("Больше не предупреждать"), &dialog);
    layout->addWidget(never);
    for (const auto &paragraph : {QStringLiteral("Транспорт MAX работает через ваш личный аккаунт MAX: туннель создаёт звонок в группе и держит его открытым, используя неофициальные методы веб-клиента."),
                                  QStringLiteral("Такое использование нарушает правила сервиса и заметно повышает риск ограничения или блокировки аккаунта MAX, вплоть до потери привязанного номера. Оценить вероятность бана заранее невозможно."),
                                  QStringLiteral("Используйте отдельный, второстепенный аккаунт и не привязывайте основной. Продолжая, вы принимаете эти риски на себя.")}) {
        auto *text = new QLabel(paragraph, &dialog);
        text->setWordWrap(true);
        layout->addWidget(text);
    }
    auto *cancel = new QPushButton(QStringLiteral("Отмена"), &dialog);
    auto *accept = new QPushButton(&dialog);
    layout->addWidget(cancel);
    layout->addWidget(accept);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto left = std::make_shared<int>(5);
    const auto show = [accept, left] {
        accept->setEnabled(*left <= 0);
        accept->setText(*left > 0 ? QStringLiteral("Всё равно продолжить (%1)").arg(*left) : QStringLiteral("Всё равно продолжить"));
    };
    show();
    auto *timer = new QTimer(&dialog);
    QObject::connect(timer, &QTimer::timeout, &dialog, [left, show, timer] { --*left; show(); if (*left <= 0) timer->stop(); });
    timer->start(1000);
    if (dialog.exec() != QDialog::Accepted) return false;
    if (never->isChecked()) { settings->mrtt_max_risk_acknowledged = true; settings->Save(); }
    return true;
}
QJsonObject RelayMaxAccount(QString *error) {
    return relayAccountValue("max-token", 0, error).toObject();
}
QStringList RelayAccountHashes(int count, QString *error) {
    const auto value = relayAccountValue("vk-hashes", count, error);
    const auto hashes = value.isObject() ? value.toObject()["hashes"].toArray() : value.toArray();
    QStringList result;
    for (const auto &hash : hashes) result << hash.toString();
    if (error->isEmpty() && (result.size() != count || QSet<QString>(result.begin(), result.end()).size() != count))
        *error = QStringLiteral("VK не вернул необходимые хеши звонков");
    return result;
}
// Which created hashes a server's profiles use, kept outside the profiles so they stay out of exports and share links.
static QString relayHashKey(const QString &type, const QString &server, int port) { return QString("%1|%2:%3").arg(type, server.trimmed()).arg(port); }
static QStringList relayCreatedHashes(QString *error) {
    QStringList created;
    for (const auto &hash : relayAccountValue("vk-info", 0, error).toObject()["hashes"].toArray()) created << hash.toString();
    return created;
}
static QStringList relaySelected(const QString &key, const QStringList &created) {
    QStringList selected;
    const auto all = QJsonDocument::fromJson(Configs::dataManager->settingsRepo->vk_hash_selection.toUtf8()).object();
    for (const auto &hash : all[key].toArray()) if (created.contains(hash.toString())) selected << hash.toString();
    return selected;
}
QStringList RelayAutoHashes(const QString &type, const QString &server, int port, int count, QString *error) {
    QString ignored;
    const auto selected = relaySelected(relayHashKey(type, server, port), relayCreatedHashes(&ignored));
    return selected.isEmpty() ? RelayAccountHashes(count, error) : selected;
}
void RelayPickHashes(QWidget *parent, const QString &type, const QString &server, int port, int limit) {
    if (server.trimmed().isEmpty()) {
        QMessageBox::information(parent, QStringLiteral("Хеши профиля"), QStringLiteral("Сначала сохраните профиль с адресом сервера: выбор хешей привязан к серверу."));
        return;
    }
    QString error;
    const auto created = relayCreatedHashes(&error);
    if (!error.isEmpty()) { QMessageBox::warning(parent, QStringLiteral("Хеши профиля"), error); return; }
    if (created.isEmpty()) {
        QMessageBox::information(parent, QStringLiteral("Хеши профиля"), QStringLiteral("Созданных хешей пока нет. Создайте их в настройках VK-релея, вкладка генерации хешей."));
        return;
    }
    const auto key = relayHashKey(type, server, port);
    const auto current = relaySelected(key, created);
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Хеши профиля"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(QStringLiteral("Отметьте до %1 хешей для этого сервера. Если ничего не отмечено, берутся первые созданные, а недостающие создаются при подключении.").arg(limit));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *list = new QListWidget(&dialog);
    list->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    for (const auto &hash : created) {
        auto *item = new QListWidgetItem(hash, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(current.contains(hash) ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list);
    auto *count = new QLabel(&dialog);
    layout->addWidget(count);
    const auto checkedHashes = [list] {
        QStringList hashes;
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->checkState() == Qt::Checked) hashes << list->item(i)->text();
        return hashes;
    };
    const auto refreshCount = [=] { count->setText(QStringLiteral("Выбрано: %1 из %2").arg(checkedHashes().size()).arg(limit)); };
    refreshCount();
    QObject::connect(list, &QListWidget::itemChanged, &dialog, [=](QListWidgetItem *item) {
        if (item->checkState() == Qt::Checked && checkedHashes().size() > limit) item->setCheckState(Qt::Unchecked);
        refreshCount();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(560, 420);
    if (dialog.exec() != QDialog::Accepted) return;
    auto all = QJsonDocument::fromJson(Configs::dataManager->settingsRepo->vk_hash_selection.toUtf8()).object();
    all[key] = QJsonArray::fromStringList(checkedHashes());
    Configs::dataManager->settingsRepo->vk_hash_selection = QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact));
    Configs::dataManager->settingsRepo->Save();
}
QString RelayAccountToken(QString *error) {
    return relayAccountValue("vk-calls-token", 0, error).toString();
}
bool ResetQWDTTHashSources() {
    auto *settings = Configs::dataManager->settingsRepo.get();
    settings->vk_relay_linked = false;
    settings->qwdtt_hash_source = "manual";
    bool saved = settings->Save();
    for (const auto *type : {"qwdtt", "csqtt", "mrtt", "wdttplus", "freeturn"}) for (int id : Configs::dataManager->profilesRepo->GetProfileIdsByType(type)) {
        const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
        if (!profile) continue;
        auto config = profile->outbound->ExportToJson();
        if (config["hash_source"] == "auto" || (config["hash_source"] == "auto_js" && config["vk_access_token"].toString().isEmpty())) {
            if (auto *relay = dynamic_cast<Configs::qwdtt *>(profile->outbound.get())) relay->hashSource = "manual";
            if (auto *relay = dynamic_cast<Configs::wdttplus *>(profile->outbound.get())) relay->hashSource = "manual";
            if (auto *relay = dynamic_cast<Configs::freeturn *>(profile->outbound.get())) relay->hashSource = "manual";
            if (auto *relay = dynamic_cast<Configs::mrtt *>(profile->outbound.get())) relay->hashSource = "manual";
            if (auto *relay = dynamic_cast<Configs::csqtt *>(profile->outbound.get())) {
                relay->hashSource = "manual";
                if (relay->vkAnonPath == "auto_js") relay->vkAnonPath = "vkcalls";
            }
            saved = Configs::dataManager->profilesRepo->Save(profile) && saved;
        }
    }
    return saved;
}

namespace {
void autoEnabled(QComboBox *combo, bool enabled) {
    if (auto *model = qobject_cast<QStandardItemModel *>(combo->model()))
        model->item(1)->setEnabled(enabled);
}

// A press held on a row starts selection, as a long tap does on the phone.
class LongPress : public QObject {
  public:
    LongPress(QWidget *target, std::function<void()> onLong, std::function<void()> onClick)
        : QObject(target), onLong(std::move(onLong)), onClick(std::move(onClick)) {
        timer.setSingleShot(true);
        timer.setInterval(450);
        connect(&timer, &QTimer::timeout, this, [this] { this->onLong(); });
        target->installEventFilter(this);
    }
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            timer.start();
        if (event->type() == QEvent::MouseButtonRelease && timer.isActive()) {
            timer.stop();
            onClick();
        }
        return false;
    }

  private:
    QTimer timer;
    std::function<void()> onLong, onClick;
};

// Puts [hashes] into the chosen qWDTT, CSQTT and WDTT Plus profiles as their own (manual) hashes.
void ApplyHashesToProfiles(QWidget *parent, const QStringList &hashes, QLabel *status) {
    QList<std::shared_ptr<Configs::Profile>> profiles;
    for (const auto *type : {"qwdtt", "csqtt", "mrtt", "wdttplus", "freeturn"}) for (int id : Configs::dataManager->profilesRepo->GetProfileIdsByType(type))
        if (auto profile = Configs::dataManager->profilesRepo->GetProfile(id)) profiles << profile;
    if (profiles.isEmpty()) { status->setText("Нет профилей qWDTT, CSQTT, MRTT, WDTT Plus или FreeTurn"); return; }
    QDialog dialog(parent);
    dialog.setWindowTitle("Применить к профилям");
    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);
    for (const auto &profile : profiles) {
        auto *item = new QListWidgetItem(profile->outbound->DisplayTypeAndName(), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    layout->addWidget(list, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Применить");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(460, 380);
    if (dialog.exec() != QDialog::Accepted) return;
    int applied = 0;
    for (int i = 0; i < profiles.size(); i++) {
        if (list->item(i)->checkState() != Qt::Checked) continue;
        const auto &profile = profiles[i];
        // The profile editors' slots: CSQTT has six, the others four.
        if (auto *relay = dynamic_cast<Configs::qwdtt *>(profile->outbound.get())) { relay->hashes = hashes.mid(0, 4); relay->hashSource = "manual"; }
        if (auto *relay = dynamic_cast<Configs::wdttplus *>(profile->outbound.get())) { relay->hashes = hashes.mid(0, 4); relay->hashSource = "manual"; }
        if (auto *relay = dynamic_cast<Configs::csqtt *>(profile->outbound.get())) { relay->hashes = hashes.mid(0, 6); relay->hashSource = "manual"; }
        if (auto *relay = dynamic_cast<Configs::mrtt *>(profile->outbound.get())) { relay->hashes = hashes.mid(0, 6); relay->hashSource = "manual"; relay->manualRelayHashes = true; }
        if (auto *relay = dynamic_cast<Configs::freeturn *>(profile->outbound.get())) { relay->links = hashes.join('\n'); relay->hashSource = "manual"; }
        if (Configs::dataManager->profilesRepo->Save(profile)) applied++;
    }
    if (applied) status->setText(QString("Хеши применены к профилям: %1").arg(applied));
}

void ShowHashGenerator(QWidget *parent) {
    constexpr int maxHashes = 30;
    QDialog dialog(parent);
    dialog.setWindowTitle("Генерация хешей");
    dialog.resize(560, 440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *top = new QHBoxLayout;
    layout->addLayout(top);
    auto *hint = new QLabel(&dialog);
    hint->setWordWrap(true);
    top->addWidget(hint, 1);
    auto *add = new QPushButton("+", &dialog);
    add->setToolTip("Создать хеши");
    add->setFixedWidth(40);
    auto *selectAll = new QPushButton("Выбрать все", &dialog);
    auto *removeSelected = new QPushButton("Удалить", &dialog);
    auto *applySelected = new QPushButton("Применить к профилям", &dialog);
    top->addWidget(add);
    top->addWidget(applySelected);
    top->addWidget(selectAll);
    top->addWidget(removeSelected);
    auto *list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(list, 1);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    auto *task = new RelayTask(&dialog);
    struct State {
        QStringList hashes;
        QSet<QString> checked;
        bool selecting = false;
        bool busy = false;
    };
    auto state = std::make_shared<State>();
    auto rebuild = std::make_shared<std::function<void()>>();
    // Every change ends by reading the account again, so a half-finished request still shows what VK created.
    const auto reload = [=] {
        task->start({{"operation", "vk-info"}}, [=](const QJsonObject &event) {
            if (event["type"] == "result") {
                state->hashes.clear();
                for (const auto &hash : event["value"].toObject()["hashes"].toArray()) state->hashes << hash.toString();
                for (const auto &hash : QSet<QString>(state->checked)) if (!state->hashes.contains(hash)) state->checked.remove(hash);
                if (state->checked.isEmpty()) state->selecting = false;
            }
            if (event["type"] == "error") status->setText(event["text"].toString());
            if (event["type"] == "finished") { state->busy = false; (*rebuild)(); }
        });
    };
    const auto remove = [=, &dialog](const QStringList &hashes) {
        const auto text = hashes.size() == 1 ? QStringLiteral("Удалить хеш и завершить его звонок в VK?")
                                             : QString("Удалить выбранные хеши (%1) и завершить их звонки в VK?").arg(hashes.size());
        if (QMessageBox::question(&dialog, "Удаление хешей", text) != QMessageBox::Yes) return;
        state->busy = true;
        (*rebuild)();
        status->setText("Удаление…");
        task->start({{"operation", "vk-hashes-delete"}, {"hashes", QJsonArray::fromStringList(hashes)}}, [=](const QJsonObject &event) {
            if (event["type"] == "result") {
                state->checked.clear();
                const int unfinished = event["value"].toObject()["unfinished"].toInt();
                status->setText(unfinished ? QString("Хеши удалены. Звонков, которые не удалось завершить в VK: %1").arg(unfinished) : QString("Хеши удалены"));
            }
            if (event["type"] == "error") status->setText(event["text"].toString());
            if (event["type"] == "finished") reload();
        });
    };
    *rebuild = [=] {
        list->clear();
        for (const auto &hash : state->hashes) {
            auto *item = new QListWidgetItem(list);
            auto *row = new QWidget(list);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(6, 2, 6, 2);
            auto *check = new QCheckBox(row);
            check->setVisible(state->selecting);
            check->setChecked(state->checked.contains(hash));
            check->setAttribute(Qt::WA_TransparentForMouseEvents);
            rowLayout->addWidget(check);
            auto *label = new QLabel(hash, row);
            label->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            rowLayout->addWidget(label, 1);
            auto *copy = new QPushButton("Копировать", row);
            QObject::connect(copy, &QPushButton::clicked, row, [hash, status] {
                QApplication::clipboard()->setText(hash);
                status->setText("Хеш скопирован");
            });
            rowLayout->addWidget(copy);
            auto *drop = new QPushButton("✕", row);
            drop->setToolTip("Удалить хеш и завершить звонок в VK");
            drop->setFixedWidth(32);
            drop->setVisible(!state->selecting);
            drop->setEnabled(!state->busy);
            QObject::connect(drop, &QPushButton::clicked, row, [=] { remove({hash}); });
            rowLayout->addWidget(drop);
            new LongPress(row, [=] {
                if (state->busy || state->selecting) return;
                state->selecting = true;
                state->checked = {hash};
                QTimer::singleShot(0, list, [=] { (*rebuild)(); });
            }, [=] {
                if (!state->selecting) return;
                if (!state->checked.remove(hash)) state->checked.insert(hash);
                if (state->checked.isEmpty()) state->selecting = false;
                QTimer::singleShot(0, list, [=] { (*rebuild)(); });
            });
            item->setSizeHint(row->sizeHint());
            list->setItemWidget(item, row);
        }
        hint->setText(state->hashes.isEmpty() ? QStringLiteral("Хешей пока нет. Нажмите +, чтобы создать.")
                      : state->selecting ? QString("Выбрано: %1").arg(state->checked.size())
                                         : QString("Хешей: %1. Удерживайте хеш, чтобы выделить несколько.").arg(state->hashes.size()));
        add->setVisible(!state->selecting);
        add->setEnabled(!state->busy && state->hashes.size() < maxHashes);
        selectAll->setVisible(state->selecting);
        removeSelected->setVisible(state->selecting);
        removeSelected->setEnabled(!state->busy && !state->checked.isEmpty());
        applySelected->setVisible(state->selecting);
        applySelected->setEnabled(!state->busy && !state->checked.isEmpty());
    };
    QObject::connect(applySelected, &QPushButton::clicked, &dialog, [=, &dialog] {
        QStringList chosen;
        for (const auto &hash : state->hashes) if (state->checked.contains(hash)) chosen << hash;
        ApplyHashesToProfiles(&dialog, chosen, status);
    });
    QObject::connect(selectAll, &QPushButton::clicked, &dialog, [=] {
        state->checked = QSet<QString>(state->hashes.begin(), state->hashes.end());
        (*rebuild)();
    });
    QObject::connect(removeSelected, &QPushButton::clicked, &dialog, [=] {
        QStringList chosen;
        for (const auto &hash : state->hashes) if (state->checked.contains(hash)) chosen << hash;
        remove(chosen);
    });
    QObject::connect(add, &QPushButton::clicked, &dialog, [=, &dialog] {
        bool ok = false;
        const int count = QInputDialog::getInt(&dialog, "Создать хеши", "Сколько хешей создать:", 1, 1,
                                               qMin(6, maxHashes - static_cast<int>(state->hashes.size())), 1, &ok);
        if (!ok) return;
        state->busy = true;
        (*rebuild)();
        status->setText("Создание хешей…");
        task->start({{"operation", "vk-hashes"}, {"count", static_cast<int>(state->hashes.size()) + count}}, [=](const QJsonObject &event) {
            if (event["type"] == "progress" || event["type"] == "error") status->setText(event["text"].toString());
            if (event["type"] == "progress" && event["value"].toObject().contains("hashes")) {
                state->hashes.clear();
                for (const auto &hash : event["value"].toObject()["hashes"].toArray()) state->hashes << hash.toString();
                (*rebuild)();
            }
            if (event["type"] == "result") status->setText("Хеши созданы");
            if (event["type"] == "finished") reload();
        });
    });
    state->busy = true;
    (*rebuild)();
    reload();
    dialog.exec();
    *rebuild = nullptr; // the row lambdas hold it
}
} // namespace

bool SaveVKRelayPreviews(const QString &directory) {
    int saves = 0;
    const auto save = [&saves](const QJsonObject &) { ++saves; };
    {
        // A new server opens on its connection screen and is not stored before it connects.
        QPointer<QDialog> dialog = OpenPanelDeploy(nullptr, NewPanelServer(), save);
        auto *pages = dialog->findChild<QStackedWidget *>();
        if (!pages || pages->count() != 9 || pages->currentIndex() != 0) return false;
        QPushButton *next = nullptr;
        for (auto *button : dialog->findChildren<QPushButton *>())
            if (button->text() == "Далее") next = button;
        if (!next || !next->isEnabled()) return false;
        next->click();
        if (pages->currentIndex() != 0 || saves != 0) return false;
        dialog->close();
        if (dialog) delete dialog;
    }
    QJsonObject installed = NewPanelServer();
    for (const auto &[key, text] : QList<QPair<QString, QString>>{{"host", "203.0.113.20"}, {"connectedHost", "203.0.113.20"}, {"connectedPort", "22"},
                                                                  {"installedRole", "combined"}, {"mode", "nginx"}, {"installedDomain", "panel.example.org"},
                                                                  {"panelUrl", "https://panel.example.org/Abcdefghijklmnopq/"}, {"panelUser", "admin"},
                                                                  {"panelPassword", "secret-password"}, {"installedProtocols", "wdtt,qwdtt,dnstt"}, {"version", "1.4.83"}})
        installed[key] = text;
    {
        // An installed server opens on its page: the password stays hidden until shown.
        QPointer<QDialog> dialog = OpenPanelDeploy(nullptr, installed, save);
        auto *pages = dialog->findChild<QStackedWidget *>();
        if (!pages || pages->currentIndex() != 8) return false;
        for (int i = 0; i < 4; ++i) QApplication::processEvents();
        bool hidden = false;
        for (auto *edit : dialog->findChildren<QLineEdit *>())
            if (edit->text() == "secret-password" && edit->isVisible()) hidden = edit->echoMode() == QLineEdit::Password;
        if (!hidden) return false;
        dialog->close();
        if (dialog) delete dialog;
    }
    if (directory.isEmpty()) return true;
    QDir().mkpath(directory);
    const auto previousTheme = themeManager()->current_theme;
    bool saved = true;
    for (const QString theme : {QStringLiteral("flatgray"), QStringLiteral("qdarkstyle")}) {
        themeManager()->ApplyTheme(theme, true);
        const auto snapshot = [&](QWidget &widget, const QString &name) {
            widget.show();
            for (int i = 0; i < 6; ++i) QApplication::processEvents();
            saved = widget.grab().save(directory + "/vk-" + name + "-" + theme + ".png") && saved;
        };
        {
            DialogVKRelay dialog;
            snapshot(dialog, "account");
            auto *tabs = dialog.findChild<QTabWidget *>();
            tabs->setCurrentIndex(tabs->count() - 1);
            snapshot(dialog, "deploy-list");
        }
        for (const auto &[name, server] : QList<QPair<QString, QJsonObject>>{{"deploy-connect", NewPanelServer()}, {"deploy-server", installed}}) {
            QPointer<QDialog> dialog = OpenPanelDeploy(nullptr, server, save);
            snapshot(*dialog, name);
            dialog->close();
            if (dialog) delete dialog;
        }
    }
    themeManager()->ApplyTheme(previousTheme, true);
    return saved;
}

DialogVKRelay::DialogVKRelay(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Настройки VK-релея");
    resize(860, 600);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    auto *accountPage = new QWidget(tabs);
    tabs->addTab(accountPage, "Аккаунт и хеши");
    auto *accountLayout = new QVBoxLayout(accountPage);
    auto *accountGroup = new QGroupBox("Аккаунт VK", accountPage);
    accountLayout->addWidget(accountGroup);
    auto *accountForm = new QVBoxLayout(accountGroup);
    auto *identity = new QLabel("Проверка аккаунта…", accountGroup);
    accountForm->addWidget(identity);
    auto *accountId = new QLabel(accountGroup);
    accountId->setForegroundRole(QPalette::PlaceholderText);
    accountId->setTextInteractionFlags(Qt::TextSelectableByMouse);
    accountId->hide();
    accountForm->addWidget(accountId);
    auto *accountButtons = new QHBoxLayout;
    accountForm->addLayout(accountButtons);
    auto *link = new QPushButton("Привязать аккаунт VK", accountGroup);
    auto *unlink = new QPushButton("Отвязать аккаунт", accountGroup);
    accountButtons->addWidget(link);
    accountButtons->addWidget(unlink);
    unlink->setEnabled(false);
    auto *maxGroup = new QGroupBox("Аккаунт MAX", accountPage);
    accountLayout->addWidget(maxGroup);
    auto *maxForm = new QVBoxLayout(maxGroup);
    auto *maxIdentity = new QLabel("Проверка аккаунта…", maxGroup);
    maxIdentity->setWordWrap(true);
    maxForm->addWidget(maxIdentity);
    auto *maxId = new QLabel(maxGroup);
    maxId->setForegroundRole(QPalette::PlaceholderText);
    maxId->setTextInteractionFlags(Qt::TextSelectableByMouse);
    maxId->hide();
    maxForm->addWidget(maxId);
    auto *maxButtons = new QHBoxLayout;
    maxForm->addLayout(maxButtons);
    auto *maxLink = new QPushButton("Привязать MAX", maxGroup);
    auto *maxUnlink = new QPushButton("Отвязать MAX", maxGroup);
    maxButtons->addWidget(maxLink);
    maxButtons->addWidget(maxUnlink);
    maxUnlink->setEnabled(false);
    auto *hashGroup = new QGroupBox("Хеши VK", accountPage);
    accountLayout->addWidget(hashGroup);
    auto *hashForm = new QFormLayout(hashGroup);
    auto *source = new QComboBox(hashGroup);
    source->addItem("Вручную", "manual");
    source->addItem("Авто", "auto");
    autoEnabled(source, false);
    hashForm->addRow("Хеши новых профилей", source);
    auto *settings = Configs::dataManager->settingsRepo.get();
    source->setCurrentIndex(settings->qwdtt_hash_source == "auto" ? 1 : 0);
    auto *generate = new QPushButton("Генерация хешей", hashGroup);
    hashForm->addRow(generate);
    generate->setEnabled(false);
    connect(generate, &QPushButton::clicked, this, [this] { ShowHashGenerator(this); });
    const auto saveDefaults = [settings, source] {
        settings->qwdtt_hash_source = source->currentData().toString();
        settings->Save();
    };
    connect(source, &QComboBox::currentIndexChanged, this, saveDefaults);
    auto *status = new QLabel(this);
    status->setWordWrap(true);
    accountLayout->addStretch();
    auto *accountTask = new RelayTask(this);
    const auto infoHandler = [=](const QJsonObject &event) {
        if (event["type"] == "result") {
            const auto info = event["value"].toObject();
            const bool linked = info["linked"].toBool();
            identity->setText(linked ? "Аккаунт привязан" : "Аккаунт не привязан");
            link->setText(linked ? "Изменить привязку VK" : "Привязать аккаунт VK");
            accountId->setText(linked && !info["user"].toString().isEmpty() ? "ID: " + info["user"].toString() : QString());
            accountId->setVisible(!accountId->text().isEmpty());
            settings->vk_relay_linked = linked;
            autoEnabled(source, linked);
            unlink->setEnabled(linked);
            generate->setEnabled(linked);
            if (!linked)
                source->setCurrentIndex(0);
            settings->Save();
        }
        if (event["type"] == "progress" || event["type"] == "error")
            status->setText(event["text"].toString());
        if (event["type"] == "cancelled")
            status->clear();
        if (event["type"] == "finished")
            link->setEnabled(true);
    };
    accountTask->start({{"operation", "vk-info"}}, infoHandler);
    connect(link, &QPushButton::clicked, this, [=] {
        link->setEnabled(false);
        accountTask->start({{"operation", "vk-login"}}, infoHandler);
    });
    connect(unlink, &QPushButton::clicked, this, [=, this] {
        if (QMessageBox::question(
                this, "Отвязать VK",
                "Все профили qWDTT с автоматическими хешами перейдут в ручной режим. Отвязать аккаунт?") !=
            QMessageBox::Yes)
            return;
        accountTask->start({{"operation", "secret-delete"}, {"id", "account"}}, [=](const QJsonObject &event) {
            if (event["type"] == "done") {
                if (!ResetQWDTTHashSources())
                    status->setText(
                        "Аккаунт отвязан, но не удалось сохранить часть профилей. Проверьте доступ к базе.");
                source->setCurrentIndex(0);
                autoEnabled(source, false);
                identity->setText("Аккаунт не привязан");
                link->setText("Привязать аккаунт VK");
                accountId->clear();
                accountId->hide();
                unlink->setEnabled(false);
                generate->setEnabled(false);
            }
            if (event["type"] == "error")
                status->setText(event["text"].toString());
        });
    });
    auto *maxTask = new RelayTask(this);
    const auto maxHandler = [=](const QJsonObject &event) {
        if (event["type"] == "result") {
            const auto info = event["value"].toObject();
            const bool linked = info["linked"].toBool();
            maxIdentity->setText(linked ? "MAX привязан" : "Войдите в MAX и откройте нужную группу. Приложение сохранит привязку автоматически.");
            maxLink->setText(linked ? "Изменить привязку" : "Привязать MAX");
            maxId->setText(linked ? "Группа: " + info["group"].toString() : QString());
            maxId->setVisible(linked);
            maxUnlink->setEnabled(linked);
        }
        if (event["type"] == "progress" || event["type"] == "error") status->setText(event["text"].toString());
        if (event["type"] == "cancelled") status->clear();
        if (event["type"] == "finished") maxLink->setEnabled(true);
    };
    maxTask->start({{"operation", "max-info"}}, maxHandler);
    connect(maxLink, &QPushButton::clicked, this, [=, this] {
        // The warning of the original app, with its five seconds and the choice not to see it again.
        if (!ConfirmMaxRisk(this)) return;
        maxLink->setEnabled(false);
        maxTask->start({{"operation", "max-login"}, {"value", palette().window().color().lightness() < 128 ? "dark" : "light"}}, maxHandler);
    });
    connect(maxUnlink, &QPushButton::clicked, this, [=, this] {
        if (QMessageBox::question(this, "Отвязать MAX", "Токен и группа MAX будут удалены из этого приложения. Профили MRTT с транспортом MAX перестанут подключаться.") != QMessageBox::Yes) return;
        maxTask->start({{"operation", "secret-delete"}, {"id", "max"}}, [=](const QJsonObject &event) {
            if (event["type"] == "done") maxTask->start({{"operation", "max-info"}}, maxHandler);
            if (event["type"] == "error") status->setText(event["text"].toString());
        });
    });
    connect(this, &QDialog::finished, maxTask, &RelayTask::cancel);
    auto *storeTask = new RelayTask(this);
    // Every saved VPS, including entries of the earlier per-protocol deploy: they stay stored, the list shows panel servers.
    auto servers = std::make_shared<QJsonArray>();
    auto *deployList = new QListWidget;
    const auto refresh = [servers, deployList] {
        deployList->clear();
        for (const auto &entry : *servers) {
            const auto server = entry.toObject();
            if (server["kind"] != "panel") continue;
            const auto name = server["name"].toString();
            auto *item = new QListWidgetItem(name.isEmpty() ? server["host"].toString() : name + " (" + server["host"].toString() + ")", deployList);
            item->setData(Qt::UserRole, server["id"]);
        }
    };
    const auto writeServers = [=] {
        storeTask->start({{"operation", "secret-set"}, {"id", "servers"}, {"value", QString::fromUtf8(QJsonDocument(*servers).toJson(QJsonDocument::Compact))}},
                         [=](const QJsonObject &event) {
                             if (event["type"] == "error") status->setText(event["text"].toString());
                         });
    };
    const auto persist = [=](const QJsonObject &server) {
        bool found = false;
        for (int i = 0; i < servers->size(); ++i)
            if ((*servers)[i].toObject()["id"] == server["id"]) {
                (*servers)[i] = server;
                found = true;
                break;
            }
        if (!found) servers->append(server);
        refresh();
        writeServers();
    };
    {
        // The mode each relay protocol takes when a link or a new profile names none.
        auto *page = new QWidget(tabs);
        tabs->addTab(page, "Режимы по умолчанию");
        auto *form = new QFormLayout(page);
        auto *note = new QLabel("Режим, который получают новые и импортированные профили, если ссылка его не задаёт. "
                                "Профили с уже выбранным режимом не меняются.", page);
        note->setWordWrap(true);
        form->addRow(note);
        auto &settings = *Configs::dataManager->settingsRepo;
        const auto choice = [page, form](const QString &title, QString &value, const QList<QPair<QString, QString>> &items) {
            auto *box = new QComboBox(page);
            for (const auto &[key, label] : items) box->addItem(label, key);
            box->setCurrentIndex(std::max(0, box->findData(value)));
            QObject::connect(box, &QComboBox::currentIndexChanged, page, [box, &value] {
                value = box->currentData().toString();
                Configs::dataManager->settingsRepo->Save();
            });
            form->addRow(title, box);
        };
        choice("qWDTT", settings.relay_default_qwdtt_mode, {{"wg", "WG (по умолчанию)"}, {"raw", "RAW"}});
        choice("WDTT на движке qWDTT", settings.relay_default_wdttplus_mode, {{"wg", "WG (по умолчанию)"}, {"raw", "RAW"}});
        choice("CSQTT: транспорт TURN", settings.relay_default_csqtt_transport, {{"udp", "UDP (по умолчанию)"}, {"tcp_tls", "TCP/TLS"}});
        choice("CSQTT: режим SRTP", settings.relay_default_csqtt_srtp, {{"auto", "Автоматически (по умолчанию)"}, {"wrap", "WRAP-SRTP"}, {"dtls", "DTLS-SRTP"}});
        choice("MRTT: транспорт TURN", settings.relay_default_mrtt_transport, {{"udp", "UDP (по умолчанию)"}, {"tcp_tls", "TCP/TLS"}});
    }
    {
        auto *page = new QWidget(tabs);
        page->setObjectName("panelDeployPage");
        page->setEnabled(false);
        tabs->addTab(page, "Деплой");
        auto *pageLayout = new QVBoxLayout(page);
        auto *intro = new QLabel("Rawline устанавливает на VPS WDTT Panel и ноду с VPN-протоколами. Протоколы, пользователи и подписки "
                                 "настраиваются в самой панели, здесь доступны адрес панели, вход и удаление.", page);
        intro->setWordWrap(true);
        pageLayout->addWidget(intro);
        deployList->setParent(page);
        pageLayout->addWidget(deployList, 1);
        const auto open = [=, this](const QJsonValue &id) {
            for (const auto &entry : *servers)
                if (entry.toObject()["id"] == id) {
                    OpenPanelDeploy(this, entry.toObject(), persist);
                    return;
                }
        };
        connect(deployList, &QListWidget::itemActivated, this, [=](QListWidgetItem *item) { open(item->data(Qt::UserRole).toJsonValue()); });
        auto *buttons = new QHBoxLayout;
        pageLayout->addLayout(buttons);
        auto *add = new QPushButton("Добавить VPS", page);
        auto *openButton = new QPushButton("Открыть", page);
        auto *remove = new QPushButton("Удалить из списка", page);
        buttons->addWidget(add);
        buttons->addWidget(openButton);
        buttons->addStretch();
        buttons->addWidget(remove);
        connect(add, &QPushButton::clicked, this, [=, this] { OpenPanelDeploy(this, NewPanelServer(), persist); });
        connect(openButton, &QPushButton::clicked, this, [=] {
            if (auto *item = deployList->currentItem()) open(item->data(Qt::UserRole).toJsonValue());
        });
        connect(remove, &QPushButton::clicked, this, [=, this] {
            auto *item = deployList->currentItem();
            if (!item) return;
            if (QMessageBox::question(this, "Удалить из списка", "Удалить VPS из списка? Панель и нода на сервере останутся.") != QMessageBox::Yes) return;
            const auto id = item->data(Qt::UserRole).toJsonValue();
            for (int i = 0; i < servers->size(); ++i)
                if ((*servers)[i].toObject()["id"] == id) {
                    servers->removeAt(i);
                    break;
                }
            refresh();
            writeServers();
        });
        storeTask->start({{"operation", "secret-get"}, {"id", "servers"}}, [=](const QJsonObject &event) {
            if (event["type"] == "result") {
                const auto raw = event["value"].toString().toUtf8();
                const auto doc = QJsonDocument::fromJson(raw);
                if (!raw.isEmpty() && !doc.isArray()) {
                    status->setText("Не удалось прочитать список серверов");
                    return;
                }
                *servers = doc.array();
                refresh();
                page->setEnabled(true);
            }
            if (event["type"] == "error") status->setText(event["text"].toString());
        });
    }
    layout->addWidget(status);
    connect(this, &QDialog::finished, accountTask, &RelayTask::cancel);
}
