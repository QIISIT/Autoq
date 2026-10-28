#pragma once
// База игр: в списке только игры, которые добавил пользователь.
// Встроенные (Genshin, Minecraft…) — шаблоны в окне «Добавить игру».
// У каждой игры свой режим действий, правила окна, текущие настройки,
// пресеты и профили. Всё хранится файлами в папке данных AutoQ (см. AppSettings):
// settings.ini — настройки и игры, profiles/<игра>/<профиль>.ini — профили.
#include <QString>
#include <QStringList>
#include <QList>
#include <QByteArray>
#include <QSettings>
#include <memory>
#include "winmatch.h"
#include "keys.h"

// Папка данных AutoQ — обычная видимая папка с файлами (без реестра):
//   settings.ini — все настройки и игры, profiles/<игра>/<профиль>.ini — профили.
// По умолчанию: Windows %APPDATA%\AutoQ, macOS ~/Library/Application Support/AutoQ,
// Linux ~/.config/AutoQ. Можно выбрать свою (Настройки → Основные → Данные).
// Папка AutoQ-data рядом с программой — портативный режим: всё хранится там.
// Работает и до создания QApplication.
namespace AppSettings {
    void setScope(const QString& org, const QString& app);  // для тестов: своя временная папка
    void dropScope(const QString& org, const QString& app); // для тестов: удалить её
    std::unique_ptr<QSettings> open();                      // settings.ini
    // Горячая перезагрузка: settings.ini поправили снаружи (руками, синхронизацией) —
    // а не сама программа. Свои записи запоминаются (их отпечаток), чужие — нет.
    bool changedOutside();
    void markSeen();                      // текущий файл принят — дальше сравнивать с ним
    QString dataDir();
    QString defaultDataDir();
    QString portableDir();                // <папка программы>/AutoQ-data
    bool portable();                      // портативный режим
    bool customDataDir();                 // выбрана своя папка (не по умолчанию)
    QString settingsFile();
    QString profilesDir();
    bool hasData(const QString& dir);     // в папке уже есть настройки AutoQ
    // Сменить папку данных ("" — по умолчанию). copyCurrent — перенести туда текущие
    // настройки и профили; иначе взять то, что там уже лежит. error — путь, где не вышло
    bool setDataDir(const QString& dir, bool copyCurrent, QString* error = nullptr);
    // имя файла из имени профиля или стиля: запрещённые в именах файлов знаки — «_»
    QString safeFileName(const QString& name);
}

// Общие настройки программы
namespace Prefs {
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
    Hold     = 2,   // зажать и держать, пока кликер включён (копать, бежать)
    Steps    = 3    // по шагам: нажать и отпустить — отдельные шаги (кикфлип: ЛКМ↓, ПКМ↓↑, ЛКМ↑)
};
constexpr int kLastMode = 3;          // режимы 0…3 (для проверки чисел из файлов)
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

// Одно действие: клик кнопкой мыши или нажатие клавиши.
// В режиме «По шагам» — нажать (Down), отпустить (Up) или нажать и сразу отпустить (Click)
struct ActionStep {
    enum Press { Click = 0, Down = 1, Up = 2 };
    bool mouse = true;
    int button = 1;           // 1 ЛКМ, 2 СКМ, 3 ПКМ, 8 Mouse 4, 9 Mouse 5
    QString key = "f";        // имя из Keys
    int holdMs = 0;           // сколько держать нажатым (0 — обычный щелчок)
    int afterMs = 0;          // пауза после действия перед следующим («По очереди», «По шагам»)
    int press = Click;        // «По шагам»: Down — нажать, Up — отпустить, Click — нажать и отпустить
    QString group;            // не пусто — вызов группы (её id): кнопка и «держать» не нужны
    int repeat = 1;           // вызов группы: сколько раз подряд (1…kMaxRepeat)
    bool isGroup() const { return !group.isEmpty(); }
    bool sameInput(const ActionStep& o) const;   // та же кнопка / клавиша
    bool operator==(const ActionStep& o) const;
    bool operator!=(const ActionStep& o) const { return !(*this == o); }
    QString itemName() const;                    // "mouse:1" или имя клавиши
    static ActionStep fromItem(const QString& name);
    QString label() const;                       // "ЛКМ", "F"
    QString pressMark() const;                   // шаг: "↓" нажать, "↑" отпустить, "↕" нажать и отпустить
};

// Группа действий — как функция: несколько действий или шагов под своим именем. В ряд
// ставится вызов группы (ActionStep::group); поправил группу — поменялось во всех вызовах.
// Группы общие для всех игр и профилей (до 1.18 — у каждой игры свои). В файл профиля
// пишется копия групп, которые он вызывает: профиль с другого ПК приносит их с собой.
// Группа в группе — нельзя.
struct ActionGroup {
    QString id;               // 8 знаков 0-9a-f: случайный, у копий одной группы — один
    QString name;
    bool steps = false;       // шаги («По шагам»: нажать / отпустить) или действия («По очереди»)
    QList<ActionStep> items;
    // пауза между повторами (×N у вызова), мс. Пауза после вызова — только после последнего
    // повтора. -1 — группа из 1.17.0 (между повторами — пауза после вызова, как тогда)
    int betweenMs = 0;
    bool operator==(const ActionGroup& o) const {
        return id == o.id && name == o.name && steps == o.steps && items == o.items && betweenMs == o.betweenMs;
    }
    bool operator!=(const ActionGroup& o) const { return !(*this == o); }
};
constexpr int kMaxGroups = 200;       // на все игры вместе
constexpr int kMaxRepeat = 99;
constexpr int kMaxRun = 4096;         // круг с развёрнутыми группами — не длиннее (шагов)

namespace Groups {
    bool validId(const QString& id);
    QString newId(const QList<ActionGroup>& taken);
    const ActionGroup* find(const QList<ActionGroup>& groups, const QString& id);
    // Развернуть вызовы групп — то, что жмёт кликер. steps — ряд «По шагам» (группа из
    // действий становится шагами), once — «Вместе» / «Зажать» (без повторов). Между повторами —
    // пауза группы (betweenMs), после последнего повтора — пауза после вызова. owner[i] — номер
    // карточки ряда, из которой шаг i (подсветить, что жмётся сейчас)
    QList<ActionStep> expand(const QList<ActionStep>& chain, const QList<ActionGroup>& groups,
                             bool steps, bool once = false, QList<int>* owner = nullptr);
    // вызов -> обычные карточки («Разгруппировать», группу удалили)
    QList<ActionStep> inlineCall(const ActionStep& call, const ActionGroup& g, bool steps);
    // группу удалили: все её вызовы в ряду — её действиями (не больше max)
    QList<ActionStep> inlineAll(const QList<ActionStep>& chain, const ActionGroup& g, bool steps, int max);
    // пауза между повторами вызова call (у групп из 1.17.0 — пауза после вызова)
    int betweenMs(const ActionGroup& g, const ActionStep& call);
    QStringList usedIds(const QList<ActionStep>& chain);
    int uses(const QList<ActionStep>& chain, const QString& id);
    QString preview(const ActionGroup& g, int max = 4);     // «ЛКМ ↓ ПКМ ↕ ЛКМ ↑ +2»
    // группа так, как её жмут в ряду этого вида: в «По шагам» — шагами (действие —
    // «нажать» и «отпустить»), в ряду действий — действиями (см. Steps::toActions)
    ActionGroup inRow(const ActionGroup& g, bool steps);
    bool validName(const QString& name, QString* why = nullptr);
    QString freeName(const QList<ActionGroup>& groups);    // «Группа 1», «Группа 2»…
    // группа для «Создать группу…»: новое имя и id, одно действие или клавиша «нажать ↓, отпустить ↑»
    ActionGroup blank(const QList<ActionGroup>& groups, bool steps);
}

// Настройки действий одной игры (текущие и в профилях)
struct GameState {
    QList<ActionStep> actions{ActionStep{}};     // хотя бы одно
    QList<ActionStep> steps;     // «По шагам» (пусто — ещё не настраивали: сделаются из действий)
    int startDelayMs = 0;        // старт через N мс после нажатия «Старт» / клавиши
    int intervalMs = 100;        // пауза после цикла (после последнего действия) — «повтор через»
    bool randomize = true;
    int jitterMs = 30;
    bool lockWindow = true;
    bool frameSync = false;      // «В такт с монитором»: клики на сетке кадров
    int frameHz = 0;             // частота такта: 0 — как у монитора, иначе своя (FPS игры)
    QString soundOn, soundOff;   // свой звук включения / выключения ("" — как в настройках)
    // группы игры — копия: хранятся у игры (Games::groups), не в состоянии и не в профиле;
    // в сравнении не участвуют (поправил группу — профиль не стал «несохранённым»)
    QList<ActionGroup> groups;
    bool multi() const { return actions.size() > 1 || (actions.size() == 1 && actions[0].isGroup()); }
    // ряд в редакторе: шаги («По шагам») или действия, с вызовами групп
    QList<ActionStep> chain(GameMode m) const;
    // что жмёт кликер в этом режиме: ряд, группы развёрнуты
    QList<ActionStep> runList(GameMode m) const;
    QList<int> runOwners(GameMode m) const;      // шаг кликера -> номер карточки ряда
    // кнопки и клавиши, которые нажимаются (для подписей и счётчиков): действия,
    // а в «По шагам» — каждая клавиша, которую шаги нажимают, один раз
    QList<ActionStep> pressList(GameMode m) const;
    bool usesMouse(GameMode m) const;
    // меньше какого «повтор через» в этом режиме нельзя (см. repeatMinMs)
    int repeatMin(GameMode m, int gameMinMs) const;
    // круг целиком — не быстрее (см. minCycleMs)
    int minCycle(GameMode m, int gameMinMs) const;
    bool operator==(const GameState& o) const;
};
// «Повтор через» — не меньше минимума игры. Но в «По шагам» и «По очереди» из 2+ действий
// это пауза между клавишами — там от 0
int repeatMinMs(GameMode m, int count, int gameMinMs);
// Круг целиком (от начала до начала следующего) — не быстрее: там, где «повтор через» от 0, —
// 1 мс (только чтобы кликер не крутился вхолостую), иначе — минимум игры. Следит кликер
// (ClickerConfig::minCycleMs)
int minCycleMs(GameMode m, int count, int gameMinMs);
constexpr int kMaxActions = 64;
constexpr int kMaxSteps = 128;
constexpr int kMaxStartDelayMs = 60000;

namespace Steps {
    // действия -> шаги: каждое действие — «нажать», пауза «держать», «отпустить», пауза после
    QList<ActionStep> fromActions(const QList<ActionStep>& actions);
    // каждая клавиша, которую шаги нажимают, один раз (по порядку первого нажатия)
    QList<ActionStep> pressedInputs(const QList<ActionStep>& steps);
    // номер клавиши шага в pressedInputs (-1 — шаг «отпустить» или клавиша не нажимается)
    int inputIndex(const QList<ActionStep>& inputs, const ActionStep& step);
    // шаги, у которых «отпустить» без «нажать» раньше в цикле, и клавиши, которые
    // остаются нажатыми в конце (их кликер отпустит сам)
    QList<int> strayUps(const QList<ActionStep>& steps);
    QList<ActionStep> leftDown(const QList<ActionStep>& steps);
    // шаги -> действия: «нажать ↓» и сразу «отпустить ↑» той же клавиши — одно действие
    // («держать» — пауза между ними), «клик ↕» — клик. exact = false — что-то не собралось:
    // «нажать» без пары стало кликом, лишнее «отпустить» пропало (его пауза — у предыдущего)
    QList<ActionStep> toActions(const QList<ActionStep>& steps, bool* exact = nullptr);
}

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
    // убрать игру из списка; файлы профилей остаются (withProfiles — удалить и их):
    // встроенная игра, добавленная снова, находит их сама, своя — через adoptProfiles
    bool remove(const QString& id, bool withProfiles = false);
    // своя игра с тем же названием, что у убранной раньше: её профили — к этой игре.
    // Вернёт, сколько профилей вернулось
    int adoptProfiles(const QString& gameId, const QString& name);
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
    // Группы действий — общие для всех игр и их профилей
    QList<ActionGroup> groups();
    void saveGroups(const QList<ActionGroup>& groups);
    // группу удалили: в сохранённых рядах остальных игр её вызовы — её действиями
    // (как в открытой игре). Файлы профилей не трогаются: в них своя копия группы
    void inlineGroupEverywhere(const ActionGroup& g, const QString& exceptGameId);
    QString profileFile(const QString& gameId, const QString& name);   // путь к файлу профиля ("" — нет)
    QString profilesFolder(const QString& gameId);                     // папка профилей игры
    void reloadProfiles();     // файлы в папках профилей поменяли снаружи — читать списки заново
    // Профиль в файл — поделиться или сохранить копию (без клавиши профиля)
    bool exportProfile(const QString& gameId, const QString& name, const QString& file, QString* error = nullptr);
    // Профиль из файла — в профили игры; вернёт имя (занято — «Имя (2)»), "" — не вышло.
    // fromGame — название игры, для которой профиль делали (если другая)
    QString importProfile(const QString& gameId, const QString& file, QString* error = nullptr,
                          QString* fromGame = nullptr);
    bool validProfileName(const QString& name, QString* why);
    // профиль, с которым сейчас работаем ("" — без профиля)
    QString activeProfile(const QString& gameId);
    void setActiveProfile(const QString& gameId, const QString& name);

    // Клавиши игры: старт/стоп, «активна» (клавиша работает, даже когда открыта другая
    // игра), режим клавиши, клавиша «активна / неактивна», клавиши профилей
    HotkeySpec defaultStartKey();                // общая клавиша по умолчанию (F6)
    HotkeySpec startKey(const QString& gameId);
    void setStartKey(const QString& gameId, const HotkeySpec& sp);   // другая клавиша — block снова выкл.
    bool startKeyBlock(const QString& gameId);   // «не пропускать нажатие в игру» (см. HotkeySpec::block)
    void setStartKeyBlock(const QString& gameId, bool on);
    bool armed(const QString& gameId);
    void setArmed(const QString& gameId, bool on);
    int keyMode(const QString& gameId);          // 0 — нажал: вкл/выкл, 1 — работает, пока держишь
    void setKeyMode(const QString& gameId, int mode);
    HotkeySpec armKey(const QString& gameId);    // пустая — нет
    void setArmKey(const QString& gameId, const HotkeySpec& sp);
    // «Показывать в оверлее КПС» — у каждого профиля свой (по умолчанию да);
    // profile "" — игра без профиля
    bool profileInOverlay(const QString& gameId, const QString& profile);
    void setProfileInOverlay(const QString& gameId, const QString& profile, bool on);
    HotkeySpec profileKey(const QString& gameId, const QString& profile);
    void setProfileKey(const QString& gameId, const QString& profile, const HotkeySpec& sp);

    QStringList splitList(const QString& text);  // "a, b; c" -> {a, b, c}
    QString joinList(const QStringList& list);

    void migrate();   // настройки старых версий -> база игр
}
