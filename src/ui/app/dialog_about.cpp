#include "include/ui/app/dialog_about.h"

#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "NkrVersion.h"
#include "include/global/Configs.hpp"

DialogAbout::DialogAbout(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("About Rawline"));

    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon(QStringLiteral(":/Rawline/Rawline.png")).pixmap(64));
    icon->setAlignment(Qt::AlignTop);

    auto *title = new QLabel(QStringLiteral("Rawline"), this);
    auto titleFont = title->font();
    titleFont.setBold(true);
    if (titleFont.pointSizeF() > 0) {
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    } else {
        titleFont.setPixelSize(titleFont.pixelSize() * 3 / 2);
    }
    title->setFont(titleFont);

    const auto valueLabel = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    auto *singBoxVersion = valueLabel(QStringLiteral("…"));
    auto *xrayVersion = valueLabel(QStringLiteral("…"));
    auto *goVersion = valueLabel(QStringLiteral("…"));

    auto *versions = new QFormLayout;
    versions->addRow(tr("Version:"), valueLabel(QStringLiteral(NKR_VERSION)));
    versions->addRow(QStringLiteral("sing-box:"), singBoxVersion);
    versions->addRow(QStringLiteral("Xray-core:"), xrayVersion);
    versions->addRow(QStringLiteral("Qt:"), valueLabel(QString::fromLatin1(qVersion())));
    versions->addRow(QStringLiteral("Go:"), goVersion);

    // GPL-3.0 asks an interactive program to show its license and warranty notice.
    auto *description = new QLabel(tr("Free software under the GNU General Public License v3.0, provided without any warranty.") + QStringLiteral("<br>")
                                       + tr("Based on %1").arg(QStringLiteral("<a href=\"https://github.com/throneproj/Throne\">Throne</a>")), this);
    description->setOpenExternalLinks(true);
    description->setWordWrap(true);
    description->setMinimumWidth(fontMetrics().averageCharWidth() * 50);

    auto *text = new QVBoxLayout;
    text->addWidget(title);
    text->addWidget(description);
    text->addSpacing(fontMetrics().height() / 2);
    text->addLayout(versions);

    auto *top = new QHBoxLayout;
    top->setSpacing(fontMetrics().height());
    top->addWidget(icon);
    top->addLayout(text, 1);

    auto *github = new QPushButton(QStringLiteral("GitHub"), this);
    github->setAutoDefault(false);
    connect(github, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/ttrroyy/Rawline")));
    });

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(github);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addSpacing(fontMetrics().height() / 2);
    layout->addLayout(buttons);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    // A core predating --version still prints its sing-box and Xray lines before failing for want of a GUI socket.
    auto *core = new QProcess(this);
    core->setStandardInputFile(QProcess::nullDevice());
    const auto showCoreVersions = [=, this](const QByteArray &output) {
        QHash<QString, QString> found;
        for (const auto &line : QString::fromUtf8(output).split(QLatin1Char('\n'))) {
            const auto colon = line.indexOf(QLatin1String(": "));
            if (colon > 0) found.insert(line.left(colon), line.mid(colon + 2).trimmed());
        }
        const auto unknown = tr("Unknown");
        singBoxVersion->setText(found.value(QStringLiteral("sing-box"), unknown));
        xrayVersion->setText(found.value(QStringLiteral("Xray-core"), unknown));
        goVersion->setText(found.value(QStringLiteral("Go"), unknown));
    };
    connect(core, &QProcess::finished, this, [=] { showCoreVersions(core->readAllStandardOutput()); });
    connect(core, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) showCoreVersions({});
    });
    QTimer::singleShot(3000, core, [core] { core->kill(); });
    core->start(Configs::FindCoreRealPath(), {QStringLiteral("--version")});
}
