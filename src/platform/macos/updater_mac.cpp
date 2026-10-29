#include "services/updater_os.h"
#include <QProcess>
#include <QStandardPaths>

#include <csignal>
#include <unistd.h>

using Updates::Package;

QString UpdaterOs::tool(const QString& name) { return QStandardPaths::findExecutable(name); }

// новая версия — образ .dmg: AutoQ перетаскивают в «Программы» сами
Package UpdaterOs::detectPackage(const QString&) { return Package::Dmg; }

bool UpdaterOs::installsAtStart() { return false; }
QString UpdaterOs::workDir(const QString&) { return QString(); }
bool UpdaterOs::trusted(const QString&, const QString&) { return false; }
void UpdaterOs::cleanup(const QString&) {}
bool UpdaterOs::launchInstaller(const QString&) { return false; }
bool UpdaterOs::replaceProgram(const QString&, const QString&) { return false; }

void UpdaterOs::ownProcessGroup(QProcess* p) { p->setChildProcessModifier([]() { ::setpgid(0, 0); }); }

void UpdaterOs::killGroup(QProcess* p) {
    if (p->processId() > 0) ::kill(-pid_t(p->processId()), SIGKILL);
}
