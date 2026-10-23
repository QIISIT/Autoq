#pragma once
// macOS: то, что доступно только из Objective-C (AppKit) или системных API, —
// за простыми функциями C++. Реализация — macsupport.mm (собирается только на Mac).
#ifdef __APPLE__

namespace AqMac {
    int frontmostPid();                       // процесс активного приложения
    // «Универсальный доступ» (Accessibility): нужен, чтобы отправлять клики и
    // нажатия и чтобы ловить горячую клавишу. prompt — показать системный запрос.
    bool accessibilityTrusted(bool prompt = false);
    // «Мониторинг ввода» (Input Monitoring): глобальное чтение клавиатуры.
    // request — показать системный запрос (один раз; дальше — только в Настройках).
    bool inputMonitoringAllowed(bool request = false);
    // «Запись экрана»: без неё не видны заголовки чужих окон (процесс виден и так)
    bool screenCaptureAllowed();
    // Открыть Настройки → Конфиденциальность и безопасность → нужный раздел:
    // "Privacy_Accessibility", "Privacy_ListenEvent", "Privacy_ScreenCapture"
    void openPrivacyPane(const char* anchor);
    // Окно поверх всего, на всех рабочих столах и поверх полноэкранных игр;
    // не прячется, когда AutoQ не активен. view — NSView* окна (QWidget::winId()).
    void keepOnTop(unsigned long long view, bool overlay);
}

#endif
