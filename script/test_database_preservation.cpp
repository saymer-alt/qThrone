#include "include/database/PreserveDatabaseFiles.h"
#include <QDir>
#include <QTemporaryDir>
#include <iostream>

static void require(bool condition) {
    if (!condition) throw std::runtime_error("Database preservation check failed");
}

static void writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly));
    require(file.write(data) == data.size());
}

static QByteArray readFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

int main() {
    QTemporaryDir directory;
    require(directory.isValid());
    const QString path = directory.filePath("stats.db");
    writeFile(path, "database");
    writeFile(path + "-wal", "journal");
    writeFile(path + "-shm", "shared-memory");
    Configs::PreserveDatabaseFiles(path);
    require(!QFile::exists(path));
    const auto backups = QDir(directory.path()).entryList({"stats.db.broken-*"}, QDir::Files);
    require(backups.size() == 3);
    for (const auto& name : backups) {
        const auto bytes = readFile(directory.filePath(name));
        require(bytes == (name.endsWith("-wal") ? "journal" : name.endsWith("-shm") ? "shared-memory" : "database"));
    }
    writeFile(path, "must survive");
    bool failed = false;
    try {
        Configs::PreserveDatabaseFiles(path, [](const QString&, const QString&) { return false; });
    } catch (const std::runtime_error&) { failed = true; }
    require(failed);
    require(readFile(path) == "must survive");
    std::cout << "PASS: database and journals preserved; rename failure never deletes the original\n";
}
