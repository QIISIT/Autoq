#include "core/hotkey.h"
#include <chrono>
#include <vector>

#include <ApplicationServices/ApplicationServices.h>
#include <unistd.h>

// Перехват клавиатуры и мыши (CGEventTap) в своём потоке. Обычная горячая клавиша
// забирается себе (до игры не доходит), «сквозная» (буква, цифра, кнопка мыши) только
// замечается. Нужен «Универсальный доступ» — без него перехват не создаётся, и поток
// пробует снова раз в 2 секунды.

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void GlobalHotkey::init() {}

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
    bool ownHeld[128] = {};   // нажатие забрали себе, а привязки пересобрали, пока клавиша зажата
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
    // нажатие забрали себе до пересборки привязок — его повторы и отпускание тоже наши
    if (key && code >= 0 && code < 128 && t->ownHeld[code]) {
        const bool rep = down && CGEventGetIntegerValueField(ev, kCGKeyboardEventAutorepeat) != 0;
        if (!down || !rep) t->ownHeld[code] = false;
        if (!down || rep) return nullptr;
    }
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
            // повтор нажатия: как решили при нажатии, какие бы модификаторы ни были сейчас
            // (Ctrl отпустили раньше Q — повторы Q всё равно наши, раз наше и отпускание)
            if (b.down || repeat) { if (b.swallowUp) swallow = true; continue; }
            if (mods != b.mods) continue;
            b.down = true;
            b.fired = !chord;
            if (b.fired && t->fire) t->fire(b.id, true);
            // забрали себе нажатие — забираем и повторы, и отпускание; пропустили в игру
            // (сработало сочетание) — пропускаем и их, иначе клавиша «залипнет» в игре
            b.swallowUp = !b.pass && !chord;
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
        for (const MacBind& b : t.binds) {
            if (b.fired) fire(b.id, false);
            if (!b.mouse && b.down && b.swallowUp && b.code >= 0 && b.code < 128) t.ownHeld[b.code] = true;
        }
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
