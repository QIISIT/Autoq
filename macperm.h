#pragma once
// macOS: проверить разрешения и объяснить, где их включить.
#ifdef __APPLE__
class QWidget;
class GlobalHotkey;

namespace MacPermissions {
    // Нет «Универсального доступа» — показать окно с кнопкой «Открыть настройки».
    void checkAndExplain(QWidget* parent, GlobalHotkey* hotkey);
}
#endif
