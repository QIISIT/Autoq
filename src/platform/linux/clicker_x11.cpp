#include "core/clicker_os.h"
#include "core/games.h"
#include "core/keys.h"
#include <thread>

#include "platform/linux/winmatch_x11.h"
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

InputSender::InputSender() { h_ = XOpenDisplay(nullptr); }

InputSender::~InputSender() {
    if (!h_) return;
    Display* d = static_cast<Display*>(h_);
    XSync(d, False);
    XCloseDisplay(d);
}

bool InputSender::available() const { return h_ != nullptr; }

bool InputSender::send(const ActionStep& a, bool down) {
    Display* d = static_cast<Display*>(h_);
    if (!d) return false;
    if (a.mouse) {
        XTestFakeButtonEvent(d, static_cast<unsigned>(a.button), down ? True : False, CurrentTime);
    } else {
        const KeyDef* k = Keys::byName(a.key);
        if (!k) return false;
        const KeyCode kc = XKeysymToKeycode(d, k->x11Sym);
        if (!kc) return false;
        XTestFakeKeyEvent(d, kc, down ? True : False, CurrentTime);
    }
    XFlush(d);
    return true;
}

// XTest шлёт в фокус X, а менеджер окон переводит его раньше, чем обновляет
// _NET_ACTIVE_WINDOW: между проверкой и нажатием фокус сменить некому — сервер захвачен
bool InputSender::pressIn(const ActionStep& a, unsigned long long window, bool& focused) {
    focused = true;
    Display* d = static_cast<Display*>(h_);
    if (!d) return false;
    XGrabServer(d);
    focused = x11FocusIn(d, window);
    const bool sent = focused && send(a, true);
    XUngrabServer(d);
    XFlush(d);
    return sent;
}

PreciseTimer::PreciseTimer() = default;
PreciseTimer::~PreciseTimer() = default;
void PreciseTimer::sleepFor(std::chrono::steady_clock::duration d) { std::this_thread::sleep_for(d); }
