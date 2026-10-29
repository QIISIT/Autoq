#pragma once
// Что в автообновлении своё у каждой ОС. Реализация — platform/*/updater_*.cpp
#include "core/updates.h"
#include <QString>

class QProcess;

namespace UpdaterOs {
    QString tool(const QString& name);              // путь к системной программе (curl, tar…)
    // Как поставлена эта копия AutoQ — от этого зависит, какой файл релиза качать
    Updates::Package detectPackage(const QString& exe);
    bool installsAtStart();                         // скачанное ставится само при следующем запуске
    QString workDir(const QString& exe);            // куда качать; "" — в папку кэша
    bool trusted(const QString& file, const QString& exe);   // файл лежит там, куда качает workDir
    void cleanup(const QString& exe);               // ничего не ждёт установки — убрать скачанное
    bool launchInstaller(const QString& file);      // Windows: установщик с /S /UPDATE
    bool replaceProgram(const QString& built, const QString& exe);   // Linux: собранную — на место этой
    void ownProcessGroup(QProcess* p);              // сборка — своей группой процессов,
    void killGroup(QProcess* p);                    // чтобы остановить её всю
}
