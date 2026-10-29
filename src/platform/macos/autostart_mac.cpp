#include "services/autostart.h"
#include <cstdio>
#include <string>
#include <cstdlib>
#include <fstream>
#include <climits>

#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>

static std::string execPath() {
    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) return {};
    char real[PATH_MAX];
    if (!realpath(raw, real)) return std::string(raw);
    return std::string(real);
}

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
