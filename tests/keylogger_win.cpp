// Окно-логгер для тестов под Windows: пишет нажатия так, как их видят игры.
//   keylogger_win <файл> [--occupy <vk_hex>]
// Строки:
//   "K vk=XX scan=XX ext=N"   — WM_KEYDOWN / WM_SYSKEYDOWN
//   "R vk=XX make=XX e0=N"    — Raw Input (так читают ввод многие игры)
//   "B 1|2|3|8|9"             — кнопки мыши
// --occupy занимает глобальную клавишу (для проверки "клавиша занята").
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static FILE* g_f = nullptr;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        std::fprintf(g_f, "K vk=%02X scan=%02X ext=%d\n", static_cast<unsigned>(w),
                     static_cast<unsigned>((l >> 16) & 0xFF), static_cast<int>((l >> 24) & 1));
        return 0;
    case WM_INPUT: {
        BYTE buf[512];
        UINT sz = sizeof(buf);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, buf, &sz,
                            sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1)) {
            RAWINPUT* r = reinterpret_cast<RAWINPUT*>(buf);
            if (r->header.dwType == RIM_TYPEKEYBOARD && !(r->data.keyboard.Flags & RI_KEY_BREAK))
                std::fprintf(g_f, "R vk=%02X make=%02X e0=%d\n", r->data.keyboard.VKey,
                             r->data.keyboard.MakeCode, (r->data.keyboard.Flags & RI_KEY_E0) ? 1 : 0);
        }
        break;
    }
    case WM_LBUTTONDOWN: std::fprintf(g_f, "B 1\n"); return 0;
    case WM_MBUTTONDOWN: std::fprintf(g_f, "B 2\n"); return 0;
    case WM_RBUTTONDOWN: std::fprintf(g_f, "B 3\n"); return 0;
    case WM_XBUTTONDOWN: std::fprintf(g_f, "B %d\n", HIWORD(w) == XBUTTON1 ? 8 : 9); return TRUE;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int main(int argc, char** argv) {
    g_f = std::fopen(argc > 1 ? argv[1] : "keylog.txt", "w");
    if (!g_f) return 1;
    std::setvbuf(g_f, nullptr, _IONBF, 0);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"KeyLoggerWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    // --title "<заголовок>" — чтобы изобразить окно игры/браузера
    wchar_t title[256] = L"KeyLogger";
    for (int i = 2; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--title") == 0)
            MultiByteToWideChar(CP_UTF8, 0, argv[i + 1], -1, title, 255);
    HWND hwnd = CreateWindowW(L"KeyLoggerWnd", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              40, 40, 600, 400, nullptr, nullptr, inst, nullptr);
    RAWINPUTDEVICE rid = {1, 6, 0, hwnd};   // клавиатура
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    for (int i = 2; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--occupy") == 0)
            RegisterHotKey(hwnd, 7, MOD_NOREPEAT, static_cast<UINT>(std::strtoul(argv[i + 1], nullptr, 16)));
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
