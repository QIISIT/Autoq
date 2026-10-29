#include "core/winmatch.h"
#include "core/i18n.h"
#include "platform/macos/macsupport.h"
#include <QFileInfo>

#include <ApplicationServices/ApplicationServices.h>
#include <libproc.h>
#include <unistd.h>

ActiveWindowProbe::ActiveWindowProbe() { ownPid_ = getpid(); }
ActiveWindowProbe::~ActiveWindowProbe() {}
bool ActiveWindowProbe::available() const { return true; }

static QString cfString(CFStringRef s) {
    if (!s) return QString();
    const CFIndex len = CFStringGetLength(s);
    QString out(static_cast<int>(len), Qt::Uninitialized);
    CFStringGetCharacters(s, CFRangeMake(0, len), reinterpret_cast<UniChar*>(out.data()));
    return out;
}

static long long dictInt(CFDictionaryRef d, CFStringRef key) {
    auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(d, key));
    long long v = 0;
    if (n && CFGetTypeID(n) == CFNumberGetTypeID()) CFNumberGetValue(n, kCFNumberLongLongType, &v);
    return v;
}

static double dictDouble(CFDictionaryRef d, CFStringRef key, double def) {
    auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(d, key));
    double v = def;
    if (n && CFGetTypeID(n) == CFNumberGetTypeID()) CFNumberGetValue(n, kCFNumberDoubleType, &v);
    return v;
}

static CGRect dictBounds(CFDictionaryRef d) {
    CGRect r = CGRectZero;
    auto b = static_cast<CFDictionaryRef>(CFDictionaryGetValue(d, kCGWindowBounds));
    if (b) CGRectMakeWithDictionaryRepresentation(b, &r);
    return r;
}

static QString dictString(CFDictionaryRef d, CFStringRef key, bool* present = nullptr) {
    auto s = static_cast<CFStringRef>(CFDictionaryGetValue(d, key));
    if (present) *present = s != nullptr;
    return (s && CFGetTypeID(s) == CFStringGetTypeID()) ? cfString(s) : QString();
}

static bool isOwnOverlay(CFDictionaryRef d, long long ownPid) {
    // оверлей КПС пропускает клики сквозь себя — под ним всё равно игра
    return dictInt(d, kCGWindowOwnerPID) == ownPid &&
           dictString(d, kCGWindowName).contains(T("оверлей"));
}

bool ActiveWindowProbe::exists(unsigned long long handle) {
    if (!handle) return false;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,
                                                 static_cast<CGWindowID>(handle));
    if (!list) return false;
    const bool ok = CFArrayGetCount(list) > 0;
    CFRelease(list);
    return ok;
}

bool ActiveWindowProbe::pointerOver(unsigned long long handle) {
    if (!handle) return false;
    CGEventRef ev = CGEventCreate(nullptr);
    if (!ev) return true;                       // не узнать — не мешаем
    const CGPoint pt = CGEventGetLocation(ev);
    CFRelease(ev);
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!list) return true;
    bool over = false;
    // окна идут сверху вниз; панель Dock и строка меню (слой 20+) не в счёт
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
        auto d = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
        const long long layer = dictInt(d, kCGWindowLayer);
        if (layer < 0 || layer >= 20) continue;
        if (dictDouble(d, kCGWindowAlpha, 1.0) < 0.05) continue;
        if (!CGRectContainsPoint(dictBounds(d), pt)) continue;
        if (isOwnOverlay(d, ownPid_)) continue;
        over = static_cast<unsigned long long>(dictInt(d, kCGWindowNumber)) == handle;
        break;
    }
    CFRelease(list);
    return over;
}

static void macFill(WindowInfo& info, CFDictionaryRef best, int pid, long long ownPid);

QList<WindowInfo> ActiveWindowProbe::listWindows() {
    QList<WindowInfo> out;
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!list) return out;
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
        auto d = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
        if (dictInt(d, kCGWindowLayer) != 0) continue;
        WindowInfo w;
        macFill(w, d, static_cast<int>(dictInt(d, kCGWindowOwnerPID)), ownPid_);
        out << w;
    }
    CFRelease(list);
    return out;
}

WindowInfo ActiveWindowProbe::query() {
    WindowInfo info;
    const int pid = AqMac::frontmostPid();
    if (pid <= 0) return info;
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!list) return info;
    CFDictionaryRef best = nullptr, any = nullptr;
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
        auto d = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
        if (dictInt(d, kCGWindowOwnerPID) != pid) continue;
        if (isOwnOverlay(d, ownPid_)) continue;
        if (!any) any = d;
        if (dictInt(d, kCGWindowLayer) == 0) { best = d; break; }   // обычное окно приложения
    }
    if (!best) best = any;
    if (best) macFill(info, best, pid, ownPid_);
    CFRelease(list);
    return info;
}

static void macFill(WindowInfo& info, CFDictionaryRef best, int pid, long long ownPid) {
    {
        info.valid = true;
        info.handle = static_cast<unsigned long long>(dictInt(best, kCGWindowNumber));
        bool hasName = false;
        info.title = dictString(best, kCGWindowName, &hasName).trimmed();
        // без разрешения «Запись экрана» macOS не отдаёт заголовки чужих окон
        info.titleKnown = hasName || pid == ownPid;
        info.pid = pid;
        const QString owner = dictString(best, kCGWindowOwnerName);
        char path[PROC_PIDPATHINFO_MAXSIZE] = {};
        QString exe, bundle;
        if (proc_pidpath(pid, path, sizeof(path)) > 0) {
            const QString full = QString::fromUtf8(path);
            exe = QFileInfo(full).fileName();
            const int app = full.indexOf(".app/");
            if (app > 0) bundle = QFileInfo(full.left(app + 4)).fileName();   // "Roblox.app"
        }
        for (const QString& s : QStringList{exe, owner, bundle, QString(bundle).remove(".app")})
            if (!s.isEmpty() && !info.candidates.contains(s)) info.candidates << s;
        info.process = !exe.isEmpty() ? exe : owner;
        info.processKnown = !exe.isEmpty() || !owner.isEmpty();
    }
}
