#include "sounds.h"
#include "i18n.h"
#include "games.h"
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

QByteArray makeWav(const QVector<double>& samples, int volume) {
    const double gain = qBound(0, volume, 100) / 100.0;
    QByteArray pcm;
    pcm.resize(samples.size() * 2);
    for (int i = 0; i < samples.size(); ++i) {
        const double v = qBound(-1.0, samples[i] * gain, 1.0);
        qToLittleEndian<qint16>(static_cast<qint16>(qRound(v * 30000)), pcm.data() + 2 * i);
    }
    QByteArray w;
    auto u32 = [&w](quint32 v) { char b[4]; qToLittleEndian(v, b); w.append(b, 4); };
    auto u16 = [&w](quint16 v) { char b[2]; qToLittleEndian(v, b); w.append(b, 2); };
    w.append("RIFF"); u32(36 + pcm.size()); w.append("WAVE");
    w.append("fmt "); u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
    w.append("data"); u32(pcm.size());
    w.append(pcm);
    return w;
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

void playId(const QString& id) {
    if (id.isEmpty() || id == "none") return;
    const QString prefix = Sounds::customPrefix();
    if (id.startsWith(prefix)) {                     // свой файл WAV
        const QString file = id.mid(prefix.size());
        if (!QFileInfo::exists(file)) return;
#ifdef _WIN32
        PlaySoundW(reinterpret_cast<LPCWSTR>(file.utf16()), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#else
        if (!player().isEmpty()) QProcess::startDetached(player(), {file});
#endif
        return;
    }
    if (!find(id)) return;
    const int vol = Sounds::volume();
    const QByteArray data = Sounds::wav(id, vol);
#ifdef _WIN32
    // PlaySound читает данные, пока играет, — держим их живыми
    static QHash<QString, QByteArray> keep;
    const QString key = QString("%1-%2").arg(id).arg(vol);
    if (!keep.contains(key)) keep.insert(key, data);
    PlaySoundW(reinterpret_cast<LPCWSTR>(keep[key].constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    const QString exe = player();
    if (exe.isEmpty()) return;
    const QString dir = QDir::temp().absoluteFilePath(QString("autoq-sounds-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(dir);
    const QString file = dir + QString("/%1-%2.wav").arg(id).arg(vol);
    if (!QFile::exists(file)) {
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
    if (id.startsWith(customPrefix())) return QFileInfo(id.mid(customPrefix().size())).fileName();
    const SoundDef* d = find(id);
    return d ? T(d->name) : id;
}

QString Sounds::defaultFor(Kind k) { return k == On ? "ticks-up" : "ticks-down"; }
QString Sounds::customPrefix() { return QStringLiteral("file:"); }

bool Sounds::enabled() { return prefBool("sound/enabled", true); }
void Sounds::setEnabled(bool on) { AppSettings::open()->setValue("sound/enabled", on); }
int Sounds::volume() { return qBound(0, AppSettings::open()->value("sound/volume", 45).toInt(), 100); }
void Sounds::setVolume(int v) { AppSettings::open()->setValue("sound/volume", qBound(0, v, 100)); }

QString Sounds::sound(Kind k) {
    const QString v = AppSettings::open()->value(k == On ? "sound/on" : "sound/off").toString();
    if (v.startsWith(customPrefix()) || find(v)) return v;
    return defaultFor(k);
}

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

void Sounds::play(Kind k, bool hold) {
    if (!enabled()) return;
    if (hold && !inHoldMode()) return;
    // одна клавиша на несколько игр — один звук
    static qint64 last = 0;
    static Kind lastKind = On;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (k == lastKind && now - last < 150) return;
    last = now;
    lastKind = k;
    playId(sound(k));
}

void Sounds::preview(const QString& id) { playId(id); }
