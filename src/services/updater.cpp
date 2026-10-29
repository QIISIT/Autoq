#include "services/updater.h"
#include "services/updater_os.h"
#include "appinfo.h"
#include "services/autostart.h"
#include "core/appsettings.h"
#include "core/i18n.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>

using Updates::Package;

namespace {
constexpr qint64 kCheckEverySec = 6 * 3600;        // автопроверка — раз в 6 часов
constexpr int kTickMs = 30 * 60 * 1000;            // как часто смотреть, не пора ли
constexpr int kFirstCheckMs = 20 * 1000;           // первая — через 20 с после запуска

QString tool(const char* name) { return UpdaterOs::tool(QString::fromLatin1(name)); }

QString cacheDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    return base + "/AutoQ-update";
}

// своя сборка из build — только сообщаем о новой версии
Package detectPackage(const QString& exe) {
    if (exe.isEmpty() || QFile::exists(QFileInfo(exe).absolutePath() + "/CMakeCache.txt")) return Package::None;
    return UpdaterOs::detectPackage(exe);
}

// portable: работающий exe можно переименовать — старый становится AutoQ.exe.old
// (удаляется при следующем запуске), новый встаёт на его место
bool swapExe(const QString& exe, const QString& fresh) {
    const QString old = exe + ".old";
    QFile::remove(old);
    if (!QFile::rename(exe, old)) return false;
    if (QFile::rename(fresh, exe) || QFile::copy(fresh, exe)) return true;
    QFile::rename(old, exe);
    return false;
}

QString pendingValue(const char* key) {
    auto s = AppSettings::open();
    return s->value(QStringLiteral("update/") + QLatin1String(key)).toString();
}

void clearPending() {
    auto s = AppSettings::open();
    for (const char* k : {"pendingTag", "pendingVersion", "pendingPage", "pendingKind", "pendingFile", "pendingSha256"})
        s->remove(QStringLiteral("update/") + QLatin1String(k));
}
}

// ---------------------------------------------------------------------------

Updater& Updater::instance() {
    static Updater* u = nullptr;
    if (!u) {
        u = new Updater();
        u->setParent(qApp);               // закрывается вместе с программой (и останавливает сборку)
    }
    return *u;
}

Updater::Updater() {
    // путь к программе — до того, как обновление его заменит: Qt запоминает его при первом
    // вызове, а после замены файла (Linux) /proc/self/exe показывает «… (deleted)».
    // По этому пути AutoQ и перезапускается (main.cpp)
    exePath_ = QCoreApplication::applicationFilePath();
    package_ = detectPackage(exePath_);
    QFile::remove(exePath_ + ".old");     // остался от прошлой подмены exe (portable)

    auto s = AppSettings::open();
    lastCheck_ = QDateTime::fromString(s->value("update/lastCheck").toString(), Qt::ISODate);
    const QString lastRun = s->value("update/lastRunVersion").toString();
    if (lastRun != QLatin1String(AQ_VERSION)) s->setValue("update/lastRunVersion", AQ_VERSION);
    if (!lastRun.isEmpty() && Updates::compareVersions(lastRun, AQ_VERSION) < 0)
        startNotice_ = T("AutoQ обновлена до %1.").arg(AQ_VERSION);

    const QString tag = s->value("update/pendingTag").toString();
    const Package kind = Updates::packageFromKey(s->value("update/pendingKind").toString());
    const QString file = s->value("update/pendingFile").toString();
    if (tag.isEmpty() || !Updates::isNewer(tag, AQ_VERSION) || kind != package_ ||
        (kind != Package::Source && !QFile::exists(file))) {
        // ничего не ждёт установки (или уже стоит, или скачанное пропало) — убрать за собой
        s.reset();
        if (!tag.isEmpty()) clearPending();
        UpdaterOs::cleanup(exePath_);
        QDir(cacheDir()).removeRecursively();
        return;
    }
    // скачано раньше и ещё не поставлено
    release_.tag = tag;
    release_.version = s->value("update/pendingVersion").toString();
    release_.page = s->value("update/pendingPage").toString();
    pendingFile_ = file;
    state_ = State::Ready;
    // при запуске уже пробовали поставить — и всё ещё старая версия: второй раз сам не ставит
    triedAtStart_ = s->value("update/autoTried").toString() == tag;
    s.reset();
    if (triedAtStart_ && (package_ == Package::Setup || package_ == Package::Portable))
        startNotice_ = T("Не получилось поставить AutoQ %1 при запуске — Настройки → Основные → «Обновить сейчас».")
                           .arg(release_.version);
}

Updater::~Updater() {
    // сборка (cmake, компилятор) — остановить всю
    if (proc_ && proc_->state() != QProcess::NotRunning) UpdaterOs::killGroup(proc_);
}

bool Updater::autoEnabled() {
    auto s = AppSettings::open();
    return s->value("update/auto", true).toBool();
}

void Updater::setAutoEnabled(bool on) {
    {
        auto s = AppSettings::open();
        s->setValue("update/auto", on);
    }
    emit changed();
    if (!on) return;
    // новую версию уже нашли кнопкой «Проверить сейчас» — сразу качать
    if (state_ == State::Available && Updates::pickAsset(release_, package_)) download(false);
    else QTimer::singleShot(2000, this, &Updater::autoTick);
}

bool Updater::installPendingAtStart() {
    if (!UpdaterOs::installsAtStart() || !autoEnabled()) return false;
    auto s = AppSettings::open();
    const QString tag = s->value("update/pendingTag").toString();
    const Package kind = Updates::packageFromKey(s->value("update/pendingKind").toString());
    const QString file = s->value("update/pendingFile").toString();
    if (tag.isEmpty() || !Updates::isNewer(tag, AQ_VERSION) || s->value("update/autoTried").toString() == tag) return false;
    const QString exe = QCoreApplication::applicationFilePath();
    // только из своей папки рядом с программой и только тот файл, что скачали и проверили
    if (!UpdaterOs::trusted(file, exe)) return false;
    if (Updates::sha256OfFile(file) != s->value("update/pendingSha256").toString()) return false;
    if (kind == Package::Setup) {
        if (!Autostart::elevated()) return false;     // без прав было бы окно UAC прямо при запуске
        s->setValue("update/autoTried", tag);         // не вышло — второй раз сам не пробует
        s->sync();
        return UpdaterOs::launchInstaller(file);
    }
    if (kind == Package::Portable) {
        s->setValue("update/autoTried", tag);
        s->sync();
        // новая копия дождётся, пока эта выйдет (--restarted)
        return swapExe(exe, file) && QProcess::startDetached(exe, {QStringLiteral("--restarted")});
    }
    return false;
}

// ---------------------------------------------------------------------------

void Updater::start() {
    if (timer_) return;
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &Updater::autoTick);
    timer_->start(kTickMs);
    QTimer::singleShot(kFirstCheckMs, this, &Updater::autoTick);
}

void Updater::autoTick() {
    if (!autoEnabled() || busy()) return;
    if (lastCheck_.isValid() && lastCheck_.secsTo(QDateTime::currentDateTime()) < kCheckEverySec) return;
    check(false);
}

void Updater::checkNow() {
    if (!busy()) check(true);
}

bool Updater::busy() const {
    return state_ == State::Checking || state_ == State::Downloading || state_ == State::Building;
}

void Updater::check(bool manual) {
    const State before = state_;
    // проверить не вышло: скачанное раньше никуда не делось — его можно ставить и без сети
    auto offline = [this, before, manual](const QString& why) {
        if (before != State::Ready) { fail(why); return; }
        setState(State::Ready);
        if (manual) emit notice(why);
    };
    const QString curl = tool("curl");
    if (curl.isEmpty()) { offline(T("Нет программы curl — без неё AutoQ не может проверить обновления.")); return; }
    setState(State::Checking);
    run(curl, {"-fsSL", "--max-time", "20", "-H", "Accept: application/vnd.github+json",
               "-H", QStringLiteral("User-Agent: AutoQ/%1").arg(QLatin1String(AQ_VERSION)), Updates::latestApiUrl()},
        30000, [this, manual, offline](bool ok, const QByteArray& out) {
            const Updates::Release r = ok ? Updates::parseRelease(out) : Updates::Release();
            if (!r.valid()) { offline(T("Не удалось проверить обновления — нет связи с GitHub.")); return; }
            lastCheck_ = QDateTime::currentDateTime();
            {
                auto s = AppSettings::open();
                s->setValue("update/lastCheck", lastCheck_.toString(Qt::ISODate));   // в settings.ini — читаемо
            }
            gotRelease(r, manual);
        });
}

void Updater::gotRelease(const Updates::Release& r, bool manual) {
    if (!Updates::isNewer(r.tag, AQ_VERSION)) {
        release_ = Updates::Release();
        pendingFile_.clear();
        clearPending();
        setState(State::UpToDate);
        return;
    }
    release_ = r;
    // эта версия уже скачана (или собрана) — заново не качать
    if (pendingValue("pendingTag") == r.tag && (package_ == Package::Source || QFile::exists(pendingFile_))) {
        setState(State::Ready);
        return;
    }
    pendingFile_.clear();
    if (!Updates::pickAsset(r, package_)) {
        setState(State::Available);
        if (!manual) announce(T("Вышла AutoQ %1 — подробнее: Настройки → Основные → Обновления.").arg(r.version));
        return;
    }
    if (!autoEnabled()) { setState(State::Available); return; }   // ждём кнопку «Обновить»
    download(false);
}

void Updater::update() {
    if (busy()) return;
    if (state_ == State::Ready) { installNow(); return; }
    if (canUpdate()) download(true);
}

// ---------------------------------------------------------------------------

QString Updater::workDir() const {
    const QString own = UpdaterOs::workDir(exePath_);
    return own.isEmpty() ? cacheDir() : own;
}

void Updater::download(bool installAfter) {
    const Updates::Asset* a = Updates::pickAsset(release_, package_);
    if (!a) return;
    const QString curl = tool("curl");
    if (curl.isEmpty()) { fail(T("Нет программы curl — без неё AutoQ не может скачать обновление.")); return; }
    if (package_ == Package::Source && (tool("cmake").isEmpty() || tool("tar").isEmpty())) {
        fail(T("Для сборки нужны cmake и компилятор (как при установке). Новая версия — на странице релиза."));
        return;
    }
    const QString dir = workDir();
    QDir(dir).removeRecursively();        // прошлые скачанные файлы больше не нужны
    if (!QDir().mkpath(dir)) { fail(T("Не удалось создать папку для обновления: %1").arg(QDir::toNativeSeparators(dir))); return; }
    const Updates::Asset asset = *a;
    const QString part = dir + "/" + asset.name + ".part";
    setState(State::Downloading);
    run(curl, {"-fsSL", "--retry", "2", "--max-time", "900", "-o", QDir::toNativeSeparators(part), asset.url}, 960000,
        [this, part, asset, installAfter](bool ok, const QByteArray&) {
            if (!ok) { QFile::remove(part); fail(T("Не удалось скачать обновление — нет связи с GitHub.")); return; }
            downloaded(part, asset, installAfter);
        });
}

void Updater::downloaded(const QString& part, const Updates::Asset& asset, bool installAfter) {
    if (asset.size > 0 && QFileInfo(part).size() != asset.size) {
        QFile::remove(part);
        fail(T("Файл обновления скачался не целиком — попробуй ещё раз."));
        return;
    }
    if (!asset.sha256.isEmpty()) { verified(part, asset, installAfter); return; }
    // у старых релизов GitHub не пишет SHA-256 — тогда сумма из SHA256SUMS.txt релиза
    const Updates::Asset* sums = nullptr;
    for (const Updates::Asset& x : release_.assets)
        if (x.name.compare(QLatin1String("SHA256SUMS.txt"), Qt::CaseInsensitive) == 0) sums = &x;
    if (!sums) {
        QFile::remove(part);
        fail(T("У релиза нет контрольной суммы — такой файл AutoQ ставить не будет. Новая версия — на странице релиза."));
        return;
    }
    run(tool("curl"), {"-fsSL", "--max-time", "30", sums->url}, 40000, [this, part, asset, installAfter](bool ok, const QByteArray& out) {
        Updates::Asset withSum = asset;
        withSum.sha256 = ok ? Updates::sumFor(out, asset.name) : QString();
        if (withSum.sha256.isEmpty()) {
            QFile::remove(part);
            fail(T("У релиза нет контрольной суммы — такой файл AutoQ ставить не будет. Новая версия — на странице релиза."));
            return;
        }
        verified(part, withSum, installAfter);
    });
}

// SHA-256 сходится с тем, что пишет GitHub, — файл можно ставить
void Updater::verified(const QString& part, const Updates::Asset& asset, bool installAfter) {
    if (Updates::sha256OfFile(part) != asset.sha256) {
        QFile::remove(part);
        fail(T("Файл обновления не прошёл проверку SHA-256 — ставить его нельзя."));
        return;
    }
    const QString dir = QFileInfo(part).absolutePath();
    switch (package_) {
        case Package::Setup:
        case Package::Dmg: {
            const QString file = dir + "/" + asset.name;
            QFile::remove(file);
            if (!QFile::rename(part, file)) { fail(T("Не удалось сохранить файл обновления.")); return; }
            ready(file, installAfter);
            return;
        }
        case Package::Portable: {
            // из Portable.zip нужен только AutoQ.exe (tar в Windows 10/11 открывает zip)
            const QString out = dir + "/unpacked";
            QDir().mkpath(out);
            run(tool("tar"), {"-xf", QDir::toNativeSeparators(part), "-C", QDir::toNativeSeparators(out), "AutoQ.exe"}, 120000,
                [this, part, out, installAfter](bool ok, const QByteArray&) {
                    const QString file = QFileInfo(part).absolutePath() + "/AutoQ-" + release_.tag + ".exe";
                    QFile::remove(file);
                    const bool moved = ok && QFile::rename(out + "/AutoQ.exe", file);
                    QFile::remove(part);
                    QDir(out).removeRecursively();
                    if (!moved) { fail(T("Не удалось распаковать AutoQ.exe из архива обновления.")); return; }
                    ready(file, installAfter);
                });
            return;
        }
        case Package::Source:
            build(part, installAfter);
            return;
        case Package::None:
            return;
    }
}

// Linux: та же сборка, что при установке (cmake + Ninja, если есть), с низким приоритетом и
// на половине ядер — чтобы не мешать игре. Готовая программа сразу заменяет старую
void Updater::build(const QString& tarball, bool installAfter) {
    const QString dir = QFileInfo(tarball).absolutePath();
    const QString src = dir + "/src";
    const QString log = dir + "/build.log";
    QDir().mkpath(src);
    const QString script = QStringLiteral(
        "set -e\n"
        "cd \"$1\"\n"
        "tar -xzf \"$2\"\n"
        "top=$(dirname \"$(find . -maxdepth 2 -name CMakeLists.txt | head -n 1)\")\n"
        "gen=\"\"\n"
        "if command -v ninja >/dev/null 2>&1; then gen=\"-G Ninja\"; fi\n"
        "cmake -S \"$top\" -B \"$1/out\" $gen -DCMAKE_BUILD_TYPE=Release\n"
        "cmake --build \"$1/out\" --parallel \"$3\"\n"
        "test -x \"$1/out/AutoQ\"\n");
    const QString jobs = QString::number(qMax(1, QThread::idealThreadCount() / 2));
    QStringList args = {"-c", script, "sh", src, tarball, jobs};
    QString program = tool("sh");
    if (const QString nice = tool("nice"); !nice.isEmpty()) {
        args = QStringList{"-n", "19", program} + args;
        program = nice;
    }
    setState(State::Building);
    run(program, args, 40 * 60 * 1000, [this, src, log, tarball, installAfter](bool ok, const QByteArray&) {
        const QString built = src + "/out/AutoQ";
        if (!ok || !QFile::exists(built)) {
            fail(T("Не получилось собрать AutoQ %1 — подробности в %2").arg(release_.version, log));
            return;
        }
        if (!UpdaterOs::replaceProgram(built, exePath_)) {
            fail(T("Не удалось заменить программу: нет доступа к %1").arg(exePath_));
            return;
        }
        QDir(src).removeRecursively();
        QFile::remove(tarball);
        QFile::remove(log);
        ready(QString(), installAfter);
    }, log);
}

void Updater::ready(const QString& file, bool installAfter) {
    pendingFile_ = file;
    triedAtStart_ = false;
    {
        auto s = AppSettings::open();
        s->setValue("update/pendingTag", release_.tag);
        s->setValue("update/pendingVersion", release_.version);
        s->setValue("update/pendingPage", release_.page);
        s->setValue("update/pendingKind", Updates::packageKey(package_));
        s->setValue("update/pendingFile", file);
        s->setValue("update/pendingSha256", file.isEmpty() ? QString() : Updates::sha256OfFile(file));
    }
    setState(State::Ready);
    if (installAfter && package_ != Package::Source) { installNow(); return; }
    if (package_ == Package::Source)
        announce(T("Собрана AutoQ %1 — заработает после перезапуска AutoQ.").arg(release_.version));
    else if (startsAtLaunch())
        announce(T("Скачана AutoQ %1 — поставится при следующем запуске AutoQ.").arg(release_.version));
    else
        announce(T("Вышла AutoQ %1 — подробнее: Настройки → Основные → Обновления.").arg(release_.version));
}

void Updater::installNow() {
    if (state_ != State::Ready) return;
    const QString want = pendingValue("pendingSha256");
    if (package_ != Package::Source && (want.isEmpty() || Updates::sha256OfFile(pendingFile_) != want)) {
        clearPending();
        fail(T("Скачанный файл изменился или пропал — проверь обновления ещё раз."));
        return;
    }
    switch (package_) {
        case Package::Setup:
            if (UpdaterOs::launchInstaller(pendingFile_)) {
                auto s = AppSettings::open();
                s->setValue("update/autoTried", release_.tag);
                s.reset();
                emit quitRequested();
            } else {
                emit notice(T("Обновление отменено: Windows не дала прав администратора (окно UAC)."));
            }
            return;
        case Package::Portable:
            if (swapExe(exePath_, pendingFile_)) {
                clearPending();
                emit restartRequested();
            } else {
                fail(T("Не удалось заменить AutoQ.exe — нет доступа к папке программы."));
            }
            return;
        case Package::Source:
            emit restartRequested();
            return;
        case Package::Dmg:
            QDesktopServices::openUrl(QUrl::fromLocalFile(pendingFile_));
            emit notice(T("Образ AutoQ %1 открыт: перетащи AutoQ в «Программы» с заменой и запусти заново.").arg(release_.version));
            return;
        case Package::None:
            return;
    }
}

// ---------------------------------------------------------------------------

void Updater::fail(const QString& text) {
    error_ = text;
    setState(State::Failed);
}

void Updater::setState(State s) {
    state_ = s;
    emit changed();
}

void Updater::announce(const QString& text) {
    auto s = AppSettings::open();
    const QString key = QStringLiteral("%1:%2").arg(release_.tag).arg(int(state_));
    if (s->value("update/announced").toString() == key) return;
    s->setValue("update/announced", key);
    s.reset();
    emit notice(text);
}

bool Updater::startsAtLaunch() const {
    if (!UpdaterOs::installsAtStart() || !autoEnabled() || triedAtStart_) return false;
    if (package_ == Package::Portable) return true;
    return package_ == Package::Setup && Autostart::elevated() && UpdaterOs::trusted(pendingFile_, exePath_);
}

QString Updater::releasePage() const {
    return release_.page.isEmpty() ? Updates::releasesPageUrl() : release_.page;
}

bool Updater::canUpdate() const {
    if (state_ == State::Ready) return true;
    // вышла новая версия (или прошлая попытка не удалась) — можно скачать
    return (state_ == State::Available || state_ == State::Failed) && release_.valid() &&
           Updates::pickAsset(release_, package_);
}

QString Updater::updateButtonText() const {
    if (state_ == State::Ready) {
        if (package_ == Package::Source) return T("Перезапустить");
        if (package_ == Package::Dmg) return T("Открыть образ");
        return T("Обновить сейчас");
    }
    return T("Обновить до %1").arg(release_.version);
}

QString Updater::statusText() const {
    const QString v = release_.version;
    switch (state_) {
        case State::Checking: return T("Проверяю обновления…");
        case State::UpToDate: return T("У тебя последняя версия — AutoQ %1.").arg(AQ_VERSION);
        case State::Available:
            return Updates::pickAsset(release_, package_) ? T("Вышла AutoQ %1.").arg(v)
                                                          : T("Вышла AutoQ %1. Эту копию AutoQ не обновить отсюда — скачай новую версию на странице релиза.").arg(v);
        case State::Downloading: return T("Скачиваю AutoQ %1…").arg(v);
        case State::Building: return T("Собираю AutoQ %1 из исходников — пару минут, в фоне…").arg(v);
        case State::Ready:
            if (package_ == Package::Source) return T("AutoQ %1 собрана — заработает после перезапуска.").arg(v);
            if (package_ == Package::Dmg) return T("AutoQ %1 скачана — открой образ и перетащи AutoQ в «Программы».").arg(v);
            return startsAtLaunch() ? T("AutoQ %1 скачана — поставится при следующем запуске.").arg(v)
                                    : T("AutoQ %1 скачана — нажми «Обновить сейчас».").arg(v);
        case State::Failed: return error_;
        case State::Idle: break;
    }
    QString t = T("Сейчас стоит AutoQ %1.").arg(AQ_VERSION) + ' ' +
                (autoEnabled() ? T("Новые версии AutoQ проверяет сам, раз в 6 часов.")
                               : T("Автообновление выключено — проверить можно кнопкой."));
    if (lastCheck_.isValid())
        t += ' ' + T("Последняя проверка: %1.").arg(I18n::locale().toString(lastCheck_, QLocale::ShortFormat));
    return t;
}

// ---------------------------------------------------------------------------

void Updater::run(const QString& program, const QStringList& args, int timeoutMs,
                  const std::function<void(bool, const QByteArray&)>& done, const QString& logFile) {
    auto* p = new QProcess(this);
    proc_ = p;
    if (!logFile.isEmpty()) {
        p->setProcessChannelMode(QProcess::MergedChannels);
        p->setStandardOutputFile(logFile);
    }
    UpdaterOs::ownProcessGroup(p);
    auto* guard = new QTimer(p);
    guard->setSingleShot(true);
    connect(guard, &QTimer::timeout, p, [p]() {
        UpdaterOs::killGroup(p);
        p->kill();
    });
    const bool capture = logFile.isEmpty();          // вывод в файл — читать нечего
    auto finish = [p, done, capture](bool ok) {
        if (p->property("aqDone").toBool()) return;
        p->setProperty("aqDone", true);
        const QByteArray out = ok && capture ? p->readAllStandardOutput() : QByteArray();
        p->deleteLater();
        done(ok, out);
    };
    connect(p, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus st) {
        finish(st == QProcess::NormalExit && code == 0);
    });
    connect(p, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) finish(false);
    });
    if (program.isEmpty()) { QTimer::singleShot(0, this, [finish]() { finish(false); }); return; }
    p->start(program, args);
    guard->start(timeoutMs);
}
