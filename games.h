#pragma once
// База игр: в списке только игры, которые добавил пользователь.
// Встроенные (Genshin, Minecraft…) — шаблоны в окне «Добавить игру».
// У каждой игры свой режим действий, правила окна, текущие настройки,
// пресеты и профили. Всё хранится в QSettings (Linux: ~/.config/AutoQ,
// Windows: реестр HKCU\Software\AutoQ).
#include <QString>
#include <QStringList>
#include <QList>
#include <QByteArray>
#include <QSettings>
#include <memory>
#include "winmatch.h"
#include "keys.h"

namespace AppSettings {
    void setScope(const QString& org, const QString& app);  // для тестов
    std::unique_ptr<QSettings> open();
}

// Общие настройки программы
namespace Prefs {
    QList<int> captureChoices();          // 3, 5, 7, 10 секунд — быстрые кнопки
    int captureSeconds();                 // сколько ждать, пока переключишься в игру (1–120)
    void setCaptureSeconds(int sec);

    // Основные (Настройки → Основные)
    bool closeToTray();                   // крестик сворачивает в трей (иначе — выход)
    void setCloseToTray(bool on);
    bool notifications();                 // всплывающие уведомления у значка в трее
    void setNotifications(bool on);
    bool startMini();                     // при запуске открывать мини-окно
    void setStartMini(bool on);
    bool stopOnGameClose();               // окно игры закрылось — выключить кликер
    void setStopOnGameClose(bool on);
    bool mouseOnlyOverGame();             // мышью — только когда курсор над окном игры
    void setMouseOnlyOverGame(bool on);
    bool miniOnTop();                     // мини-окно поверх всех окон
    void setMiniOnTop(bool on);
    bool elevateAtStart();                // Windows: просить права администратора при запуске (окно UAC)
    void setElevateAtStart(bool on);

    // Первый запуск: выбор оформления и анимаций (один раз; «Пройти заново» — в настройках)
    bool onboardingDone();
    void setOnboardingDone(bool done);
}

enum class GameMode {
    Sequence = 0,   // действия по очереди: "ЛКМ, потом F" (Genshin)
    Combo    = 1,   // одно действие или комбинация одновременно: "F3+A" (Minecraft)
    Hold     = 2    // зажать и держать, пока кликер включён (копать, бежать)
};
GameMode gameModeFromInt(int v);
QString gameModeName(GameMode m);     // "По очереди" / "Вместе" / "Зажать"

struct GameDef {
    QString id;
    QString name;
    GameMode mode = GameMode::Sequence;
    QStringList processes;     // имена exe: "GenshinImpact.exe"
    QStringList titles;        // фрагменты заголовка: "Genshin Impact"
    int minIntervalMs = 5;
    bool builtin = false;
    QString icon = "gamepad";      // значок из своего набора AutoQ (AqIcons::gameGlyphs)
    QString color = "#FF5A66";     // цвет значка
    QByteArray image;              // своя картинка (PNG 128×128) — если задана, вместо значка
    WindowRule rule() const { return WindowRule{processes, titles}; }
};

// Одно действие: клик кнопкой мыши или нажатие клавиши
struct ActionStep {
    bool mouse = true;
    int button = 1;           // 1 ЛКМ, 2 СКМ, 3 ПКМ, 8 Mouse 4, 9 Mouse 5
    QString key = "f";        // имя из Keys
    int holdMs = 0;           // сколько держать нажатым (0 — обычный щелчок)
    int afterMs = 0;          // пауза после действия перед следующим (режим «По очереди»)
    bool operator==(const ActionStep& o) const;
    bool operator!=(const ActionStep& o) const { return !(*this == o); }
    QString itemName() const;                    // "mouse:1" или имя клавиши
    static ActionStep fromItem(const QString& name);
    QString label() const;                       // "ЛКМ", "F"
};

// Настройки действий одной игры (текущие и в профилях)
struct GameState {
    QList<ActionStep> actions{ActionStep{}};     // хотя бы одно
    int intervalMs = 100;
    bool randomize = true;
    int jitterMs = 30;
    bool lockWindow = true;
    bool frameSync = false;      // «В такт с монитором»: клики на сетке кадров
    int frameHz = 0;             // частота такта: 0 — как у монитора, иначе своя (FPS игры)
    bool multi() const { return actions.size() > 1; }
    bool usesMouse() const;
    bool operator==(const GameState& o) const;
};
constexpr int kMaxActions = 64;

struct GamePreset {
    QString name;
    GameState state;
    int mode = -1;     // режим, который включает пресет (-1 — не менять)
};

namespace Games {
    QList<GameDef> builtinDefaults();            // шаблоны: Genshin, Star Rail, Minecraft…
    QList<GameDef> list();                       // игры пользователя (может быть пусто)
    GameDef get(const QString& id, bool* ok = nullptr);
    bool contains(const QString& id);
    void save(const GameDef& g);
    bool addBuiltin(const QString& id);          // добавить игру из шаблона
    bool remove(const QString& id);
    bool resetToDefault(const QString& id);      // вернуть встроенной игре правила
    GameDef builtinDefault(const QString& id, bool* ok = nullptr);
    QString newCustomId();
    bool nameTaken(const QString& name, const QString& exceptId);

    QString currentId();
    void setCurrentId(const QString& id);

    QList<GamePreset> presets(const GameDef& g);
    GameState defaultState(const GameDef& g);
    GameState loadState(const GameDef& g);
    void saveState(const QString& gameId, const GameState& st);

    QStringList profiles(const QString& gameId);
    bool profileExists(const QString& gameId, const QString& name);
    // mode — режим, сохранённый вместе с профилем (-1 — профиль старый, без режима)
    GameState loadProfile(const QString& gameId, const QString& name, int* mode = nullptr);
    void saveProfile(const QString& gameId, const QString& name, const GameState& st, int mode = -1);
    void removeProfile(const QString& gameId, const QString& name);
    bool renameProfile(const QString& gameId, const QString& from, const QString& to);
    bool validProfileName(const QString& name, QString* why);
    // профиль, с которым сейчас работаем ("" — без профиля)
    QString activeProfile(const QString& gameId);
    void setActiveProfile(const QString& gameId, const QString& name);

    // Клавиши игры: старт/стоп, «активна» (клавиша работает, даже когда открыта другая
    // игра), режим клавиши, клавиша «активна / неактивна», клавиши профилей
    HotkeySpec defaultStartKey();                // общая клавиша по умолчанию (F6)
    void setDefaultStartKey(const HotkeySpec& sp);
    HotkeySpec startKey(const QString& gameId);
    void setStartKey(const QString& gameId, const HotkeySpec& sp);
    bool armed(const QString& gameId);
    void setArmed(const QString& gameId, bool on);
    int keyMode(const QString& gameId);          // 0 — нажал: вкл/выкл, 1 — работает, пока держишь
    void setKeyMode(const QString& gameId, int mode);
    HotkeySpec armKey(const QString& gameId);    // пустая — нет
    void setArmKey(const QString& gameId, const HotkeySpec& sp);
    HotkeySpec profileKey(const QString& gameId, const QString& profile);
    void setProfileKey(const QString& gameId, const QString& profile, const HotkeySpec& sp);

    QStringList splitList(const QString& text);  // "a, b; c" -> {a, b, c}
    QString joinList(const QStringList& list);

    void migrate();   // настройки старых версий -> база игр
}
