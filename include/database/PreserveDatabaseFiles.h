#pragma once

#include <QFile>
#include <QString>
#include <QUuid>
#include <functional>
#include <stdexcept>

namespace Configs {
    inline void PreserveDatabaseFiles(
        const QString& base,
        const std::function<bool(const QString&, const QString&)>& renameFile =
            [](const QString& from, const QString& to) { return QFile::rename(from, to); }) {
        const QString destination = base + ".broken-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (const char* suffix : {"", "-wal", "-shm"}) {
            const QString from = base + suffix;
            if (!QFile::exists(from)) continue;
            if (!renameFile(from, destination + suffix)) {
                throw std::runtime_error(QString("Cannot preserve database file %1; original file was not deleted")
                                             .arg(from).toStdString());
            }
        }
    }
}
