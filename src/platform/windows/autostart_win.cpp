#include "services/autostart.h"
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

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
