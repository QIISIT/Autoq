#include "core/winmatch.h"
#include <QFile>

#include "platform/linux/winmatch_x11.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <unistd.h>
#include <climits>

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
    // обработчик ошибок (GlobalHotkey::init) её проглатывает, а здесь вернётся 0.
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

bool x11FocusIn(Display* d, unsigned long long handle) {
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
