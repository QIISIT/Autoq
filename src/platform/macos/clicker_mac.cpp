#include "core/clicker_os.h"
#include "core/games.h"
#include "core/keys.h"
#include "platform/macos/macsupport.h"
#include <thread>

#include <ApplicationServices/ApplicationServices.h>

InputSender::InputSender() { h_ = CGEventSourceCreate(kCGEventSourceStateHIDSystemState); }

InputSender::~InputSender() {
    if (h_) CFRelease(static_cast<CGEventSourceRef>(h_));
}

// без «Универсального доступа» macOS молча выбрасывает события
bool InputSender::available() const { return AqMac::accessibilityTrusted(false); }

bool InputSender::send(const ActionStep& a, bool down) {
    CGEventSourceRef source = static_cast<CGEventSourceRef>(h_);
    CGEventRef e = nullptr;
    if (a.mouse) {
        CGEventRef where = CGEventCreate(nullptr);
        const CGPoint pt = where ? CGEventGetLocation(where) : CGPointZero;
        if (where) CFRelease(where);
        CGEventType type;
        CGMouseButton btn;
        switch (a.button) {
            case 3:  type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp; btn = kCGMouseButtonRight; break;
            case 2:  type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; btn = kCGMouseButtonCenter; break;
            // боковые: у macOS номера 3 и 4
            case 8:  type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; btn = static_cast<CGMouseButton>(3); break;
            case 9:  type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; btn = static_cast<CGMouseButton>(4); break;
            default: type = down ? kCGEventLeftMouseDown  : kCGEventLeftMouseUp;  btn = kCGMouseButtonLeft;  break;
        }
        e = CGEventCreateMouseEvent(source, type, pt, btn);
        if (!e) return false;
        if (a.button == 8 || a.button == 9)
            CGEventSetIntegerValueField(e, kCGMouseEventButtonNumber, a.button == 8 ? 3 : 4);
        // каждый клик — одиночный, иначе быстрые клики macOS считает двойными
        CGEventSetIntegerValueField(e, kCGMouseEventClickState, 1);
    } else {
        const int code = Keys::macKeyCode(a.key);
        if (code < 0) return false;
        e = CGEventCreateKeyboardEvent(source, static_cast<CGKeyCode>(code), down);
        if (!e) return false;
        // модификаторы, зажатые самим кликером (шаги «Shift↓, F, Shift↑»): у каждого потока
        // кликера свои, остальные клавиши идут с ними
        thread_local CGEventFlags held = 0;
        const QString k = a.key.toLower();
        if (k == "shift" || k == "ctrl" || k == "alt") {
            const CGEventFlags flag = k == "shift" ? kCGEventFlagMaskShift
                                    : k == "ctrl"  ? kCGEventFlagMaskControl
                                                   : kCGEventFlagMaskAlternate;
            held = down ? (held | flag) : (held & ~flag);
            CGEventSetType(e, kCGEventFlagsChanged);   // иначе игра модификатор не увидит
        }
        CGEventSetFlags(e, held);
    }
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
    return true;
}

bool InputSender::pressIn(const ActionStep& a, unsigned long long, bool& focused) {
    focused = true;
    return send(a, true);
}

PreciseTimer::PreciseTimer() = default;
PreciseTimer::~PreciseTimer() = default;
void PreciseTimer::sleepFor(std::chrono::steady_clock::duration d) { std::this_thread::sleep_for(d); }
