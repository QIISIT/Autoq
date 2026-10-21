// Окно-логгер для тестов под X11: пишет каждое нажатие клавиши и кнопки мыши.
//   keylogger_x11 <файл> [заголовок]
// Строки: "K <keycode> <keysym>" и "B <кнопка>"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "keylog.txt";
    const char* title = argc > 2 ? argv[2] : "KeyLogger";
    Display* d = XOpenDisplay(nullptr);
    if (!d) return 1;
    int s = DefaultScreen(d);
    Window w = XCreateSimpleWindow(d, RootWindow(d, s), 40, 40, 600, 400, 1,
                                   BlackPixel(d, s), WhitePixel(d, s));
    XStoreName(d, w, title);
    XChangeProperty(d, w, XInternAtom(d, "_NET_WM_NAME", False), XInternAtom(d, "UTF8_STRING", False),
                    8, PropModeReplace, reinterpret_cast<const unsigned char*>(title),
                    static_cast<int>(std::strlen(title)));
    // [класс окна] [nopid] — чтобы изобразить окно Proton-игры
    char resName[64] = "keylogger", resClass[64] = "KeyLogger";
    if (argc > 3) { std::snprintf(resName, sizeof resName, "%s", argv[3]);
                    std::snprintf(resClass, sizeof resClass, "%s", argv[3]); }
    XClassHint ch{resName, resClass};
    XSetClassHint(d, w, &ch);
    const bool noPid = argc > 4 && std::strcmp(argv[4], "nopid") == 0;
    long pid = getpid();
    if (!noPid)
        XChangeProperty(d, w, XInternAtom(d, "_NET_WM_PID", False), XA_CARDINAL, 32,
                        PropModeReplace, reinterpret_cast<unsigned char*>(&pid), 1);
    XSelectInput(d, w, KeyPressMask | ButtonPressMask | ExposureMask);
    XMapWindow(d, w);
    XFlush(d);
    FILE* f = std::fopen(out, "w");
    if (!f) return 1;
    std::setvbuf(f, nullptr, _IONBF, 0);
    std::printf("%lu\n", static_cast<unsigned long>(w));
    std::fflush(stdout);
    for (;;) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.type == KeyPress) {
            KeySym ks = XLookupKeysym(&e.xkey, 0);
            const char* n = XKeysymToString(ks);
            std::fprintf(f, "K %u %s\n", e.xkey.keycode, n ? n : "?");
        } else if (e.type == ButtonPress) {
            std::fprintf(f, "B %u\n", e.xbutton.button);
        }
    }
}
