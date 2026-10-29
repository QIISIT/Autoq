#include "core/winmatch.h"
#include <QFileInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

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
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;     // свёрнутые — тоже: игру могли свернуть
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
