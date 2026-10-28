#include "macsupport.h"
#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <utility>

// событие «reopen» (kAEReopenApplication) — в функцию из AqMac::onReopen
static std::function<void()>& reopenCallback() { static std::function<void()> f; return f; }

@interface AqReopenHandler : NSObject
- (void)handleReopen:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation AqReopenHandler
- (void)handleReopen:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply {
    (void)event;
    (void)reply;
    if (reopenCallback()) reopenCallback()();
}
@end

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

void AqMac::bringToFront(unsigned long long view) {
    @autoreleasepool {
        NSView* v = (__bridge NSView*)reinterpret_cast<void*>(view);
        NSWindow* w = v ? v.window : nil;
        // на рабочий стол, где сейчас человек (и поверх полноэкранной игры), а не туда, где окно было
        if (w) w.collectionBehavior = w.collectionBehavior | NSWindowCollectionBehaviorMoveToActiveSpace |
                                      NSWindowCollectionBehaviorFullScreenAuxiliary;
        if (@available(macOS 14.0, *)) [NSApp activate];
        else [NSApp activateIgnoringOtherApps:YES];
        if (w) [w makeKeyAndOrderFront:nil];
    }
}

void AqMac::onReopen(std::function<void()> callback) {
    @autoreleasepool {
        static AqReopenHandler* handler = [[AqReopenHandler alloc] init];
        reopenCallback() = std::move(callback);
        // свой обработчик вместо стандартного AppKit (тот ничего не показывает, если окна спрятаны)
        [[NSAppleEventManager sharedAppleEventManager] setEventHandler:handler
                                                           andSelector:@selector(handleReopen:withReplyEvent:)
                                                         forEventClass:kCoreEventClass
                                                            andEventID:kAEReopenApplication];
    }
}
