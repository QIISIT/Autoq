#include "core/framesync.h"
#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

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
