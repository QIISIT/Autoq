#include <QApplication>
#include "i18n.h"
#include <QLockFile>
#include <QDir>
#include <QMessageBox>
#include <QScreen>
#include <QStandardPaths>
#include <QFile>
#include <QSettings>
#include <QTimer>
#include <QProcess>
#include <QRegularExpression>
#include <string>
#include "appinfo.h"
#include "mainwindow.h"
#include "hotkey.h"
#include "theme.h"
#include "games.h"
#include "keys.h"
#include "icons.h"
#include "autostart.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include "macperm.h"
#else
#include <X11/Xlib.h>
#endif

int main(int argc, char** argv) {
    // Windows: нужны права администратора (игры «от администратора» не принимают
    // клики от обычных программ). Манифест их не требует — так AutoQ можно
    // поставить в обычную автозагрузку; просим права здесь.
    if (Autostart::relaunchElevatedIfNeeded(argc, argv) == 0) return 0;
    // Язык интерфейса — до создания любых окон (Настройки → Основные → Язык)
    {
        QSettings pre(AQ_NAME, "config");
        I18n::setLanguage(I18n::fromSetting(pre.value("language", "auto").toString()));
    }
#if !defined(_WIN32) && !defined(__APPLE__)
    // Пункт меню приложений autoq.desktop — до QApplication: при запуске Qt
    // регистрирует программу в xdg-desktop-portal по этому имени.
    Autostart::ensureDesktopEntry();
    // Несколько потоков обращаются к X11 (кликер + хоткей) — нужно до QApplication
    XInitThreads();
    // Под Wayland окна AutoQ — через XWayland: только так оверлей КПС встаёт поверх
    // игры в нужном месте, а мини-окно держится поверх. Отключается в настройках.
    // Если xcb не загрузится, Qt сам возьмёт wayland (список через «;»).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        !qEnvironmentVariableIsEmpty("DISPLAY")) {
        QSettings pre(AQ_NAME, "config");
        if (pre.value("useXWayland", true).toBool()) qputenv("QT_QPA_PLATFORM", "xcb;wayland");
    }
#endif

    // Размер интерфейса (Настройки → Оформление) — до QApplication
    Theme::applyGuiScaleEnv();
#ifdef __APPLE__
    // Ctrl — это Control, а Cmd — отдельная клавиша (как Win), а не наоборот
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    QApplication app(argc, argv);
    // Размер меньше 100 % на экране 100 %: Qt рисовал бы окно кусками — сразу
    // перезапускаемся с масштабом 1:1 (на Windows это проверено ещё до запуска)
    if (const double clamp = Theme::scaleClampNeeded(); clamp > 0) {
        qputenv("AQ_SCALE_CLAMP", QByteArray::number(clamp, 'f', 4));
        QStringList args = QCoreApplication::arguments().mid(1);
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        return 0;
    }
#if !defined(_WIN32) && !defined(__APPLE__)
    // Ставим ПОСЛЕ QApplication: Qt при подключении к X11 ставит свой обработчик
    // ошибок, и тогда программа не узнала бы, что клавиша запуска занята.
    // Наш обработчик тоже не роняет программу при ошибках X11.
    GlobalHotkey::installX11ErrorHandler();
#endif
    app.setOrganizationName(AQ_NAME);
    app.setApplicationName("config");
    app.setApplicationVersion(AQ_VERSION);
    app.setDesktopFileName("autoq");

    QApplication::setQuitOnLastWindowClosed(false);

    bool startHidden = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--tray") startHidden = true;

    // Только один экземпляр: второй запуск (автозапуск + ручной, или двойной
    // клик) иначе дрался бы за глобальный хоткей и дублировал иконку в трее.
#ifdef _WIN32
    // Именованный мьютекс ядра исчезает вместе с процессом — после сбоя или
    // выключения ПК не "залипает" (в отличие от файла блокировки с PID).
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\AutoQ_SingleInstance");
    bool alreadyRunning = (GetLastError() == ERROR_ALREADY_EXISTS);
#else
    // файл блокировки — свой у каждого пользователя (/tmp общий на всех)
    QString user = QString::fromLocal8Bit(qgetenv("USER"));
    user.remove(QRegularExpression("[^A-Za-z0-9_.-]"));
    QLockFile lock(QDir::temp().absoluteFilePath(user.isEmpty() ? QString("AutoQ.lock")
                                                                : QString("AutoQ-%1.lock").arg(user)));
    lock.setStaleLockTime(0); // упавший экземпляр распознаётся по PID
    bool alreadyRunning = !lock.tryLock(300);
#endif
    if (alreadyRunning) {
        if (!startHidden)
            QMessageBox::information(nullptr, AQ_NAME, T("AutoQ уже запущен — его значок в трее у часов."));
        return 0;
    }

    // старые форматы настроек -> база игр
    Games::migrate();
    Autostart::migrate();   // автозапуск: новый путь к программе / новый способ

#if !defined(_WIN32) && !defined(__APPLE__)
    // значок для меню приложений и автозапуска (Icon=autoq)
    {
        const QString dir = QDir::homePath() + "/.local/share/icons/hicolor/256x256/apps";
        const QString file = dir + "/autoq.png";
        if (!QFile::exists(file) && QDir().mkpath(dir)) AqIcons::logo(256).save(file, "PNG");
    }
#endif

    {
        auto s = AppSettings::open();
        Theme::apply(app, s->value("theme", 0).toInt() == 0);
    }

    MainWindow w;

    // глобальные клавиши: какие именно — решает главное окно (у каждой игры своя)
    GlobalHotkey hk([&w](const HotkeySpec& sp, bool down) {
        const QString spec = sp.toString();
        QMetaObject::invokeMethod(&w, [&w, spec, down]() { w.onHotkeyEvent(spec, down); }, Qt::QueuedConnection);
    });
    w.attachHotkey(&hk);

    // размер по экрану: на низких экранах — с прокруткой
    if (QScreen* scr = QGuiApplication::primaryScreen()) {
        const QRect a = scr->availableGeometry();
        w.resize(qMin(1180, a.width() - 40), qMin(800, a.height() - 60));
    } else {
        w.resize(1180, 800);
    }
    if (!startHidden) {
        if (Prefs::startMini()) w.showMini();   // Настройки → Основные (без игр — большое окно)
        else w.show();
    }

#ifdef __APPLE__
    // Клики и горячая клавиша на macOS работают только с разрешениями
    // «Универсальный доступ» и «Мониторинг ввода» — подскажем, где включить.
    QTimer::singleShot(600, &w, [&w, &hk]() { MacPermissions::checkAndExplain(&w, &hk); });
#endif

    int rc = app.exec();
    // смена языка: перезапуск (сначала отпускаем блокировку «уже запущен»)
    if (w.restartRequested()) {
#ifdef _WIN32
        if (instanceMutex) { ReleaseMutex(instanceMutex); CloseHandle(instanceMutex); instanceMutex = nullptr; }
#else
        lock.unlock();
#endif
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    }
#ifdef _WIN32
    if (instanceMutex) { ReleaseMutex(instanceMutex); CloseHandle(instanceMutex); }
#endif
    return rc;
}
