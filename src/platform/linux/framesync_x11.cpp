#include "core/framesync.h"

#include <X11/Xlib.h>

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
