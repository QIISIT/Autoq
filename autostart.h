#pragma once
#include <string>
class Autostart {
public:
    static bool setEnabled(bool enable);
    static bool isEnabled();
    // при запуске: обновить путь к программе, перевести старый способ на новый
    static void migrate();
    // Windows: права администратора. Без них — запросить (UAC) или, при запуске
    // из автозагрузки, попросить Планировщик задач. 0 — эта копия должна выйти,
    // -1 — продолжать. На Linux и macOS всегда -1.
    static int relaunchElevatedIfNeeded(int argc, char** argv);
    static bool elevated();
    // Windows: Настройки → «Перезапустить от имени администратора». Запускает копию
    // с правами (окно UAC) с флагом --restarted: она дождётся, пока эта выйдет.
    // true — копия запущена, эта должна выйти; false — отказались в UAC / не Windows.
    static bool restartElevated();
    // Linux: пункт меню приложений ~/.local/share/applications/autoq.desktop.
    // По нему KDE/GNOME узнают программу (значок в панели задач), и без него
    // xdg-desktop-portal пишет при запуске «App info not found for 'autoq'».
    // Вызывать до создания QApplication. На Windows и macOS ничего не делает.
    static void ensureDesktopEntry();
    static std::string execPath();
};
