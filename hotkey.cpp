#include "hotkey.h"
#include <chrono>
#include <algorithm>
#include <iterator>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <ApplicationServices/ApplicationServices.h>
    #include <unistd.h>
#else
    #include <X11/Xlib.h>
    #include <X11/XKBlib.h>
    #include <X11/Xproto.h>
    #include <X11/keysym.h>
    #include <X11/extensions/XInput2.h>
    #include <cstring>
#include <cstdio>
    #include <cstdlib>
    #include <ctime>
    #include <set>
    // Xlib объявляет макрос Status (= int), он ломает GlobalHotkey::Status
    #undef Status
#endif

GlobalHotkey::GlobalHotkey(Callback cb, const QList<HotkeySpec>& bindings) : callback_(std::move(cb)) {
    specs_ = bindings;
    statuses_.assign(static_cast<size_t>(bindings.size()), Status::Ok);
    running_ = true;
    worker_ = std::thread(&GlobalHotkey::loop, this);
}

GlobalHotkey::~GlobalHotkey() {
    running_ = false;
    // цикл опрашивает флаг каждые ~15 мс, поэтому ждём поток честно
    if (worker_.joinable()) worker_.join();
}

void GlobalHotkey::setBindings(const QList<HotkeySpec>& specs) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        specs_ = specs;
        statuses_.assign(static_cast<size_t>(specs.size()), Status::Ok);
    }
    rebind_ = true;
}

QList<HotkeySpec> GlobalHotkey::bindings() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return specs_;
}

void GlobalHotkey::setSuspended(bool s) {
    suspended_ = s;
    rebind_ = true;
}

GlobalHotkey::Status GlobalHotkey::status(int id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    if (id < 0 || id >= static_cast<int>(statuses_.size())) return Status::Unavailable;
    return statuses_[static_cast<size_t>(id)];
}

GlobalHotkey::Status GlobalHotkey::statusOf(const HotkeySpec& sp) const {
    std::lock_guard<std::mutex> lk(mutex_);
    for (int i = 0; i < specs_.size(); ++i)
        if (specs_[i] == sp) return i < static_cast<int>(statuses_.size()) ? statuses_[static_cast<size_t>(i)] : Status::Ok;
    return Status::Unavailable;
}

void GlobalHotkey::setStatus(int id, Status s) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (id >= 0 && id < static_cast<int>(statuses_.size())) statuses_[static_cast<size_t>(id)] = s;
}

void GlobalHotkey::resetStatuses(Status s) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (Status& st : statuses_) st = s;
}

void GlobalHotkey::fire(int id, bool down) {
    if (id < 0 || id >= active_.size()) return;
    // отпускание сообщаем всегда (даже на паузе) — чтобы ничего не осталось «зажатым»
    if ((!suspended_ || !down) && callback_) callback_(active_[id], down);
}

static void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

#ifdef _WIN32
// ===========================================================================
void GlobalHotkey::installX11ErrorHandler() {}

// «Сквозные» клавиши (буква, цифра…) и кнопки мыши: низкоуровневые хуки видят
// нажатие и пропускают его дальше — оно доходит до игры. Хуки вызываются в
// потоке, который их поставил (пока он разбирает сообщения), — блокировки не нужны.
namespace {
// hold — vk клавиши, которую держат («Tab+F»: hold = Tab, code = F); 0 — обычный хоткей
struct LLBind { unsigned code = 0; int mods = 0; int id = 0; unsigned hold = 0; bool down = false; bool fired = false; };
std::vector<LLBind> g_llKeys;    // клавиши: виртуальный код
std::vector<LLBind> g_llMouse;   // кнопки мыши: 2, 8, 9
std::vector<std::pair<int, bool>> g_llEvents;
bool g_llDown[256] = {};         // что зажал человек (нажатия кликера — SendInput — не считаются)

bool keyHeld(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
// первая клавиша сочетания зажата: по хуку (не кликером) и по состоянию клавиатуры
// (если отпускание хук пропустил — клавиша не «залипает»)
bool holdOk(unsigned vk) { return vk < 256 && g_llDown[vk] && keyHeld(static_cast<int>(vk)); }

int currentMods() {
    int m = 0;
    if (keyHeld(VK_CONTROL)) m |= ModCtrl;
    if (keyHeld(VK_MENU)) m |= ModAlt;
    if (keyHeld(VK_SHIFT)) m |= ModShift;
    if (keyHeld(VK_LWIN) || keyHeld(VK_RWIN)) m |= ModWin;
    return m;
}

void llEvent(std::vector<LLBind>& binds, unsigned code, bool down) {
    if (down) {
        // сначала сочетания «держу A, жму B»: сработало — обычный хоткей на B молчит
        bool chord = false;
        for (LLBind& b : binds) {
            if (!b.hold || b.code != code) continue;
            if (b.down) { chord = chord || b.fired; continue; }    // автоповтор
            b.down = true;
            if (holdOk(b.hold) && currentMods() == b.mods) {
                b.fired = chord = true;
                g_llEvents.push_back({b.id, true});
            }
        }
        for (LLBind& b : binds) {
            if (b.hold || b.code != code) continue;
            if (b.down) continue;              // автоповтор
            b.down = true;
            if (!chord && currentMods() == b.mods) { b.fired = true; g_llEvents.push_back({b.id, true}); }
        }
        return;
    }
    for (LLBind& b : binds) {
        if (b.code == code && b.down) {
            b.down = false;
            if (b.fired) { b.fired = false; g_llEvents.push_back({b.id, false}); }
        } else if (b.hold && b.hold == code && b.fired) {
            // отпустили первую клавишу сочетания — тоже «отпущено» (B ещё зажата: снова
            // сработает, только когда её нажмут заново)
            b.fired = false;
            g_llEvents.push_back({b.id, false});
        }
    }
}

LRESULT CALLBACK llKeyboardProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l);
        // свои же нажатия (SendInput кликера) не считаем
        if (!(k->flags & LLKHF_INJECTED)) {
            const bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN;
            if (k->vkCode < 256) g_llDown[k->vkCode] = down;
            llEvent(g_llKeys, k->vkCode, down);
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}

LRESULT CALLBACK llMouseProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(l);
        if (!(m->flags & LLMHF_INJECTED)) {
            unsigned btn = 0;
            bool down = false;
            switch (w) {
                case WM_MBUTTONDOWN: btn = 2; down = true; break;
                case WM_MBUTTONUP:   btn = 2; break;
                case WM_XBUTTONDOWN: btn = HIWORD(m->mouseData) == XBUTTON1 ? 8 : 9; down = true; break;
                case WM_XBUTTONUP:   btn = HIWORD(m->mouseData) == XBUTTON1 ? 8 : 9; break;
                default: break;
            }
            if (btn) llEvent(g_llMouse, btn, down);
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
} // namespace

void GlobalHotkey::loop() {
    // Очередь сообщений потока должна существовать ДО RegisterHotKey:
    // WM_HOTKEY приходит именно в неё.
    MSG msg;
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    std::vector<int> registered;                       // id+1 у RegisterHotKey
    struct Held { int id; unsigned vk; };
    std::vector<Held> held;                            // ждём отпускания (RegisterHotKey его не сообщает)
    std::vector<unsigned> regVk;                       // vk по id
    HHOOK kbHook = nullptr, mouseHook = nullptr;

    auto clear = [&]() {
        for (int r : registered) UnregisterHotKey(NULL, r);
        registered.clear();
        if (kbHook) { UnhookWindowsHookEx(kbHook); kbHook = nullptr; }
        if (mouseHook) { UnhookWindowsHookEx(mouseHook); mouseHook = nullptr; }
        // не оставить «зажатой»: и клавиши RegisterHotKey, и сквозные (хуки)
        for (const auto* list : {&g_llKeys, &g_llMouse})
            for (const LLBind& b : *list) if (b.fired) fire(b.id, false);
        g_llKeys.clear();
        g_llMouse.clear();
        g_llEvents.clear();
        // что человек держит прямо сейчас (Tab у «Tab+F») — помним и через переустановку хуков:
        // повторы идут от последней нажатой клавиши, новое «нажато» для Tab хук может не увидеть.
        // Отпущенное за это время — забываем (как на Linux: там зажатое сверяется с XQueryKeymap)
        for (int vk = 0; vk < 256; ++vk) g_llDown[vk] = g_llDown[vk] && keyHeld(vk);
        for (const Held& h : held) fire(h.id, false);
        held.clear();
    };
    auto apply = [&]() {
        clear();
        const QList<HotkeySpec> specs = bindings();
        active_ = specs;
        if (suspended_) { resetStatuses(Status::Suspended); return; }
        regVk.assign(static_cast<size_t>(specs.size()), 0);
        for (int id = 0; id < specs.size(); ++id) {
            const HotkeySpec& sp = specs[id];
            if (!sp.isValid() || !sp.isGlobalSafe()) { setStatus(id, Status::Invalid); continue; }
            if (sp.isMouse()) {
                g_llMouse.push_back({static_cast<unsigned>(sp.mouseButton()), sp.mods, id});
                continue;
            }
            const KeyDef* k = Keys::byName(sp.key);
            if (!k) { setStatus(id, Status::Invalid); continue; }
            if (sp.passthrough()) {
                unsigned hold = 0;
                if (sp.isChord()) {
                    const KeyDef* h = Keys::byName(sp.hold);
                    if (!h || !h->winVK) { setStatus(id, Status::Invalid); continue; }
                    hold = h->winVK;
                }
                g_llKeys.push_back({k->winVK, sp.mods, id, hold});
                continue;
            }
            UINT mods = MOD_NOREPEAT;     // удержание не дёргает старт/стоп много раз
            if (sp.mods & ModCtrl)  mods |= MOD_CONTROL;
            if (sp.mods & ModAlt)   mods |= MOD_ALT;
            if (sp.mods & ModShift) mods |= MOD_SHIFT;
            if (sp.mods & ModWin)   mods |= MOD_WIN;
            // F12 без модификаторов Windows резервирует под отладчик — RegisterHotKey
            // вернёт ошибку, и мы честно покажем "занята".
            if (RegisterHotKey(NULL, id + 1, mods, k->winVK)) {
                registered.push_back(id + 1);
                regVk[static_cast<size_t>(id)] = k->winVK;
                setStatus(id, Status::Ok);
            } else {
                setStatus(id, Status::Busy);
            }
        }
        if (!g_llKeys.empty()) {
            kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, llKeyboardProc, GetModuleHandleW(nullptr), 0);
            for (const LLBind& b : g_llKeys) setStatus(b.id, kbHook ? Status::Ok : Status::Busy);
        }
        if (!g_llMouse.empty()) {
            mouseHook = SetWindowsHookExW(WH_MOUSE_LL, llMouseProc, GetModuleHandleW(nullptr), 0);
            for (const LLBind& b : g_llMouse) setStatus(b.id, mouseHook ? Status::Ok : Status::Busy);
        }
    };

    while (running_) {
        if (rebind_.exchange(false)) apply();
        // ждём сообщений (хуки требуют быстрого ответа — не спим вслепую)
        MsgWaitForMultipleObjectsEx(0, nullptr, 15, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY) {
                const int id = static_cast<int>(msg.wParam) - 1;
                bool already = false;
                for (const Held& h : held) already = already || h.id == id;
                // эта же клавиша вторая в сочетании («Tab+F6»), и первая зажата — сработало сочетание
                if (id >= 0 && id < static_cast<int>(regVk.size()))
                    for (const LLBind& b : g_llKeys)
                        already = already || (b.hold && b.code == regVk[static_cast<size_t>(id)] &&
                                              holdOk(b.hold) && currentMods() == b.mods);
                if (!already && id >= 0 && id < static_cast<int>(regVk.size())) {
                    held.push_back({id, regVk[static_cast<size_t>(id)]});
                    fire(id, true);
                }
            }
        }
        // отпустили клавишу RegisterHotKey — узнаём по состоянию клавиатуры
        for (size_t i = 0; i < held.size();) {
            if (!keyHeld(static_cast<int>(held[i].vk))) {
                const int id = held[i].id;
                held.erase(held.begin() + static_cast<long>(i));
                fire(id, false);
            } else {
                ++i;
            }
        }
        if (!g_llEvents.empty()) {
            const auto events = g_llEvents;
            g_llEvents.clear();
            for (const auto& e : events) fire(e.first, e.second);
        }
    }
    clear();
}

#elif defined(__APPLE__)
// ===========================================================================
// macOS: перехват клавиатуры и мыши (CGEventTap) в своём потоке. Обычная
// горячая клавиша забирается себе (до игры не доходит), «сквозная» (буква,
// цифра, кнопка мыши) только замечается. Нужен «Универсальный доступ» — без
// него перехват не создаётся, и поток пробует снова раз в 2 секунды.
// ===========================================================================
void GlobalHotkey::installX11ErrorHandler() {}

namespace {
struct MacBind {
    int id = 0;
    bool mouse = false;
    int code = -1;        // kVK_* или номер кнопки мыши macOS (2 — средняя, 3/4 — боковые)
    int hold = -1;        // kVK_* клавиши, которую держат («Tab+F»), -1 — обычный хоткей
    int mods = 0;
    bool pass = false;
    bool down = false;
    bool fired = false;
    bool swallowUp = false;
};
struct MacTap {
    CFMachPortRef port = nullptr;
    std::vector<MacBind> binds;
    bool active = false;
    long long ownPid = 0;
    std::function<void(int, bool)> fire;
    bool down[128] = {};  // что зажал человек (нажатия кликера не считаются)
};

// первая клавиша сочетания зажата: по перехвату и по состоянию клавиатуры системы
// (пропустили отпускание — клавиша не «залипает»)
bool macHoldOk(const MacTap* t, int code) {
    return code >= 0 && code < 128 && t->down[code] &&
           CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, static_cast<CGKeyCode>(code));
}

int macMods(CGEventFlags f) {
    int m = 0;
    if (f & kCGEventFlagMaskControl)   m |= ModCtrl;
    if (f & kCGEventFlagMaskAlternate) m |= ModAlt;
    if (f & kCGEventFlagMaskShift)     m |= ModShift;
    if (f & kCGEventFlagMaskCommand)   m |= ModWin;
    return m;
}

CGEventRef macTapCallback(CGEventTapProxy, CGEventType type, CGEventRef ev, void* refcon) {
    auto* t = static_cast<MacTap*>(refcon);
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (t->port) CGEventTapEnable(t->port, true);   // система отключила — включаем обратно
        return ev;
    }
    const bool key = type == kCGEventKeyDown || type == kCGEventKeyUp;
    const bool mouse = type == kCGEventOtherMouseDown || type == kCGEventOtherMouseUp;
    if (!key && !mouse) return ev;
    // нажатия самого кликера не считаем
    if (CGEventGetIntegerValueField(ev, kCGEventSourceUnixProcessID) == t->ownPid) return ev;
    const int code = key ? static_cast<int>(CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode))
                         : static_cast<int>(CGEventGetIntegerValueField(ev, kCGMouseEventButtonNumber));
    const bool down = type == kCGEventKeyDown || type == kCGEventOtherMouseDown;
    if (key && code >= 0 && code < 128) t->down[code] = down;   // и на паузе: первая клавиша сочетания
    if (!t->active) return ev;
    const bool repeat = down && key && CGEventGetIntegerValueField(ev, kCGKeyboardEventAutorepeat) != 0;
    const int mods = macMods(CGEventGetFlags(ev));
    bool swallow = false;
    // сначала сочетания «держу A, жму B»: сработало — обычный хоткей на B молчит
    bool chord = false;
    if (down && key)
        for (MacBind& b : t->binds) {
            if (b.hold < 0 || b.mouse || b.code != code || mods != b.mods) continue;
            if (b.down || repeat) { chord = chord || b.fired; continue; }
            b.down = true;
            if (macHoldOk(t, b.hold)) {
                b.fired = chord = true;
                if (t->fire) t->fire(b.id, true);
            }
        }
    for (MacBind& b : t->binds) {
        if (!down && key && b.hold == code && b.fired) {
            // отпустили первую клавишу сочетания — тоже «отпущено»
            b.fired = false;
            if (t->fire) t->fire(b.id, false);
            continue;
        }
        if (b.mouse != mouse || b.code != code) continue;
        if (down) {
            if (b.hold >= 0) continue;          // сочетания — выше
            if (mods != b.mods) continue;
            if (!b.down && !repeat) {
                b.down = true;
                b.fired = !chord;
                if (b.fired && t->fire) t->fire(b.id, true);
                // забрали себе нажатие — забираем и повторы, и отпускание; пропустили в игру
                // (сработало сочетание) — пропускаем и их, иначе клавиша «залипнет» в игре
                b.swallowUp = !b.pass && !chord;
            }
            if (b.swallowUp) swallow = true;
        } else {
            if (b.down) {
                b.down = false;
                if (b.fired && t->fire) t->fire(b.id, false);
                b.fired = false;
            }
            if (b.swallowUp) { b.swallowUp = false; swallow = true; }
        }
    }
    return swallow ? nullptr : ev;
}
} // namespace

void GlobalHotkey::loop() {
    MacTap t;
    t.ownPid = static_cast<long long>(getpid());
    t.fire = [this](int id, bool down) { fire(id, down); };
    CFRunLoopSourceRef source = nullptr;

    auto createTap = [&]() {
        const CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
                                 CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp);
        t.port = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                                  mask, macTapCallback, &t);
        if (!t.port) return;
        source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, t.port, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        CGEventTapEnable(t.port, true);
    };
    auto apply = [&]() {
        for (const MacBind& b : t.binds) if (b.fired) fire(b.id, false);
        t.active = false;
        t.binds.clear();
        const QList<HotkeySpec> specs = bindings();
        active_ = specs;
        if (suspended_) { resetStatuses(Status::Suspended); return; }
        for (int id = 0; id < specs.size(); ++id) {
            const HotkeySpec& sp = specs[id];
            if (!sp.isValid() || !sp.isGlobalSafe()) { setStatus(id, Status::Invalid); continue; }
            MacBind b;
            b.id = id;
            b.mods = sp.mods;
            b.pass = sp.passthrough();
            if (sp.isMouse()) {
                b.mouse = true;
                const int btn = sp.mouseButton();
                b.code = btn == 2 ? 2 : btn == 8 ? 3 : 4;
            } else {
                b.code = Keys::macKeyCode(sp.key);
                if (b.code < 0) { setStatus(id, Status::Invalid); continue; }
                if (sp.isChord()) {
                    b.hold = Keys::macKeyCode(sp.hold);
                    if (b.hold < 0) { setStatus(id, Status::Invalid); continue; }
                }
            }
            t.binds.push_back(b);
            setStatus(id, t.port ? Status::Ok : Status::Unavailable);
        }
        t.active = t.port != nullptr;
    };

    createTap();
    auto lastTry = std::chrono::steady_clock::now();
    while (running_) {
        if (rebind_.exchange(false)) apply();
        if (!t.port) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastTry > std::chrono::seconds(2)) {
                lastTry = now;
                createTap();
                if (t.port) apply();
            }
            sleepMs(50);
            continue;
        }
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    }
    if (t.port) {
        CGEventTapEnable(t.port, false);
        if (source) {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
            CFRelease(source);
        }
        CFMachPortInvalidate(t.port);
        CFRelease(t.port);
    }
}

#else
// ===========================================================================
// Ошибка BadAccess на XGrabKey = клавишу уже захватила другая программа.
static std::atomic<Display*> g_hkDisplay{nullptr};
static std::atomic<bool> g_grabFailed{false};

static int nonFatalXErrorHandler(Display* d, XErrorEvent* e) {
    if (d == g_hkDisplay.load() && e->request_code == X_GrabKey && e->error_code == BadAccess)
        g_grabFailed = true;
    return 0;   // не завершать программу (поведение Xlib по умолчанию)
}

void GlobalHotkey::installX11ErrorHandler() {
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
        bool grabbed = false;  // XGrabKey (иначе — сырые события)
        unsigned xmods = 0;
        bool down = false;
        bool fired = false;
    };
    std::vector<XBind> binds;
    bool rawSelected = false;
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
            if (b.grabbed)
                for (unsigned int v : lockVariants) XUngrabKey(d, static_cast<KeyCode>(b.code), b.xmods | v, root);
            if (b.fired) fire(b.id, false);
        }
        binds.clear();
        if (rawSelected) selectRaw(false, false);
        rawSelected = false;
        XSync(d, False);
    };
    auto apply = [&]() {
        ungrab();
        // что за время паузы отпустили — забываем (зажатое осталось зажатым)
        {
            char keys[32] = {};
            XQueryKeymap(d, keys);
            for (auto it = pressed.begin(); it != pressed.end();)
                it = (*it > 0 && *it < 256 && (keys[*it / 8] & (1 << (*it % 8)))) ? std::next(it) : pressed.erase(it);
        }
        const QList<HotkeySpec> specs = bindings();
        active_ = specs;
        if (suspended_) { resetStatuses(Status::Suspended); return; }
        bool needKeys = false, needButtons = false;
        for (int id = 0; id < specs.size(); ++id) {
            const HotkeySpec& sp = specs[id];
            if (!sp.isValid() || !sp.isGlobalSafe()) { setStatus(id, Status::Invalid); continue; }
            XBind b;
            b.id = id;
            b.mods = sp.mods;
            if (sp.isMouse()) {
                if (!xi2) { setStatus(id, Status::Unavailable); continue; }
                b.mouse = true;
                b.code = sp.mouseButton();
                needButtons = true;
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
                needKeys = true;
                binds.push_back(b);
                setStatus(id, Status::Ok);
                continue;
            }
            if (sp.mods & ModCtrl)  b.xmods |= ControlMask;
            if (sp.mods & ModAlt)   b.xmods |= Mod1Mask;
            if (sp.mods & ModShift) b.xmods |= ShiftMask;
            if (sp.mods & ModWin)   b.xmods |= Mod4Mask;
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
            binds.push_back(b);
            setStatus(id, Status::Ok);
        }
        // сырые клавиши — всегда: по ним видно модификаторы и отпускание перехваченной клавиши
        if (!binds.empty()) { selectRaw(true, needButtons); rawSelected = true; }
        (void)needKeys;
    };

    // «держу A, жму B»: сработало ли сочетание на B (для обычного хоткея на той же клавише)
    auto chordOn = [&](int code) {
        for (const XBind& b : binds)
            if (b.hold && !b.mouse && b.code == code && holdOk(b.hold) && modsNow() == b.mods) return true;
        return false;
    };
    auto rawEvent = [&](bool mouse, int code, bool down) {
        bool chord = false;
        if (down && !mouse)                         // сначала сочетания: сработало — обычный на B молчит
            for (XBind& b : binds) {
                if (!b.hold || b.mouse || b.code != code) continue;
                if (b.down) { chord = chord || b.fired; continue; }
                b.down = true;
                if (holdOk(b.hold) && modsNow() == b.mods) { b.fired = chord = true; fire(b.id, true); }
            }
        for (XBind& b : binds) {
            if (!down && !mouse && b.hold && b.hold == code && b.fired) {
                b.fired = false;                    // отпустили первую клавишу сочетания
                fire(b.id, false);
                continue;
            }
            if (b.mouse != mouse || b.code != code) continue;
            if (b.grabbed && down) continue;        // нажатие перехваченной клавиши приходит через grab
            if (down) {
                if (b.hold || b.down) continue;
                b.down = true;
                if (!chord && modsNow() == b.mods) { b.fired = true; fire(b.id, true); }
            } else if (b.down) {
                b.down = false;
                if (b.fired) { b.fired = false; fire(b.id, false); }
            }
        }
    };

    while (running_) {
        if (rebind_.exchange(false)) apply();
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == GenericEvent && ev.xcookie.extension == xiOpcode) {
                if (!XGetEventData(d, &ev.xcookie)) continue;
                const auto* re = static_cast<const XIRawEvent*>(ev.xcookie.data);
                const int code = re->detail;
                const bool fake = xtestDevices.count(re->sourceid) > 0;
                if (qEnvironmentVariableIsSet("AQ_DEBUG_HK"))
                    std::fprintf(stderr, "raw evtype=%d detail=%d src=%d fake=%d\n", ev.xcookie.evtype, code, re->sourceid, fake);
                switch (ev.xcookie.evtype) {
                    case XI_RawKeyPress: {
                        if (fake) break;
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
                        if (!fake) rawEvent(true, code, true);
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
            if (ev.type == KeyPress) {
                bool ours = false;
                // клавиша — вторая в сочетании, и первая зажата: сработало сочетание
                const bool chord = chordOn(static_cast<int>(ev.xkey.keycode));
                for (XBind& b : binds)
                    if (b.grabbed && ev.xkey.keycode == static_cast<unsigned>(b.code) &&
                        (ev.xkey.state & relevant) == b.xmods) {
                        ours = true;
                        if (b.down) continue;
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
                for (XBind& b : binds)
                    if (b.grabbed && ev.xkey.keycode == static_cast<unsigned>(b.code) && b.down) {
                        b.down = false;
                        if (b.fired) { b.fired = false; fire(b.id, false); }
                    }
            }
        }
        sleepMs(15);
    }
    ungrab();
    g_hkDisplay = nullptr;
    XCloseDisplay(d);
}
#endif
