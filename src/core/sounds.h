#pragma once
// Звуки включения и выключения кликера (Настройки → Основные → Звук).
// Для «вкл» и «выкл» звук выбирается отдельно: из набора AutoQ или свой.
// Свои звуки (WAV) копируются в папку «Мои звуки» (<папка данных>/sounds) и есть
// во всех списках. У игры и у профиля звук можно выключить (карточка «Запуск» →
// кнопка звука: свой звук "none"), иначе — как в настройках.
// Звуки набора рисуются кодом (как и значки) — короткий WAV в памяти, без файлов
// в поставке. Громкость применяется и к своим. Windows — PlaySound; Linux — paplay /
// pw-play / aplay; macOS — afplay.
#include <QString>
#include <QStringList>
#include <QByteArray>

namespace Sounds {
    enum Kind { On, Off };

    QStringList ids();                  // набор: "ticks-up", "soft-down", "coin", … и "none"
    QString label(const QString& id);   // «Тик-тик вверх»
    QString defaultFor(Kind k);         // вкл — "ticks-up", выкл — "ticks-down"
    QString customPrefix();             // "file:" — файл где угодно (до 1.13): "file:/путь/к/звуку.wav"
    QString userPrefix();               // "user:" — из папки «Мои звуки»: "user:щелчок.wav"
    QString userDir();                  // <папка данных>/sounds
    QStringList userSounds();           // свои звуки по имени: "user:…"
    // добавить свой звук: копия файла — в «Мои звуки» (WAV до 5 МБ) -> "user:…"; "" — не вышло
    QString addUserSound(const QString& file, QString* error = nullptr);
    bool exists(const QString& id);     // есть такой: из набора или свой файл на месте

    bool enabled();                     // звук при вкл/выкл (по умолчанию да)
    void setEnabled(bool on);
    int volume();                       // 0–100 (для своих файлов — как в файле)
    void setVolume(int v);
    QString sound(Kind k);              // звук из настроек: id из набора, "user:…" или "file:…"
    // свой звук игры или профиля (own), если он есть, иначе — из настроек
    QString resolve(Kind k, const QString& own);
    void setSound(Kind k, const QString& id);
    bool inHoldMode();                  // и в режиме «Пока держу» (по умолчанию нет — часто щёлкает)
    void setInHoldMode(bool on);

    // кликер включился / выключился; hold — игра в режиме «Пока держу»;
    // own — свой звук игры/профиля. Несколько игр разом — один звук.
    void play(Kind k, bool hold = false, const QString& own = QString());
    void preview(const QString& id);    // «▶» в настройках — даже если звук выключен

    QByteArray wav(const QString& id, int volume);   // готовый WAV набора (для тестов); "none" — пусто
    // свой WAV с громкостью: PCM 8/16/24/32 бит или float -> 16 бит, не длиннее 10 с; не WAV — как есть
    QByteArray withVolume(const QByteArray& wav, int volume);
}
