#include "core/winmatch.h"
#include "core/i18n.h"
#include <QFileInfo>
#include <QCoreApplication>
#include <QFile>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include "platform/macos/macsupport.h"
    #include <ApplicationServices/ApplicationServices.h>
    #include <libproc.h>
    #include <unistd.h>
#else
    #include <X11/Xlib.h>
    #include <X11/Xatom.h>
    #include <X11/Xutil.h>
    #include <unistd.h>
    #include <climits>
#endif

// ---------------------------------------------------------------------------
// Сопоставление с правилом (общее для обеих систем)
// ---------------------------------------------------------------------------
static QString normProc(QString s) {
    s = s.trimmed().toLower();
    if (s.endsWith(".exe")) s.chop(4);
    return s;
}

bool processMatches(const QStringList& names, const WindowInfo& info) {
    for (const QString& rawName : names) {
        const QString n = normProc(rawName);
        if (n.isEmpty()) continue;
        for (const QString& c : info.candidates) {
            if (normProc(c) == n) return true;
            // /proc/<pid>/comm обрезается ядром до 15 символов: "GenshinImpact.e"
            if (c.length() == 15) {
                const QString cl = c.toLower();
                if ((n + ".exe").startsWith(cl) || n.startsWith(cl)) return true;
            }
        }
    }
    return false;
}

bool matchesRule(const WindowRule& rule, const WindowInfo& info) {
    if (!info.valid) return false;
    bool hasTitles = false, titleOk = false;
    for (const QString& t : rule.titles) {
        const QString tt = t.trimmed();
        if (tt.isEmpty()) continue;
        hasTitles = true;
        if (info.title.contains(tt, Qt::CaseInsensitive)) { titleOk = true; break; }
    }
    bool hasProcs = false;
    for (const QString& p : rule.processes) if (!p.trimmed().isEmpty()) { hasProcs = true; break; }

    if (!hasProcs && !hasTitles) return false;          // пустое правило — ни с чем
    if (!hasTitles) titleOk = true;
    // заголовок узнать нельзя, но процесс известен — решает процесс
    if (hasTitles && !info.titleKnown && hasProcs && info.processKnown) titleOk = true;
    if (hasProcs) {
        if (info.processKnown) return titleOk && processMatches(rule.processes, info);
        return hasTitles && titleOk;                    // процесс неизвестен — по заголовку
    }
    return titleOk;
}

QString ownProcessName() {
    return QFileInfo(QCoreApplication::applicationFilePath()).fileName();
}

// ---------------------------------------------------------------------------
#ifdef _WIN32
// ---------------------------------------------------------------------------
ActiveWindowProbe::ActiveWindowProbe() {}
ActiveWindowProbe::~ActiveWindowProbe() {}
bool ActiveWindowProbe::available() const { return true; }

static QString exeNameOfPid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return QString();
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = MAX_PATH * 2;
    QString out;
    if (QueryFullProcessImageNameW(h, 0, buf, &len))
        out = QFileInfo(QString::fromWCharArray(buf, static_cast<int>(len))).fileName();
    CloseHandle(h);
    return out;
}

struct UwpSearch { DWORD hostPid; DWORD found; };
static BOOL CALLBACK findUwpChild(HWND child, LPARAM lp) {
    auto* s = reinterpret_cast<UwpSearch*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(child, &pid);
    if (pid && pid != s->hostPid) { s->found = pid; return FALSE; }
    return TRUE;
}

bool ActiveWindowProbe::exists(unsigned long long handle) {
    return handle && IsWindow(reinterpret_cast<HWND>(static_cast<uintptr_t>(handle)));
}

bool ActiveWindowProbe::pointerOver(unsigned long long handle) {
    if (!handle) return false;
    POINT pt;
    if (!GetCursorPos(&pt)) return true;          // не узнать — не мешаем
    const HWND game = reinterpret_cast<HWND>(static_cast<uintptr_t>(handle));
    // верхнее окно под курсором, пропуская прозрачные для мыши (оверлей КПС)
    HWND top = ChildWindowFromPointEx(GetDesktopWindow(), pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
    if (top && GetAncestor(top, GA_ROOT) == game) return true;
    HWND under = WindowFromPoint(pt);
    if (!under) return false;
    HWND root = GetAncestor(under, GA_ROOT);
    if (root == game) return true;
    // окно сверху «сквозное» (WS_EX_TRANSPARENT) — клик всё равно уйдёт в игру
    if ((GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0) {
        RECT r;
        return GetWindowRect(game, &r) && PtInRect(&r, pt);
    }
    return false;
}

static WindowInfo winInfo(HWND h);

WindowInfo ActiveWindowProbe::query() {
    HWND h = GetForegroundWindow();
    if (!h) return WindowInfo{};
    return winInfo(h);
}

static BOOL CALLBACK collectWindow(HWND h, LPARAM lp) {
    auto* out = reinterpret_cast<QList<WindowInfo>*>(lp);
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) || IsIconic(h)) return TRUE;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindowTextLengthW(h) <= 0) return TRUE;
    out->append(winInfo(h));
    return TRUE;
}

QList<WindowInfo> ActiveWindowProbe::listWindows() {
    QList<WindowInfo> out;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&out));
    return out;
}

static WindowInfo winInfo(HWND h) {
    WindowInfo info;
    info.valid = true;
    info.handle = static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h));
    wchar_t buf[1024] = {};
    int n = GetWindowTextW(h, buf, 1023);
    info.title = QString::fromWCharArray(buf, n > 0 ? n : 0).trimmed();

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    QString exe = pid ? exeNameOfPid(pid) : QString();
    // Приложения из Microsoft Store (Minecraft Bedrock) живут внутри
    // ApplicationFrameHost.exe — настоящий процесс у дочернего окна.
    if (exe.compare("ApplicationFrameHost.exe", Qt::CaseInsensitive) == 0) {
        UwpSearch s{pid, 0};
        EnumChildWindows(h, findUwpChild, reinterpret_cast<LPARAM>(&s));
        if (s.found) { pid = s.found; exe = exeNameOfPid(pid); }
    }
    info.pid = pid;
    if (!exe.isEmpty()) {
        info.process = exe;
        info.candidates << exe;
        info.processKnown = true;
    }
    return info;
}

// ---------------------------------------------------------------------------
#elif defined(__APPLE__)
// ---------------------------------------------------------------------------
// macOS
// ---------------------------------------------------------------------------
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

#else  // Linux / X11
// ---------------------------------------------------------------------------
ActiveWindowProbe::ActiveWindowProbe() {
    display_ = XOpenDisplay(nullptr);
}
ActiveWindowProbe::~ActiveWindowProbe() {
    if (display_) XCloseDisplay(static_cast<Display*>(display_));
}
bool ActiveWindowProbe::available() const { return display_ != nullptr; }

bool ActiveWindowProbe::exists(unsigned long long handle) {
    Display* d = static_cast<Display*>(display_);
    if (!d || !handle) return false;
    // У закрытого окна XGetWindowAttributes вернёт ошибку BadWindow — наш
    // обработчик ошибок (main.cpp) её проглатывает, а здесь вернётся 0.
    XWindowAttributes wa;
    return XGetWindowAttributes(d, static_cast<Window>(handle), &wa) != 0;
}

// Верхнее окно над w: рамка менеджера окон вокруг него (или само окно, если рамки нет)
static Window topLevelOf(Display* d, Window w) {
    for (int guard = 0; guard < 16; ++guard) {
        Window rr = 0, parent = 0, *kids = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(d, w, &rr, &parent, &kids, &n)) break;
        if (kids) XFree(kids);
        if (!parent || parent == rr) break;
        w = parent;
    }
    return w;
}

bool x11FocusIn(void* display, unsigned long long handle) {
    Display* d = static_cast<Display*>(display);
    if (!d) return true;
    if (!handle) return false;
    Window focus = None;
    int revert = 0;
    XGetInputFocus(d, &focus, &revert);
    if (focus == static_cast<Window>(handle)) return true;
    if (focus == None) return false;                  // клавиатура сейчас ни у кого
    // PointerRoot или корень: клавиши идут окну под курсором — не знаем, не мешаем
    if (focus == PointerRoot || focus == DefaultRootWindow(d)) return true;
    // фокус в дочернем окне игры или в рамке вокруг неё
    return topLevelOf(d, focus) == topLevelOf(d, static_cast<Window>(handle));
}

bool ActiveWindowProbe::pointerOver(unsigned long long handle) {
    Display* d = static_cast<Display*>(display_);
    if (!d || !handle) return false;
    Window root = DefaultRootWindow(d), r, c;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned int mask = 0;
    // курсор не на этом экране X (под Wayland — над не-X окном) — не знаем, не мешаем
    if (!XQueryPointer(d, root, &r, &c, &rx, &ry, &wx, &wy, &mask)) return true;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(d, static_cast<Window>(handle), &wa)) return false;
    int x = 0, y = 0;
    Window child;
    if (!XTranslateCoordinates(d, root, static_cast<Window>(handle), rx, ry, &x, &y, &child)) return false;
    if (!(x >= 0 && y >= 0 && x < wa.width && y < wa.height)) return false;
    // поверх игры под курсором другое окно (мини-окно AutoQ и т.п.) — клик ушёл бы в него.
    // Окна, прозрачные для мыши (оверлей КПС), сервер здесь не возвращает.
    if (c && topLevelOf(d, static_cast<Window>(handle)) != c) return false;
    return true;
}

static QString readTextProp(Display* d, Window w, Atom prop, Atom type) {
    Atom actual; int fmt; unsigned long n, after; unsigned char* data = nullptr;
    QString out;
    if (XGetWindowProperty(d, w, prop, 0, 4096, False, type, &actual, &fmt, &n,
                           &after, &data) == Success && data) {
        if (actual == type && fmt == 8)
            out = QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<int>(n));
        XFree(data);
    }
    return out;
}

static QString fileNameOf(QString path) {
    path.replace('\\', '/');                  // Wine пишет argv[0] как "C:\...\Game.exe"
    int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.mid(slash + 1) : path;
}

static WindowInfo x11Info(Display* d, Window win);

WindowInfo ActiveWindowProbe::query() {
    Display* d = static_cast<Display*>(display_);
    if (!d) return WindowInfo{};
    Window root = DefaultRootWindow(d);
    Atom netActive = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    Atom actual; int fmt; unsigned long n, after; unsigned char* data = nullptr;
    Window win = 0;
    if (XGetWindowProperty(d, root, netActive, 0, 1, False, XA_WINDOW, &actual, &fmt,
                           &n, &after, &data) == Success && data) {
        if (n > 0) win = *reinterpret_cast<Window*>(data);
        XFree(data);
    }
    if (!win) return WindowInfo{};   // под Wayland: активно не X11-окно
    return x11Info(d, win);
}

QList<WindowInfo> ActiveWindowProbe::listWindows() {
    QList<WindowInfo> out;
    Display* d = static_cast<Display*>(display_);
    if (!d) return out;
    Atom actual; int fmt; unsigned long n = 0, after; unsigned char* data = nullptr;
    if (XGetWindowProperty(d, DefaultRootWindow(d), XInternAtom(d, "_NET_CLIENT_LIST", False), 0, 4096,
                           False, XA_WINDOW, &actual, &fmt, &n, &after, &data) == Success && data) {
        const Window* wins = reinterpret_cast<Window*>(data);
        for (unsigned long i = 0; i < n; ++i) {
            WindowInfo w = x11Info(d, wins[i]);
            if (!w.title.isEmpty() || w.processKnown) out << w;
        }
        XFree(data);
    }
    return out;
}

static WindowInfo x11Info(Display* d, Window win) {
    WindowInfo info;
    Atom actual; int fmt; unsigned long n, after; unsigned char* data = nullptr;
    info.valid = true;
    info.handle = static_cast<unsigned long long>(win);

    // Заголовок: _NET_WM_NAME (UTF-8), иначе классический WM_NAME
    Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
    info.title = readTextProp(d, win, XInternAtom(d, "_NET_WM_NAME", False), utf8).trimmed();
    if (info.title.isEmpty()) {
        XTextProperty tp;
        if (XGetWMName(d, win, &tp) && tp.value) {
            char** list = nullptr; int cnt = 0;
            if (Xutf8TextPropertyToTextList(d, &tp, &list, &cnt) >= Success && cnt > 0 && list) {
                info.title = QString::fromUtf8(list[0]).trimmed();
                XFreeStringList(list);
            }
            XFree(tp.value);
        }
    }

    // Класс окна: у Wine/Proton res_name = имя exe ("genshinimpact.exe")
    QStringList classNames;
    XClassHint hint{nullptr, nullptr};
    if (XGetClassHint(d, win, &hint)) {
        if (hint.res_name)  { classNames << QString::fromUtf8(hint.res_name);  XFree(hint.res_name); }
        if (hint.res_class) { classNames << QString::fromUtf8(hint.res_class); XFree(hint.res_class); }
    }

    // PID -> процесс
    Atom pidAtom = XInternAtom(d, "_NET_WM_PID", False);
    data = nullptr;
    if (XGetWindowProperty(d, win, pidAtom, 0, 1, False, XA_CARDINAL, &actual, &fmt,
                           &n, &after, &data) == Success && data) {
        if (n > 0) info.pid = static_cast<long long>(*reinterpret_cast<unsigned long*>(data));
        XFree(data);
    }
    QString argv0Name, exeName, comm;
    if (info.pid > 0) {
        const QString base = QString("/proc/%1/").arg(info.pid);
        char link[PATH_MAX + 1] = {};
        ssize_t len = readlink((base + "exe").toLocal8Bit().constData(), link, PATH_MAX);
        if (len > 0) exeName = fileNameOf(QString::fromLocal8Bit(link, static_cast<int>(len)));
        QFile c(base + "comm");
        if (c.open(QIODevice::ReadOnly)) comm = QString::fromUtf8(c.readAll()).trimmed();
        QFile cl(base + "cmdline");
        if (cl.open(QIODevice::ReadOnly)) {
            QByteArray all = cl.readAll();
            int z = all.indexOf('\0');
            argv0Name = fileNameOf(QString::fromUtf8(z >= 0 ? all.left(z) : all));
        }
    }
    // сначала имена процесса (в нормальном регистре), потом класс окна
    for (const QString& s : QStringList{argv0Name, exeName, comm} + classNames)
        if (!s.isEmpty() && !info.candidates.contains(s)) info.candidates << s;
    // Процесс "известен", если нашли его через /proc, либо класс окна — это имя exe
    // (так подписывает окна Wine/Proton). Класс вроде "steam_proton" процессом
    // не считаем: тогда игру опознаём по заголовку (Steam из Flatpak живёт со своими
    // номерами процессов, и /proc по _NET_WM_PID его не находит).
    bool classIsExe = false;
    for (const QString& c : classNames)
        if (c.endsWith(".exe", Qt::CaseInsensitive)) classIsExe = true;
    const bool fromProc = !(argv0Name.isEmpty() && exeName.isEmpty() && comm.isEmpty());
    info.processKnown = fromProc || classIsExe;

    // Что показать пользователю: имя exe игры (Wine), иначе исполняемый файл
    for (const QString& s : info.candidates)
        if (s.endsWith(".exe", Qt::CaseInsensitive)) { info.process = s; break; }
    if (info.process.isEmpty()) info.process = !exeName.isEmpty() ? exeName
                                             : !comm.isEmpty() ? comm
                                             : info.candidates.value(0);
    return info;
}
#endif
