#include "services/autostart.h"
#include <cstdio>
#include <string>
#include <cstdlib>
#include <fstream>
#include <climits>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

static std::string execPath() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    std::string p(buf, static_cast<size_t>(n));
    // программу уже заменило обновление (до перезапуска) — путь тот же
    const std::string deleted = " (deleted)";
    if (p.size() > deleted.size() && p.compare(p.size() - deleted.size(), deleted.size(), deleted) == 0)
        p.resize(p.size() - deleted.size());
    return p;
}

// Exec="путь": внутри кавычек " ` $ \ — через «\», и каждый «\» ещё раз удвоен для строки
// .desktop (литеральный «\» — четыре). % — %%
static std::string execLine(const std::string& exe, const char* args = "") {
    std::string out = "Exec=\"";
    for (char c : exe) {
        if (c == '"' || c == '`' || c == '$') out += "\\\\";
        else if (c == '\\') out += "\\\\\\";
        else if (c == '%') out += '%';
        out += c;
    }
    return out + "\"" + args;
}

static void mkdirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

// $XDG_CONFIG_HOME/autostart (по умолчанию ~/.config/autostart) — как у AppSettings
static std::string desktopFilePath() {
    std::string cfg;
    if (const char* x = getenv("XDG_CONFIG_HOME"); x && x[0] == '/') cfg = x;
    else if (const char* home = getenv("HOME")) cfg = std::string(home) + "/.config";
    else return {};
    mkdirs(cfg + "/autostart");
    return cfg + "/autostart/AutoQ.desktop";
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
        // поля, которые понимают и KDE, и GNOME, и XFCE
        f << "[Desktop Entry]\n"
          << "Type=Application\n"
          << "Version=1.0\n"
          << "Name=AutoQ\n"
          << "Comment=Автокликер для игр\n"
          << execLine(execPath(), " --tray") << "\n"
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

    std::ostringstream want;
    want << "[Desktop Entry]\n"
         << "Type=Application\n"
         << "Version=1.0\n"
         << "Name=AutoQ\n"
         << "GenericName=Автокликер\n"
         << "GenericName[en]=Autoclicker\n"
         << "Comment=Автокликер для игр\n"
         << "Comment[en]=Autoclicker for games\n"
         << execLine(exe) << "\n"
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
