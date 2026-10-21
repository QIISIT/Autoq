#pragma once
// Звуки включения и выключения кликера (Настройки → Основные → Звук).
// Для «вкл» и «выкл» звук выбирается отдельно: из набора AutoQ или свой файл WAV.
// Звуки набора рисуются кодом (как и значки) — короткий WAV в памяти, без файлов
// в поставке. Windows — PlaySound; Linux — paplay / pw-play / aplay; macOS — afplay.
#include <QString>
#include <QStringList>
#include <QByteArray>

namespace Sounds {
    enum Kind { On, Off };

    QStringList ids();                  // набор: "ticks-up", "soft-down", "coin", … и "none"
    QString label(const QString& id);   // «Тик-тик вверх»
    QString defaultFor(Kind k);         // вкл — "ticks-up", выкл — "ticks-down"
    QString customPrefix();             // "file:" — свой файл: "file:/путь/к/звуку.wav"

    bool enabled();                     // звук при вкл/выкл (по умолчанию да)
    void setEnabled(bool on);
    int volume();                       // 0–100 (для своих файлов — как в файле)
    void setVolume(int v);
    QString sound(Kind k);              // id из набора или "file:…"
    void setSound(Kind k, const QString& id);
    bool inHoldMode();                  // и в режиме «Пока держу» (по умолчанию нет — часто щёлкает)
    void setInHoldMode(bool on);

    // кликер включился / выключился; hold — игра в режиме «Пока держу».
    // Несколько игр разом — один звук.
    void play(Kind k, bool hold = false);
    void preview(const QString& id);    // «▶» в настройках — даже если звук выключен

    QByteArray wav(const QString& id, int volume);   // готовый WAV набора (для тестов); "none" — пусто
}
