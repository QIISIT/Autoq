#include "services/updater_os.h"
#include "core/appsettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

using Updates::Package;

namespace {
// Скачанное лежит в папке программы: в Program Files писать может только администратор —
// туда не подложить чужой установщик, который AutoQ с правами запустил бы сам
QString ownUpdateDir(const QString& exe) { return QFileInfo(exe).absolutePath() + "/AutoQ-update"; }

// QFileInfo::isWritable на NTFS прав не проверяет — пробуем создать файл
bool dirWritable(const QString& dir) {
    if (!QDir().mkpath(dir)) return false;
    QFile probe(dir + "/.autoq-write-test");
    const bool ok = probe.open(QIODevice::WriteOnly);
    probe.remove();
    return ok;
}

bool sameDir(const QString& a, const QString& b) {
    const QString x = QFileInfo(a).canonicalFilePath(), y = QFileInfo(b).canonicalFilePath();
    return !x.isEmpty() && x.compare(y, Qt::CaseInsensitive) == 0;
}
}

// curl.exe и tar.exe — в System32 с Windows 10 1803: сначала оттуда, не из PATH
QString UpdaterOs::tool(const QString& name) {
    const QString sys = qEnvironmentVariable("SystemRoot") + "/System32/" + name + ".exe";
    return QFile::exists(sys) ? sys : QStandardPaths::findExecutable(name);
}

Package UpdaterOs::detectPackage(const QString& exe) {
    const QString dir = QFileInfo(exe).absolutePath();
    if (!AppSettings::portable() && QFile::exists(dir + "/Uninstall.exe")) return Package::Setup;
    return dirWritable(dir) ? Package::Portable : Package::None;
}

bool UpdaterOs::installsAtStart() { return true; }

QString UpdaterOs::workDir(const QString& exe) {
    const QString own = ownUpdateDir(exe);
    return dirWritable(own) ? own : QString();
}

bool UpdaterOs::trusted(const QString& file, const QString& exe) {
    return sameDir(QFileInfo(file).absolutePath(), ownUpdateDir(exe));
}

void UpdaterOs::cleanup(const QString& exe) { QDir(ownUpdateDir(exe)).removeRecursively(); }

// /S /UPDATE: без окон, ярлык на рабочем столе не возвращает, после установки сам запускает
// AutoQ. Без прав администратора Windows покажет окно UAC
bool UpdaterOs::launchInstaller(const QString& file) {
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"open";
    sei.lpFile = path.c_str();
    sei.lpParameters = L"/S /UPDATE";
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

bool UpdaterOs::replaceProgram(const QString&, const QString&) { return false; }
void UpdaterOs::ownProcessGroup(QProcess*) {}
void UpdaterOs::killGroup(QProcess*) {}
