// Папка по умолчанию хранит location.ini — путь к своей папке данных, если её выбрали
#include "core/appsettings.h"
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QCryptographicHash>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <climits>
    #include <cstdlib>
#else
    #include <unistd.h>
    #include <climits>
#endif

namespace {
QString g_testDir;                // тесты: своя временная папка вместо настоящей
QString g_cachedDir;              // вычисленная папка данных (сбрасывается при смене)
bool g_resolved = false;

QString exeDir() {
#ifdef _WIN32
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n == 0 || n >= 32768) return QString();
    return QFileInfo(QString::fromWCharArray(buf, int(n))).absolutePath();
#elif defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) return QString();
    char real[PATH_MAX];
    const char* p = realpath(raw, real) ? real : raw;
    return QFileInfo(QString::fromUtf8(p)).absolutePath();
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return QString();
    return QFileInfo(QString::fromUtf8(buf, int(n))).absolutePath();
#endif
}

bool writable(const QString& dir) {
    if (!QDir().mkpath(dir)) return false;
    QTemporaryFile probe(dir + "/.autoq-write-test-XXXXXX");
    return probe.open();
}

QString locationFile() { return AppSettings::defaultDataDir() + "/location.ini"; }

// Настройки версий до 1.13: реестр Windows, ~/.config/AutoQ/config.conf, plist на macOS
std::unique_ptr<QSettings> legacyStore() {
#ifdef _WIN32
    return std::make_unique<QSettings>("HKEY_CURRENT_USER\\Software\\AutoQ\\config", QSettings::NativeFormat);
#else
    return std::make_unique<QSettings>(QSettings::NativeFormat, QSettings::UserScope, "AutoQ", "config");
#endif
}

// Первый запуск 1.13: перенести старые настройки в settings.ini (один раз).
// Старое не удаляется — с ним можно вернуться на прошлую версию.
void importLegacy(const QString& file) {
    if (QFile::exists(file)) return;
    auto old = legacyStore();
    const QStringList keys = old->allKeys();
    if (keys.isEmpty()) return;
    QSettings to(file, QSettings::IniFormat);
    for (const QString& k : keys) to.setValue(k, old->value(k));
    to.setValue("storage/importedFrom", old->fileName());
    to.sync();
}

// Пояснение в папке данных — что здесь лежит (пишется, если его нет)
void writeReadme(const QString& dir) {
    QFile f(dir + "/README.txt");
    if (f.exists() || !f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    f.write(u8"Папка данных AutoQ\n"
            u8"==================\n"
            u8"settings.ini          — все настройки и игры\n"
            u8"profiles/<игра>/*.ini — профили, по файлу на каждый. Файл профиля можно\n"
            u8"                        отправить другу: AutoQ → Профиль → ⋯ → Импорт.\n"
            u8"Папку можно сменить: Настройки → Основные → Данные. Чтобы перенести AutoQ на\n"
            u8"другой компьютер — скопируй эту папку целиком.\n"
            u8"\n"
            u8"AutoQ data folder\n"
            u8"=================\n"
            u8"settings.ini           — all settings and games\n"
            u8"profiles/<game>/*.ini  — profiles, one file each. A profile file can be shared:\n"
            u8"                         AutoQ → Profile & start → ⋯ → Import.\n"
            u8"Change the folder in Settings → General → Data. To move AutoQ to another PC,\n"
            u8"copy this whole folder.\n");
}
}

void AppSettings::setScope(const QString& org, const QString& app) {
    g_testDir = QDir::temp().filePath(org + "/" + app);
    QDir().mkpath(g_testDir);
}

void AppSettings::dropScope(const QString& org, const QString& app) {
    const QString dir = QDir::temp().filePath(org + "/" + app);
    if (g_testDir == dir) g_testDir.clear();
    QDir(dir).removeRecursively();
}

QString AppSettings::defaultDataDir() {
#ifdef _WIN32
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty()) base = QDir::homePath() + "/AppData/Roaming";
    return QDir::cleanPath(base + "/AutoQ");
#elif defined(__APPLE__)
    return QDir::homePath() + "/Library/Application Support/AutoQ";
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base)) base = QDir::homePath() + "/.config";
    return QDir::cleanPath(base + "/AutoQ");
#endif
}

QString AppSettings::portableDir() {
    const QString d = exeDir();
    return d.isEmpty() ? QString() : d + "/AutoQ-data";
}

bool AppSettings::portable() {
    if (!g_testDir.isEmpty()) return false;
    const QString p = portableDir();
    return !p.isEmpty() && QFileInfo(p).isDir() && writable(p);
}

QString AppSettings::dataDir() {
    if (!g_testDir.isEmpty()) return g_testDir;
    if (g_resolved) return g_cachedDir;
    QString dir;
    if (portable()) {
        dir = portableDir();
    } else {
        const QString custom = QSettings(locationFile(), QSettings::IniFormat).value("dataDir").toString();
        dir = (!custom.isEmpty() && writable(custom)) ? QDir::cleanPath(custom) : defaultDataDir();
    }
    QDir().mkpath(dir);
    importLegacy(dir + "/settings.ini");
    writeReadme(dir);
    g_cachedDir = dir;
    g_resolved = true;
    return dir;
}

bool AppSettings::customDataDir() {
    if (!g_testDir.isEmpty() || portable()) return false;
    return QDir::cleanPath(dataDir()) != QDir::cleanPath(defaultDataDir());
}

QString AppSettings::settingsFile() { return dataDir() + "/settings.ini"; }
QString AppSettings::profilesDir() { return dataDir() + "/profiles"; }

namespace {
QByteArray g_ownHash;               // settings.ini после нашей последней записи (или принятый)
bool g_outside = false;             // заметили чужую правку, ещё не приняли

QByteArray fileHash(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1);
}

// settings.ini, который запоминает свои записи: файл поменялся, пока объект жил, — это мы
class TrackedSettings : public QSettings {
public:
    explicit TrackedSettings(const QString& file)
        : QSettings(file, QSettings::IniFormat), file_(file), before_(fileHash(file)) {
        // файл уже не тот, что мы записали, — правили снаружи (запись ниже это не скроет)
        if (!g_ownHash.isEmpty() && before_ != g_ownHash) g_outside = true;
    }
    ~TrackedSettings() override {
        sync();
        const QByteArray after = fileHash(file_);
        if (after != before_) g_ownHash = after;
    }
private:
    QString file_;
    QByteArray before_;
};
}

std::unique_ptr<QSettings> AppSettings::open() {
    return std::make_unique<TrackedSettings>(settingsFile());
}

bool AppSettings::changedOutside() {
    const QByteArray now = fileHash(settingsFile());
    return g_outside || (!now.isEmpty() && now != g_ownHash);
}

void AppSettings::markSeen() {
    g_ownHash = fileHash(settingsFile());
    g_outside = false;
}

static bool copyTree(const QString& from, const QString& to, QString* err) {
    QDir src(from);
    if (!QDir().mkpath(to)) { if (err) *err = to; return false; }
    for (const QFileInfo& fi : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString dst = to + "/" + fi.fileName();
        if (fi.isDir()) {
            if (!copyTree(fi.absoluteFilePath(), dst, err)) return false;
        } else {
            if (fi.fileName() == "location.ini") continue;     // указатель живёт только в папке по умолчанию
            QFile::remove(dst);
            if (!QFile::copy(fi.absoluteFilePath(), dst)) { if (err) *err = dst; return false; }
        }
    }
    return true;
}

bool AppSettings::hasData(const QString& dir) { return QFile::exists(dir + "/settings.ini"); }

bool AppSettings::setDataDir(const QString& dirIn, bool copyCurrent, QString* error) {
    if (!g_testDir.isEmpty() || portable()) { if (error) *error = QStringLiteral("portable"); return false; }
    const QString dir = QDir::cleanPath(dirIn.isEmpty() ? defaultDataDir() : dirIn);
    const QString cur = QDir::cleanPath(dataDir());
    if (dir == cur) return true;
    if (!writable(dir)) { if (error) *error = dir; return false; }
    if (copyCurrent) {
        // всё, что сейчас записано, — на новое место (настройки, профили)
        QSettings(settingsFile(), QSettings::IniFormat).sync();
        if (!copyTree(cur, dir, error)) return false;
    }
    QSettings loc(locationFile(), QSettings::IniFormat);
    if (dir == QDir::cleanPath(defaultDataDir())) loc.remove("dataDir");
    else loc.setValue("dataDir", dir);
    loc.sync();
    if (loc.status() != QSettings::NoError) { if (error) *error = locationFile(); return false; }
    g_resolved = false;
    return QDir::cleanPath(dataDir()) == dir;
}

QString AppSettings::safeFileName(const QString& name) {
    QString out;
    for (const QChar c : name) out += (c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c)) ? QChar('_') : c;
    while (out.endsWith('.') || out.endsWith(' ')) out.chop(1);
    if (out.isEmpty()) out = QStringLiteral("_");
    static const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])$"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(out).hasMatch()) out += '_';
    return out;
}

// ---------------------------------------------------------------------------
// Общие настройки программы
// ---------------------------------------------------------------------------
int Prefs::captureSeconds() {
    auto s = AppSettings::open();
    const int v = s->value("captureSeconds", 3).toInt();
    return (v >= 1 && v <= 120) ? v : 3;
}

void Prefs::setCaptureSeconds(int sec) {
    if (sec < 1 || sec > 120) return;
    auto s = AppSettings::open();
    s->setValue("captureSeconds", sec);
}

static bool prefBool(const char* key, bool def) {
    auto s = AppSettings::open();
    return s->value(key, def).toBool();
}
static void setPrefBool(const char* key, bool on) {
    auto s = AppSettings::open();
    s->setValue(key, on);
    s->sync();
}
bool Prefs::closeToTray() { return prefBool("closeToTray", true); }
void Prefs::setCloseToTray(bool on) { setPrefBool("closeToTray", on); }
bool Prefs::notifications() { return prefBool("notifications", true); }
void Prefs::setNotifications(bool on) { setPrefBool("notifications", on); }
bool Prefs::startMini() { return prefBool("startMini", false); }
void Prefs::setStartMini(bool on) { setPrefBool("startMini", on); }
bool Prefs::stopOnGameClose() { return prefBool("stopOnGameClose", true); }
void Prefs::setStopOnGameClose(bool on) { setPrefBool("stopOnGameClose", on); }
bool Prefs::mouseOnlyOverGame() { return prefBool("mouseOnlyOverGame", true); }
void Prefs::setMouseOnlyOverGame(bool on) { setPrefBool("mouseOnlyOverGame", on); }
bool Prefs::hotkeyAnyMods() { return prefBool("hotkeyAnyMods", true); }
void Prefs::setHotkeyAnyMods(bool on) { setPrefBool("hotkeyAnyMods", on); }
bool Prefs::miniOnTop() { return prefBool("miniOnTop", true); }
void Prefs::setMiniOnTop(bool on) { setPrefBool("miniOnTop", on); }
bool Prefs::elevateAtStart() { return prefBool("elevateAtStart", true); }
void Prefs::setElevateAtStart(bool on) { setPrefBool("elevateAtStart", on); }
bool Prefs::onboardingDone() { return prefBool("onboardingDone", false); }
void Prefs::setOnboardingDone(bool done) { setPrefBool("onboardingDone", done); }
