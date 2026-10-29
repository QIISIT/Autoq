#include "services/updater_os.h"
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <csignal>
#include <cstdio>
#include <unistd.h>

using Updates::Package;

QString UpdaterOs::tool(const QString& name) { return QStandardPaths::findExecutable(name); }

// исходники собираются на месте; программа в /usr/bin — от пакетного менеджера, не трогаем
Package UpdaterOs::detectPackage(const QString& exe) {
    return QFileInfo(QFileInfo(exe).absolutePath()).isWritable() ? Package::Source : Package::None;
}

bool UpdaterOs::installsAtStart() { return false; }
QString UpdaterOs::workDir(const QString&) { return QString(); }
bool UpdaterOs::trusted(const QString&, const QString&) { return false; }
void UpdaterOs::cleanup(const QString&) {}
bool UpdaterOs::launchInstaller(const QString&) { return false; }

// новый файл рядом и переименование поверх старого: работающая программа не мешает
bool UpdaterOs::replaceProgram(const QString& built, const QString& exe) {
    const QString fresh = exe + ".new";
    QFile::remove(fresh);
    const bool copied = QFile::copy(built, fresh) &&
        QFile::setPermissions(fresh, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup |
                                     QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    if (copied && std::rename(QFile::encodeName(fresh).constData(), QFile::encodeName(exe).constData()) == 0)
        return true;
    QFile::remove(fresh);
    return false;
}

void UpdaterOs::ownProcessGroup(QProcess* p) { p->setChildProcessModifier([]() { ::setpgid(0, 0); }); }

void UpdaterOs::killGroup(QProcess* p) {
    if (p->processId() > 0) ::kill(-pid_t(p->processId()), SIGKILL);
}
