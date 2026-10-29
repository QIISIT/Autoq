#pragma once
// Папка данных AutoQ — обычная видимая папка с файлами (без реестра):
//   settings.ini — все настройки и игры, profiles/<игра>/<профиль>.ini — профили.
// По умолчанию: Windows %APPDATA%\AutoQ, macOS ~/Library/Application Support/AutoQ,
// Linux $XDG_CONFIG_HOME/AutoQ (~/.config/AutoQ). Можно выбрать свою (Настройки → Основные → Данные).
// Папка AutoQ-data рядом с программой — портативный режим: всё хранится там.
// Работает и до создания QApplication.
#include <QSettings>
#include <QString>
#include <memory>

namespace AppSettings {
    void setScope(const QString& org, const QString& app);  // для тестов: своя временная папка
    void dropScope(const QString& org, const QString& app); // для тестов: удалить её
    std::unique_ptr<QSettings> open();                      // settings.ini
    // Горячая перезагрузка: settings.ini поправили снаружи (руками, синхронизацией) —
    // а не сама программа. Свои записи запоминаются (их отпечаток), чужие — нет.
    bool changedOutside();
    void markSeen();                      // текущий файл принят — дальше сравнивать с ним
    QString dataDir();
    QString defaultDataDir();
    QString portableDir();                // <папка программы>/AutoQ-data
    bool portable();                      // портативный режим
    bool customDataDir();                 // выбрана своя папка (не по умолчанию)
    QString settingsFile();
    QString profilesDir();
    bool hasData(const QString& dir);     // в папке уже есть настройки AutoQ
    // Сменить папку данных ("" — по умолчанию). copyCurrent — перенести туда текущие
    // настройки и профили; иначе взять то, что там уже лежит. error — путь, где не вышло
    bool setDataDir(const QString& dir, bool copyCurrent, QString* error = nullptr);
    // имя файла из имени профиля или стиля: запрещённые в именах файлов знаки — «_»
    QString safeFileName(const QString& name);
}

// Общие настройки программы
namespace Prefs {
    int captureSeconds();                 // сколько ждать, пока переключишься в игру (1–120)
    void setCaptureSeconds(int sec);

    // Основные (Настройки → Основные)
    bool closeToTray();                   // крестик сворачивает в трей (иначе — выход)
    void setCloseToTray(bool on);
    bool notifications();                 // всплывающие уведомления у значка в трее
    void setNotifications(bool on);
    bool startMini();                     // при запуске открывать мини-окно
    void setStartMini(bool on);
    bool stopOnGameClose();               // окно игры закрылось — выключить кликер
    void setStopOnGameClose(bool on);
    bool mouseOnlyOverGame();             // мышью — только когда курсор над окном игры
    void setMouseOnlyOverGame(bool on);
    bool miniOnTop();                     // мини-окно поверх всех окон
    void setMiniOnTop(bool on);
    bool elevateAtStart();                // Windows: просить права администратора при запуске (окно UAC)
    void setElevateAtStart(bool on);

    // Первый запуск: выбор оформления и анимаций (один раз; «Пройти заново» — в настройках)
    bool onboardingDone();
    void setOnboardingDone(bool done);
}
