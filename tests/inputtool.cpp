// Утилита для тестов и диагностики.
//   inputtool send-all <пауза_мс>   — нажать каждую клавишу из списка действий
//                                     и все кнопки мыши (через код кликера)
//   inputtool send <имя> ...         — нажать указанные клавиши / mouse1..3
//   inputtool probe                  — показать активное окно и какой игре оно подходит
#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include "core/keys.h"
#include "core/clicker.h"
#include "core/winmatch.h"
#include "core/games.h"
#include "core/hotkey.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <X11/Xlib.h>
#endif

static unsigned keycodeOf(const KeyDef* k) {
#ifdef _WIN32
    return k->winVK;
#else
    static Display* d = XOpenDisplay(nullptr);
    return d ? XKeysymToKeycode(d, k->x11Sym) : 0;
#endif
}

static ActionStep stepOf(const QString& n) {
    ActionStep a;
    if (n.startsWith("mouse")) { a.mouse = true; a.button = n.mid(5).toInt(); }
    else { a.mouse = false; a.key = n; }
    return a;
}

static bool sendName(const QString& n) { return Clicker::sendOnce(stepOf(n)); }

int main(int argc, char** argv) {
#ifndef _WIN32
    XInitThreads();
    GlobalHotkey::installX11ErrorHandler();
#endif
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) { std::printf("usage: send-all <ms> | send <keys> | probe\n"); return 2; }

    if (args[0] == "send-all") {
        const int pause = args.value(1, "60").toInt();
        for (const QString& n : Keys::actionKeyNames()) {
            const KeyDef* k = Keys::byName(n);
            const bool ok = sendName(n);
            std::printf("%s %u %d %d\n", qPrintable(n), keycodeOf(k), ok ? 1 : 0, k->winExtended ? 1 : 0);
            std::fflush(stdout);
            QThread::msleep(pause);
        }
        for (int b : {1, 2, 3, 8, 9}) {
            const bool ok = sendName(QString("mouse%1").arg(b));
            std::printf("mouse%d 0 %d 0\n", b, ok ? 1 : 0);
            std::fflush(stdout);
            QThread::msleep(pause);
        }
        return 0;
    }
    if (args[0] == "send") {
        for (int i = 1; i < args.size(); ++i) {
            if (!sendName(args[i])) { std::printf("FAIL %s\n", qPrintable(args[i])); return 1; }
            QThread::msleep(40);
        }
        return 0;
    }
    // setup <хоткей> <клавиша|mouseN> <интервал> <привязка 0/1> [заголовок]
    // — своя игра "KeyLogger" (окно по заголовку), выбрана текущей
    if (args[0] == "setup") {
        GameDef g;
        bool exists = false;
        for (const GameDef& x : Games::list()) if (x.name == "KeyLogger") { g = x; exists = true; }
        if (!exists) g.id = Games::newCustomId();
        g.name = "KeyLogger"; g.mode = GameMode::Sequence;
        g.titles = {args.value(5, "KeyLogger")}; g.processes.clear(); g.minIntervalMs = 5;
        Games::save(g);
        GameState st;
        // несколько действий через запятую: "x,mouse8"
        st.actions.clear();
        for (const QString& a : args.value(2, "x").split(',', Qt::SkipEmptyParts)) st.actions << stepOf(a);
        st.intervalMs = args.value(3, "100").toInt();
        st.randomize = false; st.lockWindow = args.value(4, "1") == "1";
        Games::saveState(g.id, st);
        Games::setCurrentId(g.id);
        Games::setStartKey(g.id, HotkeySpec::parse(args.value(1, "f8")));
        auto s = AppSettings::open();
        s->setValue("hotkey", args.value(1, "f8"));
        s->setValue("onboardingDone", true);
        s->sync();
        std::printf("%s\n", qPrintable(g.id));
        return 0;
    }
    // setup-game <id игры> <хоткей> <клавиша> <интервал> <привязка 0/1>
    if (args[0] == "setup-game") {
        bool ok = false;
        GameDef g = Games::get(args.value(1), &ok);
        if (!ok) { std::printf("no game\n"); return 1; }
        GameState st = Games::defaultState(g);
        st.actions = {stepOf(args.value(3, "x"))};
        st.intervalMs = args.value(4, "100").toInt(); st.randomize = false;
        st.lockWindow = args.value(5, "1") == "1";
        Games::saveState(g.id, st);
        Games::setCurrentId(g.id);
        Games::setStartKey(g.id, HotkeySpec::parse(args.value(2, "f8")));
        auto s = AppSettings::open();
        s->setValue("hotkey", args.value(2, "f8"));
        s->setValue("onboardingDone", true);
        s->sync();
        return 0;
    }
    // grab <хоткей> <секунд> — занять глобальную клавишу (проверка "занята")
    if (args[0] == "grab") {
        GlobalHotkey hk([](const HotkeySpec&, bool) {}, {HotkeySpec::parse(args.value(1))});
        QThread::msleep(500);
        std::printf("status=%d\n", static_cast<int>(hk.status(0)));
        std::fflush(stdout);
        QThread::msleep(args.value(2, "10").toInt() * 1000);
        return 0;
    }
    // listen <секунд> <хоткей> [хоткей…] — печатать, какие хоткеи нажаты / отпущены
    if (args[0] == "listen") {
        QList<HotkeySpec> specs;
        for (int i = 2; i < args.size(); ++i) specs << HotkeySpec::parse(args[i]);
        GlobalHotkey hk([](const HotkeySpec& sp, bool down) {
            std::printf("%s %s\n", qPrintable(sp.toString()), down ? "down" : "up");
            std::fflush(stdout);
        }, specs);
        QThread::msleep(300);
        for (int i = 0; i < specs.size(); ++i) std::printf("status %s=%d\n", qPrintable(specs[i].toString()), static_cast<int>(hk.status(i)));
        std::printf("ready\n");
        std::fflush(stdout);
        QThread::msleep(args.value(1, "5").toInt() * 1000);
        return 0;
    }
    // state <клавиша> — зажата ли сейчас клавиша (проверка режима «Зажать»)
    if (args[0] == "state") {
        const KeyDef* k = Keys::byName(args.value(1));
        if (!k) { std::printf("unknown\n"); return 2; }
#ifdef _WIN32
        const bool down = (GetAsyncKeyState(k->winVK) & 0x8000) != 0;
#else
        Display* d = XOpenDisplay(nullptr);
        if (!d) { std::printf("NO_DISPLAY\n"); return 1; }
        char keys[32] = {};
        XQueryKeymap(d, keys);
        const KeyCode kc = XKeysymToKeycode(d, k->x11Sym);
        const bool down = kc && (keys[kc / 8] & (1 << (kc % 8)));
        XCloseDisplay(d);
#endif
        std::printf("%s\n", down ? "down" : "up");
        return 0;
    }
    if (args[0] == "probe") {
        ActiveWindowProbe probe;
        if (!probe.available()) { std::printf("NO_DISPLAY\n"); return 1; }
        const WindowInfo w = probe.query();
        std::printf("valid=%d\ntitle=%s\nprocess=%s\nknown=%d\ncandidates=%s\n",
                    w.valid, w.title.toUtf8().constData(), w.process.toUtf8().constData(),
                    w.processKnown, w.candidates.join("|").toUtf8().constData());
        for (const GameDef& g : Games::builtinDefaults())
            if (matchesRule(g.rule(), w)) std::printf("match=%s\n", qPrintable(g.id));
        return 0;
    }
    return 2;
}
