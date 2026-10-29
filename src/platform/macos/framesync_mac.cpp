#include "core/framesync.h"

#include <ApplicationServices/ApplicationServices.h>

bool FrameSync::vblank(double*, std::chrono::steady_clock::time_point*) { return false; }
double FrameSync::monitorHz(unsigned long long) { return 0; }

// Рамка окна в CGWindowList — в точках, от левого верхнего угла главного экрана (как у Qt)
bool FrameSync::windowCenter(unsigned long long windowHandle, int* x, int* y) {
    if (!windowHandle) return false;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,
                                                 static_cast<CGWindowID>(windowHandle));
    if (!list) return false;
    bool ok = false;
    if (CFArrayGetCount(list) > 0) {
        auto d = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, 0));
        auto b = static_cast<CFDictionaryRef>(CFDictionaryGetValue(d, kCGWindowBounds));
        CGRect r = CGRectZero;
        if (b && CGRectMakeWithDictionaryRepresentation(b, &r) && r.size.width > 0 && r.size.height > 0) {
            *x = static_cast<int>(r.origin.x + r.size.width / 2);
            *y = static_cast<int>(r.origin.y + r.size.height / 2);
            ok = true;
        }
    }
    CFRelease(list);
    return ok;
}
