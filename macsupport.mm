#include "macsupport.h"
#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

int AqMac::frontmostPid() {
    @autoreleasepool {
        NSRunningApplication* app = [[NSWorkspace sharedWorkspace] frontmostApplication];
        return app ? static_cast<int>(app.processIdentifier) : 0;
    }
}

bool AqMac::accessibilityTrusted(bool prompt) {
    if (!prompt) return AXIsProcessTrusted();
    const void* keys[] = { kAXTrustedCheckOptionPrompt };
    const void* values[] = { kCFBooleanTrue };
    CFDictionaryRef opts = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                              &kCFCopyStringDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    const bool ok = AXIsProcessTrustedWithOptions(opts);
    CFRelease(opts);
    return ok;
}

bool AqMac::inputMonitoringAllowed(bool request) {
    if (@available(macOS 10.15, *)) {
        if (CGPreflightListenEventAccess()) return true;
        return request ? CGRequestListenEventAccess() : false;
    }
    return true;
}

bool AqMac::screenCaptureAllowed() {
    if (@available(macOS 10.15, *)) return CGPreflightScreenCaptureAccess();
    return true;
}

void AqMac::openPrivacyPane(const char* anchor) {
    @autoreleasepool {
        NSString* s = [NSString stringWithFormat:
            @"x-apple.systempreferences:com.apple.preference.security?%s", anchor];
        NSURL* url = [NSURL URLWithString:s];
        if (url) [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

void AqMac::keepOnTop(unsigned long long view, bool overlay) {
    @autoreleasepool {
        NSView* v = (__bridge NSView*)reinterpret_cast<void*>(view);
        NSWindow* w = v ? v.window : nil;
        if (!w) return;
        NSUInteger behavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                              NSWindowCollectionBehaviorFullScreenAuxiliary |
                              NSWindowCollectionBehaviorIgnoresCycle;
        if (overlay) behavior |= NSWindowCollectionBehaviorStationary;
        w.collectionBehavior = behavior;
        w.hidesOnDeactivate = NO;
        // поверх окон игр (и полноэкранных); оверлей — выше строки меню,
        // мини-окно — обычная «плавающая» панель (её AutoQ считает перекрывающей игру)
        const NSInteger level = overlay ? NSStatusWindowLevel : NSFloatingWindowLevel;
        if (w.level != level) w.level = level;
    }
}
