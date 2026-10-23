#include "autostart.h"
#include <fstream>
#include <cstdio>
#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shellapi.h>
#else
    #include <unistd.h>
    #include <limits.h>
    #include <sys/stat.h>
    #include <cstdlib>
    #include <cstring>
    #include <sstream>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

std::string Autostart::execPath() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    return std::string(buf, n);
#elif defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) return {};
    char real[PATH_MAX];
    if (!realpath(raw, real)) return std::string(raw);
    return std::string(real);
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return std::string(buf);
#endif
}

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Windows. AutoQ работает с правами администратора (иначе Genshin и другие
// игры «от администратора» не принимают клики), но окно UAC при каждом входе
// в систему — плохо, а программы с правами администратора из ключа Run
// Windows вообще не запускает. Поэтому:
//  • ключ HKCU\...\Run\AutoQ = "AutoQ.exe --autostart" — его видно в
//    Диспетчере задач → Автозагрузка, там же его можно отключить;
//  • при таком запуске AutoQ (ещё без прав) просит Планировщик задач
//    запустить задачу «AutoQ» — она стартует AutoQ с наивысшими правами
//    без окна UAC и сразу в трей. У задачи нет своих триггеров: сама по себе
//    при входе она не запускается, только по просьбе из ключа Run.
// Отключили в Диспетчере задач — Windows пишет это в StartupApproved\Run,
// и AutoQ показывает автозапуск выключенным.
// ---------------------------------------------------------------------------
static const wchar_t* kTaskName = L"AutoQ";
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kApprovedKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

static std::wstring exePathW() {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return buf;
}

// Запустить команду без окна и дождаться кода выхода
static DWORD runHidden(std::wstring cmd) {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return static_cast<DWORD>(-1);
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = static_cast<DWORD>(-1);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static std::wstring xmlEscape(const std::wstring& in) {
    std::wstring out;
    for (wchar_t c : in) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

static std::wstring currentUser() {
    wchar_t dom[256] = {}, usr[256] = {};
    GetEnvironmentVariableW(L"USERDOMAIN", dom, 256);
    GetEnvironmentVariableW(L"USERNAME", usr, 256);
    std::wstring u = usr;
    if (dom[0]) u = std::wstring(dom) + L"\\" + u;
    return u;
}

static std::wstring taskFilePath() {
    wchar_t win[MAX_PATH + 1] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    return std::wstring(win) + L"\\System32\\Tasks\\" + kTaskName;
}

static bool taskExists() {
    DWORD a = GetFileAttributesW(taskFilePath().c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring runValue() {
    HKEY h;
    std::wstring out;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS) return out;
    wchar_t buf[2048] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
    if (RegQueryValueExW(h, kTaskName, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ))
        out = buf;
    RegCloseKey(h);
    return out;
}

// Отключено ли в Диспетчере задач (первый байт 0x03 — выключено, 0x02/0x06 — включено)
static bool disabledInTaskManager() {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS) return false;
    BYTE data[16] = {};
    DWORD size = sizeof(data), type = 0;
    bool off = false;
    if (RegQueryValueExW(h, kTaskName, nullptr, &type, data, &size) == ERROR_SUCCESS && size >= 1)
        off = (data[0] & 0x01) != 0;
    RegCloseKey(h);
    return off;
}

static std::wstring runCommand() { return L"\"" + exePathW() + L"\" --autostart"; }

static bool createTask() {
    const std::wstring exe = exePathW();
    const std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
    const std::wstring user = xmlEscape(currentUser());
    const std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Description>AutoQ: start with administrator rights (from Startup)</Description></RegistrationInfo>\r\n"
        L"  <Triggers />\r\n"
        L"  <Principals>\r\n"
        L"    <Principal id=\"Author\">\r\n"
        L"      <UserId>" + user + L"</UserId>\r\n"
        L"      <LogonType>InteractiveToken</LogonType>\r\n"
        L"      <RunLevel>HighestAvailable</RunLevel>\r\n"
        L"    </Principal>\r\n"
        L"  </Principals>\r\n"
        L"  <Settings>\r\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
        L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n"
        L"    <StartWhenAvailable>false</StartWhenAvailable>\r\n"
        L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n"
        L"    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n"
        L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
        L"    <Enabled>true</Enabled>\r\n"
        L"    <Hidden>false</Hidden>\r\n"
        L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
        L"    <Priority>7</Priority>\r\n"
        L"  </Settings>\r\n"
        L"  <Actions Context=\"Author\">\r\n"
        L"    <Exec>\r\n"
        L"      <Command>&quot;" + xmlEscape(exe) + L"&quot;</Command>\r\n"
        L"      <Arguments>--tray</Arguments>\r\n"
        L"      <WorkingDirectory>" + xmlEscape(dir) + L"</WorkingDirectory>\r\n"
        L"    </Exec>\r\n"
        L"  </Actions>\r\n"
        L"</Task>\r\n";
    wchar_t tmp[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring xmlPath = std::wstring(tmp) + L"AutoQ_task.xml";
    HANDLE f = CreateFileW(xmlPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const wchar_t bom = 0xFEFF;   // schtasks ждёт UTF-16 с BOM
    WriteFile(f, &bom, sizeof(bom), &written, nullptr);
    WriteFile(f, xml.data(), static_cast<DWORD>(xml.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
    const DWORD code = runHidden(std::wstring(L"schtasks.exe /Create /TN \"") + kTaskName +
                                 L"\" /XML \"" + xmlPath + L"\" /F");
    DeleteFileW(xmlPath.c_str());
    return code == 0;
}

bool Autostart::isEnabled() { return !runValue().empty() && !disabledInTaskManager(); }

bool Autostart::setEnabled(bool enable) {
    if (!enable) {
        HKEY h;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
            RegDeleteValueW(h, kTaskName);
            RegCloseKey(h);
        }
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
            RegDeleteValueW(h, kTaskName);
            RegCloseKey(h);
        }
        if (taskExists()) runHidden(std::wstring(L"schtasks.exe /Delete /TN \"") + kTaskName + L"\" /F");
        return !isEnabled();
    }
    // задача нужна для запуска с правами без UAC; без неё (нет прав) — запустится
    // и без прав, просто в трей
    createTask();
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS)
        return false;
    const std::wstring cmd = runCommand();
    RegSetValueExW(h, kTaskName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                   static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    // если раньше отключали в Диспетчере задач — включаем обратно
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        const BYTE on[12] = {0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        RegSetValueExW(h, kTaskName, 0, REG_BINARY, on, sizeof(on));
        RegCloseKey(h);
    }
    return isEnabled();
}

void Autostart::migrate() {
    // до 1.12: задача с триггером «при входе» и без ключа Run — переделать на новый способ
    if (taskExists() && runValue().empty()) { setEnabled(true); return; }
    // программу перенесли в другую папку — обновить путь
    const std::wstring v = runValue();
    if (!v.empty() && v != runCommand() && !disabledInTaskManager()) setEnabled(true);
}

void Autostart::ensureDesktopEntry() {}

static bool processElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION e = {};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size) && e.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

bool Autostart::elevated() { return processElevated(); }

int Autostart::relaunchElevatedIfNeeded(int argc, char** argv) {
    if (processElevated()) return -1;
    bool fromAutostart = false, alreadyTried = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--autostart") fromAutostart = true;
        if (a == "--elevated" || a == "--no-elevate") alreadyTried = true;
    }
    if (alreadyTried) return -1;                       // второй раз не просим — без петли
    if (fromAutostart) {
        // при входе в систему: задача запустит AutoQ с правами без окна UAC
        if (taskExists() && runHidden(std::wstring(L"schtasks.exe /Run /TN \"") + kTaskName + L"\"") == 0)
            return 0;
        return -1;                                      // задачи нет — работаем без прав, в трее
    }
    const std::wstring exe = exePathW();
    std::wstring params = L"--elevated";
    for (int i = 1; i < argc; ++i) {
        std::wstring a;
        const int n = MultiByteToWideChar(CP_ACP, 0, argv[i], -1, nullptr, 0);
        if (n > 1) { a.resize(n - 1); MultiByteToWideChar(CP_ACP, 0, argv[i], -1, &a[0], n); }
        params += L" \"" + a + L"\"";
    }
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) return 0;               // запустилась копия с правами — эта выходит
    return -1;                                          // отказались в UAC — работаем как есть
}

bool Autostart::restartElevated() {
    if (processElevated()) return false;
    const std::wstring exe = exePathW();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = L"--elevated --restarted";
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}
#elif defined(__APPLE__)
// ---------------------------------------------------------------------------
// macOS: агент входа ~/Library/LaunchAgents/com.qisit.autoq.plist.
// macOS 13+ при добавлении покажет уведомление «Добавлен объект входа» —
// это нормально, его видно в Настройки → Основные → Объекты входа.
// ---------------------------------------------------------------------------
static std::string launchAgentPath() {
    const char* home = getenv("HOME");
    if (!home) return {};
    std::string lib = std::string(home) + "/Library";
    std::string dir = lib + "/LaunchAgents";
    mkdir(lib.c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    return dir + "/com.qisit.autoq.plist";
}

static std::string plistEscape(const std::string& in) {
    std::string out;
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

void Autostart::migrate() { if (isEnabled()) setEnabled(true); }   // обновить путь к программе
void Autostart::ensureDesktopEntry() {}
bool Autostart::elevated() { return true; }
bool Autostart::restartElevated() { return false; }
int Autostart::relaunchElevatedIfNeeded(int, char**) { return -1; }

bool Autostart::setEnabled(bool enable) {
    std::string path = launchAgentPath();
    if (path.empty()) return false;
    if (!enable) {
        std::remove(path.c_str());
        return true;
    }
    std::ofstream f(path);
    if (!f) return false;
    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      << "<plist version=\"1.0\">\n<dict>\n"
      << "  <key>Label</key><string>com.qisit.autoq</string>\n"
      << "  <key>ProgramArguments</key>\n  <array>\n"
      << "    <string>" << plistEscape(execPath()) << "</string>\n"
      << "    <string>--tray</string>\n"
      << "  </array>\n"
      << "  <key>RunAtLoad</key><true/>\n"
      << "  <key>ProcessType</key><string>Interactive</string>\n"
      << "  <key>LimitLoadToSessionType</key><string>Aqua</string>\n"
      << "</dict>\n</plist>\n";
    f.flush();
    return f.good();
}

bool Autostart::isEnabled() {
    std::string path = launchAgentPath();
    if (path.empty()) return false;
    std::ifstream f(path);
    return f.good();
}
#else
static std::string desktopFilePath(const char* name = "AutoQ") {
    const char* home = getenv("HOME");
    if (!home) return {};
    std::string cfg = std::string(home) + "/.config";
    std::string dir = cfg + "/autostart";
    mkdir(cfg.c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    return dir + "/" + name + ".desktop";
}

void Autostart::migrate() { if (isEnabled()) setEnabled(true); }   // обновить путь к программе
bool Autostart::elevated() { return true; }
bool Autostart::restartElevated() { return false; }
int Autostart::relaunchElevatedIfNeeded(int, char**) { return -1; }

bool Autostart::setEnabled(bool enable) {
    std::string path = desktopFilePath();
    if (path.empty()) return false;
    if (enable) {
        std::ofstream f(path);
        if (!f) return false;
        // Полный набор полей, который понимают и KDE, и GNOME, и XFCE.
        // Exec в кавычках — на случай пробелов в пути.
        f << "[Desktop Entry]\n"
          << "Type=Application\n"
          << "Version=1.0\n"
          << "Name=AutoQ\n"
          << "Comment=Автокликер для игр\n"
          << "Exec=\"" << execPath() << "\" --tray\n"
          << "Icon=autoq\n"
          << "Terminal=false\n"
          << "Categories=Utility;\n"
          << "X-GNOME-Autostart-enabled=true\n"
          << "X-KDE-autostart-after=panel\n"
          << "StartupNotify=false\n";
        f.flush();
        return f.good();
    } else {
        std::remove(path.c_str());
        return true;
    }
}
bool Autostart::isEnabled() {
    std::string path = desktopFilePath();
    if (path.empty()) return false;
    std::ifstream f(path);
    return f.good();
}

// ---------------------------------------------------------------------------
// Пункт меню приложений. Файл помечен X-AutoQ-Generated — свой файл обновляем,
// если программу перенесли в другую папку; чужой (из пакета) не трогаем.
// ---------------------------------------------------------------------------
static std::string dataHome() {
    const char* x = getenv("XDG_DATA_HOME");
    if (x && *x) return x;
    const char* home = getenv("HOME");
    if (!home) return {};
    return std::string(home) + "/.local/share";
}

static bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

static void mkdirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

void Autostart::ensureDesktopEntry() {
    const std::string base = dataHome();
    if (base.empty()) return;

    // установлено пакетом (AUR и т.п.) — у системы уже есть свой файл
    const char* dirsEnv = getenv("XDG_DATA_DIRS");
    std::string dirs = (dirsEnv && *dirsEnv) ? dirsEnv : "/usr/local/share:/usr/share";
    std::stringstream ss(dirs);
    for (std::string d; std::getline(ss, d, ':');)
        if (!d.empty() && fileExists(d + "/applications/autoq.desktop")) return;

    const std::string dir = base + "/applications";
    const std::string path = dir + "/autoq.desktop";
    const std::string exe = execPath();
    if (exe.empty()) return;
    const std::string execLine = "Exec=\"" + exe + "\"";

    std::ostringstream want;
    want << "[Desktop Entry]\n"
         << "Type=Application\n"
         << "Version=1.0\n"
         << "Name=AutoQ\n"
         << "GenericName=Автокликер\n"
         << "GenericName[en]=Autoclicker\n"
         << "Comment=Автокликер для игр\n"
         << "Comment[en]=Autoclicker for games\n"
         << execLine << "\n"
         << "Icon=autoq\n"
         << "Terminal=false\n"
         << "Categories=Utility;Game;\n"
         << "Keywords=clicker;autoclicker;кликер;автокликер;\n"
         << "StartupNotify=false\n"
         << "X-AutoQ-Generated=true\n";
    if (fileExists(path)) {
        std::ifstream in(path);
        std::stringstream have;
        have << in.rdbuf();
        if (have.str().find("X-AutoQ-Generated=true") == std::string::npos) return;   // чужой файл
        if (have.str() == want.str()) return;                                          // уже актуален
    }
    mkdirs(dir);
    std::ofstream f(path);
    if (f) f << want.str();
}
#endif
