#include "core/clicker_os.h"
#include "core/games.h"
#include "core/keys.h"
#include <algorithm>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Скан-код обязателен: игры читают ввод через Raw Input / DirectInput. Для цифрового блока
// MapVirtualKey возвращает 0 (его скан-коды общие со стрелками и Home/End) — коды из таблицы
static WORD scanCodeFor(unsigned short vk) {
    static const WORD numpad[10] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};
    if (vk >= 0x60 && vk <= 0x69) return numpad[vk - 0x60];
    if (vk == 0x6E) return 0x53;      // Num .
    if (vk == 0x2C) return 0x37;      // PrintScreen (E0 37)
    return static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
}

InputSender::InputSender() = default;
InputSender::~InputSender() = default;

bool InputSender::available() const { return true; }

bool InputSender::send(const ActionStep& a, bool down) {
    INPUT in = {};
    if (a.mouse) {
        in.type = INPUT_MOUSE;
        switch (a.button) {
            case 3:  in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;  break;
            case 2:  in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
            case 8:  in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; in.mi.mouseData = XBUTTON1; break;
            case 9:  in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; in.mi.mouseData = XBUTTON2; break;
            default: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;   break;
        }
        return SendInput(1, &in, sizeof(INPUT)) == 1;
    }
    const KeyDef* k = Keys::byName(a.key);
    if (!k) return false;
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = k->winVK;
    in.ki.wScan = scanCodeFor(k->winVK);
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (k->winExtended ? KEYEVENTF_EXTENDEDKEY : 0);
    return SendInput(1, &in, sizeof(INPUT)) == 1;
}

bool InputSender::pressIn(const ActionStep& a, unsigned long long, bool& focused) {
    focused = true;
    return send(a, true);
}

// Таймер Windows по умолчанию тикает раз в ~15,6 мс: пауза 5 мс стала бы 15. Пока кликер
// работает — точность 1 мс и таймер высокой точности (Windows 10 1803+, ~0,5 мс)
PreciseTimer::PreciseTimer() {
    timeBeginPeriod(1);
    h_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!h_) h_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
}

PreciseTimer::~PreciseTimer() {
    if (h_) CloseHandle(static_cast<HANDLE>(h_));
    timeEndPeriod(1);
}

void PreciseTimer::sleepFor(std::chrono::steady_clock::duration d) {
    LARGE_INTEGER due;
    due.QuadPart = -std::max<long long>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(d).count() / 100);
    if (h_ && SetWaitableTimer(static_cast<HANDLE>(h_), &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(static_cast<HANDLE>(h_), 100);
        return;
    }
    std::this_thread::sleep_for(d);
}
