#include "core/sounds.h"
#include "core/i18n.h"
#include "core/appsettings.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QtEndian>
#include <QtMath>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {
constexpr int kRate = 44100;

enum Wave { Sine, Soft, Square, Bell };

// Один тон: частота (с переходом f0 → f1), начало и длина, громкость, форма волны
struct Tone { double f0, f1; int startMs, ms; double gain; Wave wave; };

struct SoundDef { const char* id; const char* name; QVector<Tone> tones; };

const QList<SoundDef>& library() {
    static const QList<SoundDef> lib = {
        {"ticks-up",   "Тик-тик вверх",   {{1250, 1250, 0, 35, 0.75, Sine}, {1750, 1750, 45, 45, 0.75, Sine}}},
        {"ticks-down", "Тик-тик вниз",    {{1750, 1750, 0, 35, 0.75, Sine}, {1150, 1150, 45, 45, 0.75, Sine}}},
        {"soft-up",    "Мягко вверх",     {{523.3, 523.3, 0, 110, 0.7, Soft}, {784.0, 784.0, 70, 170, 0.7, Soft}}},
        {"soft-down",  "Мягко вниз",      {{784.0, 784.0, 0, 110, 0.7, Soft}, {523.3, 523.3, 70, 170, 0.7, Soft}}},
        {"coin",       "Монетка (8-бит)", {{988, 988, 0, 60, 0.45, Square}, {1319, 1319, 60, 160, 0.45, Square}}},
        {"bit-down",   "8-бит вниз",      {{990, 990, 0, 55, 0.45, Square}, {660, 660, 60, 55, 0.45, Square}, {440, 440, 120, 90, 0.45, Square}}},
        {"whistle-up", "Свист вверх",     {{420, 980, 0, 130, 0.8, Sine}}},
        {"whistle-down", "Свист вниз",    {{980, 380, 0, 130, 0.8, Sine}}},
        {"bell",       "Колокольчик",     {{1568, 1568, 0, 420, 0.55, Bell}}},
        {"gong",       "Низкий гонг",     {{196, 190, 0, 520, 0.9, Bell}}},
        {"beep",       "Бип",             {{1000, 1000, 0, 90, 0.6, Soft}}},
        {"beep2",      "Двойной бип",     {{1000, 1000, 0, 70, 0.6, Soft}, {1000, 1000, 110, 70, 0.6, Soft}}},
        {"pop",        "Пузырёк",         {{300, 900, 0, 45, 0.9, Sine}}},
        {"none",       "Без звука",       {}},
    };
    return lib;
}

const SoundDef* find(const QString& id) {
    for (const SoundDef& d : library())
        if (id == QLatin1String(d.id)) return &d;
    return nullptr;
}

void addTone(QVector<double>& buf, const Tone& t) {
    const int start = t.startMs * kRate / 1000;
    const int n = t.ms * kRate / 1000;
    if (buf.size() < start + n) buf.resize(start + n);
    double phase = 0;
    for (int i = 0; i < n; ++i) {
        const double k = double(i) / n;
        const double f = t.f0 + (t.f1 - t.f0) * k;
        phase += 2 * M_PI * f / kRate;
        double v, decay;
        switch (t.wave) {
            case Square: v = std::sin(phase) >= 0 ? 0.55 : -0.55; decay = k < 0.85 ? 1.0 : (1 - k) / 0.15; break;
            case Soft:   v = std::sin(phase) * 0.8 + std::sin(phase * 2) * 0.12; decay = std::exp(-k * 3.0); break;
            case Bell:   v = std::sin(phase) * 0.7 + std::sin(phase * 2.76) * 0.2 + std::sin(phase * 5.4) * 0.08;
                         decay = std::exp(-k * 4.5); break;
            default:     v = std::sin(phase) * 0.85 + std::sin(phase * 3) * 0.08; decay = std::exp(-k * 5.0); break;
        }
        const double attack = qMin(1.0, i / (0.004 * kRate));
        buf[start + i] += v * attack * decay * t.gain;
    }
}

// WAV 16 бит из готовых отсчётов PCM
QByteArray wavFile(const QByteArray& pcm, int channels, int rate) {
    QByteArray w;
    auto u32 = [&w](quint32 v) { char b[4]; qToLittleEndian(v, b); w.append(b, 4); };
    auto u16 = [&w](quint16 v) { char b[2]; qToLittleEndian(v, b); w.append(b, 2); };
    w.append("RIFF"); u32(36 + pcm.size()); w.append("WAVE");
    w.append("fmt "); u32(16); u16(1); u16(channels); u32(rate); u32(rate * 2 * channels); u16(2 * channels); u16(16);
    w.append("data"); u32(pcm.size());
    w.append(pcm);
    return w;
}

QByteArray makeWav(const QVector<double>& samples, int volume) {
    const double gain = qBound(0, volume, 100) / 100.0;
    QByteArray pcm;
    pcm.resize(samples.size() * 2);
    for (int i = 0; i < samples.size(); ++i) {
        const double v = qBound(-1.0, samples[i] * gain, 1.0);
        qToLittleEndian<qint16>(static_cast<qint16>(qRound(v * 30000)), pcm.data() + 2 * i);
    }
    return wavFile(pcm, 1, kRate);
}

// Разбор WAV: формат (1 — PCM, 3 — float), каналы, частота, разрядность, данные
struct WavInfo { int format = 0, channels = 0, rate = 0, bits = 0; QByteArray data; };

bool parseWav(const QByteArray& b, WavInfo* w) {
    if (b.size() < 12 || !b.startsWith("RIFF") || b.mid(8, 4) != "WAVE") return false;
    const char* p = b.constData();
    qint64 pos = 12;
    bool fmt = false, data = false;
    while (pos + 8 <= b.size()) {
        const QByteArray id = b.mid(pos, 4);
        const qint64 len = qFromLittleEndian<quint32>(p + pos + 4);
        const qint64 body = pos + 8;
        if (id == "fmt " && len >= 16 && body + 16 <= b.size()) {
            w->format = qFromLittleEndian<quint16>(p + body);
            w->channels = qFromLittleEndian<quint16>(p + body + 2);
            w->rate = int(qFromLittleEndian<quint32>(p + body + 4));
            w->bits = qFromLittleEndian<quint16>(p + body + 14);
            // WAVE_FORMAT_EXTENSIBLE: настоящий формат — в начале подтипа
            if (w->format == 0xFFFE && len >= 26 && body + 26 <= b.size()) w->format = qFromLittleEndian<quint16>(p + body + 24);
            fmt = true;
        } else if (id == "data") {
            w->data = b.mid(body, qMin(len, b.size() - body));
            data = true;
        }
        pos = body + len + (len & 1);
    }
    const bool pcm = w->format == 1 && (w->bits == 8 || w->bits == 16 || w->bits == 24 || w->bits == 32);
    const bool flt = w->format == 3 && (w->bits == 32 || w->bits == 64);
    return fmt && data && (pcm || flt) && w->channels >= 1 && w->channels <= 8 && w->rate >= 1000 && w->rate <= 384000;
}

#ifndef _WIN32
QString player() {
    static const QString found = [] {
#ifdef __APPLE__
        const QStringList names = {"afplay"};
#else
        const QStringList names = {"paplay", "pw-play", "aplay"};
#endif
        for (const QString& n : names) {
            const QString p = QStandardPaths::findExecutable(n);
            if (!p.isEmpty()) return p;
        }
        return QString();
    }();
    return found;
}
#endif

// путь к своему файлу звука ("user:…" или "file:…"); иначе пусто
QString soundFile(const QString& id) {
    if (id.startsWith(Sounds::userPrefix())) return Sounds::userDir() + "/" + id.mid(Sounds::userPrefix().size());
    if (id.startsWith(Sounds::customPrefix())) return id.mid(Sounds::customPrefix().size());
    return QString();
}

void playId(const QString& id) {
    if (id.isEmpty() || id == "none") return;
    const int vol = Sounds::volume();
    const QString own = soundFile(id);
    QString key;
    if (!own.isEmpty()) {                                           // свой файл — с громкостью
        const QFileInfo fi(own);
        if (!fi.isFile() || fi.size() > 8 * 1024 * 1024) return;
        key = QString::fromLatin1(QCryptographicHash::hash(fi.absoluteFilePath().toUtf8(), QCryptographicHash::Md5).toHex()) +
              QString("-%1-%2").arg(fi.lastModified().toMSecsSinceEpoch()).arg(vol);
    } else {
        if (!find(id)) return;
        key = QString("%1-%2").arg(id).arg(vol);
    }
    // готовый звук не пересчитываем: файл читается только в первый раз
    auto load = [&]() -> QByteArray {
        if (own.isEmpty()) return Sounds::wav(id, vol);
        QFile f(own);
        return f.open(QIODevice::ReadOnly) ? Sounds::withVolume(f.readAll(), vol) : QByteArray();
    };
#ifdef _WIN32
    // PlaySound читает данные, пока играет, — держим их живыми
    static QHash<QString, QByteArray> keep;
    if (!keep.contains(key)) {
        const QByteArray data = load();
        if (data.isEmpty()) return;
        if (keep.size() >= 24) { PlaySoundW(nullptr, nullptr, 0); keep.clear(); }   // не копим: сначала остановить
        keep.insert(key, data);
    }
    PlaySoundW(reinterpret_cast<LPCWSTR>(keep[key].constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    const QString exe = player();
    if (exe.isEmpty()) return;
    const QString dir = QDir::temp().absoluteFilePath(QString("autoq-sounds-%1").arg(QCoreApplication::applicationPid()));
    if (static bool once = false; !once && QCoreApplication::instance()) {    // за собой — убрать
        once = true;
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         [dir]() { QDir(dir).removeRecursively(); });
    }
    QDir().mkpath(dir);
    const QString file = dir + QString("/%1.wav").arg(key);
    if (!QFile::exists(file)) {
        const QByteArray data = load();
        if (data.isEmpty()) return;
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(data);
    }
    QProcess::startDetached(exe, {file});
#endif
}

bool prefBool(const char* key, bool def) { return AppSettings::open()->value(key, def).toBool(); }
}

QStringList Sounds::ids() {
    QStringList out;
    for (const SoundDef& d : library()) out << QString::fromLatin1(d.id);
    return out;
}

QString Sounds::label(const QString& id) {
    if (id.startsWith(userPrefix())) return QFileInfo(id.mid(userPrefix().size())).completeBaseName();
    if (id.startsWith(customPrefix())) return QFileInfo(id.mid(customPrefix().size())).fileName();
    const SoundDef* d = find(id);
    return d ? T(d->name) : id;
}

QString Sounds::defaultFor(Kind k) { return k == On ? "ticks-up" : "ticks-down"; }
QString Sounds::customPrefix() { return QStringLiteral("file:"); }
QString Sounds::userPrefix() { return QStringLiteral("user:"); }
QString Sounds::userDir() { return AppSettings::dataDir() + "/sounds"; }

QStringList Sounds::userSounds() {
    QStringList out;
    const QStringList files = QDir(userDir()).entryList({"*.wav", "*.WAV", "*.Wav"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) out << userPrefix() + f;
    return out;
}

QString Sounds::addUserSound(const QString& file, QString* error) {
    const QFileInfo fi(file);
    QFile f(file);
    if (!fi.isFile() || !f.open(QIODevice::ReadOnly)) {
        if (error) *error = T("Файл не открывается.");
        return QString();
    }
    if (fi.size() > 5 * 1024 * 1024) {
        if (error) *error = T("Файл больше 5 МБ — для звука вкл/выкл нужен короткий.");
        return QString();
    }
    const QByteArray data = f.readAll();
    WavInfo w;
    if (!parseWav(data, &w)) {
        if (error) *error = T("Это не WAV или такой WAV не поддерживается. Нужен WAV (PCM или float) — например, из Audacity: Файл → Экспорт → WAV.");
        return QString();
    }
    const QString dir = userDir();
    if (!QDir().mkpath(dir)) {
        if (error) *error = T("Не удалось создать папку: %1").arg(dir);
        return QString();
    }
    // такой же уже есть — он и будет
    if (QFileInfo(dir + "/" + fi.fileName()).absoluteFilePath() == fi.absoluteFilePath()) return userPrefix() + fi.fileName();
    QString base = fi.completeBaseName().simplified();
    base.replace(QRegularExpression(R"([<>:"/\\|?*\x00-\x1F])"), "_");
    if (base.isEmpty()) base = "sound";
    base = base.left(60);
    QString name = base + ".wav";
    for (int n = 2; QFile::exists(dir + "/" + name); ++n) {
        QFile other(dir + "/" + name);
        if (other.open(QIODevice::ReadOnly) && other.readAll() == data) return userPrefix() + name;
        name = QString("%1 (%2).wav").arg(base).arg(n);
    }
    QFile out(dir + "/" + name);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
        if (error) *error = T("Не удалось записать файл: %1").arg(out.fileName());
        return QString();
    }
    return userPrefix() + name;
}

bool Sounds::exists(const QString& id) {
    if (const QString file = soundFile(id); !file.isEmpty()) return QFileInfo(file).isFile();
    return find(id) != nullptr;
}

bool Sounds::enabled() { return prefBool("sound/enabled", true); }
void Sounds::setEnabled(bool on) { AppSettings::open()->setValue("sound/enabled", on); }
int Sounds::volume() { return qBound(0, AppSettings::open()->value("sound/volume", 45).toInt(), 100); }
void Sounds::setVolume(int v) { AppSettings::open()->setValue("sound/volume", qBound(0, v, 100)); }

QString Sounds::sound(Kind k) {
    const QString v = AppSettings::open()->value(k == On ? "sound/on" : "sound/off").toString();
    return exists(v) ? v : defaultFor(k);
}

QString Sounds::resolve(Kind k, const QString& own) { return !own.isEmpty() && exists(own) ? own : sound(k); }

void Sounds::setSound(Kind k, const QString& id) {
    AppSettings::open()->setValue(k == On ? "sound/on" : "sound/off", id);
}

bool Sounds::inHoldMode() { return prefBool("sound/hold", false); }
void Sounds::setInHoldMode(bool on) { AppSettings::open()->setValue("sound/hold", on); }

QByteArray Sounds::wav(const QString& id, int volume) {
    const SoundDef* d = find(id);
    if (!d || d->tones.isEmpty()) return QByteArray();
    QVector<double> buf;
    for (const Tone& t : d->tones) addTone(buf, t);
    buf.resize(buf.size() + kRate / 50);       // 20 мс тишины в конце — без щелчка
    return makeWav(buf, volume);
}

void Sounds::play(Kind k, bool hold, const QString& own) {
    if (!enabled()) return;
    if (hold && !inHoldMode()) return;
    // одна клавиша на несколько игр — один звук
    static qint64 last = 0;
    static Kind lastKind = On;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (k == lastKind && now - last < 150) return;
    last = now;
    lastKind = k;
    playId(resolve(k, own));
}

void Sounds::preview(const QString& id) { playId(id); }

QByteArray Sounds::withVolume(const QByteArray& wav, int volume) {
    WavInfo w;
    if (!parseWav(wav, &w)) return wav;
    const double gain = qBound(0, volume, 100) / 100.0;
    const int bps = w.bits / 8;
    qint64 frames = w.data.size() / (bps * w.channels);
    frames = qMin<qint64>(frames, qint64(w.rate) * 10);         // не длиннее 10 с
    const qint64 n = frames * w.channels;
    QByteArray pcm;
    pcm.resize(n * 2);
    const uchar* src = reinterpret_cast<const uchar*>(w.data.constData());
    for (qint64 i = 0; i < n; ++i) {
        const uchar* q = src + i * bps;
        double v;
        if (w.format == 3) v = bps == 4 ? double(qFromLittleEndian<float>(q)) : qFromLittleEndian<double>(q);
        else if (bps == 1) v = (int(q[0]) - 128) / 128.0;
        else if (bps == 2) v = qFromLittleEndian<qint16>(q) / 32768.0;
        else if (bps == 3) v = (qint32(quint32(q[0]) << 8 | quint32(q[1]) << 16 | quint32(q[2]) << 24) >> 8) / 8388608.0;
        else v = qFromLittleEndian<qint32>(q) / 2147483648.0;
        if (!std::isfinite(v)) v = 0;
        v = qBound(-1.0, v * gain, 1.0);
        qToLittleEndian<qint16>(static_cast<qint16>(qRound(v * 32767)), pcm.data() + 2 * i);
    }
    return wavFile(pcm, w.channels, w.rate);
}
