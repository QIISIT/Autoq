#include "framesync.h"
#include <cmath>
#include <algorithm>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <dwmapi.h>
#elif !defined(__APPLE__)
    #include <X11/Xlib.h>
#endif

QList<int> FrameSync::hzChoices() { return {30, 60, 75, 90, 100, 120, 144, 165, 180, 240, 280, 360}; }

double FrameSync::frameMs(double hz) { return hz >= 1 ? 1000.0 / hz : 0; }

double FrameSync::cycleMs(const QList<ActionStep>& actions, int intervalMs, bool simultaneous) {
    double c = std::max(1, intervalMs);
    const int n = actions.size();
    if (simultaneous && n > 1) {
        int hold = 0;
        for (const ActionStep& a : actions) hold = std::max(hold, a.holdMs);
        return c + hold;
    }
    for (int i = 0; i < n; ++i) {
        c += actions[i].holdMs;
        if (i + 1 < n) c += actions[i].afterMs;
    }
    return c;
}

double FrameSync::meanCycleMs(const QList<ActionStep>& actions, int intervalMs, bool simultaneous, int jitterMs,
                              int minCycleMs) {
    const int wait = std::max(1, intervalMs);
    const double busy = cycleMs(actions, intervalMs, simultaneous) - wait;   // «держать» и паузы
    if (jitterMs <= 0) return std::max<double>(minCycleMs, busy + wait);
    double sum = 0;
    for (int u = -jitterMs; u <= jitterMs; ++u) sum += std::max<double>(minCycleMs, busy + std::max(1, intervalMs + u));
    return sum / (2 * jitterMs + 1);
}

int FrameSync::frames(double cycle, double frame) {
    if (frame <= 0) return 1;
    return std::max(1, static_cast<int>(std::lround(cycle / frame)));
}

using Clock = std::chrono::steady_clock;
static Clock::duration msDur(double ms) {
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(ms));
}
static double msBetween(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

Clock::time_point FrameSync::Grid::at(long long k) const { return t0_ + msDur(frame_ * double(k)); }

Clock::time_point FrameSync::Grid::next(double frameMs, Clock::time_point now, bool useVblank) {
    if (frameMs != cfgFrame_) {          // новый такт — новая сетка от «сейчас»
        cfgFrame_ = frame_ = frameMs;
        t0_ = now;
        next_ = 0;
        synced_ = {};
    }
    // раз в 2 с сверяемся с монитором: сетка — посередине кадров, дальше всего от их границ
    if (useVblank && (synced_ == Clock::time_point{} || now - synced_ > std::chrono::seconds(2))) {
        synced_ = now;
        double period = 0;
        Clock::time_point vb;
        if (vblank(&period, &vb) && std::abs(period - cfgFrame_) < cfgFrame_ * 0.03) {
            const Clock::time_point want = at(next_);
            frame_ = period;
            t0_ = vb + msDur(period / 2);
            next_ = std::llround(msBetween(t0_, want) / frame_);
        }
    }
    // цикл затянулся (долгое «держать») или кликер ждал игру — ближайший кадр
    if (at(next_) < now) next_ = static_cast<long long>(std::ceil(msBetween(t0_, now) / frame_));
    return at(next_);
}

#ifdef _WIN32
bool FrameSync::vblank(double* periodMs, std::chrono::steady_clock::time_point* at) {
    DWM_TIMING_INFO info = {};
    info.cbSize = sizeof(info);
    if (FAILED(DwmGetCompositionTimingInfo(nullptr, &info)) || info.qpcRefreshPeriod == 0) return false;
    LARGE_INTEGER freq, now;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&now) || freq.QuadPart <= 0) return false;
    // часы QPC -> steady_clock: обе отметки «сейчас» берём подряд
    const auto steadyNow = std::chrono::steady_clock::now();
    const double ago = double(static_cast<LONGLONG>(now.QuadPart - info.qpcVBlank)) * 1000.0 / double(freq.QuadPart);
    const double period = double(info.qpcRefreshPeriod) * 1000.0 / double(freq.QuadPart);
    if (period < 1.0 || period > 100.0) return false;
    *periodMs = period;
    *at = steadyNow - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double, std::milli>(ago));
    return true;
}

double FrameSync::monitorHz(unsigned long long windowHandle) {
    HMONITOR mon = windowHandle ? MonitorFromWindow(reinterpret_cast<HWND>(windowHandle), MONITOR_DEFAULTTONEAREST)
                                : MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return 0;
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) || dm.dmDisplayFrequency <= 1) return 0;
    double hz = dm.dmDisplayFrequency;
    // Windows отдаёт целое (59 вместо 59,94) — точное значение берём у DWM, если это тот же монитор
    double period = 0;
    std::chrono::steady_clock::time_point at;
    if (vblank(&period, &at) && std::abs(1000.0 / period - hz) < hz * 0.03) hz = 1000.0 / period;
    return hz;
}

bool FrameSync::windowCenter(unsigned long long, int*, int*) { return false; }
#elif defined(__APPLE__)
bool FrameSync::vblank(double*, std::chrono::steady_clock::time_point*) { return false; }
double FrameSync::monitorHz(unsigned long long) { return 0; }
bool FrameSync::windowCenter(unsigned long long, int*, int*) { return false; }
#else
bool FrameSync::vblank(double*, std::chrono::steady_clock::time_point*) { return false; }
double FrameSync::monitorHz(unsigned long long) { return 0; }

bool FrameSync::windowCenter(unsigned long long windowHandle, int* x, int* y) {
    if (!windowHandle) return false;
    Display* d = XOpenDisplay(nullptr);
    if (!d) return false;
    bool ok = false;
    XWindowAttributes wa;
    Window child = 0;
    int rx = 0, ry = 0;
    // окно могли закрыть — ошибку X11 гасит общий обработчик программы
    if (XGetWindowAttributes(d, static_cast<Window>(windowHandle), &wa) &&
        XTranslateCoordinates(d, static_cast<Window>(windowHandle), DefaultRootWindow(d), 0, 0, &rx, &ry, &child)) {
        *x = rx + wa.width / 2;
        *y = ry + wa.height / 2;
        ok = true;
    }
    XCloseDisplay(d);
    return ok;
}
#endif
