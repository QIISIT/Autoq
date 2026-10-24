#include "games.h"
#include "i18n.h"
#include "keys.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <utility>

namespace {
QString profileDir(const QString& gameId);        // файлы профилей — ниже, у профилей
void dropProfileCache(const QString& gameId);
}

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
bool Prefs::miniOnTop() { return prefBool("miniOnTop", true); }
void Prefs::setMiniOnTop(bool on) { setPrefBool("miniOnTop", on); }
bool Prefs::elevateAtStart() { return prefBool("elevateAtStart", true); }
void Prefs::setElevateAtStart(bool on) { setPrefBool("elevateAtStart", on); }
bool Prefs::onboardingDone() { return prefBool("onboardingDone", false); }
void Prefs::setOnboardingDone(bool done) { setPrefBool("onboardingDone", done); }

GameMode gameModeFromInt(int v) {
    switch (v) {
        case 1: return GameMode::Combo;
        case 2: return GameMode::Hold;
        case 3: return GameMode::Steps;
        default: return GameMode::Sequence;
    }
}

QString gameModeName(GameMode m) {
    switch (m) {
        case GameMode::Combo: return T("Вместе");
        case GameMode::Hold:  return T("Зажать");
        case GameMode::Steps: return T("По шагам");
        default:              return T("По очереди");
    }
}

bool ActionStep::sameInput(const ActionStep& o) const {
    if (isGroup() || o.isGroup()) return group == o.group;      // вызов группы — не кнопка
    return mouse == o.mouse && (mouse ? button == o.button : key.compare(o.key, Qt::CaseInsensitive) == 0);
}

bool ActionStep::operator==(const ActionStep& o) const {
    return sameInput(o) && holdMs == o.holdMs && afterMs == o.afterMs && press == o.press &&
           (!isGroup() || repeat == o.repeat);
}

QString ActionStep::itemName() const { return mouse ? MouseButtons::itemName(button) : key; }

ActionStep ActionStep::fromItem(const QString& name) {
    ActionStep a;
    if (const int b = MouseButtons::fromItemName(name)) { a.mouse = true; a.button = b; }
    else { a.mouse = false; a.key = name; }
    return a;
}

QString ActionStep::label() const {
    if (isGroup()) return QStringLiteral("▣");
    return mouse ? MouseButtons::shortLabel(button) : Keys::label(key);
}

QString ActionStep::pressMark() const {
    return press == Up ? QStringLiteral("↑") : press == Down ? QStringLiteral("↓") : QStringLiteral("↕");
}

QList<ActionStep> GameState::chain(GameMode m) const {
    if (m != GameMode::Steps) return actions;
    return steps.isEmpty() ? Steps::fromActions(actions) : steps;
}

QList<ActionStep> GameState::runList(GameMode m) const {
    return Groups::expand(chain(m), groups, m == GameMode::Steps, m == GameMode::Combo || m == GameMode::Hold);
}

QList<int> GameState::runOwners(GameMode m) const {
    QList<int> owner;
    Groups::expand(chain(m), groups, m == GameMode::Steps, m == GameMode::Combo || m == GameMode::Hold, &owner);
    return owner;
}

QList<ActionStep> GameState::pressList(GameMode m) const {
    return m == GameMode::Steps ? Steps::pressedInputs(runList(m)) : runList(m);
}

int repeatMinMs(GameMode m, int count, int gameMinMs) {
    return (m == GameMode::Steps || (m == GameMode::Sequence && count > 1)) ? 0 : gameMinMs;
}

int GameState::repeatMin(GameMode m, int gameMinMs) const {
    return repeatMinMs(m, int(runList(m).size()), gameMinMs);
}

int minCycleMs(GameMode m, int count, int gameMinMs) { return repeatMinMs(m, count, gameMinMs) == 0 ? 1 : gameMinMs; }

int GameState::minCycle(GameMode m, int gameMinMs) const {
    return minCycleMs(m, int(runList(m).size()), gameMinMs);
}

bool GameState::usesMouse(GameMode m) const {
    for (const ActionStep& a : runList(m)) if (a.mouse) return true;
    return false;
}

bool GameState::operator==(const GameState& o) const {
    return actions == o.actions && steps == o.steps && startDelayMs == o.startDelayMs && intervalMs == o.intervalMs &&
           randomize == o.randomize && jitterMs == o.jitterMs && lockWindow == o.lockWindow &&
           frameSync == o.frameSync && frameHz == o.frameHz && soundOn == o.soundOn && soundOff == o.soundOff;
}

// ---------------------------------------------------------------------------
// Группы действий
// ---------------------------------------------------------------------------
bool Groups::validId(const QString& id) {
    static const QRegularExpression re("^[0-9a-f]{8}$");
    return re.match(id).hasMatch();
}

QString Groups::newId(const QList<ActionGroup>& taken) {
    for (;;) {
        const QString id = QString("%1").arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'));
        if (!find(taken, id)) return id;
    }
}

const ActionGroup* Groups::find(const QList<ActionGroup>& groups, const QString& id) {
    for (const ActionGroup& g : groups)
        if (g.id == id) return &g;
    return nullptr;
}

// содержимое группы так, как его жмут в ряду этого вида
static QList<ActionStep> groupItems(const ActionGroup& g, bool steps) {
    QList<ActionStep> items;
    for (const ActionStep& a : g.items)
        if (!a.isGroup()) items << a;                  // группа в группе — нельзя
    if (steps && !g.steps) return Steps::fromActions(items);
    if (!steps && g.steps) {
        // шаги в ряду действий (бывает, только если файл правили руками): «нажать» — клик
        QList<ActionStep> out;
        for (ActionStep a : items) {
            if (a.press == ActionStep::Up) continue;
            a.press = ActionStep::Click;
            out << a;
        }
        return out;
    }
    return items;
}

QList<ActionStep> Groups::expand(const QList<ActionStep>& chain, const QList<ActionGroup>& groups,
                                 bool steps, bool once, QList<int>* owner) {
    QList<ActionStep> out;
    if (owner) owner->clear();
    for (int i = 0; i < chain.size(); ++i) {
        const ActionStep& a = chain[i];
        if (!a.isGroup()) {
            if (out.size() >= kMaxRun) break;
            out << a;
            if (owner) *owner << i;
            continue;
        }
        const ActionGroup* g = find(groups, a.group);
        if (!g) continue;                               // группы нет — вызов ничего не жмёт
        const QList<ActionStep> items = groupItems(*g, steps);
        if (items.isEmpty()) continue;
        const int reps = once ? 1 : qBound(1, a.repeat, kMaxRepeat);
        for (int r = 0; r < reps; ++r)
            for (int j = 0; j < items.size(); ++j) {
                if (out.size() >= kMaxRun) return out;
                ActionStep x = items[j];
                if (j + 1 == items.size()) x.afterMs = a.afterMs;   // пауза после вызова
                out << x;
                if (owner) *owner << i;
            }
    }
    return out;
}

QList<ActionStep> Groups::inlineCall(const ActionStep& call, const ActionGroup& g, bool steps) {
    ActionStep c = call;
    c.repeat = qBound(1, call.repeat, kMaxRepeat);
    return expand({c}, {g}, steps);
}

QList<ActionStep> Groups::inlineAll(const QList<ActionStep>& chain, const ActionGroup& g, bool steps, int max) {
    QList<ActionStep> out;
    for (const ActionStep& a : chain) {
        const QList<ActionStep> part = a.isGroup() && a.group == g.id ? inlineCall(a, g, steps) : QList<ActionStep>{a};
        for (const ActionStep& x : part)
            if (out.size() < max) out << x;
    }
    return out;
}

QStringList Groups::usedIds(const QList<ActionStep>& chain) {
    QStringList ids;
    for (const ActionStep& a : chain)
        if (a.isGroup() && !ids.contains(a.group)) ids << a.group;
    return ids;
}

int Groups::uses(const QList<ActionStep>& chain, const QString& id) {
    int n = 0;
    for (const ActionStep& a : chain) n += a.group == id;
    return n;
}

QString Groups::preview(const ActionGroup& g, int max) {
    QStringList parts;
    for (int i = 0; i < g.items.size(); ++i) {
        if (parts.size() == max) { parts << QString("+%1").arg(g.items.size() - i); break; }
        const ActionStep& a = g.items[i];
        parts << (g.steps ? a.label() + ' ' + a.pressMark() : a.label());
    }
    return parts.join(QStringLiteral(" · "));
}

bool Groups::validName(const QString& name, QString* why) {
    const QString n = name.trimmed();
    QString err;
    if (n.isEmpty()) err = T("Имя не может быть пустым.");
    else if (n.length() > 40) err = T("Слишком длинное имя (максимум 40 символов).");
    if (why) *why = err;
    return err.isEmpty();
}

QString Groups::freeName(const QList<ActionGroup>& groups) {
    for (int i = 1;; ++i) {
        const QString n = T("Группа %1").arg(i);
        bool taken = false;
        for (const ActionGroup& g : groups) taken = taken || g.name.compare(n, Qt::CaseInsensitive) == 0;
        if (!taken) return n;
    }
}

// ---------------------------------------------------------------------------
// «По шагам»
// ---------------------------------------------------------------------------
QList<ActionStep> Steps::fromActions(const QList<ActionStep>& actions) {
    QList<ActionStep> out;
    for (const ActionStep& a : actions) {
        if (a.isGroup()) {                  // вызов группы остаётся вызовом (развернёт Groups::expand)
            if (out.size() + 1 > kMaxSteps) break;
            ActionStep c = a;
            c.press = ActionStep::Click;
            out << c;
            continue;
        }
        if (out.size() + 2 > kMaxSteps) break;
        ActionStep d = a, u = a;
        d.press = ActionStep::Down;
        d.holdMs = 0;
        d.afterMs = a.holdMs;
        u.press = ActionStep::Up;
        u.holdMs = 0;
        u.afterMs = a.afterMs;
        out << d << u;
    }
    return out;
}

QList<ActionStep> Steps::pressedInputs(const QList<ActionStep>& steps) {
    QList<ActionStep> out;
    for (const ActionStep& s : steps) {
        if (s.press == ActionStep::Up) continue;
        bool seen = false;
        for (const ActionStep& o : out) seen = seen || o.sameInput(s);
        if (seen) continue;
        ActionStep a = s;
        a.press = ActionStep::Click;
        a.holdMs = a.afterMs = 0;
        out << a;
    }
    return out;
}

int Steps::inputIndex(const QList<ActionStep>& inputs, const ActionStep& step) {
    if (step.press == ActionStep::Up) return -1;
    for (int i = 0; i < inputs.size(); ++i)
        if (inputs[i].sameInput(step)) return i;
    return -1;
}

QList<int> Steps::strayUps(const QList<ActionStep>& steps) {
    QList<int> out;
    QList<ActionStep> down;
    for (int i = 0; i < steps.size(); ++i) {
        const ActionStep& s = steps[i];
        int k = -1;
        for (int j = 0; j < down.size(); ++j) if (down[j].sameInput(s)) { k = j; break; }
        if (s.press == ActionStep::Up) {
            if (k < 0) out << i;
            else down.removeAt(k);
        } else if (s.press == ActionStep::Click) {
            if (k >= 0) down.removeAt(k);       // нажать и отпустить: после шага не зажата
        } else if (k < 0) {
            down << s;
        }
    }
    return out;
}

QList<ActionStep> Steps::leftDown(const QList<ActionStep>& steps) {
    QList<ActionStep> down;
    for (const ActionStep& s : steps) {
        int k = -1;
        for (int j = 0; j < down.size(); ++j) if (down[j].sameInput(s)) { k = j; break; }
        if (s.press == ActionStep::Up || s.press == ActionStep::Click) { if (k >= 0) down.removeAt(k); }
        else if (k < 0) down << s;
    }
    return down;
}

// ---------------------------------------------------------------------------
// Встроенная база
// ---------------------------------------------------------------------------
QList<GameDef> Games::builtinDefaults() {
    auto mk = [](const char* id, const char* name, GameMode mode, QStringList procs,
                 QStringList titles, int minMs, const char* icon, const char* color) {
        GameDef g; g.id = id; g.name = QString::fromUtf8(name); g.mode = mode;
        g.processes = std::move(procs); g.titles = std::move(titles); g.minIntervalMs = minMs; g.builtin = true;
        g.icon = icon; g.color = color;
        return g;
    };
    // Значки — свой набор AutoQ (жанровые пиктограммы), не логотипы игр
    return {
        mk("genshin", "Genshin Impact", GameMode::Sequence,
           {"GenshinImpact.exe", "YuanShen.exe"}, {"Genshin Impact", "原神"}, 20, "compass", "#F2C66D"),
        mk("starrail", "Honkai: Star Rail", GameMode::Sequence,
           {"StarRail.exe"}, {"Honkai: Star Rail", "崩坏：星穹铁道"}, 20, "train", "#B9A8FF"),
        mk("zzz", "Zenless Zone Zero", GameMode::Sequence,
           {"ZenlessZoneZero.exe"}, {"ZenlessZoneZero", "Zenless Zone Zero", "绝区零"}, 20, "city", "#FF9F43"),
        mk("wuwa", "Wuthering Waves", GameMode::Sequence,
           {"Client-Win64-Shipping.exe"}, {"Wuthering Waves", "鸣潮"}, 20, "waves", "#6FD3F2"),
        mk("minecraft", "Minecraft (Java)", GameMode::Combo,
           {"javaw.exe", "java.exe", "java"}, {"Minecraft"}, 5, "cube", "#6EDC8C"),
        mk("minecraft_bedrock", "Minecraft (Bedrock)", GameMode::Combo,
           {"Minecraft.Windows.exe"}, {"Minecraft"}, 5, "layers", "#6EDC8C"),
        mk("roblox", "Roblox", GameMode::Sequence,
           {"RobloxPlayerBeta.exe", "RobloxPlayer"}, {"Roblox"}, 5, "users", "#E6E6EA"),
    };
}

GameDef Games::builtinDefault(const QString& id, bool* ok) {
    for (const GameDef& d : builtinDefaults())
        if (d.id == id) { if (ok) *ok = true; return d; }
    if (ok) *ok = false;
    return GameDef{};
}

static ActionStep mouseStep(int button) { ActionStep a; a.mouse = true; a.button = button; return a; }
static ActionStep keyStep(const QString& key) { ActionStep a; a.mouse = false; a.key = key; return a; }

static GameState mouseSpam(int button, int interval, bool rand) {
    GameState s; s.actions = {mouseStep(button)}; s.intervalMs = interval;
    s.randomize = rand; s.jitterMs = rand ? 30 : 0; return s;
}
static GameState keySpam(const QString& key, int interval, bool rand) {
    GameState s; s.actions = {keyStep(key)}; s.intervalMs = interval;
    s.randomize = rand; s.jitterMs = rand ? 30 : 0; return s;
}
static GameState clickThenKey(const QString& key, int interval) {
    GameState s = mouseSpam(1, interval, true);
    s.actions << keyStep(key); return s;
}

static GameState holdState(bool mouse, int button, const QString& key) {
    GameState s; s.actions = {mouse ? mouseStep(button) : keyStep(key)};
    s.intervalMs = 100; s.randomize = false; s.jitterMs = 0;
    return s;
}

QList<GamePreset> Games::presets(const GameDef& g) {
    // каждый пресет включает свой режим: «Скип диалогов» — по очереди, F3+A — вместе и т.д.
    const int S = static_cast<int>(GameMode::Sequence), C = static_cast<int>(GameMode::Combo),
              H = static_cast<int>(GameMode::Hold);
    if (g.id == "genshin" || g.id == "wuwa")
        return { {T("Скип диалогов (ЛКМ → F)"), clickThenKey("f", 100), S},
                 {T("Спам ЛКМ"), mouseSpam(1, 100, true), S},
                 {T("Спам F (подобрать / взаимодействовать)"), keySpam("f", 100, true), S},
                 {T("Спам пробел"), keySpam("space", 120, true), S},
                 {T("Зажать W (идти вперёд)"), holdState(false, 1, "w"), H} };
    if (g.id == "starrail" || g.id == "zzz")
        return { {T("Скип диалогов (ЛКМ → пробел)"), clickThenKey("space", 100), S},
                 {T("Спам ЛКМ"), mouseSpam(1, 100, true), S},
                 {T("Спам пробел"), keySpam("space", 120, true), S},
                 {T("Спам F"), keySpam("f", 100, true), S} };
    if (g.id == "minecraft" || g.id == "minecraft_bedrock") {
        GameState f3a = keySpam("f3", 1000, false);   // перезагрузка чанков — не чаще раза в секунду
        f3a.actions << keyStep("a");
        GameState walkMine = holdState(false, 1, "w");
        walkMine.actions << mouseStep(1);
        return { {T("Спам ЛКМ (ломать / бить)"), mouseSpam(1, 5, false), C},
                 {T("Спам ПКМ (есть / ставить)"), mouseSpam(3, 5, false), C},
                 {T("F3+A (перезагрузка чанков)"), f3a, C},
                 {T("Зажать ЛКМ (копать)"), holdState(true, 1, "f"), H},
                 {T("Зажать W + ЛКМ (копать туннель)"), walkMine, H} };
    }
    if (g.id == "roblox")
        return { {T("Спам ЛКМ"), mouseSpam(1, 50, false), S},
                 {T("Спам E"), keySpam("e", 100, true), S},
                 {T("Зажать ЛКМ"), holdState(true, 1, "f"), H} };
    return { {T("Спам ЛКМ"), mouseSpam(1, 100, true), S},
             {T("Спам ПКМ"), mouseSpam(3, 100, true), S},
             {T("Спам пробел"), keySpam("space", 100, true), S},
             {T("Спам F"), keySpam("f", 100, true), S},
             {T("Зажать ЛКМ"), holdState(true, 1, "f"), H} };
}

GameState Games::defaultState(const GameDef& g) {
    GameState s = presets(g).value(0).state;
    s.intervalMs = qMax(s.intervalMs, g.minIntervalMs);
    s.groups = groups(g.id);
    return s;
}

// ---------------------------------------------------------------------------
// Хранение
// ---------------------------------------------------------------------------
static QString gkey(const QString& id, const QString& field) {
    return QString("games/%1/%2").arg(id, field);
}

static void writeGame(QSettings& s, const GameDef& g) {
    s.setValue(gkey(g.id, "name"), g.name);
    s.setValue(gkey(g.id, "mode"), static_cast<int>(g.mode));
    s.setValue(gkey(g.id, "processes"), g.processes);
    s.setValue(gkey(g.id, "titles"), g.titles);
    s.setValue(gkey(g.id, "minInterval"), g.minIntervalMs);
    s.setValue(gkey(g.id, "builtin"), g.builtin);
    s.setValue(gkey(g.id, "icon"), g.icon);
    s.setValue(gkey(g.id, "color"), g.color);
    if (g.image.isEmpty()) s.remove(gkey(g.id, "image"));
    else s.setValue(gkey(g.id, "image"), g.image);
}

static bool validColor(const QString& c) {
    static const QRegularExpression re("^#[0-9A-Fa-f]{6}$");
    return re.match(c).hasMatch();
}

static GameDef readGame(QSettings& s, const QString& id) {
    GameDef g;
    g.id = id;
    bool isBuiltin = false;
    const GameDef def = Games::builtinDefault(id, &isBuiltin);
    g.name = s.value(gkey(id, "name"), isBuiltin ? def.name : id).toString();
    // поля, которых нет в настройках (повреждённая запись) — из встроенной базы
    const int defMode = isBuiltin ? static_cast<int>(def.mode) : 0;
    g.mode = gameModeFromInt(s.value(gkey(id, "mode"), defMode).toInt());
    g.processes = s.contains(gkey(id, "processes")) ? s.value(gkey(id, "processes")).toStringList()
                                                     : (isBuiltin ? def.processes : QStringList());
    g.titles = s.contains(gkey(id, "titles")) ? s.value(gkey(id, "titles")).toStringList()
                                               : (isBuiltin ? def.titles : QStringList());
    g.minIntervalMs = qBound(5, s.value(gkey(id, "minInterval"), isBuiltin ? def.minIntervalMs : 5).toInt(), 1000);
    g.builtin = s.value(gkey(id, "builtin"), isBuiltin).toBool();
    // значок: из настроек; у игр из старых версий его нет — берём встроенный
    const QString icon = s.value(gkey(id, "icon")).toString();
    const QString color = s.value(gkey(id, "color")).toString().toUpper();
    g.icon = !icon.isEmpty() ? icon : (isBuiltin ? def.icon : QString("gamepad"));
    g.color = validColor(color) ? color : (isBuiltin ? def.color : QString("#FF5A66"));
    g.image = s.value(gkey(id, "image")).toByteArray();
    if (g.image.size() > 512 * 1024) g.image.clear();   // повреждённая/огромная запись
    return g;
}

static bool hasProfiles(QSettings& s, const QString& id) {
    s.beginGroup(gkey(id, "profiles"));          // профили до 1.13 (ещё не перенесены в файлы)
    const bool any = !s.childGroups().isEmpty();
    s.endGroup();
    return any || !QDir(AppSettings::profilesDir() + "/" + id).entryList({"*.ini"}, QDir::Files).isEmpty();
}

// Пользователь что-то менял у встроенной игры (правила, режим, значок)
static bool builtinEdited(QSettings& s, const QString& id, const GameDef& def) {
    const GameDef g = readGame(s, id);
    return g.mode != def.mode || g.processes != def.processes || g.titles != def.titles ||
           g.icon != def.icon || g.color != def.color || !g.image.isEmpty() || g.name != def.name;
}

// С версии 1.7 в списке только игры пользователя. Новая установка — пустой
// список (игры добавляются из шаблонов). При обновлении со старых версий, где
// все встроенные игры добавлялись сами, оставляем только те, которыми
// пользовались: текущую, с настройками, с профилями или изменённые.
static void ensureList(QSettings& s) {
    if (s.value("games/listVersion", 0).toInt() >= 2) return;
    if (!s.contains("games/order")) {
        s.setValue("games/order", QStringList());
    } else {
        const QStringList order = s.value("games/order").toStringList();
        const QString cur = s.value("currentGame").toString();
        QStringList keep;
        for (const QString& id : order) {
            bool isBuiltin = false;
            const GameDef def = Games::builtinDefault(id, &isBuiltin);
            const bool used = !isBuiltin || id == cur || s.contains(gkey(id, "state/interval")) ||
                              hasProfiles(s, id) || builtinEdited(s, id, def);
            if (used) keep << id;
            else s.remove("games/" + id);
        }
        s.setValue("games/order", keep);
        if (!keep.contains(cur)) s.setValue("currentGame", keep.value(0));
    }
    s.remove("games/knownBuiltins");
    s.setValue("games/listVersion", 2);
}

QList<GameDef> Games::list() {
    auto s = AppSettings::open();
    ensureList(*s);
    QList<GameDef> out;
    for (const QString& id : s->value("games/order").toStringList())
        out << readGame(*s, id);
    return out;
}

GameDef Games::get(const QString& id, bool* ok) {
    auto s = AppSettings::open();
    ensureList(*s);
    bool exists = !id.isEmpty() && s->value("games/order").toStringList().contains(id);
    if (ok) *ok = exists;
    return exists ? readGame(*s, id) : GameDef{};
}

bool Games::contains(const QString& id) {
    bool ok = false;
    get(id, &ok);
    return ok;
}

bool Games::addBuiltin(const QString& id) {
    bool isBuiltin = false;
    const GameDef def = builtinDefault(id, &isBuiltin);
    if (!isBuiltin) return false;
    if (contains(id)) return true;
    auto s = AppSettings::open();
    s->remove("games/" + id);              // остатки старой записи — начинаем с чистого листа
    s->sync();
    save(def);
    return true;
}

void Games::save(const GameDef& g) {
    if (g.id.isEmpty()) return;
    auto s = AppSettings::open();
    ensureList(*s);
    QStringList order = s->value("games/order").toStringList();
    if (!order.contains(g.id)) { order << g.id; s->setValue("games/order", order); }
    writeGame(*s, g);
    s->sync();
}

bool Games::remove(const QString& id) {
    auto s = AppSettings::open();
    ensureList(*s);
    QStringList order = s->value("games/order").toStringList();
    if (!order.contains(id)) return false;
    order.removeAll(id);
    s->setValue("games/order", order);
    s->remove("games/" + id);
    QDir(profileDir(id)).removeRecursively();      // и файлы профилей этой игры
    dropProfileCache(id);
    if (s->value("currentGame").toString() == id) {
        if (order.isEmpty()) s->remove("currentGame");
        else s->setValue("currentGame", order.first());
    }
    s->sync();
    return true;
}

bool Games::resetToDefault(const QString& id) {
    bool ok = false;
    GameDef d = builtinDefault(id, &ok);
    if (ok) save(d);
    return ok;
}

QString Games::newCustomId() {
    auto s = AppSettings::open();
    QStringList order = s->value("games/order").toStringList();
    int n = s->value("games/nextCustom", 1).toInt();
    while (order.contains(QString("custom%1").arg(n))) ++n;
    s->setValue("games/nextCustom", n + 1);
    return QString("custom%1").arg(n);
}

bool Games::nameTaken(const QString& name, const QString& exceptId) {
    for (const GameDef& g : list())
        if (g.id != exceptId && g.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString Games::currentId() {
    auto s = AppSettings::open();
    ensureList(*s);
    QStringList order = s->value("games/order").toStringList();
    QString id = s->value("currentGame").toString();
    if (!order.contains(id)) id = order.value(0);
    return id;
}

void Games::setCurrentId(const QString& id) {
    auto s = AppSettings::open();
    s->setValue("currentGame", id);
}

// ---------------------------------------------------------------------------
// Действия хранятся строками: "m:1:<держать>:<пауза после>" или "k:f3:<держать>:<пауза после>";
// шаги «По шагам» — с пятым полем: "m:1:0:150:d" (нажать) / "m:1:0:0:u" (отпустить) /
// "m:1:0:0:c" (нажать и отпустить; шаг без поля — «нажать», как в 1.14).
// Вызов группы: "g:<id группы>:<повторы>:<пауза после>" (версии до 1.17 его пропускают)
static QString encodeStep(const ActionStep& a, bool step = false) {
    if (a.isGroup()) return QString("g:%1:%2:%3").arg(a.group).arg(qBound(1, a.repeat, kMaxRepeat)).arg(a.afterMs);
    QString s = QString("%1:%2:%3:%4").arg(a.mouse ? "m" : "k", a.mouse ? QString::number(a.button) : a.key)
                                      .arg(a.holdMs).arg(a.afterMs);
    if (a.press == ActionStep::Down) s += ":d";
    else if (a.press == ActionStep::Up) s += ":u";
    else if (step) s += ":c";
    return s;
}

static int validButton(int b) { return MouseButtons::valid(b) ? b : 1; }
static QString validKey(const QString& k, const QString& fallback) {
    return Keys::byName(k) ? k.toLower() : fallback;
}

static bool decodeStep(const QString& str, ActionStep* out) {
    const QStringList p = str.split(':');
    if (p.size() < 2) return false;
    ActionStep a;
    if (p[0] == "g") {
        if (!Groups::validId(p[1])) return false;
        a.group = p[1];
        a.mouse = false;
        a.key.clear();
        a.repeat = qBound(1, p.value(2).toInt(), kMaxRepeat);
        a.afterMs = qBound(0, p.value(3).toInt(), 60000);
        a.press = ActionStep::Click;
        *out = a;
        return true;
    }
    if (p[0] == "m") {
        const int b = p[1].toInt();
        if (!MouseButtons::valid(b)) return false;
        a.mouse = true; a.button = b;
    } else if (p[0] == "k") {
        if (!Keys::byName(p[1])) return false;
        a.mouse = false; a.key = p[1].toLower();
    } else {
        return false;
    }
    a.holdMs = qBound(0, p.value(2).toInt(), 10000);
    a.afterMs = qBound(0, p.value(3).toInt(), 60000);
    const QString pr = p.value(4);
    a.press = pr == "d" ? ActionStep::Down : pr == "u" ? ActionStep::Up : pr == "c" ? ActionStep::Click : -1;
    *out = a;
    return true;
}

static void writeState(QSettings& s, const QString& prefix, const GameState& st) {
    QStringList acts;
    for (const ActionStep& a : st.actions) acts << encodeStep(a);
    s.setValue(prefix + "actions", acts);
    // шаги и старт через N мс — ключи только если заданы (старые версии их не знают)
    QStringList steps;
    for (const ActionStep& a : st.steps) steps << encodeStep(a, true);
    if (steps.isEmpty()) s.remove(prefix + "steps"); else s.setValue(prefix + "steps", steps);
    if (st.startDelayMs > 0) s.setValue(prefix + "startDelay", st.startDelayMs); else s.remove(prefix + "startDelay");
    s.setValue(prefix + "interval", st.intervalMs);
    s.setValue(prefix + "randomize", st.randomize);
    s.setValue(prefix + "jitter", st.jitterMs);
    s.setValue(prefix + "lockWindow", st.lockWindow);
    s.setValue(prefix + "frameSync", st.frameSync);
    s.setValue(prefix + "frameHz", st.frameHz);
    // свой звук: пусто — как в настройках (ключ не пишем)
    if (st.soundOn.isEmpty()) s.remove(prefix + "soundOn"); else s.setValue(prefix + "soundOn", st.soundOn);
    if (st.soundOff.isEmpty()) s.remove(prefix + "soundOff"); else s.setValue(prefix + "soundOff", st.soundOff);
    // поля версий до 1.12 (два действия) больше не нужны
    for (const char* k : {"mouse1", "button1", "key1", "useSlot2", "mouse2", "button2", "key2", "between"})
        s.remove(prefix + k);
}

static GameState readState(QSettings& s, const QString& prefix, const GameState& def) {
    GameState st;
    st.actions.clear();
    if (s.contains(prefix + "actions")) {
        for (const QString& e : s.value(prefix + "actions").toStringList()) {
            ActionStep a;
            if (decodeStep(e, &a) && st.actions.size() < kMaxActions) {
                a.press = ActionStep::Click;     // у действий «нажать/отпустить» не бывает
                st.actions << a;
            }
        }
    } else if (s.contains(prefix + "mouse1") || s.contains(prefix + "key1") || s.contains(prefix + "useSlot2")) {
        // формат 1.4–1.11: два действия и пауза между ними
        const ActionStep d1 = def.actions.value(0);
        ActionStep a1;
        a1.mouse = s.value(prefix + "mouse1", d1.mouse).toBool();
        a1.button = validButton(s.value(prefix + "button1", d1.button).toInt());
        a1.key = validKey(s.value(prefix + "key1", d1.key).toString(), d1.key);
        a1.afterMs = qBound(0, s.value(prefix + "between", 0).toInt(), 5000);
        st.actions << a1;
        if (s.value(prefix + "useSlot2", def.multi()).toBool()) {
            const ActionStep d2 = def.actions.value(1);
            ActionStep a2;
            a2.mouse = s.value(prefix + "mouse2", d2.mouse).toBool();
            a2.button = validButton(s.value(prefix + "button2", d2.button).toInt());
            a2.key = validKey(s.value(prefix + "key2", d2.key).toString(), d2.key);
            st.actions << a2;
        }
    }
    if (st.actions.isEmpty()) st.actions = def.actions;
    if (st.actions.isEmpty()) st.actions = {ActionStep{}};
    for (const QString& e : s.value(prefix + "steps").toStringList()) {
        ActionStep a;
        if (!decodeStep(e, &a) || st.steps.size() >= kMaxSteps) continue;
        if (a.press < 0) a.press = ActionStep::Down;     // без пятого поля — «нажать»
        if (a.isGroup()) a.press = ActionStep::Click;
        a.holdMs = 0;
        st.steps << a;
    }
    if (!s.contains(prefix + "steps")) st.steps = def.steps;
    st.startDelayMs = qBound(0, s.value(prefix + "startDelay", def.startDelayMs).toInt(), kMaxStartDelayMs);
    st.intervalMs = qBound(0, s.value(prefix + "interval", def.intervalMs).toInt(), 60000);
    st.randomize = s.value(prefix + "randomize", def.randomize).toBool();
    st.jitterMs = qBound(0, s.value(prefix + "jitter", def.jitterMs).toInt(), 1000);
    st.lockWindow = s.value(prefix + "lockWindow", def.lockWindow).toBool();
    st.frameSync = s.value(prefix + "frameSync", def.frameSync).toBool();
    st.frameHz = s.value(prefix + "frameHz", def.frameHz).toInt();
    if (st.frameHz < 0 || st.frameHz > 1000) st.frameHz = 0;
    st.soundOn = s.value(prefix + "soundOn", def.soundOn).toString().left(1000);
    st.soundOff = s.value(prefix + "soundOff", def.soundOff).toString().left(1000);
    return st;
}

// Группы: <prefix>groups — порядок (id), <prefix>group/<id>/name, steps, items
static void writeGroups(QSettings& s, const QString& prefix, const QList<ActionGroup>& groups) {
    s.remove(prefix + "group");
    QStringList order;
    for (const ActionGroup& g : groups) {
        if (!Groups::validId(g.id) || order.contains(g.id)) continue;
        order << g.id;
        const QString k = prefix + "group/" + g.id + "/";
        s.setValue(k + "name", g.name.trimmed().left(40));
        s.setValue(k + "steps", g.steps);
        QStringList items;
        for (const ActionStep& a : g.items)
            if (!a.isGroup()) items << encodeStep(a, g.steps);
        s.setValue(k + "items", items);
    }
    if (order.isEmpty()) s.remove(prefix + "groups");
    else s.setValue(prefix + "groups", order);
}

static QList<ActionGroup> readGroups(QSettings& s, const QString& prefix) {
    QList<ActionGroup> out;
    for (const QString& id : s.value(prefix + "groups").toStringList()) {
        if (!Groups::validId(id) || Groups::find(out, id) || out.size() >= kMaxGroups) continue;
        const QString k = prefix + "group/" + id + "/";
        ActionGroup g;
        g.id = id;
        g.name = s.value(k + "name").toString().trimmed().left(40);
        if (g.name.isEmpty()) g.name = Groups::freeName(out);
        g.steps = s.value(k + "steps", false).toBool();
        for (const QString& e : s.value(k + "items").toStringList()) {
            ActionStep a;
            if (!decodeStep(e, &a) || a.isGroup() || g.items.size() >= (g.steps ? kMaxSteps : kMaxActions)) continue;
            if (!g.steps) a.press = ActionStep::Click;
            else if (a.press < 0) a.press = ActionStep::Down;
            if (g.steps) a.holdMs = 0;
            g.items << a;
        }
        if (!g.items.isEmpty()) out << g;
    }
    return out;
}

QList<ActionGroup> Games::groups(const QString& gameId) {
    auto s = AppSettings::open();
    return readGroups(*s, gkey(gameId, ""));
}

void Games::saveGroups(const QString& gameId, const QList<ActionGroup>& groups) {
    if (gameId.isEmpty()) return;
    auto s = AppSettings::open();
    writeGroups(*s, gkey(gameId, ""), groups);
    s->sync();
}

// Группы игры — в состояние. embedded — копии из файла профиля: каких у игры нет,
// те добавляются к игре (профиль принёс их с собой). Свои группы игры важнее копий:
// группу могли поправить после того, как профиль сохранили
static void attachGroups(GameState& st, const QString& gameId, const QList<ActionGroup>& embedded = {}) {
    QList<ActionGroup> mine = Games::groups(gameId);
    bool added = false;
    for (const ActionGroup& g : embedded)
        if (!Groups::find(mine, g.id) && mine.size() < kMaxGroups) { mine << g; added = true; }
    if (added) Games::saveGroups(gameId, mine);
    st.groups = mine;
}

GameState Games::loadState(const GameDef& g) {
    auto s = AppSettings::open();
    GameState def = defaultState(g);
    GameState st = (s->contains(gkey(g.id, "state/interval")) || s->contains(gkey(g.id, "state/actions")))
                       ? readState(*s, gkey(g.id, "state/"), def) : def;
    attachGroups(st, g.id);
    st.intervalMs = qMax(st.intervalMs, st.repeatMin(g.mode, g.minIntervalMs));
    return st;
}

void Games::saveState(const QString& gameId, const GameState& st) {
    auto s = AppSettings::open();
    writeState(*s, gkey(gameId, "state/"), st);
}

// ---------------------------------------------------------------------------
bool Games::validProfileName(const QString& name, QString* why) {
    const QString n = name.trimmed();
    QString err;
    if (n.isEmpty()) err = T("Имя не может быть пустым.");
    else if (n.contains('/') || n.contains('\\')) err = T("В имени нельзя использовать / и \\.");
    else if (n.length() > 60) err = T("Слишком длинное имя (максимум 60 символов).");
    if (why) *why = err;
    return err.isEmpty();
}

// ---------------------------------------------------------------------------
// Профили — по файлу на профиль: <папка данных>/profiles/<игра>/<имя>.ini.
// Имя профиля записано в файле ([AutoQ] name), имя файла — только для людей
// (запрещённые в именах файлов знаки заменены на «_»). Регистр не различается
// (как в реестре и файлах Windows): «Fast» и «fast» — один профиль.
// ---------------------------------------------------------------------------
namespace {
const char* kProfilePrefix = "profile/";

QString profileDir(const QString& gameId) { return AppSettings::profilesDir() + "/" + AppSettings::safeFileName(gameId); }

struct ProfileFile { QString name, path; };

// Список файлов профилей игры. Читается заново, если папку меняли (или прошло 2 с —
// файлы могли поправить руками); свои записи сбрасывают кэш сразу.
struct ProfileCache { QString dir; QDateTime mtime; qint64 at = 0; QList<ProfileFile> list; };
QHash<QString, ProfileCache>& profileCache() { static QHash<QString, ProfileCache> c; return c; }
void dropProfileCache(const QString& gameId) { profileCache().remove(gameId); }

QList<ProfileFile> profileFiles(const QString& gameId) {
    const QString dirPath = profileDir(gameId);
    const QFileInfo di(dirPath);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    ProfileCache& c = profileCache()[gameId];
    if (c.dir == dirPath && c.at && now - c.at < 2000 && c.mtime == di.lastModified()) return c.list;
    QList<ProfileFile> out;
    QDir dir(dirPath);
    for (const QFileInfo& fi : dir.entryInfoList({"*.ini"}, QDir::Files, QDir::Name)) {
        QSettings f(fi.absoluteFilePath(), QSettings::IniFormat);
        QString n = f.value("AutoQ/name").toString().trimmed();
        if (n.isEmpty()) n = fi.completeBaseName().trimmed();     // файл положили руками
        if (!Games::validProfileName(n, nullptr)) continue;
        // имя уже занято (в папку положили копию или экспорт с тем же именем) — «Имя (2)»:
        // иначе файл лежит в папке, а в списке его нет. Сохранение запишет в файл новое имя
        auto taken = [&out](const QString& name) {
            for (const ProfileFile& p : out)
                if (p.name.compare(name, Qt::CaseInsensitive) == 0) return true;
            return false;
        };
        const QString base = n.left(54);
        for (int i = 2; taken(n); ++i) n = QString("%1 (%2)").arg(base).arg(i);
        out << ProfileFile{n, fi.absoluteFilePath()};
    }
    c = ProfileCache{dirPath, di.lastModified(), now, out};
    return out;
}

ProfileFile findProfile(const QString& gameId, const QString& name) {
    const QString n = name.trimmed();
    for (const ProfileFile& p : profileFiles(gameId))
        if (p.name.compare(n, Qt::CaseInsensitive) == 0) return p;
    return ProfileFile{};
}

// Файл для нового профиля: «Имя.ini», при совпадении — «Имя (2).ini»
QString newProfilePath(const QString& gameId, const QString& name) {
    const QString dir = profileDir(gameId);
    const QString base = AppSettings::safeFileName(name.trimmed());
    QString path = dir + "/" + base + ".ini";
    for (int i = 2; QFile::exists(path); ++i) path = QString("%1/%2 (%3).ini").arg(dir, base).arg(i);
    return path;
}

std::unique_ptr<QSettings> openProfile(const QString& path) {
    return std::make_unique<QSettings>(path, QSettings::IniFormat);
}

void writeProfileFile(const QString& path, const QString& gameId, const QString& name, const GameState& st,
                      int mode, const QString& hotkey, bool inOverlay) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    auto f = openProfile(path);
    f->clear();
    f->setValue("AutoQ/type", "profile");
    f->setValue("AutoQ/name", name.trimmed());
    f->setValue("AutoQ/game", gameId);
    bool ok = false;
    const GameDef g = Games::get(gameId, &ok);
    if (ok) f->setValue("AutoQ/gameName", g.name);
    writeState(*f, kProfilePrefix, st);
    // копии групп, которые профиль вызывает: на другом ПК он принесёт их с собой
    QList<ActionGroup> used;
    for (const QString& id : Groups::usedIds(st.actions + st.steps))
        if (const ActionGroup* gr = Groups::find(st.groups, id)) used << *gr;
    writeGroups(*f, kProfilePrefix, used);
    if (mode >= 0 && mode <= kLastMode) f->setValue(QString(kProfilePrefix) + "mode", mode);
    if (!hotkey.isEmpty()) f->setValue(QString(kProfilePrefix) + "hotkey", hotkey);
    if (!inOverlay) f->setValue(QString(kProfilePrefix) + "overlay", false);
    f->sync();
}

GameState defaultFor(const QString& gameId) {
    bool ok = false;
    const GameDef g = Games::get(gameId, &ok);
    return ok ? Games::defaultState(g) : GameState{};
}
}

QString Games::profileFile(const QString& gameId, const QString& name) { return findProfile(gameId, name).path; }
void Games::reloadProfiles() { profileCache().clear(); }
QString Games::profilesFolder(const QString& gameId) { return profileDir(gameId); }

QStringList Games::profiles(const QString& gameId) {
    QStringList names;
    for (const ProfileFile& p : profileFiles(gameId)) names << p.name;
    names.sort(Qt::CaseInsensitive);
    return names;
}

// Имя профиля как оно уже записано (без учёта регистра)
static QString storedProfileName(const QString& gameId, const QString& name) {
    const ProfileFile p = findProfile(gameId, name);
    return p.path.isEmpty() ? name.trimmed() : p.name;
}

bool Games::profileExists(const QString& gameId, const QString& name) {
    return !findProfile(gameId, name).path.isEmpty();
}

GameState Games::loadProfile(const QString& gameId, const QString& name, int* mode) {
    bool ok = false;
    GameDef g = get(gameId, &ok);
    const ProfileFile pf = findProfile(gameId, name);
    const GameState def = ok ? defaultState(g) : GameState{};
    if (mode) *mode = -1;
    if (pf.path.isEmpty()) return def;
    auto f = openProfile(pf.path);
    GameState st = readState(*f, kProfilePrefix, def);
    attachGroups(st, gameId, readGroups(*f, kProfilePrefix));
    const int m = f->value(QString(kProfilePrefix) + "mode", -1).toInt();
    const bool hasMode = m >= 0 && m <= kLastMode;
    // режим профиля (если он свой) — от него зависит, можно ли «повтор через» меньше минимума
    if (ok) st.intervalMs = qMax(st.intervalMs, st.repeatMin(hasMode ? gameModeFromInt(m) : g.mode, g.minIntervalMs));
    if (mode) *mode = hasMode ? m : -1;
    return st;
}

void Games::saveProfile(const QString& gameId, const QString& name, const GameState& st, int mode) {
    if (!validProfileName(name, nullptr)) return;
    const ProfileFile pf = findProfile(gameId, name);
    if (pf.path.isEmpty()) {
        writeProfileFile(newProfilePath(gameId, name), gameId, name, st, mode, QString(), true);
    } else {
        // клавиша и «в оверлее» — не часть настроек, остаются как были
        auto f = openProfile(pf.path);
        const QString hk = f->value(QString(kProfilePrefix) + "hotkey").toString();
        const bool ov = f->value(QString(kProfilePrefix) + "overlay", true).toBool();
        f.reset();
        writeProfileFile(pf.path, gameId, pf.name, st, mode, hk, ov);
    }
    dropProfileCache(gameId);
}

void Games::removeProfile(const QString& gameId, const QString& name) {
    const ProfileFile pf = findProfile(gameId, name);
    if (!pf.path.isEmpty()) QFile::remove(pf.path);
    dropProfileCache(gameId);
    auto s = AppSettings::open();
    if (!pf.name.isEmpty() &&
        s->value(gkey(gameId, "activeProfile")).toString().compare(pf.name, Qt::CaseInsensitive) == 0)
        s->remove(gkey(gameId, "activeProfile"));
    s->sync();
}

bool Games::renameProfile(const QString& gameId, const QString& from, const QString& to) {
    const ProfileFile a = findProfile(gameId, from);
    const QString b = to.trimmed();
    if (!validProfileName(b, nullptr) || a.path.isEmpty()) return false;
    if (a.name == b) return true;
    const bool caseOnly = a.name.compare(b, Qt::CaseInsensitive) == 0;   // «fast» -> «Fast»
    if (!caseOnly && profileExists(gameId, b)) return false;
    int mode = -1;
    const GameState st = loadProfile(gameId, a.name, &mode);
    QString hk;
    bool ov = true;
    {
        auto f = openProfile(a.path);
        hk = f->value(QString(kProfilePrefix) + "hotkey").toString();
        ov = f->value(QString(kProfilePrefix) + "overlay", true).toBool();
    }
    const bool wasActive = activeProfile(gameId) == a.name;
    QFile::remove(a.path);              // сначала убрать: «fast.ini» и «Fast.ini» в Windows — один файл
    dropProfileCache(gameId);
    writeProfileFile(newProfilePath(gameId, b), gameId, b, st, mode, hk, ov);
    dropProfileCache(gameId);
    if (wasActive) setActiveProfile(gameId, b);
    return true;
}

bool Games::exportProfile(const QString& gameId, const QString& name, const QString& file, QString* error) {
    const ProfileFile pf = findProfile(gameId, name);
    if (pf.path.isEmpty()) { if (error) *error = T("Профиль не найден."); return false; }
    int mode = -1;
    const GameState st = loadProfile(gameId, pf.name, &mode);
    bool ov = true;
    { auto f = openProfile(pf.path); ov = f->value(QString(kProfilePrefix) + "overlay", true).toBool(); }
    QFile::remove(file);
    writeProfileFile(file, gameId, pf.name, st, mode, QString(), ov);   // без клавиши — у друга она своя
    if (!QFile::exists(file)) { if (error) *error = T("Не удалось записать файл: %1").arg(file); return false; }
    return true;
}

QString Games::importProfile(const QString& gameId, const QString& file, QString* error, QString* fromGame) {
    auto f = openProfile(file);
    if (f->status() != QSettings::NoError ||
        !(f->contains(QString(kProfilePrefix) + "actions") || f->contains(QString(kProfilePrefix) + "interval"))) {
        if (error) *error = T("Это не профиль AutoQ.");
        return QString();
    }
    const GameState def = defaultFor(gameId);
    GameState st = readState(*f, kProfilePrefix, def);
    attachGroups(st, gameId, readGroups(*f, kProfilePrefix));     // группы профиля — к игре
    bool ok = false;
    const GameDef g = get(gameId, &ok);
    const int m = f->value(QString(kProfilePrefix) + "mode", -1).toInt();
    if (ok) st.intervalMs = qMax(st.intervalMs, st.repeatMin((m >= 0 && m <= kLastMode) ? gameModeFromInt(m) : g.mode,
                                                             g.minIntervalMs));
    const bool ov = f->value(QString(kProfilePrefix) + "overlay", true).toBool();
    if (fromGame) *fromGame = f->value("AutoQ/game").toString() == gameId ? QString()
                                                                        : f->value("AutoQ/gameName").toString();
    QString base = f->value("AutoQ/name").toString().trimmed();
    if (!validProfileName(base, nullptr)) base = QFileInfo(file).completeBaseName().trimmed().left(50);
    if (!validProfileName(base, nullptr)) base = T("Профиль");
    // имя занято — «Имя (2)», «Имя (3)»…
    QString name = base;
    for (int i = 2; profileExists(gameId, name); ++i) name = QString("%1 (%2)").arg(base.left(50)).arg(i);
    writeProfileFile(newProfilePath(gameId, name), gameId, name, st, (m >= 0 && m <= kLastMode) ? m : -1, QString(), ov);
    dropProfileCache(gameId);
    return name;
}

// ---------------------------------------------------------------------------
// Клавиши игры
// ---------------------------------------------------------------------------
HotkeySpec Games::defaultStartKey() {
    auto s = AppSettings::open();
    HotkeySpec sp = HotkeySpec::parse(s->value("hotkey", "f6").toString());
    return sp.isValid() ? sp : HotkeySpec::parse("f6");
}

HotkeySpec Games::startKey(const QString& gameId) {
    auto s = AppSettings::open();
    HotkeySpec sp = HotkeySpec::parse(s->value(gkey(gameId, "hotkey")).toString());
    if (!sp.isValid()) return defaultStartKey();
    sp.block = s->value(gkey(gameId, "hotkeyBlock"), false).toBool();
    return sp;
}

void Games::setStartKey(const QString& gameId, const HotkeySpec& sp) {
    if (gameId.isEmpty() || !sp.isValid()) return;
    auto s = AppSettings::open();
    // другая клавиша — «не пропускать в игру» снова выключено: буква, которую выбрали
    // вместо Mouse 4, не должна вдруг перестать печататься
    if (HotkeySpec::parse(s->value(gkey(gameId, "hotkey")).toString()) != sp) s->remove(gkey(gameId, "hotkeyBlock"));
    s->setValue(gkey(gameId, "hotkey"), sp.toString());
    s->sync();
}

bool Games::startKeyBlock(const QString& gameId) {
    auto s = AppSettings::open();
    return s->value(gkey(gameId, "hotkeyBlock"), false).toBool();
}

void Games::setStartKeyBlock(const QString& gameId, bool on) {
    if (gameId.isEmpty()) return;
    auto s = AppSettings::open();
    if (on) s->setValue(gkey(gameId, "hotkeyBlock"), true);
    else s->remove(gkey(gameId, "hotkeyBlock"));
    s->sync();
}

// «Используется» (кружок в списке): только такую категорию можно запустить —
// кнопкой, клавишей, из трея. Новая категория — сразу используется.
bool Games::armed(const QString& gameId) {
    auto s = AppSettings::open();
    return s->value(gkey(gameId, "armed"), true).toBool();
}

void Games::setArmed(const QString& gameId, bool on) {
    if (gameId.isEmpty()) return;
    auto s = AppSettings::open();
    s->setValue(gkey(gameId, "armed"), on);
    s->sync();
}

int Games::keyMode(const QString& gameId) {
    auto s = AppSettings::open();
    return s->value(gkey(gameId, "keyMode"), 0).toInt() == 1 ? 1 : 0;
}

void Games::setKeyMode(const QString& gameId, int mode) {
    if (gameId.isEmpty()) return;
    auto s = AppSettings::open();
    s->setValue(gkey(gameId, "keyMode"), mode == 1 ? 1 : 0);
    s->sync();
}

HotkeySpec Games::armKey(const QString& gameId) {
    auto s = AppSettings::open();
    return HotkeySpec::parse(s->value(gkey(gameId, "armKey")).toString());
}

void Games::setArmKey(const QString& gameId, const HotkeySpec& sp) {
    if (gameId.isEmpty()) return;
    auto s = AppSettings::open();
    if (sp.isValid()) s->setValue(gkey(gameId, "armKey"), sp.toString());
    else s->remove(gkey(gameId, "armKey"));
    s->sync();
}

bool Games::profileInOverlay(const QString& gameId, const QString& profile) {
    if (profile.trimmed().isEmpty()) return AppSettings::open()->value(gkey(gameId, "overlay"), true).toBool();
    const ProfileFile pf = findProfile(gameId, profile);
    if (pf.path.isEmpty()) return true;
    return openProfile(pf.path)->value(QString(kProfilePrefix) + "overlay", true).toBool();
}

void Games::setProfileInOverlay(const QString& gameId, const QString& profile, bool on) {
    std::unique_ptr<QSettings> s;
    QString key;
    if (profile.trimmed().isEmpty()) {
        s = AppSettings::open();
        key = gkey(gameId, "overlay");
    } else {
        const ProfileFile pf = findProfile(gameId, profile);
        if (pf.path.isEmpty()) return;
        s = openProfile(pf.path);
        key = QString(kProfilePrefix) + "overlay";
    }
    if (on) s->remove(key);   // по умолчанию — да
    else s->setValue(key, false);
    s->sync();
}

HotkeySpec Games::profileKey(const QString& gameId, const QString& profile) {
    const ProfileFile pf = findProfile(gameId, profile);
    if (pf.path.isEmpty()) return HotkeySpec{};
    return HotkeySpec::parse(openProfile(pf.path)->value(QString(kProfilePrefix) + "hotkey").toString());
}

void Games::setProfileKey(const QString& gameId, const QString& profile, const HotkeySpec& sp) {
    const ProfileFile pf = findProfile(gameId, profile);
    if (pf.path.isEmpty()) return;
    auto f = openProfile(pf.path);
    const QString k = QString(kProfilePrefix) + "hotkey";
    if (sp.isValid()) f->setValue(k, sp.toString());
    else f->remove(k);
    f->sync();
}

QString Games::activeProfile(const QString& gameId) {
    const QString n = AppSettings::open()->value(gkey(gameId, "activeProfile")).toString();
    if (n.isEmpty()) return QString();
    return findProfile(gameId, n).name;      // профиля больше нет — нет и активного
}

void Games::setActiveProfile(const QString& gameId, const QString& name) {
    if (gameId.isEmpty()) return;
    auto s = AppSettings::open();
    if (name.trimmed().isEmpty()) s->remove(gkey(gameId, "activeProfile"));
    else s->setValue(gkey(gameId, "activeProfile"), storedProfileName(gameId, name));
    s->sync();
}

// ---------------------------------------------------------------------------
QStringList Games::splitList(const QString& text) {
    QStringList out;
    for (const QString& p : text.split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts)) {
        const QString t = p.trimmed();
        if (!t.isEmpty() && !out.contains(t, Qt::CaseInsensitive)) out << t;
    }
    return out;
}

QString Games::joinList(const QStringList& list) { return list.join(", "); }

// ---------------------------------------------------------------------------
// Перенос настроек версий до 1.4 (плоские ключи, категории Genshin/Minecraft)
// ---------------------------------------------------------------------------
static GameState readOldState(QSettings& s, const QString& p, const GameState& def) {
    GameState st = def;
    auto oldButton = [](int idx) { return idx == 1 ? 3 : 1; };   // было: 0 — ЛКМ, 1 — ПКМ
    const ActionStep d1 = def.actions.value(0), d2 = def.actions.value(1);
    ActionStep a1 = d1;
    if (s.contains(p + "action1")) a1.mouse = s.value(p + "action1").toInt() == 0;
    else if (s.contains(p + "action")) a1.mouse = s.value(p + "action").toInt() == 0;
    a1.button = oldButton(s.value(p + "mouse1", 0).toInt());
    a1.key = validKey(s.value(p + "key1", s.value(p + "key", d1.key)).toString(), d1.key);
    a1.afterMs = qBound(0, s.value(p + "betweenMs", 0).toInt(), 5000);
    a1.holdMs = 0;
    st.actions = {a1};
    if (s.value(p + "useSlot2", s.value(p + "secondKey", false)).toBool()) {
        ActionStep a2;
        a2.mouse = s.value(p + "action2", 1).toInt() == 0;
        a2.button = oldButton(s.value(p + "mouse2", 0).toInt());
        a2.key = validKey(s.value(p + "key2", d2.key).toString(), d2.key);
        st.actions << a2;
    }
    st.intervalMs = qBound(1, s.value(p + "interval", def.intervalMs).toInt(), 60000);
    st.randomize = s.value(p + "randomize", def.randomize).toBool();
    st.jitterMs = qBound(0, s.value(p + "jitter", def.jitterMs).toInt(), 1000);
    return st;
}

void Games::migrate() {
    auto s = AppSettings::open();
    if (s->value("version", 1).toInt() < 2) {
        const bool hadOld = s->contains("action1") || s->contains("interval") ||
                            s->contains("key1") || s->contains("action");
        s->beginGroup("profiles");
        const QStringList names = s->childGroups();
        s->endGroup();
        const QString wn = s->value("windowName").toString().trimmed();
        if (hadOld || !names.isEmpty() || !wn.isEmpty()) {
            const QString gid = s->value("category", 0).toInt() == 1 ? "minecraft" : "genshin";
            // игра из старой версии — в список пользователя
            QStringList order = s->value("games/order").toStringList();
            if (!order.contains(gid)) {
                order << gid;
                s->setValue("games/order", order);
                writeGame(*s, Games::builtinDefault(gid));
            }
            GameDef g = readGame(*s, gid);
            if (hadOld) {
                GameState st = readOldState(*s, "", defaultState(g));
                st.lockWindow = s->value("lockWindow", false).toBool();
                st.intervalMs = qMax(st.intervalMs, g.minIntervalMs);
                writeState(*s, gkey(gid, "state/"), st);
            }
            s->setValue("currentGame", gid);
            // Своё имя окна (например "Vortex | Майнкрафт сборка") — в заголовки игры
            if (!wn.isEmpty()) {
                bool covered = false;
                for (const QString& t : g.titles)
                    if (wn.contains(t, Qt::CaseInsensitive)) { covered = true; break; }
                if (!covered) { g.titles << wn; writeGame(*s, g); }
            }
            // Профили -> профили этой игры
            for (const QString& name : names) {
                GameState st = readOldState(*s, "profiles/" + name + "/", defaultState(g));
                st.intervalMs = qMax(st.intervalMs, g.minIntervalMs);
                if (validProfileName(name, nullptr))
                    writeState(*s, gkey(gid, "profiles/" + name + "/"), st);
            }
        }
        s->remove("profiles");
        for (const char* k : {"action", "action1", "key", "key1", "mouse1", "useSlot2", "secondKey",
                              "action2", "key2", "mouse2", "comboMode", "betweenMs", "interval",
                              "randomize", "jitter", "lockWindow", "windowName", "category"})
            s->remove(k);
        s->setValue("version", 2);
    }
    ensureList(*s);
    // 1.13: кружок — «используется» (по умолчанию да). Раньше пустой кружок значил
    // «работает, только когда открыта»: открытая игра остаётся рабочей, остальные —
    // как были
    if (s->value("armedVersion", 1).toInt() < 2) {
        const QString cur = s->value("currentGame").toString();
        for (const QString& id : s->value("games/order").toStringList()) {
            const QString k = gkey(id, "armed");
            if (id == cur) s->setValue(k, true);
            else if (!s->contains(k)) s->setValue(k, false);
        }
        s->setValue("armedVersion", 2);
    }
    // 1.13: профили — отдельными файлами (раньше — внутри настроек)
    for (const QString& id : s->value("games/order").toStringList() + s->value("games/knownBuiltins").toStringList()) {
        s->beginGroup(gkey(id, "profiles"));
        const QStringList names = s->childGroups();
        s->endGroup();
        if (names.isEmpty()) continue;
        const GameState def = defaultFor(id);
        for (const QString& name : names) {
            const QString prefix = gkey(id, "profiles/" + name + "/");
            if (validProfileName(name, nullptr) && !profileExists(id, name)) {
                const int m = s->value(prefix + "mode", -1).toInt();
                writeProfileFile(newProfilePath(id, name), id, name, readState(*s, prefix, def),
                                 (m >= 0 && m <= kLastMode) ? m : -1, s->value(prefix + "hotkey").toString(),
                                 s->value(prefix + "overlay", true).toBool());
                dropProfileCache(id);
            }
        }
        s->remove(gkey(id, "profiles"));
    }
    s->sync();
}
