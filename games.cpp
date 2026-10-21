#include "games.h"
#include "i18n.h"
#include "keys.h"
#include <QRegularExpression>

// ---------------------------------------------------------------------------
static QString g_org = "AutoQ";
static QString g_app = "config";

void AppSettings::setScope(const QString& org, const QString& app) { g_org = org; g_app = app; }
std::unique_ptr<QSettings> AppSettings::open() { return std::make_unique<QSettings>(g_org, g_app); }

// ---------------------------------------------------------------------------
QList<int> Prefs::captureChoices() { return {3, 5, 7, 10}; }

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
bool Prefs::onboardingDone() { return prefBool("onboardingDone", false); }
void Prefs::setOnboardingDone(bool done) { setPrefBool("onboardingDone", done); }

GameMode gameModeFromInt(int v) {
    switch (v) { case 1: return GameMode::Combo; case 2: return GameMode::Hold; default: return GameMode::Sequence; }
}

QString gameModeName(GameMode m) {
    switch (m) {
        case GameMode::Combo: return T("Вместе");
        case GameMode::Hold:  return T("Зажать");
        default:              return T("По очереди");
    }
}

bool ActionStep::operator==(const ActionStep& o) const {
    return mouse == o.mouse && (mouse ? button == o.button : key.compare(o.key, Qt::CaseInsensitive) == 0) &&
           holdMs == o.holdMs && afterMs == o.afterMs;
}

QString ActionStep::itemName() const { return mouse ? MouseButtons::itemName(button) : key; }

ActionStep ActionStep::fromItem(const QString& name) {
    ActionStep a;
    if (const int b = MouseButtons::fromItemName(name)) { a.mouse = true; a.button = b; }
    else { a.mouse = false; a.key = name; }
    return a;
}

QString ActionStep::label() const { return mouse ? MouseButtons::shortLabel(button) : Keys::label(key); }

bool GameState::usesMouse() const {
    for (const ActionStep& a : actions) if (a.mouse) return true;
    return false;
}

bool GameState::operator==(const GameState& o) const {
    return actions == o.actions && intervalMs == o.intervalMs &&
           randomize == o.randomize && jitterMs == o.jitterMs && lockWindow == o.lockWindow;
}

// ---------------------------------------------------------------------------
// Встроенная база
// ---------------------------------------------------------------------------
QList<GameDef> Games::builtinDefaults() {
    auto mk = [](const char* id, const char* name, GameMode mode, QStringList procs,
                 QStringList titles, int minMs, const char* icon, const char* color) {
        GameDef g; g.id = id; g.name = QString::fromUtf8(name); g.mode = mode;
        g.processes = procs; g.titles = titles; g.minIntervalMs = minMs; g.builtin = true;
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
    s.beginGroup(gkey(id, "profiles"));
    const bool any = !s.childGroups().isEmpty();
    s.endGroup();
    return any;
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
// Действия хранятся строками: "m:1:<держать>:<пауза после>" или "k:f3:<держать>:<пауза после>"
static QString encodeStep(const ActionStep& a) {
    return QString("%1:%2:%3:%4").arg(a.mouse ? "m" : "k", a.mouse ? QString::number(a.button) : a.key)
                                 .arg(a.holdMs).arg(a.afterMs);
}

static int validButton(int b) { return MouseButtons::valid(b) ? b : 1; }
static QString validKey(const QString& k, const QString& fallback) {
    return Keys::byName(k) ? k.toLower() : fallback;
}

static bool decodeStep(const QString& str, ActionStep* out) {
    const QStringList p = str.split(':');
    if (p.size() < 2) return false;
    ActionStep a;
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
    *out = a;
    return true;
}

static void writeState(QSettings& s, const QString& prefix, const GameState& st) {
    QStringList acts;
    for (const ActionStep& a : st.actions) acts << encodeStep(a);
    s.setValue(prefix + "actions", acts);
    s.setValue(prefix + "interval", st.intervalMs);
    s.setValue(prefix + "randomize", st.randomize);
    s.setValue(prefix + "jitter", st.jitterMs);
    s.setValue(prefix + "lockWindow", st.lockWindow);
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
            if (decodeStep(e, &a) && st.actions.size() < kMaxActions) st.actions << a;
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
    st.intervalMs = qBound(1, s.value(prefix + "interval", def.intervalMs).toInt(), 60000);
    st.randomize = s.value(prefix + "randomize", def.randomize).toBool();
    st.jitterMs = qBound(0, s.value(prefix + "jitter", def.jitterMs).toInt(), 1000);
    st.lockWindow = s.value(prefix + "lockWindow", def.lockWindow).toBool();
    return st;
}

GameState Games::loadState(const GameDef& g) {
    auto s = AppSettings::open();
    GameState def = defaultState(g);
    GameState st = (s->contains(gkey(g.id, "state/interval")) || s->contains(gkey(g.id, "state/actions")))
                       ? readState(*s, gkey(g.id, "state/"), def) : def;
    st.intervalMs = qMax(st.intervalMs, g.minIntervalMs);
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

QStringList Games::profiles(const QString& gameId) {
    auto s = AppSettings::open();
    s->beginGroup(gkey(gameId, "profiles"));
    QStringList names = s->childGroups();
    s->endGroup();
    names.sort(Qt::CaseInsensitive);
    return names;
}

// Имя профиля как оно уже записано (без учёта регистра). В реестре Windows
// ключи не различают регистр: «Fast» и «fast» — один профиль, и записывать
// надо под тем именем, что уже есть, иначе профиль «потеряется».
static QString storedProfileName(const QString& gameId, const QString& name) {
    const QString n = name.trimmed();
    for (const QString& p : Games::profiles(gameId))
        if (p.compare(n, Qt::CaseInsensitive) == 0) return p;
    return n;
}

bool Games::profileExists(const QString& gameId, const QString& name) {
    return profiles(gameId).contains(name.trimmed(), Qt::CaseInsensitive);
}

GameState Games::loadProfile(const QString& gameId, const QString& name, int* mode) {
    auto s = AppSettings::open();
    bool ok = false;
    GameDef g = get(gameId, &ok);
    const QString prefix = gkey(gameId, "profiles/" + storedProfileName(gameId, name) + "/");
    GameState st = readState(*s, prefix, ok ? defaultState(g) : GameState{});
    if (ok) st.intervalMs = qMax(st.intervalMs, g.minIntervalMs);
    if (mode) {
        const int m = s->value(prefix + "mode", -1).toInt();
        *mode = (m >= 0 && m <= 2) ? m : -1;
    }
    return st;
}

void Games::saveProfile(const QString& gameId, const QString& name, const GameState& st, int mode) {
    if (!validProfileName(name, nullptr)) return;
    const QString stored = storedProfileName(gameId, name);
    auto s = AppSettings::open();
    const QString prefix = gkey(gameId, "profiles/" + stored + "/");
    writeState(*s, prefix, st);
    if (mode >= 0 && mode <= 2) s->setValue(prefix + "mode", mode);
    else s->remove(prefix + "mode");
    s->sync();
}

void Games::removeProfile(const QString& gameId, const QString& name) {
    const QString stored = storedProfileName(gameId, name);
    auto s = AppSettings::open();
    s->remove(gkey(gameId, "profiles/" + stored));
    if (s->value(gkey(gameId, "activeProfile")).toString().compare(stored, Qt::CaseInsensitive) == 0)
        s->remove(gkey(gameId, "activeProfile"));
    s->sync();
}

bool Games::renameProfile(const QString& gameId, const QString& from, const QString& to) {
    const QString a = storedProfileName(gameId, from), b = to.trimmed();
    if (!validProfileName(b, nullptr) || !profileExists(gameId, a)) return false;
    if (a == b) return true;
    const bool caseOnly = a.compare(b, Qt::CaseInsensitive) == 0;   // «fast» -> «Fast»
    if (!caseOnly && profileExists(gameId, b)) return false;
    int mode = -1;
    const GameState st = loadProfile(gameId, a, &mode);
    const HotkeySpec key = profileKey(gameId, a);
    const bool wasActive = activeProfile(gameId) == a;
    removeProfile(gameId, a);           // сначала убрать: в реестре Windows это тот же ключ
    {
        auto s = AppSettings::open();
        const QString prefix = gkey(gameId, "profiles/" + b + "/");
        writeState(*s, prefix, st);
        if (mode >= 0 && mode <= 2) s->setValue(prefix + "mode", mode);
        if (key.isValid()) s->setValue(prefix + "hotkey", key.toString());
        s->sync();
    }
    if (wasActive) setActiveProfile(gameId, b);
    return true;
}

// ---------------------------------------------------------------------------
// Клавиши игры
// ---------------------------------------------------------------------------
HotkeySpec Games::defaultStartKey() {
    auto s = AppSettings::open();
    HotkeySpec sp = HotkeySpec::parse(s->value("hotkey", "f6").toString());
    return sp.isValid() ? sp : HotkeySpec::parse("f6");
}

void Games::setDefaultStartKey(const HotkeySpec& sp) {
    if (!sp.isValid()) return;
    auto s = AppSettings::open();
    s->setValue("hotkey", sp.toString());
}

HotkeySpec Games::startKey(const QString& gameId) {
    auto s = AppSettings::open();
    const HotkeySpec sp = HotkeySpec::parse(s->value(gkey(gameId, "hotkey")).toString());
    return sp.isValid() ? sp : defaultStartKey();
}

void Games::setStartKey(const QString& gameId, const HotkeySpec& sp) {
    if (gameId.isEmpty() || !sp.isValid()) return;
    auto s = AppSettings::open();
    s->setValue(gkey(gameId, "hotkey"), sp.toString());
    s->sync();
}

bool Games::armed(const QString& gameId) {
    auto s = AppSettings::open();
    return s->value(gkey(gameId, "armed"), false).toBool();
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

HotkeySpec Games::profileKey(const QString& gameId, const QString& profile) {
    if (!profileExists(gameId, profile)) return HotkeySpec{};
    auto s = AppSettings::open();
    return HotkeySpec::parse(s->value(gkey(gameId, "profiles/" + storedProfileName(gameId, profile) + "/hotkey")).toString());
}

void Games::setProfileKey(const QString& gameId, const QString& profile, const HotkeySpec& sp) {
    if (!profileExists(gameId, profile)) return;
    auto s = AppSettings::open();
    const QString k = gkey(gameId, "profiles/" + storedProfileName(gameId, profile) + "/hotkey");
    if (sp.isValid()) s->setValue(k, sp.toString());
    else s->remove(k);
    s->sync();
}

QString Games::activeProfile(const QString& gameId) {
    auto s = AppSettings::open();
    const QString n = s->value(gkey(gameId, "activeProfile")).toString();
    if (n.isEmpty()) return QString();
    s->beginGroup(gkey(gameId, "profiles"));
    const QStringList groups = s->childGroups();
    s->endGroup();
    for (const QString& g : groups)
        if (g.compare(n, Qt::CaseInsensitive) == 0) return g;
    return QString();
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
    s->sync();
}
