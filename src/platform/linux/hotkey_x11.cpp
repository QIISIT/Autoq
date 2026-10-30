#include "core/hotkey.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#undef Status           // макрос Xlib (= int) ломает GlobalHotkey::Status

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Ошибка BadAccess на XGrabKey = клавишу уже захватила другая программа.
static std::atomic<Display*> g_hkDisplay{nullptr};
static std::atomic<bool> g_grabFailed{false};

static int nonFatalXErrorHandler(Display* d, XErrorEvent* e) {
    if (d == g_hkDisplay.load() && (e->request_code == X_GrabKey || e->request_code == X_GrabButton) &&
        e->error_code == BadAccess)
        g_grabFailed = true;
    return 0;   // не завершать программу (поведение Xlib по умолчанию)
}

void GlobalHotkey::init() {
    XSetErrorHandler(nonFatalXErrorHandler);
}

void GlobalHotkey::loop() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) { resetStatuses(Status::Unavailable); return; }
    g_hkDisplay = d;
    Window root = DefaultRootWindow(d);
    // Автоповтор при удержании: только повторные KeyPress, без KeyRelease между
    // ними — так удержание клавиши не включает/выключает кликер много раз.
    Bool detectable = False;
    XkbSetDetectableAutoRepeat(d, True, &detectable);

    // XInput2: «сырые» нажатия для сквозных клавиш (буква, цифра…) и кнопок
    // мыши — видим их, не забирая у игры и других программ.
    int xiOpcode = 0, xiEvent = 0, xiError = 0;
    bool xi2 = XQueryExtension(d, "XInputExtension", &xiOpcode, &xiEvent, &xiError);
    if (xi2) {
        // 2.1+: сырые события приходят и во время чужих захватов (кнопка мыши
        // зажата над окном игры) — иначе отпускание кнопки/клавиши не видно
        int major = 2, minor = 2;
        xi2 = XIQueryVersion(d, &major, &minor) == Success;
        if (!xi2) { major = 2; minor = 0; xi2 = XIQueryVersion(d, &major, &minor) == Success; }
    }
    // Нажатия, отправленные через XTEST (сам кликер AutoQ, xdotool), — не от
    // человека: иначе действие одной игры включало бы другую игру.
    std::set<int> xtestDevices;
    const bool acceptXTest = !qEnvironmentVariableIsEmpty("AQ_ACCEPT_XTEST");   // для тестов
    if (xi2 && !acceptXTest) {
        int n = 0;
        if (XIDeviceInfo* info = XIQueryDevice(d, XIAllDevices, &n)) {
            for (int i = 0; i < n; ++i)
                if (info[i].name && std::strstr(info[i].name, "XTEST")) xtestDevices.insert(info[i].deviceid);
            XIFreeDeviceInfo(info);
        }
    }
    auto selectRaw = [&](bool keys, bool buttons) {
        if (!xi2) return;
        unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {};
        // клавиши — всегда, пока есть хоть одна привязка: по ним узнаём модификаторы
        if (keys || buttons) { XISetMask(bits, XI_RawKeyPress); XISetMask(bits, XI_RawKeyRelease); }
        if (buttons) { XISetMask(bits, XI_RawButtonPress); XISetMask(bits, XI_RawButtonRelease); }
        XIEventMask m;
        m.deviceid = XIAllMasterDevices;
        m.mask_len = sizeof(bits);
        m.mask = bits;
        XISelectEvents(d, root, &m, 1);
        XSync(d, False);
    };
    // коды модификаторов — чтобы знать, зажаты ли Ctrl/Alt/Shift/Win
    auto codesOf = [&](std::initializer_list<KeySym> syms) {
        std::set<int> out;
        for (KeySym ks : syms) if (KeyCode c = XKeysymToKeycode(d, ks)) out.insert(c);
        return out;
    };
    const std::set<int> shiftCodes = codesOf({XK_Shift_L, XK_Shift_R});
    const std::set<int> ctrlCodes = codesOf({XK_Control_L, XK_Control_R});
    const std::set<int> altCodes = codesOf({XK_Alt_L, XK_Alt_R, XK_ISO_Level3_Shift});
    const std::set<int> winCodes = codesOf({XK_Super_L, XK_Super_R, XK_Meta_L, XK_Meta_R});
    std::set<int> pressed;     // что сейчас зажато (по сырым событиям)
    auto anyOf = [&](const std::set<int>& codes) {
        for (int c : codes) if (pressed.count(c)) return true;
        return false;
    };
    auto modsNow = [&]() {
        int m = 0;
        if (anyOf(ctrlCodes)) m |= ModCtrl;
        if (anyOf(altCodes)) m |= ModAlt;
        if (anyOf(shiftCodes)) m |= ModShift;
        if (anyOf(winCodes)) m |= ModWin;
        return m;
    };

    // NumLock (Mod2) и CapsLock не должны мешать хоткею
    const unsigned int lockVariants[] = {0, Mod2Mask, LockMask, Mod2Mask | LockMask};
    const unsigned int relevant = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

    struct XBind {
        int id = 0;
        int mods = 0;
        bool mouse = false;
        int code = 0;          // KeyCode или номер кнопки мыши
        int hold = 0;          // KeyCode клавиши, которую держат («Tab+F»), 0 — обычный хоткей
        bool grabbed = false;  // XGrabKey / XGrabButton (иначе — сырые события)
        unsigned xmods = 0;
        std::vector<unsigned> extra;   // те же перехваты с лишними модификаторами (GlobalHotkey::anyMods)
        bool down = false;
        bool fired = false;
        bool raw = false;      // нажал человек: решено по сырому событию, KeyPress/ButtonPress перехвата — не в счёт
    };
    std::vector<XBind> binds;
    bool rawSelected = false;
    bool grabbedAny = anyMods();    // с какой настройкой «лишних модификаторов» стоят перехваты
    // модификаторы подходят привязке: точно или с лишними — по настройке, и только когда на той
    // же клавише нет привязки точнее (GlobalHotkey::modsMatch)
    auto modsOk = [&](const XBind& b, int held) {
        std::vector<int> sameKey;
        for (const XBind& o : binds)
            if (o.mouse == b.mouse && o.code == b.code && o.hold == b.hold) sameKey.push_back(o.mods);
        return modsMatch(held, b.mods, sameKey);
    };
    auto xToMods = [&](unsigned state) {
        int m = 0;
        if (state & ControlMask) m |= ModCtrl;
        if (state & Mod1Mask)    m |= ModAlt;
        if (state & ShiftMask)   m |= ModShift;
        if (state & Mod4Mask)    m |= ModWin;
        return m;
    };
    // Отпускание мимо нас не должно оставлять клавишу «зажатой»: XWayland, когда фокус уходит
    // в окно Wayland (Alt+Tab, Meta в KDE), отпускает клавиши без сырых событий — и F6 потом
    // считался бы Alt+F6. Сверяем с сервером: чего он зажатым не видит, того нет (keep — только что нажатая)
    auto syncPressed = [&](int keep) {
        char keys[32] = {};
        XQueryKeymap(d, keys);
        for (auto it = pressed.begin(); it != pressed.end();) {
            const bool down = *it > 0 && *it < 256 && (keys[*it / 8] & (1 << (*it % 8)));
            it = (down || *it == keep) ? std::next(it) : pressed.erase(it);
        }
    };
    // зажата ли клавиша по мнению сервера (видит и XTEST — поэтому только вместе с pressed)
    auto keymapDown = [&](int code) {
        char keys[32] = {};
        XQueryKeymap(d, keys);
        return code > 0 && code < 256 && (keys[code / 8] & (1 << (code % 8))) != 0;
    };
    // первая клавиша сочетания зажата человеком (и сервер с этим согласен — пропущенное
    // отпускание не оставит её «залипшей»)
    auto holdOk = [&](int code) { return pressed.count(code) > 0 && keymapDown(code); };

    auto ungrab = [&]() {
        for (const XBind& b : binds) {
            if (b.grabbed) {
                std::vector<unsigned> all = b.extra;
                all.push_back(b.xmods);
                for (unsigned x : all)
                    for (unsigned int v : lockVariants) {
                        if (b.mouse) XUngrabButton(d, static_cast<unsigned>(b.code), x | v, root);
                        else XUngrabKey(d, static_cast<KeyCode>(b.code), x | v, root);
                    }
            }
            if (b.fired) fire(b.id, false);
        }
        binds.clear();
        if (rawSelected) selectRaw(false, false);
        rawSelected = false;
        XSync(d, False);
    };
    auto apply = [&]() {
        ungrab();
        syncPressed(0);     // что за время паузы отпустили — забываем (зажатое осталось зажатым)
        const QList<HotkeySpec> specs = bindings();
        active_ = specs;
        grabbedAny = anyMods();
        if (suspended_) { resetStatuses(Status::Suspended); return; }
        bool needButtons = false;
        // перехваты: (кнопка мыши — со сдвигом, клавиша) + модификаторы X
        auto grabKey = [](bool mouse, int code) { return (mouse ? 0x10000 : 0) | code; };
        auto toX = [](int mods) {
            unsigned x = 0;
            if (mods & ModCtrl)  x |= ControlMask;
            if (mods & ModAlt)   x |= Mod1Mask;
            if (mods & ModShift) x |= ShiftMask;
            if (mods & ModWin)   x |= Mod4Mask;
            return x;
        };
        // свои сочетания других привязок (Shift+F6 у соседней) — их перехватит она сама
        std::set<std::pair<int, unsigned>> own;
        for (const HotkeySpec& sp : specs) {
            if (!sp.isValid() || !sp.isGlobalSafe() || sp.passthrough()) continue;
            if (sp.isMouse()) { own.insert({grabKey(true, sp.mouseButton()), toX(sp.mods)}); continue; }
            const KeyDef* k = Keys::byName(sp.key);
            if (const KeyCode kc = k ? XKeysymToKeycode(d, k->x11Sym) : 0) own.insert({grabKey(false, kc), toX(sp.mods)});
        }
        std::set<std::pair<int, unsigned>> taken;   // уже перехвачено с лишними модификаторами
        // Shift+F6, Ctrl+F6… для привязки F6: без этого X отдал бы их игре, а Windows и macOS
        // такое нажатие забирают. Занято другой программой — не беда, F6 видно и по сырому событию
        auto grabExtra = [&](XBind& b) {
            if (!grabbedAny) return;
            const unsigned freeMods = relevant & ~b.xmods;
            for (unsigned e = 1; e <= relevant; ++e) {
                if ((e & freeMods) != e) continue;
                const std::pair<int, unsigned> combo{grabKey(b.mouse, b.code), b.xmods | e};
                if (own.count(combo) || !taken.insert(combo).second) continue;
                for (unsigned int v : lockVariants) {
                    if (b.mouse)
                        XGrabButton(d, static_cast<unsigned>(b.code), combo.second | v, root, False,
                                    ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
                    else
                        XGrabKey(d, static_cast<KeyCode>(b.code), combo.second | v, root, True, GrabModeAsync, GrabModeAsync);
                }
                b.extra.push_back(combo.second);
            }
            XSync(d, False);
            g_grabFailed = false;
        };
        for (int id = 0; id < specs.size(); ++id) {
            const HotkeySpec& sp = specs[id];
            if (!sp.isValid() || !sp.isGlobalSafe()) { setStatus(id, Status::Invalid); continue; }
            XBind b;
            b.id = id;
            b.mods = sp.mods;
            b.xmods = toX(sp.mods);
            if (sp.isMouse()) {
                if (!xi2) { setStatus(id, Status::Unavailable); continue; }
                b.mouse = true;
                b.code = sp.mouseButton();
                needButtons = true;
                if (!sp.passthrough()) {
                    // «не пропускать в игру»: нажатие забираем себе (XGrabButton), отпускание видно
                    // сырым событием или, пока перехват ещё наш, обычным ButtonRelease
                    g_grabFailed = false;
                    for (unsigned int v : lockVariants)
                        XGrabButton(d, static_cast<unsigned>(b.code), b.xmods | v, root, False,
                                    ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
                    XSync(d, False);
                    if (g_grabFailed) {
                        for (unsigned int v : lockVariants) XUngrabButton(d, static_cast<unsigned>(b.code), b.xmods | v, root);
                        XSync(d, False);
                        setStatus(id, Status::Busy);
                        continue;
                    }
                    b.grabbed = true;
                    grabExtra(b);
                }
                binds.push_back(b);
                setStatus(id, Status::Ok);
                continue;
            }
            const KeyDef* k = Keys::byName(sp.key);
            const KeyCode kc = k ? XKeysymToKeycode(d, k->x11Sym) : 0;
            if (!kc) { setStatus(id, Status::Invalid); continue; }
            b.code = kc;
            if (sp.isChord()) {
                const KeyDef* h = Keys::byName(sp.hold);
                b.hold = h ? XKeysymToKeycode(d, h->x11Sym) : 0;
                if (!b.hold) { setStatus(id, Status::Invalid); continue; }
            }
            if (sp.passthrough()) {
                if (!xi2) { setStatus(id, Status::Unavailable); continue; }
                binds.push_back(b);
                setStatus(id, Status::Ok);
                continue;
            }
            g_grabFailed = false;
            for (unsigned int v : lockVariants)
                XGrabKey(d, kc, b.xmods | v, root, True, GrabModeAsync, GrabModeAsync);
            XSync(d, False);    // дождаться ответа сервера, чтобы узнать про BadAccess
            if (g_grabFailed) {
                for (unsigned int v : lockVariants) XUngrabKey(d, kc, b.xmods | v, root);
                XSync(d, False);
                setStatus(id, Status::Busy);
                continue;
            }
            b.grabbed = true;
            grabExtra(b);
            // зажатое через переустановку — уже нажато: его автоповтор не новое нажатие (иначе клавиша
            // «используется», если её держать, переключала бы туда-сюда с частотой повтора)
            b.down = pressed.count(b.code) > 0;
            binds.push_back(b);
            setStatus(id, Status::Ok);
        }
        // сырые клавиши — всегда: по ним видно модификаторы и отпускание перехваченной клавиши
        if (!binds.empty()) { selectRaw(true, needButtons); rawSelected = true; }
    };

    // «держу A, жму B»: сработало ли сочетание на B (для обычного хоткея на той же клавише)
    auto chordOn = [&](int code) {
        for (const XBind& b : binds)
            if (b.hold && !b.mouse && b.code == code && holdOk(b.hold) && modsOk(b, modsNow())) return true;
        return false;
    };
    auto rawEvent = [&](bool mouse, int code, bool down) {
        bool chord = false;
        if (down && !mouse)                         // сначала сочетания: сработало — обычный на B молчит
            for (XBind& b : binds) {
                if (!b.hold || b.mouse || b.code != code) continue;
                if (b.down) { chord = chord || b.fired; continue; }
                b.down = true;
                if (holdOk(b.hold) && modsOk(b, modsNow())) { b.fired = chord = true; fire(b.id, true); }
            }
        for (XBind& b : binds) {
            if (!down && !mouse && b.hold && b.hold == code && b.fired) {
                b.fired = false;                    // отпустили первую клавишу сочетания
                fire(b.id, false);
                continue;
            }
            if (b.mouse != mouse || b.code != code) continue;
            // Перехваченную клавишу человека тоже решаем здесь, по его модификаторам (modsNow).
            // Перехват X сверяет модификаторы всей клавиатуры, а с ними и зажатые кликером через
            // XTEST: кликер держит Shift — и F6 человека для X уже Shift+F6, перехват молчит.
            if (down) {
                if (b.hold) continue;
                // прошлое нажатие (через XTEST) отпустили мимо нас — сначала «отпущено»
                if (b.down && b.fired) fire(b.id, false);
                b.down = b.raw = true;
                b.fired = !chord && modsOk(b, modsNow());
                if (b.fired) fire(b.id, true);
            } else {
                b.raw = false;
                if (!b.down) continue;
                b.down = false;
                if (b.fired) { b.fired = false; fire(b.id, false); }
            }
        }
    };

    const bool debugHk = qEnvironmentVariableIsSet("AQ_DEBUG_HK");   // сырые события в stderr
    while (running_) {
        if (rebind_.exchange(false) || grabbedAny != anyMods()) apply();
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == GenericEvent && ev.xcookie.extension == xiOpcode) {
                if (!XGetEventData(d, &ev.xcookie)) continue;
                const auto* re = static_cast<const XIRawEvent*>(ev.xcookie.data);
                const int code = re->detail;
                const bool fake = xtestDevices.count(re->sourceid) > 0;
                if (debugHk)
                    std::fprintf(stderr, "raw evtype=%d detail=%d src=%d fake=%d\n", ev.xcookie.evtype, code, re->sourceid, fake);
                switch (ev.xcookie.evtype) {
                    case XI_RawKeyPress: {
                        if (fake) break;
                        syncPressed(code);
                        const bool already = pressed.count(code) > 0;
                        pressed.insert(code);
                        if (!already) rawEvent(false, code, true);
                        break;
                    }
                    case XI_RawKeyRelease:
                        if (fake) break;
                        pressed.erase(code);
                        rawEvent(false, code, false);
                        break;
                    case XI_RawButtonPress:
                        if (fake) break;
                        syncPressed(0);
                        rawEvent(true, code, true);
                        break;
                    case XI_RawButtonRelease:
                        if (!fake) rawEvent(true, code, false);
                        break;
                    default:
                        break;
                }
                XFreeEventData(d, &ev.xcookie);
                continue;
            }
            if (ev.type == ButtonPress) {
                bool ours = false;
                for (XBind& b : binds)
                    if (b.grabbed && b.mouse && ev.xbutton.button == static_cast<unsigned>(b.code)) {
                        ours = true;                    // перехват наш — мышь отпускаем в любом случае
                        if (!modsOk(b, xToMods(ev.xbutton.state))) continue;
                        if (b.raw) continue;            // нажатие человека — уже по сырому событию (оно раньше)
                        // перехват срабатывает только на новое нажатие: если отпускание прошло
                        // мимо нас — сначала «отпущено», иначе клавиша срабатывала бы через раз
                        if (b.down && b.fired) fire(b.id, false);
                        b.down = true;
                        b.fired = true;
                        fire(b.id, true);
                    }
                // Пока кнопка зажата, X держит мышь у нас — клики кликера («пока держу») не дошли
                // бы до игры. Отпускаем сразу; отпускание кнопки — сырым событием XInput2 или,
                // если отпустили раньше, чем мы успели, обычным (оно пришло ещё нам).
                if (ours) { XUngrabPointer(d, CurrentTime); XFlush(d); }
                continue;
            }
            if (ev.type == ButtonRelease) {
                for (XBind& b : binds)
                    if (b.grabbed && b.mouse && ev.xbutton.button == static_cast<unsigned>(b.code) && b.down) {
                        b.down = b.raw = false;
                        if (b.fired) { b.fired = false; fire(b.id, false); }
                    }
                continue;
            }
            if (ev.type == KeyPress) {
                bool ours = false;
                // клавиша — вторая в сочетании, и первая зажата: сработало сочетание
                const bool chord = chordOn(static_cast<int>(ev.xkey.keycode));
                for (XBind& b : binds)
                    if (b.grabbed && !b.mouse && ev.xkey.keycode == static_cast<unsigned>(b.code)) {
                        ours = true;                    // перехват наш — клавиатуру отпускаем в любом случае
                        if (!modsOk(b, xToMods(ev.xkey.state))) continue;
                        if (b.down) continue;           // нажатие человека — уже по сырому событию (оно раньше) / автоповтор
                        b.down = true;
                        b.fired = !chord;
                        if (b.fired) fire(b.id, true);
                    }
                // Пока клавиша зажата, X держит всю клавиатуру у нас — нажатия кликера не
                // дошли бы до игры (режим «пока держу»). Отпускаем клавиатуру сразу;
                // отпускание самой клавиши видно по сырым событиям XInput2.
                if (ours && xi2) { XUngrabKeyboard(d, CurrentTime); XFlush(d); }
            } else if (ev.type == KeyRelease) {
                // Если сервер не умеет "detectable autorepeat", автоповтор приходит
                // парами Release+Press с одним временем — такой Release пропускаем.
                if (!detectable && XEventsQueued(d, QueuedAfterReading)) {
                    XEvent next;
                    XPeekEvent(d, &next);
                    if (next.type == KeyPress && next.xkey.keycode == ev.xkey.keycode &&
                        next.xkey.time == ev.xkey.time) {
                        XNextEvent(d, &next);   // съесть повтор
                        continue;
                    }
                }
                // пока клавиатура у нас, сырое отпускание не приходит — отпущена по этому событию
                pressed.erase(static_cast<int>(ev.xkey.keycode));
                for (XBind& b : binds)
                    if (b.grabbed && !b.mouse && ev.xkey.keycode == static_cast<unsigned>(b.code) && b.down) {
                        b.down = b.raw = false;
                        if (b.fired) { b.fired = false; fire(b.id, false); }
                    }
            }
        }
        // Перехваченная клавиша нажата через XTEST (xdotool, VNC, Synergy): сырые события таких
        // пропускаем, а обычное отпускание после XUngrabKeyboard уходит окну в фокусе. Отпущена
        // ли — смотрим у сервера, иначе осталась бы «зажатой» и следующее нажатие молчало бы.
        for (XBind& b : binds)
            if (b.grabbed && !b.mouse && b.down && !b.raw && !keymapDown(b.code)) {
                b.down = false;
                if (b.fired) { b.fired = false; fire(b.id, false); }
            }
        sleepMs(15);
    }
    ungrab();
    g_hkDisplay = nullptr;
    XCloseDisplay(d);
}
