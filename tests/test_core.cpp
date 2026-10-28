// Юнит-тесты ядра (без интерфейса). Запуск: test_core
// Собирается и под Linux, и под Windows (проверяется в Wine).
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QSet>
#include <QRegularExpression>
#include <QFile>
#include <QtEndian>
#include "i18n.h"
#include "sounds.h"
#include "framesync.h"
#include "cardlayout.h"
#include <thread>
#include <cstdio>
#include "keys.h"
#include "winmatch.h"
#include "games.h"
#include "clicker.h"
#include "updates.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static WindowInfo win(const QString& title, QStringList cands, bool known = true) {
    WindowInfo w; w.valid = true; w.title = title; w.candidates = cands;
    w.processKnown = known; w.process = cands.value(0); return w;
}

static void testKeys() {
    // имена уникальны, каждое находится по имени
    QSet<QString> names;
    for (const KeyDef& k : Keys::all()) {
        const QString n = QString::fromLatin1(k.name);
        CHECK(!names.contains(n));
        names.insert(n);
        CHECK(Keys::byName(n) == &k);
        CHECK(Keys::byName(n.toUpper()) == &k);
        CHECK(k.winVK != 0);
#ifndef _WIN32
        CHECK(k.x11Sym != 0);
#endif
        // захват клавиши: Qt-код -> та же клавиша
        CHECK(Keys::byQtKey(k.qtKey, k.qtKeypad) == &k);
    }
    // F1..F24: правильные коды Windows, F1-F12 в списке действий
    const QStringList act = Keys::actionKeyNames();
    for (int i = 1; i <= 24; ++i) {
        const KeyDef* k = Keys::byName(QString("f%1").arg(i));
        CHECK(k != nullptr);
        if (!k) continue;
        CHECK(k->winVK == 0x70 + i - 1);
        CHECK(k->qtKey == Qt::Key_F1 + i - 1);
        CHECK(k->hotkeyAlone);
        CHECK(act.contains(QString("f%1").arg(i)) == (i <= 12));
    }
    CHECK(Keys::byName("a")->winVK == 0x41);
    CHECK(Keys::byName("z")->winVK == 0x5A);
    CHECK(Keys::byName("0")->winVK == 0x30);
    CHECK(Keys::byName("num5")->winVK == 0x65);
    CHECK(Keys::byName("up")->winExtended);
    CHECK(Keys::byName("pageup")->winExtended);
    CHECK(!Keys::byName("f3")->winExtended);
    CHECK(!Keys::byName("space")->winExtended);
    CHECK(Keys::byName("nosuchkey") == nullptr);
    // все буквы и цифры есть в списке действий
    for (char c = 'a'; c <= 'z'; ++c) CHECK(act.contains(QString(QChar(c))));
    for (char c = '0'; c <= '9'; ++c) CHECK(act.contains(QString(QChar(c))));
    // русская раскладка: "А" (физическая F) -> f, "Ф" -> a
    CHECK(Keys::fromKeyEvent(0x0410, false, 0) == Keys::byName("f"));
    CHECK(Keys::fromKeyEvent(0x0424, false, 0) == Keys::byName("a"));
    CHECK(Keys::fromKeyEvent(Qt::Key_Exclam, false, 0) == Keys::byName("1"));
    CHECK(Keys::fromKeyEvent(Qt::Key_Enter, true, 0) == Keys::byName("enter"));
    CHECK(Keys::fromKeyEvent(Qt::Key_5, true, 0) == Keys::byName("num5"));
    CHECK(Keys::fromKeyEvent(Qt::Key_VolumeUp, false, 0) == nullptr);
#ifdef _WIN32
    CHECK(Keys::fromKeyEvent(0x0410, false, 0x46) == Keys::byName("f"));
#endif
}

static void testHotkeys() {
    HotkeySpec a = HotkeySpec::parse("ctrl+alt+f7");
    CHECK(a.isValid());
    CHECK(a.mods == (ModCtrl | ModAlt));
    CHECK(a.toString() == "ctrl+alt+f7");
    CHECK(a.display() == "Ctrl+Alt+F7");
    CHECK(HotkeySpec::parse(a.toString()) == a);
    CHECK(HotkeySpec::parse(" Shift + F12 ").toString() == "shift+f12");
    CHECK(HotkeySpec::parse("f6").isGlobalSafe());
    CHECK(HotkeySpec::parse("pageup").isGlobalSafe());
    CHECK(HotkeySpec::parse("num5").isGlobalSafe());
    // 1.7: любая клавиша — буквы и цифры тоже (они «сквозные»)
    CHECK(HotkeySpec::parse("a").isGlobalSafe());
    CHECK(HotkeySpec::parse("shift+a").isGlobalSafe());
    CHECK(HotkeySpec::parse("ctrl+a").isGlobalSafe());
    CHECK(HotkeySpec::parse("alt+1").isGlobalSafe());
    CHECK(HotkeySpec::parse("space").isGlobalSafe());
    CHECK(HotkeySpec::parse("slash").isGlobalSafe());
    CHECK(!HotkeySpec::parse("ctrl+shift").isGlobalSafe()); // только модификаторы
    HotkeySpec mod; mod.key = "shift";
    CHECK(!mod.isGlobalSafe());
    // сквозные: буква/цифра/знак без Ctrl/Alt/Win — нажатие доходит до программ
    CHECK(HotkeySpec::parse("z").passthrough());
    CHECK(HotkeySpec::parse("5").passthrough());
    CHECK(HotkeySpec::parse("shift+z").passthrough());
    CHECK(HotkeySpec::parse("space").passthrough());
    CHECK(HotkeySpec::parse("num5").passthrough());
    CHECK(HotkeySpec::parse("up").passthrough());
    CHECK(HotkeySpec::parse("bracketleft").passthrough());
    CHECK(!HotkeySpec::parse("ctrl+z").passthrough());
    CHECK(!HotkeySpec::parse("alt+5").passthrough());
    CHECK(!HotkeySpec::parse("f6").passthrough());
    CHECK(!HotkeySpec::parse("pageup").passthrough());
    CHECK(!HotkeySpec::parse("shift+f6").passthrough());
    // «не пропускать в игру»: только сквозные сами по себе (мышь, буква без Ctrl/Alt/Win), не сочетания
    {
        HotkeySpec m4 = HotkeySpec::parse("mouse:8"), z = HotkeySpec::parse("z"), f6 = HotkeySpec::parse("f6");
        HotkeySpec chord = HotkeySpec::parse("tab+f"), cz = HotkeySpec::parse("ctrl+z");
        CHECK(m4.canBlock() && z.canBlock() && HotkeySpec::parse("shift+z").canBlock());
        CHECK(!f6.canBlock() && !chord.canBlock() && !cz.canBlock() && !HotkeySpec{}.canBlock());
        CHECK(m4.passthrough() && z.passthrough());
        m4.block = z.block = f6.block = chord.block = true;
        CHECK(!m4.passthrough() && !z.passthrough() && !f6.passthrough());
        CHECK(chord.passthrough());                                  // сочетание — всегда сквозное
        CHECK(m4 == HotkeySpec::parse("mouse:8") && m4.toString() == "mouse:8");   // в == и строке block нет
    }
    // вся клавиатура для выбора: без Shift/Ctrl/Alt, все годятся, у всех есть группа
    const QStringList all = Keys::hotkeyKeyNames();
    CHECK(all.size() > 90);
    CHECK(!all.contains("shift") && !all.contains("ctrl") && !all.contains("alt"));
    for (const QString& k : all) {
        HotkeySpec p; p.key = k;
        CHECK(p.isGlobalSafe());
        CHECK(!Keys::groupOf(k).isEmpty());
        CHECK(HotkeySpec::parse(p.toString()) == p);
    }
    CHECK(Keys::groupOf("q") == "Буквы");
    CHECK(Keys::groupOf("7") == "Цифры");
    CHECK(Keys::groupOf("f13") == "F-клавиши");
    CHECK(Keys::groupOf("num7") == "Цифровой блок");
    CHECK(Keys::groupOf("pageup") == "Навигация");
    CHECK(Keys::groupOf("slash") == "Знаки");
    CHECK(Keys::groupOf("space") == "Основные");
    // знаки и русская раскладка
    CHECK(Keys::fromKeyEvent(Qt::Key_BracketLeft, false, 0) == Keys::byName("bracketleft"));
    CHECK(Keys::fromKeyEvent(0x0425, false, 0) == Keys::byName("bracketleft"));   // «Х»
    CHECK(Keys::fromKeyEvent(0x0411, false, 0) == Keys::byName("comma"));         // «Б»
    CHECK(Keys::fromKeyEvent(Qt::Key_Question, false, 0) == Keys::byName("slash")); // Shift+/
    CHECK(Keys::fromKeyEvent(Qt::Key_Asterisk, false, 0) == Keys::byName("8"));     // Shift+8, не Num *
    CHECK(Keys::fromKeyEvent(Qt::Key_Asterisk, true, 0) == Keys::byName("nummultiply"));
    CHECK(Keys::fromKeyEvent(Qt::Key_Plus, true, 0) == Keys::byName("numadd"));
    CHECK(Keys::byName("minus")->winVK == 0xBD);
    CHECK(Keys::byName("numdivide")->winExtended);
    CHECK(!HotkeySpec::parse("").isValid());
    CHECK(!HotkeySpec::parse("ctrl+").isValid());
    CHECK(!HotkeySpec::parse("hyper+f1").isValid());
    CHECK(!HotkeySpec::parse("ctrl+nosuch").isValid());
    for (const QString& k : Keys::hotkeyPresetNames()) {
        HotkeySpec p; p.key = k;
        CHECK(p.isValid());
        CHECK(p.isGlobalSafe());
    }
}

static void testMatching() {
    const WindowRule genshin{{"GenshinImpact.exe", "YuanShen.exe"}, {"Genshin Impact", "原神"}};
    // Windows: процесс + заголовок
    CHECK(matchesRule(genshin, win("Genshin Impact", {"GenshinImpact.exe"})));
    CHECK(matchesRule(genshin, win("原神", {"YuanShen.exe"})));
    CHECK(matchesRule(genshin, win("Genshin Impact", {"genshinimpact.EXE"})));   // регистр
    // вкладка браузера с "Genshin Impact" в заголовке — НЕ игра
    CHECK(!matchesRule(genshin, win("Genshin Impact Wiki — Mozilla Firefox", {"firefox.exe"})));
    CHECK(!matchesRule(genshin, win("Genshin Impact - YouTube", {"chrome.exe"})));
    // лаунчер HoYoPlay — не игра
    CHECK(!matchesRule(genshin, win("HoYoPlay", {"HYP.exe"})));
    // процесс узнать нельзя (античит не дал) — решает заголовок
    CHECK(matchesRule(genshin, win("Genshin Impact", {}, false)));
    CHECK(!matchesRule(genshin, win("Firefox", {}, false)));
    // Linux/Proton: класс окна "genshinimpact.exe", argv0 "GenshinImpact.exe", comm обрезан
    CHECK(matchesRule(genshin, win("Genshin Impact",
        {"GenshinImpact.exe", "wine64-preloader", "GenshinImpact.e", "genshinimpact.exe", "steam_proton"})));
    CHECK(matchesRule(genshin, win("Genshin Impact", {"GenshinImpact.e"})));      // только comm
    CHECK(!matchesRule(genshin, win("Genshin Impact", {"steam_proton"})));

    const WindowRule mc{{"javaw.exe", "java.exe", "java"}, {"Minecraft", "Майнкрафт"}};
    CHECK(matchesRule(mc, win("Minecraft 1.20.1 - Singleplayer", {"javaw.exe"})));
    CHECK(matchesRule(mc, win("Minecraft* 1.21", {"java"})));
    CHECK(matchesRule(mc, win("Vortex | Майнкрафт сборка", {"java"})));
    CHECK(matchesRule(mc, win("vortex | МАЙНКРАФТ сборка", {"java"})));         // кириллица без регистра
    CHECK(!matchesRule(mc, win("IntelliJ IDEA", {"java"})));                     // другая java-программа
    CHECK(!matchesRule(mc, win("Minecraft Wiki — Firefox", {"firefox"})));

    // только заголовок
    const WindowRule t{{}, {"KeyLogger"}};
    CHECK(matchesRule(t, win("KeyLogger test", {"x"})));
    CHECK(!matchesRule(t, win("Other", {"x"})));
    // только процесс, ".exe" можно не писать
    const WindowRule p{{"notepad"}, {}};
    CHECK(matchesRule(p, win("Untitled - Notepad", {"notepad.exe"})));
    CHECK(!matchesRule(p, win("x", {"notepad++.exe"})));
    // пустое правило и невалидное окно
    CHECK(!matchesRule(WindowRule{}, win("anything", {"a"})));
    CHECK(!matchesRule(genshin, WindowInfo{}));
    CHECK(!matchesRule(WindowRule{{" "}, {""}}, win("x", {"x"})));
}

static void testGames() {
    // отдельное место хранения, чтобы не трогать настоящие настройки
    AppSettings::setScope("AutoQTest", QString("t%1").arg(QCoreApplication::applicationPid()));
    { auto s = AppSettings::open(); s->clear(); s->sync(); }

    // 1.7: новая установка — список игр пуст, игры добавляются из шаблонов
    CHECK(Games::list().isEmpty());
    CHECK(Games::currentId().isEmpty());
    CHECK(!Games::contains("genshin"));
    for (const GameDef& d : Games::builtinDefaults()) CHECK(Games::addBuiltin(d.id));
    CHECK(Games::addBuiltin("genshin"));                  // повторно — без дубля
    CHECK(!Games::addBuiltin("nosuchgame"));
    const QList<GameDef> games = Games::list();
    CHECK(games.size() == Games::builtinDefaults().size());
    CHECK(Games::currentId() == "genshin");
    bool ok = false;
    GameDef g = Games::get("genshin", &ok);
    CHECK(ok);
    CHECK(g.mode == GameMode::Sequence);              // Genshin: действия по очереди
    CHECK(Games::get("minecraft").mode == GameMode::Combo);   // Minecraft: комбинация
    // Genshin по умолчанию: скип диалогов ЛКМ -> F, два действия
    GameState gs = Games::loadState(g);
    CHECK(gs.actions.size() == 2);
    CHECK(gs.actions[0].mouse && gs.actions[0].button == 1);
    CHECK(!gs.actions[1].mouse && gs.actions[1].key == "f");
    CHECK(gs.lockWindow);
    // у каждого пресета валидные клавиши, интервал не ниже минимума игры
    for (const GameDef& gg : games)
        for (const GamePreset& pr : Games::presets(gg)) {
            CHECK(!pr.state.actions.isEmpty());
            for (const ActionStep& a : pr.state.actions)
                CHECK(a.mouse ? MouseButtons::valid(a.button) : Keys::byName(a.key) != nullptr);
            CHECK(!pr.name.isEmpty());
            CHECK(Games::defaultState(gg).intervalMs >= gg.minIntervalMs);
        }
    // F3+A в Minecraft
    bool hasF3A = false;
    for (const GamePreset& pr : Games::presets(Games::get("minecraft")))
        if (pr.state.actions.size() == 2 && pr.state.actions[0].key == "f3" && pr.state.actions[1].key == "a") hasF3A = true;
    CHECK(hasF3A);

    // состояние игры сохраняется и читается (как после перезапуска)
    GameState st = gs; st.intervalMs = 333; st.actions[0].key = "f11"; st.actions[0].mouse = false; st.jitterMs = 7;
    st.actions[0].holdMs = 40; st.actions[0].afterMs = 120;
    { ActionStep m4; m4.mouse = true; m4.button = 8; m4.holdMs = 15; st.actions << m4; }   // третье — Mouse 4
    Games::saveState("genshin", st);
    CHECK(Games::loadState(Games::get("genshin")) == st);
    // минимум интервала игры соблюдается при чтении; у нескольких действий по очереди
    // «повтор через» — пауза между клавишами: от 0
    st.intervalMs = 5; Games::saveState("genshin", st);
    CHECK(Games::loadState(Games::get("genshin")).intervalMs == 5);
    { GameState one = st; one.actions = {st.actions[0]}; Games::saveState("genshin", one); }
    CHECK(Games::loadState(Games::get("genshin")).intervalMs == 20);
    Games::saveState("genshin", st);
    // у Minecraft свои настройки
    CHECK(!(Games::loadState(Games::get("minecraft")) == st));

    // профили — у каждой игры свои
    QString why;
    CHECK(Games::validProfileName("Мой профиль", &why));
    CHECK(!Games::validProfileName("a/b", &why));
    CHECK(!Games::validProfileName("a\\b", &why));
    CHECK(!Games::validProfileName("   ", &why));
    GameState pst = Games::defaultState(g); pst.actions[1].key = "e"; pst.intervalMs = 250;
    Games::saveProfile("genshin", "Скип быстрый", pst);
    Games::saveProfile("genshin", "a/b", pst);              // недопустимое имя — не сохраняется
    CHECK(Games::profiles("genshin") == QStringList{"Скип быстрый"});
    CHECK(Games::profiles("minecraft").isEmpty());
    CHECK(Games::profileExists("genshin", "Скип быстрый"));
    CHECK(Games::loadProfile("genshin", "Скип быстрый") == pst);
    Games::removeProfile("genshin", "Скип быстрый");
    CHECK(Games::profiles("genshin").isEmpty());

    // своя игра
    GameDef c; c.id = Games::newCustomId(); c.name = "Моя игра"; c.mode = GameMode::Combo;
    c.processes = Games::splitList("game.exe, Game2.exe ; game.exe"); c.titles = {"My Game"};
    CHECK(c.processes == QStringList({"game.exe", "Game2.exe"}));
    Games::save(c);
    CHECK(Games::get(c.id).name == "Моя игра");
    CHECK(Games::get(c.id).mode == GameMode::Combo);
    CHECK(Games::nameTaken("моя ИГРА", QString()));
    CHECK(!Games::nameTaken("Моя игра", c.id));
    CHECK(Games::newCustomId() != c.id);
    Games::setCurrentId(c.id);
    CHECK(Games::currentId() == c.id);
    CHECK(Games::remove(c.id));
    CHECK(Games::currentId() != c.id);
    // встроенную игру можно поправить и вернуть к стандартным правилам
    GameDef mc = Games::get("minecraft");
    mc.titles << "Vortex";
    Games::save(mc);
    CHECK(Games::get("minecraft").titles.contains("Vortex"));
    CHECK(Games::resetToDefault("minecraft"));
    CHECK(!Games::get("minecraft").titles.contains("Vortex"));
    // можно удалить все игры — список пуст, текущей игры нет
    for (const GameDef& gg : Games::list()) CHECK(Games::remove(gg.id));
    CHECK(Games::list().isEmpty());
    CHECK(Games::currentId().isEmpty());
    // удалённые встроенные игры не возвращаются сами
    CHECK(Games::list().isEmpty());
    // и снова добавляются из шаблона — с чистыми настройками
    CHECK(Games::addBuiltin("minecraft"));
    CHECK(Games::list().size() == 1);
    CHECK(Games::currentId() == "minecraft");
    CHECK(Games::get("minecraft").titles == Games::builtinDefault("minecraft").titles);

    // --- перенос настроек версии 1.3 (категории) ---
    { auto s = AppSettings::open(); s->clear();
      s->setValue("category", 1);              // была выбрана Minecraft
      s->setValue("action1", 1); s->setValue("key1", "f3"); s->setValue("mouse1", 0);
      s->setValue("useSlot2", true); s->setValue("action2", 1); s->setValue("key2", "a");
      s->setValue("comboMode", 1); s->setValue("interval", 7); s->setValue("randomize", true);
      s->setValue("jitter", 27); s->setValue("lockWindow", true);
      s->setValue("windowName", "Vortex | Майнкрафт сборка");
      s->setValue("hotkey", "pageup"); s->setValue("theme", 1);
      s->setValue("profiles/FFf/action1", 0); s->setValue("profiles/FFf/mouse1", 1);
      s->setValue("profiles/FFf/key1", "f"); s->setValue("profiles/FFf/interval", 50);
      s->sync(); }
    Games::migrate();
    {
        GameDef m = Games::get("minecraft");
        GameState ms = Games::loadState(m);
        CHECK(Games::currentId() == "minecraft");
        CHECK(Games::list().size() == 1);                 // в списке только игра из старой версии
        CHECK(ms.actions.size() == 2);
        CHECK(!ms.actions[0].mouse && ms.actions[0].key == "f3");
        CHECK(!ms.actions[1].mouse && ms.actions[1].key == "a");
        CHECK(ms.intervalMs == 7);
        CHECK(ms.randomize && ms.jitterMs == 27);
        CHECK(ms.lockWindow);
        CHECK(m.titles.contains("Vortex | Майнкрафт сборка"));
        CHECK(Games::profiles("minecraft") == QStringList{"FFf"});
        GameState fp = Games::loadProfile("minecraft", "FFf");
        CHECK(fp.actions.value(0).mouse && fp.actions.value(0).button == 3 && fp.intervalMs == 50);
        auto s = AppSettings::open();
        CHECK(s->value("version").toInt() == 2);
        CHECK(!s->contains("action1") && !s->contains("windowName") && !s->contains("category"));
        CHECK(s->value("hotkey").toString() == "pageup");   // глобальные настройки не тронуты
        CHECK(s->value("theme").toInt() == 1);
        CHECK(!s->childGroups().contains("profiles"));
    }
    // повторный перенос ничего не ломает
    Games::saveState("minecraft", Games::defaultState(Games::get("minecraft")));
    Games::migrate();
    CHECK(Games::loadState(Games::get("minecraft")) == Games::defaultState(Games::get("minecraft")));

    { auto s = AppSettings::open(); s->clear(); s->sync(); }
}

// --- 1.5: значки игр, время захвата, перенос настроек со старого имени ---
static void testV15() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "v15_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    for (const GameDef& d : Games::builtinDefaults()) Games::addBuiltin(d.id);

    // у всех встроенных игр свой значок и цвет
    QSet<QString> pairs;
    for (const GameDef& d : Games::builtinDefaults()) {
        CHECK(!d.icon.isEmpty());
        CHECK(d.color.startsWith('#') && d.color.size() == 7);
        CHECK(d.image.isEmpty());
        pairs.insert(d.icon);
    }
    CHECK(pairs.size() == Games::builtinDefaults().size());   // значки не повторяются
    CHECK(Games::get("genshin").icon == "compass");
    CHECK(Games::get("minecraft").icon == "cube");
    CHECK(Games::get("roblox").icon == "users");

    // сохранить/прочитать значок, цвет и свою картинку
    GameDef g = Games::get("zzz");
    g.icon = "rocket"; g.color = "#6FD3F2"; g.image = QByteArray("\x89PNG fake", 9);
    Games::save(g);
    GameDef r = Games::get("zzz");
    CHECK(r.icon == "rocket" && r.color == "#6FD3F2" && r.image == g.image);
    g.image.clear();
    Games::save(g);
    CHECK(Games::get("zzz").image.isEmpty());
    // «По умолчанию» возвращает встроенный значок
    Games::resetToDefault("zzz");
    CHECK(Games::get("zzz").icon == "city");
    // мусор в цвете -> встроенный цвет; у своей игры без значка — геймпад
    { auto s = AppSettings::open(); s->setValue("games/wuwa/color", "red"); s->sync(); }
    CHECK(Games::get("wuwa").color == "#6FD3F2");
    GameDef c; c.id = Games::newCustomId(); c.name = "Моя игра"; c.titles = {"My"};
    Games::save(c);
    { auto s = AppSettings::open(); s->remove("games/" + c.id + "/icon"); s->remove("games/" + c.id + "/color"); s->sync(); }
    CHECK(Games::get(c.id).icon == "gamepad");
    CHECK(Games::get(c.id).color == "#FF5A66");
    // игра из версии 1.4 (без полей значка) получает встроенный значок
    { auto s = AppSettings::open(); s->remove("games/starrail/icon"); s->remove("games/starrail/color"); s->sync(); }
    CHECK(Games::get("starrail").icon == "train");
    CHECK(Games::get("starrail").color == "#B9A8FF");

    // время на переключение: только 3/5/7/10, по умолчанию 3
    CHECK(Prefs::captureSeconds() == 3);
    for (int v : {5, 7, 10, 3}) { Prefs::setCaptureSeconds(v); CHECK(Prefs::captureSeconds() == v); }
    Prefs::setCaptureSeconds(4);                       // своё время (1.10) — можно любое 1–120
    CHECK(Prefs::captureSeconds() == 4);
    Prefs::setCaptureSeconds(0);                       // вне диапазона — игнор
    CHECK(Prefs::captureSeconds() == 4);
    Prefs::setCaptureSeconds(3);
    CHECK(Prefs::captureSeconds() == 3);
    CHECK(Prefs::closeToTray() && Prefs::notifications() && !Prefs::startMini() &&
          Prefs::stopOnGameClose() && Prefs::mouseOnlyOverGame());
    Prefs::setMouseOnlyOverGame(false);
    CHECK(!Prefs::mouseOnlyOverGame());
    Prefs::setMouseOnlyOverGame(true);
    { auto s = AppSettings::open(); s->setValue("captureSeconds", 999); s->sync(); }
    CHECK(Prefs::captureSeconds() == 3);
    { auto s = AppSettings::open(); s->setValue("captureSeconds", "abc"); s->sync(); }
    CHECK(Prefs::captureSeconds() == 3);

    // настройки формата версии 2 -> база игр
    AppSettings::setScope("AutoQTest", "imp_" + pid);
    {
        auto s = AppSettings::open();
        s->clear();
        s->setValue("hotkey", "pageup");
        s->setValue("version", 2);
        s->setValue("currentGame", "minecraft");
        s->setValue("games/order", QStringList{"genshin", "minecraft"});
        s->setValue("games/knownBuiltins", QStringList{"genshin", "minecraft", "starrail", "zzz",
                                                       "wuwa", "minecraft_bedrock", "roblox"});
        s->setValue("games/minecraft/name", "Minecraft (Java)");
        s->setValue("games/minecraft/mode", 1);
        s->setValue("games/minecraft/titles", QStringList{"Minecraft", "Vortex | Майнкрафт сборка"});
        s->setValue("games/minecraft/state/interval", 7);
        s->setValue("games/minecraft/profiles/FFf/interval", 9);
        s->setValue("games/genshin/name", "Genshin Impact");
        s->sync();
    }
    Games::migrate();
    CHECK(Games::currentId() == "minecraft");
    CHECK(Games::list().size() == 1);                  // Genshin не трогали — убрана из списка
    CHECK(!Games::contains("genshin"));
    CHECK(Games::get("minecraft").titles.contains("Vortex | Майнкрафт сборка"));
    CHECK(Games::get("minecraft").icon == "cube");     // значок добавился сам
    CHECK(Games::loadState(Games::get("minecraft")).intervalMs == 7);
    CHECK(Games::profileExists("minecraft", "FFf"));
    CHECK(Games::loadProfile("minecraft", "FFf").intervalMs == 9);
    AppSettings::setScope("AutoQTest", "empty_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    Games::migrate();
    CHECK(Games::list().isEmpty());

    for (const QString& sc : {"v15_" + pid, "imp_" + pid, "empty_" + pid}) {
        AppSettings::dropScope("AutoQTest", sc);
    }
}

// --- режимы: по очереди / вместе / зажать ---
static void testModes() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "modes_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    for (const GameDef& d : Games::builtinDefaults()) Games::addBuiltin(d.id);
    CHECK(gameModeFromInt(0) == GameMode::Sequence);
    CHECK(gameModeFromInt(1) == GameMode::Combo);
    CHECK(gameModeFromInt(2) == GameMode::Hold);
    CHECK(gameModeFromInt(7) == GameMode::Sequence);      // мусор -> по очереди
    CHECK(gameModeFromInt(-1) == GameMode::Sequence);
    CHECK(gameModeName(GameMode::Hold) == "Зажать");
    CHECK(gameModeName(GameMode::Combo) == "Вместе");
    // режим сохраняется у игры
    GameDef g = Games::get("genshin");
    g.mode = GameMode::Hold;
    Games::save(g);
    CHECK(Games::get("genshin").mode == GameMode::Hold);
    g.mode = GameMode::Combo;
    Games::save(g);
    CHECK(Games::get("genshin").mode == GameMode::Combo);
    Games::resetToDefault("genshin");
    CHECK(Games::get("genshin").mode == GameMode::Sequence);
    // у каждого пресета задан режим; у Minecraft есть «Зажать ЛКМ», у Genshin скип — по очереди
    bool mcHold = false, genshinSkipSeq = false;
    for (const GameDef& gg : Games::list())
        for (const GamePreset& p : Games::presets(gg)) {
            CHECK(p.mode >= 0 && p.mode <= 2);
            if (gg.id == "minecraft" && p.mode == 2 && p.state.actions.value(0).mouse && p.state.actions.value(0).button == 1) mcHold = true;
            if (gg.id == "genshin" && p.name.startsWith("Скип") && p.mode == 0) genshinSkipSeq = true;
            if (p.mode == 2) CHECK(!p.state.randomize);
        }
    CHECK(mcHold);
    CHECK(genshinSkipSeq);
    // F3+A — «вместе»
    bool f3a = false;
    for (const GamePreset& p : Games::presets(Games::get("minecraft")))
        if (p.name.startsWith("F3+A")) { f3a = true; CHECK(p.mode == 1); }
    CHECK(f3a);
    // своя игра тоже получает «Зажать ЛКМ»
    GameDef c; c.id = "customX"; c.name = "X";
    bool customHold = false;
    for (const GamePreset& p : Games::presets(c)) if (p.mode == 2) customHold = true;
    CHECK(customHold);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
}

// --- 1.7: список игр пользователя, профили, привязка к окну ---
static void testV17() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "v17_" + pid);
    {   // настройки версии 1.6: все встроенные игры были в списке
        auto s = AppSettings::open(); s->clear();
        s->setValue("version", 2);
        s->setValue("games/order", QStringList{"genshin", "starrail", "zzz", "wuwa", "minecraft",
                                                "minecraft_bedrock", "roblox", "custom1"});
        s->setValue("games/knownBuiltins", QStringList{"genshin", "starrail", "zzz", "wuwa", "minecraft",
                                                        "minecraft_bedrock", "roblox"});
        for (const GameDef& d : Games::builtinDefaults()) {
            s->setValue("games/" + d.id + "/name", d.name);
            s->setValue("games/" + d.id + "/mode", static_cast<int>(d.mode));
            s->setValue("games/" + d.id + "/processes", d.processes);
            s->setValue("games/" + d.id + "/titles", d.titles);
            s->setValue("games/" + d.id + "/icon", d.icon);
            s->setValue("games/" + d.id + "/color", d.color);
        }
        s->setValue("games/genshin/state/interval", 120);          // пользовались
        s->setValue("games/zzz/profiles/P1/interval", 90);         // есть профиль
        s->setValue("games/wuwa/titles", QStringList{"Wuthering Waves", "Моё окно"});   // правили
        s->setValue("games/roblox/mode", 2);                       // меняли режим
        s->setValue("currentGame", "minecraft");                   // выбрана сейчас
        s->setValue("games/custom1/name", "Своя");
        s->setValue("games/custom1/titles", QStringList{"Своя"});
        s->sync();
    }
    QStringList ids;
    for (const GameDef& g : Games::list()) ids << g.id;
    CHECK(ids == (QStringList{"genshin", "zzz", "wuwa", "minecraft", "roblox", "custom1"}));
    CHECK(Games::currentId() == "minecraft");
    {
        auto s = AppSettings::open();
        CHECK(!s->childGroups().isEmpty());
        CHECK(!s->contains("games/starrail/name"));                // данные нетронутых убраны
        CHECK(s->value("games/listVersion").toInt() == 2);
    }
    CHECK(Games::loadState(Games::get("genshin")).intervalMs == 120);
    CHECK(Games::get("roblox").mode == GameMode::Hold);
    // второй раз ничего не убирается
    CHECK(Games::list().size() == 6);

    // активный профиль, режим в профиле, переименование
    GameState st = Games::defaultState(Games::get("minecraft"));
    st.intervalMs = 44;
    Games::saveProfile("minecraft", "Копать", st, static_cast<int>(GameMode::Hold));
    Games::saveProfile("minecraft", "Без режима", st);
    int mode = 99;
    CHECK(Games::loadProfile("minecraft", "Копать", &mode) == st);
    CHECK(mode == 2);
    Games::loadProfile("minecraft", "Без режима", &mode);
    CHECK(mode == -1);
    CHECK(Games::activeProfile("minecraft").isEmpty());
    Games::setActiveProfile("minecraft", "Копать");
    CHECK(Games::activeProfile("minecraft") == "Копать");
    CHECK(Games::renameProfile("minecraft", "Копать", "Копать быстро"));
    CHECK(Games::activeProfile("minecraft") == "Копать быстро");
    CHECK(!Games::profileExists("minecraft", "Копать"));
    Games::loadProfile("minecraft", "Копать быстро", &mode);
    CHECK(mode == 2);
    CHECK(!Games::renameProfile("minecraft", "Копать быстро", "Без режима"));   // имя занято
    CHECK(!Games::renameProfile("minecraft", "Нет такого", "X"));
    CHECK(!Games::renameProfile("minecraft", "Копать быстро", "a/b"));
    Games::removeProfile("minecraft", "Копать быстро");
    CHECK(Games::activeProfile("minecraft").isEmpty());          // удалённый — больше не активный
    Games::setActiveProfile("minecraft", "Без режима");
    QFile::remove(Games::profileFile("minecraft", "Без режима"));   // файл профиля удалили руками
    CHECK(Games::activeProfile("minecraft").isEmpty());          // профиля нет — нет и активного
    Games::setActiveProfile("minecraft", "");
    CHECK(Games::activeProfile("minecraft").isEmpty());

    // привязка к окну: без выбранного окна при включённой привязке — ни одного нажатия
    ClickerConfig c;
    c.rule = WindowRule{{"javaw.exe", "java"}, {"Minecraft"}};
    WindowInfo game = win("Minecraft 1.21", {"java"});
    game.handle = 0x400001; game.pid = 777;
    c.lockToWindow = false;
    CHECK(clickAllowed(c, WindowInfo{}));                         // привязка выключена — везде
    c.lockToWindow = true;
    CHECK(!clickAllowed(c, game));                                // окно не выбрано
    c.boundHandle = 0x400001; c.boundPid = 777;
    CHECK(clickAllowed(c, game));
    WindowInfo other = win("Firefox", {"firefox"}); other.handle = 0x500001; other.pid = 900;
    CHECK(!clickAllowed(c, other));
    WindowInfo second = game; second.handle = 0x400099;             // второе окно той же игры
    CHECK(clickAllowed(c, second));
    WindowInfo restarted = game; restarted.handle = 0x600001; restarted.pid = 1234;   // игру перезапустили
    CHECK(!clickAllowed(c, restarted));
    WindowInfo samePidOther = other; samePidOther.pid = 777;          // то же приложение, не окно игры
    CHECK(!clickAllowed(c, samePidOther));
    CHECK(!clickAllowed(c, WindowInfo{}));

    { auto s = AppSettings::open(); s->clear(); s->sync(); }
}

// 1.9: профили без учёта регистра (реестр Windows), заголовок неизвестен (macOS)
static void testV19() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "v19_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    Games::addBuiltin("genshin");
    GameState st; st.intervalMs = 150;
    Games::saveProfile("genshin", "Fast", st, 1);
    CHECK(Games::profileExists("genshin", "fast"));
    st.intervalMs = 222;
    Games::saveProfile("genshin", "FAST", st, 1);           // тот же профиль, не второй
    CHECK(Games::profiles("genshin") == QStringList{"Fast"});
    CHECK(Games::loadProfile("genshin", "fast").intervalMs == 222);
    Games::setActiveProfile("genshin", "fast");
    CHECK(Games::activeProfile("genshin") == "Fast");
    CHECK(Games::renameProfile("genshin", "Fast", "fast"));  // смена только регистра
    CHECK(Games::profiles("genshin") == QStringList{"fast"});
    CHECK(Games::activeProfile("genshin") == "fast");
    CHECK(Games::loadProfile("genshin", "fast").intervalMs == 222);
    Games::saveProfile("genshin", "Other", st, 0);
    CHECK(!Games::renameProfile("genshin", "fast", "OTHER"));  // занято
    Games::removeProfile("genshin", "FAST");
    CHECK(!Games::profileExists("genshin", "fast"));
    CHECK(Games::activeProfile("genshin").isEmpty());

    WindowRule r; r.processes = {"java"}; r.titles = {"Minecraft"};
    WindowInfo w; w.valid = true; w.processKnown = true; w.candidates = {"java"}; w.process = "java";
    CHECK(!matchesRule(r, w));            // заголовок известен (пустой) — не совпал
    w.titleKnown = false;
    CHECK(matchesRule(r, w));             // macOS без «Записи экрана» — решает процесс
    w.candidates = {"chrome"};
    CHECK(!matchesRule(r, w));
    WindowRule onlyTitle; onlyTitle.titles = {"Minecraft"};
    CHECK(!matchesRule(onlyTitle, w));    // только заголовок, а он неизвестен — нет

    CHECK(Keys::macKeyCode("a") == 0x00 && Keys::macKeyCode("f6") == 0x61 && Keys::macKeyCode("pause") == -1);
    CHECK(Keys::byMacKeyCode(0x31) && QString(Keys::byMacKeyCode(0x31)->name) == "space");
    int withMac = 0;
    for (const KeyDef& k : Keys::all()) if (Keys::macKeyCode(k.name) >= 0) ++withMac;
    CHECK(withMac >= Keys::all().size() - 8);   // нет только F21–F24, Pause, ScrollLock, Print
    QSet<int> codes;
    for (const KeyDef& k : Keys::all()) { const int c = Keys::macKeyCode(k.name); if (c >= 0) codes.insert(c); }
    CHECK(codes.size() == withMac);            // коды не повторяются
}

// 1.11: английский интерфейс
struct AqTranslation { const char* ru; const char* en; };   // словарь — i18n_en.cpp
extern const AqTranslation kAqEnglish[];
extern const int kAqEnglishCount;

static QString unescapeC(const QString& in) {
    QString out;
    for (int i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            const QChar c = in[++i];
            out += c == 'n' ? QChar('\n') : c == 't' ? QChar('\t') : c;
        } else {
            out += in[i];
        }
    }
    return out;
}

static void testI18n() {
    CHECK(I18n::language() == I18n::Ru);
    CHECK(T("Старт") == QString::fromUtf8("Старт"));               // по умолчанию — как есть
    CHECK(I18n::fromSetting("ru") == I18n::Ru && I18n::fromSetting("en") == I18n::En);
    CHECK(I18n::dictionarySize() > 500);
    I18n::setLanguage(I18n::En);
    CHECK(T("Старт") == "Start");
    CHECK(T("Профиль «%1» сохранён.").arg("X") == QString::fromUtf8("Profile “X” saved."));
    CHECK(gameModeName(GameMode::Combo) == "Together");
    CHECK(Keys::label("space") == "Space" && Keys::label("up") == QString::fromUtf8("↑ Up"));
    CHECK(Keys::groupOf("a") == "Letters");
    CHECK(T("нет такой строки") == QString::fromUtf8("нет такой строки"));   // без перевода — исходный текст
    I18n::setLanguage(I18n::Ru);
    CHECK(Keys::label("space") == QString::fromUtf8("Пробел"));

    // у каждой строки T("…") в исходниках есть перевод
    QDir dir(QStringLiteral(AQ_SOURCE_DIR));
    if (!dir.exists("i18n_en.cpp")) { std::printf("  (исходники не найдены — проверка переводов пропущена)\n"); return; }
    // T("…" "…") — соседние строки склеиваются, как у компилятора
    const QRegularExpression re(QStringLiteral("\\bT\\(\\s*((?:\"(?:[^\"\\\\]|\\\\.)*\"\\s*)+)\\)"));
    const QRegularExpression part(QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\""));
    int found = 0;
    QStringList missing;
    for (const QString& f : dir.entryList({"*.cpp", "*.h"}, QDir::Files)) {
        if (f.startsWith("i18n")) continue;
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QString src = QString::fromUtf8(file.readAll());
        for (auto it = re.globalMatch(src); it.hasNext();) {
            QString ru;
            const QString lits = it.next().captured(1);
            for (auto p = part.globalMatch(lits); p.hasNext();) ru += unescapeC(p.next().captured(1));
            ++found;
            if (!I18n::hasTranslation(ru)) missing << f + ": " + ru;
        }
    }
    for (const QString& m : missing) std::printf("  нет перевода: %s\n", qPrintable(m));
    CHECK(found > 500);
    CHECK(missing.isEmpty());

    // и наоборот: в словаре нет строк, которых в исходниках уже нет (старые надписи)
    const QRegularExpression lit(QStringLiteral("((?:\"(?:[^\"\\\\\\n]|\\\\.)*\"\\s*)+)"));
    QSet<QString> literals;
    for (const QString& f : dir.entryList({"*.cpp", "*.h", "*.mm"}, QDir::Files)) {
        if (f.startsWith("i18n_en")) continue;
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QString src = QString::fromUtf8(file.readAll());
        for (auto it = lit.globalMatch(src); it.hasNext();) {
            QString joined;
            for (auto p = part.globalMatch(it.next().captured(1)); p.hasNext();) {
                const QString one = unescapeC(p.next().captured(1));
                literals.insert(one);
                joined += one;
            }
            literals.insert(joined);
        }
    }
    QStringList unused;
    for (int i = 0; i < kAqEnglishCount; ++i) {
        const QString ru = QString::fromUtf8(kAqEnglish[i].ru);
        if (!literals.contains(ru)) unused << ru;
    }
    for (const QString& u : unused) std::printf("  перевод не нужен (строки нет в коде): %s\n", qPrintable(u.left(80)));
    CHECK(unused.isEmpty());
}

// --- 1.12: сколько угодно действий, «держать», кнопки мыши 4/5, клавиши игр ---
static void testV112() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "v112_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    CHECK(Games::addBuiltin("genshin"));
    CHECK(Games::addBuiltin("minecraft"));

    // кнопки мыши: имена, подписи, хоткеи
    CHECK(MouseButtons::valid(8) && MouseButtons::valid(9) && !MouseButtons::valid(4));
    CHECK(MouseButtons::fromItemName("mouse:8") == 8 && MouseButtons::fromItemName("mouse:7") == 0);
    CHECK(MouseButtons::shortLabel(9) == "Mouse 5");
    CHECK(MouseButtons::hotkeyAllowed(2) && MouseButtons::hotkeyAllowed(8) && !MouseButtons::hotkeyAllowed(1));
    HotkeySpec m4 = HotkeySpec::parse("mouse:8");
    CHECK(m4.isValid() && m4.isMouse() && m4.mouseButton() == 8 && m4.passthrough() && m4.isGlobalSafe());
    CHECK(m4.display() == "Mouse 4" && m4.toString() == "mouse:8");
    CHECK(!HotkeySpec::parse("mouse:1").isValid());              // ЛКМ хоткеем нельзя
    CHECK(HotkeySpec::parse("ctrl+mouse:9").mods == ModCtrl);
    CHECK(ActionStep::fromItem("mouse:9").mouse && ActionStep::fromItem("mouse:9").button == 9);
    CHECK(!ActionStep::fromItem("f3").mouse && ActionStep::fromItem("f3").key == "f3");

    // 5 действий с «держать» и паузами — сохраняются и читаются как есть
    GameState st = Games::defaultState(Games::get("genshin"));
    st.actions.clear();
    for (int i = 0; i < 5; ++i) {
        ActionStep a;
        a.mouse = (i % 2 == 0);
        a.button = i == 4 ? 8 : 1;
        a.key = QStringList{"f", "e", "q", "r", "space"}.value(i);
        a.holdMs = i * 10;
        a.afterMs = 100 + i;
        st.actions << a;
    }
    Games::saveState("genshin", st);
    const GameState back = Games::loadState(Games::get("genshin"));
    CHECK(back == st);
    CHECK(back.actions.size() == 5 && back.actions[4].button == 8 && back.actions[3].holdMs == 30);
    CHECK(back.usesMouse(GameMode::Sequence) && back.multi());
    // мусор в записи действий — пропускается, пустой список — стандартные действия
    { auto s = AppSettings::open(); s->setValue("games/genshin/state/actions", QStringList{"k:nosuchkey:0:0", "m:5:0:0", "x", "k:f:99999:-5"}); s->sync(); }
    const GameState junk = Games::loadState(Games::get("genshin"));
    CHECK(junk.actions.size() == 1 && junk.actions[0].key == "f" && junk.actions[0].holdMs == 10000 && junk.actions[0].afterMs == 0);
    { auto s = AppSettings::open(); s->setValue("games/genshin/state/actions", QStringList{}); s->sync(); }
    CHECK(Games::loadState(Games::get("genshin")).actions == Games::defaultState(Games::get("genshin")).actions);

    // формат 1.11 (два действия и пауза) — переносится в список
    {
        auto s = AppSettings::open();
        s->remove("games/genshin/state");
        const QString p = "games/genshin/state/";
        s->setValue(p + "mouse1", false); s->setValue(p + "key1", "f3"); s->setValue(p + "between", 250);
        s->setValue(p + "useSlot2", true); s->setValue(p + "mouse2", true); s->setValue(p + "button2", 3);
        s->setValue(p + "interval", 77); s->setValue(p + "randomize", false); s->setValue(p + "lockWindow", false);
        s->sync();
    }
    GameState old = Games::loadState(Games::get("genshin"));
    CHECK(old.actions.size() == 2);
    CHECK(!old.actions[0].mouse && old.actions[0].key == "f3" && old.actions[0].afterMs == 250);
    CHECK(old.actions[1].mouse && old.actions[1].button == 3);
    CHECK(old.intervalMs == 77 && !old.randomize && !old.lockWindow);
    Games::saveState("genshin", old);                          // при записи — новый формат, старые поля убраны
    { auto s = AppSettings::open();
      CHECK(s->contains("games/genshin/state/actions") && !s->contains("games/genshin/state/mouse1") &&
            !s->contains("games/genshin/state/between")); }
    CHECK(Games::loadState(Games::get("genshin")) == old);

    // клавиши игр: своя у каждой, по умолчанию — общая
    { auto s = AppSettings::open(); s->setValue("hotkey", "f7"); s->sync(); }
    CHECK(Games::startKey("genshin").toString() == "f7" && Games::startKey("minecraft").toString() == "f7");
    Games::setStartKey("minecraft", HotkeySpec::parse("mouse:9"));
    CHECK(Games::startKey("minecraft").toString() == "mouse:9" && Games::startKey("genshin").toString() == "f7");
    Games::setStartKey("genshin", HotkeySpec{});                // пустую не записываем
    CHECK(Games::startKey("genshin").toString() == "f7");
    // «не пропускать в игру» — у клавиши старта игры; другая клавиша — снова пропускается
    CHECK(!Games::startKey("minecraft").block && !Games::startKeyBlock("minecraft"));
    Games::setStartKeyBlock("minecraft", true);
    CHECK(Games::startKey("minecraft").block && !Games::startKey("genshin").block);
    Games::setStartKey("minecraft", HotkeySpec::parse("mouse:9"));      // та же — как было
    CHECK(Games::startKeyBlock("minecraft"));
    Games::setStartKey("minecraft", HotkeySpec::parse("mouse:8"));
    CHECK(!Games::startKeyBlock("minecraft") && !Games::startKey("minecraft").block);
    Games::setStartKey("minecraft", HotkeySpec::parse("mouse:9"));
    CHECK(Games::armed("genshin"));                             // новая категория — используется
    Games::setArmed("genshin", false);
    CHECK(!Games::armed("genshin") && Games::armed("minecraft"));
    Games::setArmed("genshin", true);
    CHECK(Games::armed("genshin"));
    CHECK(Games::keyMode("genshin") == 0);
    Games::setKeyMode("genshin", 1);
    CHECK(Games::keyMode("genshin") == 1);
    Games::setKeyMode("genshin", 5);
    CHECK(Games::keyMode("genshin") == 0);
    CHECK(!Games::armKey("genshin").isValid());
    Games::setArmKey("genshin", HotkeySpec::parse("ctrl+f10"));
    CHECK(Games::armKey("genshin").display() == "Ctrl+F10");
    Games::setArmKey("genshin", HotkeySpec{});
    CHECK(!Games::armKey("genshin").isValid());
    // клавиша профиля — у профиля, переживает переименование, уходит с удалением
    Games::saveProfile("genshin", "PvP", old, 0);
    CHECK(!Games::profileKey("genshin", "PvP").isValid());
    Games::setProfileKey("genshin", "PvP", HotkeySpec::parse("f2"));
    CHECK(Games::profileKey("genshin", "pvp").toString() == "f2");
    CHECK(Games::profiles("genshin") == QStringList{"PvP"});    // ключ hotkey — не профиль
    CHECK(Games::loadProfile("genshin", "PvP") == old);
    CHECK(Games::renameProfile("genshin", "PvP", "Бой"));
    CHECK(Games::profileKey("genshin", "Бой").toString() == "f2");
    Games::removeProfile("genshin", "Бой");
    CHECK(!Games::profileKey("genshin", "Бой").isValid());
    Games::setProfileKey("genshin", "нет такого", HotkeySpec::parse("f3"));   // нет профиля — не создаёт
    CHECK(Games::profiles("genshin").isEmpty());

    // первый запуск и мини-окно
    CHECK(!Prefs::onboardingDone());
    Prefs::setOnboardingDone(true);
    CHECK(Prefs::onboardingDone());
    CHECK(Prefs::miniOnTop());
    Prefs::setMiniOnTop(false);
    CHECK(!Prefs::miniOnTop());

    { AppSettings::dropScope("AutoQTest", "v112_" + pid); }
}

// --- 1.13: кружок «используется», Tab в сочетаниях ---
static void testV113() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "v113_" + pid);
    { auto s = AppSettings::open(); s->clear();
      // 1.12: открыта Genshin (кружок пустой), Minecraft «активна», Roblox — пустой
      s->setValue("version", 2); s->setValue("games/listVersion", 2);
      s->setValue("games/order", QStringList{"genshin", "minecraft", "roblox"});
      s->setValue("currentGame", "genshin");
      s->setValue("games/minecraft/armed", true);
      s->sync(); }
    Games::migrate();
    CHECK(Games::armed("genshin"));            // открытая работала — используется
    CHECK(Games::armed("minecraft"));
    CHECK(!Games::armed("roblox"));            // раньше запускалась только открытой — выключена
    Games::setArmed("roblox", true);
    Games::migrate();                          // повторно ничего не меняет
    CHECK(Games::armed("roblox"));
    Games::addBuiltin("wuwa");
    CHECK(Games::armed("wuwa"));               // новая — сразу используется
    { auto s = AppSettings::open(); s->clear(); s->sync(); }

    // Tab и Shift+Tab — обычные клавиши для сочетаний
    CHECK(Keys::fromKeyEvent(Qt::Key_Tab, false, 0) && QString(Keys::fromKeyEvent(Qt::Key_Tab, false, 0)->name) == "tab");
    CHECK(Keys::fromKeyEvent(Qt::Key_Backtab, false, 0) && QString(Keys::fromKeyEvent(Qt::Key_Backtab, false, 0)->name) == "tab");
    CHECK(HotkeySpec::parse("alt+tab").isValid() && HotkeySpec::parse("ctrl+tab").display() == "Ctrl+Tab");
    CHECK(HotkeySpec::parse("tab").passthrough());

    // звуки вкл/выкл: настоящий WAV, громкость влияет; «вкл» и «выкл» — отдельно
    for (const QString& id : Sounds::ids()) {
        const QByteArray w = Sounds::wav(id, 50);
        if (id == "none") { CHECK(w.isEmpty()); continue; }
        CHECK(w.startsWith("RIFF") && w.mid(8, 4) == "WAVE" && w.size() > 2000 && w.size() < 100000);
    }
    CHECK(Sounds::wav("ticks-up", 50) != Sounds::wav("ticks-down", 50));
    {
        auto peak = [](const QByteArray& w) {
            int m = 0;
            for (int i = 44; i + 1 < w.size(); i += 2) m = qMax(m, qAbs(int(qint16((uchar(w[i + 1]) << 8) | uchar(w[i])))));
            return m;
        };
        const int full = peak(Sounds::wav("ticks-up", 100)), low = peak(Sounds::wav("ticks-up", 10));
        CHECK(full > 8000 && full <= 30000 && low * 8 < full && peak(Sounds::wav("ticks-up", 0)) == 0);
    }
    AppSettings::setScope("AutoQTest", "snd_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    CHECK(Sounds::sound(Sounds::On) == "ticks-up" && Sounds::sound(Sounds::Off) == "ticks-down");
    Sounds::setSound(Sounds::On, "coin");
    QTemporaryDir sndTmp;
    const QString xwav = sndTmp.filePath("x.wav");
    { QFile f(xwav); f.open(QIODevice::WriteOnly); f.write(Sounds::wav("coin", 80)); }
    Sounds::setSound(Sounds::Off, "file:" + xwav);
    CHECK(Sounds::sound(Sounds::On) == "coin" && Sounds::sound(Sounds::Off) == "file:" + xwav);
    CHECK(Sounds::label("file:" + xwav) == "x.wav");
    Sounds::setSound(Sounds::On, "нет-такого");
    CHECK(Sounds::sound(Sounds::On) == "ticks-up");     // неизвестный — по умолчанию
    Sounds::setSound(Sounds::On, "file:" + sndTmp.filePath("пропал.wav"));
    CHECK(Sounds::sound(Sounds::On) == "ticks-up");     // файла нет — по умолчанию

    // свои звуки: копия в «Мои звуки» (папка данных), видны во всех списках
    {
        CHECK(Sounds::userSounds().isEmpty() && Sounds::userDir() == AppSettings::dataDir() + "/sounds");
        const QString src = sndTmp.filePath("Мой щелчок.wav");
        { QFile f(src); f.open(QIODevice::WriteOnly); f.write(Sounds::wav("pop", 100)); }
        QString err;
        const QString id = Sounds::addUserSound(src, &err);
        CHECK(id == "user:Мой щелчок.wav" && err.isEmpty());
        CHECK(QFile::exists(Sounds::userDir() + "/Мой щелчок.wav") && Sounds::exists(id));
        CHECK(Sounds::label(id) == "Мой щелчок" && Sounds::userSounds() == QStringList{id});
        CHECK(Sounds::addUserSound(src) == id);                       // тот же файл — не копия
        { QFile f(src); f.open(QIODevice::WriteOnly); f.write(Sounds::wav("bell", 100)); }
        CHECK(Sounds::addUserSound(src) == "user:Мой щелчок (2).wav"); // другой с тем же именем
        const QString txt = sndTmp.filePath("не звук.wav");
        { QFile f(txt); f.open(QIODevice::WriteOnly); f.write("hello"); }
        CHECK(Sounds::addUserSound(txt, &err).isEmpty() && !err.isEmpty());
        CHECK(Sounds::addUserSound(sndTmp.filePath("нет.wav"), &err).isEmpty());
        // свой звук игры/профиля — если есть; иначе из настроек
        Sounds::setSound(Sounds::On, "coin");
        CHECK(Sounds::resolve(Sounds::On, "") == "coin" && Sounds::resolve(Sounds::On, id) == id);
        CHECK(Sounds::resolve(Sounds::On, "user:пропал.wav") == "coin" && Sounds::resolve(Sounds::Off, "none") == "none");
        // своя громкость и для своих файлов: 16 бит моно, 8 бит, float стерео
        auto peak16 = [](const QByteArray& w) {
            int m = 0;
            for (int i = 44; i + 1 < w.size(); i += 2) m = qMax(m, qAbs(int(qint16((uchar(w[i + 1]) << 8) | uchar(w[i])))));
            return m;
        };
        const QByteArray orig = Sounds::wav("pop", 100);
        const int p100 = peak16(Sounds::withVolume(orig, 100)), p50 = peak16(Sounds::withVolume(orig, 50));
        CHECK(qAbs(p100 - peak16(orig)) <= 2 && qAbs(p50 * 2 - p100) <= 4 && peak16(Sounds::withVolume(orig, 0)) == 0);
        CHECK(Sounds::withVolume("не wav", 50) == QByteArray("не wav"));
        auto makeWav = [](int format, int ch, int bits, const QByteArray& data) {
            QByteArray w;
            auto u32 = [&w](quint32 v) { char b[4]; qToLittleEndian(v, b); w.append(b, 4); };
            auto u16 = [&w](quint16 v) { char b[2]; qToLittleEndian(v, b); w.append(b, 2); };
            w.append("RIFF"); u32(36 + data.size()); w.append("WAVE");
            w.append("fmt "); u32(16); u16(format); u16(ch); u32(22050); u32(22050 * ch * bits / 8); u16(ch * bits / 8); u16(bits);
            w.append("data"); u32(data.size()); w.append(data);
            return w;
        };
        const QByteArray b8 = Sounds::withVolume(makeWav(1, 1, 8, QByteArray("\xFF\x80\x00", 3)), 100);
        CHECK(b8.size() == 44 + 6 && peak16(b8) >= 32000);
        QByteArray fl(16, 0);
        qToLittleEndian<float>(0.5f, fl.data()); qToLittleEndian<float>(-0.25f, fl.data() + 4);
        qToLittleEndian<float>(2.0f, fl.data() + 8);               // за пределами — обрезается
        const QByteArray fs = Sounds::withVolume(makeWav(3, 2, 32, fl), 100);
        CHECK(fs.size() == 44 + 8 && qFromLittleEndian<quint16>(fs.constData() + 22) == 2 && peak16(fs) == 32767);
        CHECK(qFromLittleEndian<qint16>(fs.constData() + 44) == 16384 || qFromLittleEndian<qint16>(fs.constData() + 44) == 16383);
        // у игры и профиля — свой звук: сохраняется, пустое — как в настройках
        Games::addBuiltin("genshin");
        GameState st = Games::loadState(Games::get("genshin"));
        CHECK(st.soundOn.isEmpty() && st.soundOff.isEmpty());
        st.soundOn = id;
        st.soundOff = "none";
        Games::saveState("genshin", st);
        const GameState back = Games::loadState(Games::get("genshin"));
        CHECK(back.soundOn == id && back.soundOff == "none" && back == st);
        Games::saveProfile("genshin", "Тихий", st, -1);
        CHECK(Games::loadProfile("genshin", "Тихий").soundOn == id);
        st.soundOn.clear();
        Games::saveState("genshin", st);
        CHECK(!AppSettings::open()->contains("games/genshin/state/soundOn"));
    }
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    for (const QString& sc : {"v113_" + pid, "snd_" + pid}) { AppSettings::dropScope("AutoQTest", sc); }

    // И1 «в такт с монитором»: кадры, длина цикла, сетка без накопления ошибки
    CHECK(qAbs(FrameSync::frameMs(144) - 6.9444) < 0.001 && FrameSync::frameMs(0) == 0);
    CHECK(FrameSync::frames(100, 1000.0 / 60) == 6 && FrameSync::frames(3, 6.94) == 1 && FrameSync::frames(25, 10) == 3);
    CHECK(FrameSync::frames(0, 10) == 1 && FrameSync::frames(50, 0) == 1);
    {
        ActionStep a1; a1.holdMs = 5; a1.afterMs = 20;
        ActionStep a2; a2.holdMs = 7; a2.afterMs = 30;      // пауза после последнего не считается
        CHECK(FrameSync::cycleMs({a1, a2}, 100, false) == 132);
        CHECK(FrameSync::cycleMs({a1, a2}, 100, true) == 107);    // вместе: самое долгое «держать»
        CHECK(FrameSync::cycleMs({a1}, 0, false) == 6);           // интервал не меньше 1 мс
        // средняя длина цикла с разбросом — как отмеряет кликер
        ActionStep tap;
        CHECK(FrameSync::meanCycleMs({tap}, 100, false, 0, 20) == 100);
        CHECK(FrameSync::meanCycleMs({tap}, 10, false, 0, 20) == 20);              // не короче минимума круга
        CHECK(qAbs(FrameSync::meanCycleMs({tap}, 100, false, 30, 20) - 100) < 1e-9); // разброс ровно вокруг
        // 1 мс ± 30: половина пауз упирается в 1 мс — в среднем ≈ 8,6 мс, а не 1
        CHECK(qAbs(FrameSync::meanCycleMs({tap}, 1, false, 30, 1) - 526.0 / 61) < 1e-9);
        CHECK(FrameSync::meanCycleMs({a1, a2}, 100, false, 0, 1) == FrameSync::cycleMs({a1, a2}, 100, false));
    }
    {
        using namespace std::chrono;
        FrameSync::Grid g;
        const auto t0 = steady_clock::now();
        CHECK(g.next(10, t0, false) == t0);                       // первый цикл — сразу
        g.advance(3);
        CHECK(g.next(10, t0 + milliseconds(5), false) == t0 + milliseconds(30));
        g.advance(3);
        CHECK(g.next(10, t0 + milliseconds(45), false) == t0 + milliseconds(60));
        g.advance(1);                                             // цикл затянулся до 75 мс — ближайший кадр
        CHECK(g.next(10, t0 + milliseconds(75), false) == t0 + milliseconds(80));
        g.advance(1000);                                          // через 1000 кадров — без сдвига
        CHECK(g.next(10, t0 + milliseconds(81), false) == t0 + milliseconds(10080));
        g.advance(0);                                             // меньше кадра не бывает
        CHECK(g.next(10, t0 + milliseconds(10081), false) == t0 + milliseconds(10090));
        const auto t1 = t0 + seconds(20);
        CHECK(g.next(1000.0 / 60, t1, false) == t1);              // другая частота — новая сетка
    }
    // настройка сохраняется в состоянии игры и в профиле; пресет её не трогает
    AppSettings::setScope("AutoQTest", "fs_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    {
        Games::addBuiltin("genshin");
        GameState st = Games::loadState(Games::get("genshin"));
        CHECK(!st.frameSync && st.frameHz == 0);
        st.frameSync = true; st.frameHz = 60;
        Games::saveState("genshin", st);
        const GameState back = Games::loadState(Games::get("genshin"));
        CHECK(back.frameSync && back.frameHz == 60 && back == st);
        Games::saveProfile("genshin", "Такт", st, 0);
        const GameState prof = Games::loadProfile("genshin", "Такт");
        CHECK(prof.frameSync && prof.frameHz == 60);
        GameState other = st; other.frameHz = 144;
        CHECK(!(other == st));
        auto s = AppSettings::open();
        s->setValue("games/genshin/state/frameHz", 5000);           // мусор в настройках — «Авто»
        s->sync();
        CHECK(Games::loadState(Games::get("genshin")).frameHz == 0);
        s->clear(); s->sync();
    }
    { AppSettings::dropScope("AutoQTest", "fs_" + pid); }

    // кликер в такте: 10 мс кадр, цикл 25 мс -> 3 кадра = 30 мс
    {
        Clicker c;
        ClickerConfig cfg;
        cfg.lockToWindow = false;
        cfg.intervalMs = 25;
        cfg.frameMs = 10;
        c.setConfig(cfg);
        c.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(610));
        c.stop();
        const auto n = c.clickCount();           // 0, 30, 60 … 600 мс — 21
        CHECK(n >= 18 && n <= 22);
        if (n < 18 || n > 22) std::printf("  такт: %llu кликов за 610 мс (ждали ~21)\n", n);
        cfg.intervalMs = 1;                      // быстрее кадра — один клик за кадр
        c.setConfig(cfg);
        c.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(305));
        c.stop();
        const auto m = c.clickCount();           // 0, 10 … 300 — 31
        CHECK(m >= 28 && m <= 32);
        if (m < 28 || m > 32) std::printf("  такт: %llu кликов за 305 мс (ждали ~31)\n", m);
    }
    // «повтор через» 0 (паузы между клавишами) — круг всё равно не быстрее минимума игры
    {
        Clicker c;
        ClickerConfig cfg;
        cfg.lockToWindow = false;
        cfg.steps = true;
        ActionStep tap; tap.press = ActionStep::Click;
        cfg.actions = {tap};
        cfg.intervalMs = 0;
        cfg.minCycleMs = 20;
        c.setConfig(cfg);
        c.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(410));
        c.stop();
        const auto n = c.cycleCount();           // 0, 20 … 400 мс — 21
        CHECK(n >= 17 && n <= 22);
        if (n < 17 || n > 22) std::printf("  минимум круга: %llu кругов за 410 мс (ждали ~21)\n", n);
    }
}

// 1.13: папка данных — настройки в settings.ini, профили — по файлу
static void testDataDir() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "dd_" + pid);
    CHECK(AppSettings::settingsFile() == AppSettings::dataDir() + "/settings.ini");
    { auto s = AppSettings::open(); s->clear(); s->setValue("x", 1); s->sync(); }
    CHECK(QFile::exists(AppSettings::settingsFile()));
    CHECK(!AppSettings::portable() && !AppSettings::customDataDir());
    CHECK(!AppSettings::setDataDir(QDir::tempPath(), true));          // у тестов папка своя
    Games::addBuiltin("genshin");
    Games::addBuiltin("minecraft");
    GameState st; st.intervalMs = 77; st.frameSync = true;
    Games::saveProfile("genshin", "Быстро: 2x", st, 1);
    const QString f = Games::profileFile("genshin", "быстро: 2X");
    CHECK(f == AppSettings::profilesDir() + "/genshin/Быстро_ 2x.ini");   // «:» в имени файла нельзя
    CHECK(Games::profiles("genshin") == QStringList{"Быстро: 2x"});        // а в имени профиля — можно
    int mode = -1;
    CHECK(Games::loadProfile("genshin", "Быстро: 2x", &mode).intervalMs == 77 && mode == 1);
    Games::setProfileKey("genshin", "Быстро: 2x", HotkeySpec::parse("f8"));
    Games::setProfileInOverlay("genshin", "Быстро: 2x", false);
    st.intervalMs = 88;
    Games::saveProfile("genshin", "быстро: 2x", st, 1);                    // тот же профиль
    CHECK(Games::profiles("genshin").size() == 1 && Games::profileKey("genshin", "Быстро: 2x").toString() == "f8");
    CHECK(!Games::profileInOverlay("genshin", "Быстро: 2x"));             // сохранение не сбросило
    CHECK(Games::renameProfile("genshin", "Быстро: 2x", "Турбо"));
    CHECK(!QFile::exists(f) && QFile::exists(AppSettings::profilesDir() + "/genshin/Турбо.ini"));
    CHECK(Games::profileKey("genshin", "Турбо").toString() == "f8" && !Games::profileInOverlay("genshin", "Турбо"));
    CHECK(Games::renameProfile("genshin", "Турбо", "ТУРБО") && Games::profiles("genshin") == QStringList{"ТУРБО"});
    // файл положили руками — тоже профиль; мусор — нет
    {
        QFile junk(AppSettings::profilesDir() + "/genshin/readme.txt");
        CHECK(junk.open(QIODevice::WriteOnly)); junk.write("hi"); junk.close();
    }
    CHECK(Games::profiles("genshin").size() == 1);
    // экспорт и импорт: в другую игру, имя занято — «(2)», клавиша не переезжает
    const QString out = AppSettings::dataDir() + "/export.ini";
    CHECK(Games::exportProfile("genshin", "турбо", out));
    QString err, from;
    CHECK(Games::importProfile("minecraft", out, &err, &from) == "ТУРБО" && from == "Genshin Impact");
    CHECK(Games::loadProfile("minecraft", "ТУРБО").intervalMs == 88 && Games::loadProfile("minecraft", "ТУРБО").frameSync);
    CHECK(!Games::profileKey("minecraft", "ТУРБО").isValid() && !Games::profileInOverlay("minecraft", "ТУРБО"));
    CHECK(Games::importProfile("minecraft", out) == "ТУРБО (2)");
    CHECK(Games::importProfile("genshin", AppSettings::settingsFile(), &err).isEmpty() && !err.isEmpty());
    CHECK(Games::importProfile("genshin", AppSettings::dataDir() + "/нет.ini", &err).isEmpty());
    // убрать игру — профили остаются; встроенная, добавленная снова, находит их сама
    CHECK(Games::remove("minecraft") && QDir(AppSettings::profilesDir() + "/minecraft").exists());
    CHECK(Games::addBuiltin("minecraft") && Games::profiles("minecraft").size() == 2);
    // «Удалить и профили»
    CHECK(Games::remove("minecraft", true) && !QDir(AppSettings::profilesDir() + "/minecraft").exists());
    CHECK(Games::addBuiltin("minecraft") && Games::profiles("minecraft").isEmpty());
    Games::saveProfile("minecraft", "Стройка", GameState{});
    CHECK(Games::remove("minecraft"));
    {   // своя игра с названием встроенной профили встроенной не забирает: они ждут её
        GameDef c; c.id = Games::newCustomId(); c.name = Games::builtinDefault("minecraft").name; Games::save(c);
        CHECK(Games::adoptProfiles(c.id, c.name) == 0);
        CHECK(Games::remove(c.id, true));
        CHECK(Games::addBuiltin("minecraft") && Games::profiles("minecraft") == QStringList{"Стройка"});
        CHECK(Games::remove("minecraft", true));
    }
    {   // своя игра: при новом добавлении другой id — профили находятся по названию игры
        GameDef c; c.id = Games::newCustomId(); c.name = "Моя игра"; Games::save(c);
        Games::saveProfile(c.id, "Мой", GameState{});
        Games::saveProfile(c.id, "Второй", GameState{});
        CHECK(Games::remove(c.id));
        GameDef other; other.id = Games::newCustomId(); other.name = "Другая"; Games::save(other);
        CHECK(Games::adoptProfiles(other.id, other.name) == 0);      // чужие профили не берёт
        GameDef again; again.id = Games::newCustomId(); again.name = "моя ИГРА"; Games::save(again);
        CHECK(again.id != c.id);
        CHECK(Games::adoptProfiles(again.id, again.name) == 2);
        CHECK(Games::profiles(again.id) == (QStringList{"Второй", "Мой"}));
        CHECK(!QDir(AppSettings::profilesDir() + "/" + c.id).exists());
        CHECK(Games::adoptProfiles(again.id, again.name) == 0);      // уже забрали
        {
            QSettings f(Games::profileFile(again.id, "Мой"), QSettings::IniFormat);
            CHECK(f.value("AutoQ/game").toString() == again.id);
        }
        CHECK(Games::remove(other.id, true) && Games::remove(again.id, true));
    }
    // профили старого формата (внутри настроек) переезжают в файлы
    {
        auto s = AppSettings::open();
        s->setValue("games/genshin/profiles/Старый/interval", 321);
        s->setValue("games/genshin/profiles/Старый/mode", 2);
        s->setValue("games/genshin/profiles/Старый/hotkey", "f9");
        s->sync();
    }
    Games::migrate();
    CHECK(Games::profileExists("genshin", "Старый") && Games::loadProfile("genshin", "Старый", &mode).intervalMs == 321);
    CHECK(mode == 2 && Games::profileKey("genshin", "Старый").toString() == "f9");
    CHECK(!AppSettings::open()->childGroups().contains("games") ||
          !AppSettings::open()->allKeys().filter("/profiles/").size());
    AppSettings::dropScope("AutoQTest", "dd_" + pid);
}

// Д.И.1: раскладка карточек — у каждой игры своя
static void testCardLayout() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "cl_" + pid);
    using namespace CardLayout;
    const Layout def = defaults();
    CHECK(def.size() == 7 && def[0].id == "head" && def[1].id == "launch" && def[2].id == "actions" &&
          def[3].id == "speed" && def[4].id == "window" && def[5].id == "profile" && def[6].id == kHidden);
    CHECK(def[0].blocks == QStringList({"game", "counter", "tools", "start"}) && def[0].row &&
          def[0].style == Clear && def[0].hideTitle && def[0].hideNumber);
    CHECK(def[1].blocks == QStringList({"hotkey", "keymode", "sounds"}) && def[1].row && def[1].style == Normal);
    CHECK(def[3].blocks == QStringList({"speed", "interval", "jitter", "framesync"}));
    CHECK(def[4].blocks == QStringList({"window", "capture"}));
    CHECK(def[5].blocks == QStringList({"profile", "preset"}));
    CHECK(def[6].blocks == QStringList({"monitor", "rules", "armed"}) && hiddenBlocks(def) == QStringList({"monitor", "rules", "armed"}));
    CHECK(isDefault(def));
    // старая настройка «порядок карточек» — порядок по умолчанию
    AppSettings::open()->setValue("cardOrder", QStringList{"profile", "speed", "window"});
    CHECK(defaults()[3].id == "profile" && defaults()[4].id == "speed" && defaults()[5].id == "window");
    AppSettings::open()->remove("cardOrder");
    // блок — в другую карточку, на нужное место
    Layout l = def;
    moveBlock(l, "framesync", "window", 1);
    CHECK(l[4].blocks == QStringList({"window", "framesync", "capture"}) &&
          l[3].blocks == QStringList({"speed", "interval", "jitter"}));
    moveBlock(l, "framesync", "window", 3);                    // внутри той же карточки — в конец
    CHECK(l[4].blocks == QStringList({"window", "capture", "framesync"}));
    // своя карточка: пустая, с названием, перед служебной «hidden»; удалили — блоки домой
    const QString c = addCustom(l, "  Моя   быстрая  ");
    CHECK(c == "c1" && l[l.size() - 2].title == "Моя быстрая" && l[l.size() - 2].blocks.isEmpty() && l.last().id == kHidden);
    CHECK(addCustom(l, "") == "c2" && l[l.size() - 2].title == "…");
    moveBlock(l, "armed", c, -1);
    moveBlock(l, "speed", c, 0);
    CHECK(l[indexOf(l, c)].blocks == QStringList({"speed", "armed"}));
    CHECK(cardOf(l, "armed") == c);
    rename(l, c, "Главное");
    rename(l, "speed", "Нельзя");                            // встроенные не переименовываются
    CHECK(l[indexOf(l, c)].title == "Главное" && l[indexOf(l, "speed")].title.isEmpty());
    // порядок карточек: «hidden» всегда последняя
    moveCard(l, c, 1);
    CHECK(l[1].id == c && l[0].id == "head" && l[2].id == "launch" && l[3].id == "actions");
    moveCard(l, "actions", l.size());
    CHECK(l[l.size() - 2].id == "actions" && l.last().id == kHidden);
    moveCard(l, kHidden, 0);
    CHECK(l.last().id == kHidden);
    setCollapsed(l, "window", true);
    // сохранение — у игры своё
    save("genshin", l);
    CHECK(load("genshin") == normalize(l));
    CHECK(isDefault(load("minecraft")));
    // своя карточка с монитором: удалили — монитор в «Скрытые», остальное домой
    moveBlock(l, "monitor", c, -1);
    CHECK(hiddenBlocks(l) == QStringList({"rules"}));
    removeCustom(l, c);
    CHECK(indexOf(l, c) < 0 && l[indexOf(l, "speed")].blocks.first() == "speed" &&
          l[indexOf(l, "launch")].blocks.last() == "sounds" && hiddenBlocks(l) == QStringList({"monitor", "rules", "armed"}));
    CHECK(l[indexOf(l, "window")].collapsed);
    // спрятать и вернуть блок — на своё место
    hideBlock(l, "jitter");
    hideBlock(l, "game");
    CHECK(hiddenBlocks(l) == QStringList({"monitor", "rules", "armed", "jitter", "game"}) && cardOf(l, "jitter") == kHidden);
    showBlock(l, "jitter");
    showBlock(l, "game");
    CHECK(l[indexOf(l, "speed")].blocks == QStringList({"speed", "interval", "jitter"}));
    CHECK(l[indexOf(l, "head")].blocks.first() == "game" && hiddenBlocks(l) == QStringList({"monitor", "rules", "armed"}));
    showBlock(l, "nope");
    CHECK(normalize(l) == l);
    // стандартная — ключа нет
    save("genshin", defaults());
    CHECK(!AppSettings::open()->contains("games/genshin/layout"));
    // испорченное и чужое — в порядок: каждый блок ровно раз, встроенные на месте
    bool ok = true;
    const Layout bad = fromJson(R"([{"id":"speed","blocks":["actions","actions","nope"]},{"id":"x"},{"id":"c9","title":"","blocks":["speed"]}])", &ok);
    CHECK(ok && bad.size() == 8 && indexOf(bad, "x") < 0 && bad[0].id == "head" && bad.last().id == kHidden);
    CHECK(bad[indexOf(bad, "speed")].blocks == QStringList({"actions", "interval", "jitter", "framesync"}));
    CHECK(bad[indexOf(bad, "c9")].blocks == QStringList({"speed"}) && bad[indexOf(bad, "c9")].title == "…");
    CHECK(bad[indexOf(bad, "actions")].blocks.isEmpty());
    QStringList all;
    for (const Card& cc : bad) all += cc.blocks;
    CHECK(all.size() == blockIds().size() && QSet<QString>(all.begin(), all.end()).size() == blockIds().size());
    fromJson("{не json", &ok);
    CHECK(!ok);
    CHECK(fromJson(toJson(l)) == normalize(l));
    // размеры, вид и цвет — в допустимых пределах
    const Layout odd = fromJson(R"([{"id":"speed","span":9,"height":-5,"style":7,"color":"red","row":true},
        {"id":"window","span":2,"height":99999,"style":2,"color":"#ABCDEF","hideTitle":true,"hideNumber":true}])");
    const Card& os = odd[indexOf(odd, "speed")];
    const Card& ow = odd[indexOf(odd, "window")];
    CHECK(os.span == 4 && os.height == 0 && os.style == Normal && os.color.isEmpty() && os.row);
    CHECK(ow.span == 2 && ow.height == kMaxHeight && ow.style == Accent && ow.color == "#abcdef" && ow.hideTitle && ow.hideNumber);
    CHECK(fromJson(toJson(odd)) == odd);
    // раскладка из 1.13 (без шапки и мелких блоков): шапка и новые блоки — на свои места
    const Layout old = fromJson(R"([{"id":"actions","blocks":["actions"]},{"id":"profile","blocks":["profile","hotkey","sounds","armed"]},
        {"id":"speed","blocks":["speed","framesync"]},{"id":"window","blocks":["window","capture"],"collapsed":true}])");
    CHECK(old.size() == 7 && old[0].id == "head" && old[1].id == "launch" && old[2].id == "actions" &&
          old[3].id == "profile" && old[4].id == "speed");
    CHECK(old[3].blocks == QStringList({"profile", "preset"}));
    CHECK(old[1].blocks == QStringList({"hotkey", "keymode", "sounds"}));
    CHECK(old[4].blocks == QStringList({"speed", "interval", "jitter", "framesync"}));
    CHECK(old[5].blocks == QStringList({"window", "capture"}) && old[5].collapsed);
    CHECK(hiddenBlocks(old) == QStringList({"monitor", "rules", "armed"}));
    // раскладка до 1.17 («Профиль и запуск» — одна карточка): блоки запуска — в «Запуск»,
    // унесённые в свою карточку и спрятанные — где были
    const Layout v116 = fromJson(R"([{"id":"head","blocks":["game","counter","tools","start"]},{"id":"actions","blocks":["actions"]},
        {"id":"profile","blocks":["hotkey","profile","sounds","armed"],"collapsed":true,"span":2},{"id":"speed","blocks":["speed","interval"]},
        {"id":"c1","title":"Моя","blocks":["keymode"]},{"id":"window","blocks":["window","rules","capture"]},
        {"id":"hidden","blocks":["monitor","jitter","framesync","preset"]}])");
    CHECK(v116.size() == 8 && v116[1].id == "launch" && v116[2].id == "actions" && v116[3].id == "profile" && v116[4].id == "speed");
    CHECK(v116[3].blocks == QStringList({"profile"}) && v116[3].collapsed && v116[3].span == 2);
    CHECK(v116[1].blocks == QStringList({"hotkey", "sounds"}) && !v116[1].collapsed && v116[1].span == 0 && v116[1].row);
    CHECK(v116[indexOf(v116, "c1")].blocks == QStringList({"keymode"}));
    CHECK(hiddenBlocks(v116) == QStringList({"monitor", "jitter", "framesync", "preset", "armed"}));
    CHECK(normalize(v116) == v116);
    // «Запуск» уже есть — блоки запуска, возвращённые в «Профиль», там и остаются
    Layout back17 = v116;
    moveBlock(back17, "hotkey", "profile", -1);
    CHECK(normalize(back17)[3].blocks == QStringList({"profile", "hotkey"}));
    // шаблоны: всё на месте, каждый блок ровно раз
    for (int i = 0; i < kTemplates; ++i) {
        int side = -1;
        const Layout t = templateLayout(i, &side);
        QStringList tb;
        for (const Card& cc : t) tb += cc.blocks;
        CHECK(normalize(t) == t && tb.size() == blockIds().size() && side >= 0 && side <= 3);
        CHECK(!templateName(i).isEmpty());
    }
    int side = -1;
    CHECK(templateLayout(0, &side) == defaults() && side == 0);
    const Layout compact = templateLayout(1, &side);
    CHECK(side == 2 && hiddenBlocks(compact).contains("jitter") && compact[indexOf(compact, "speed")].span == 2);
    const Layout mon = templateLayout(2, &side);
    CHECK(side == 0 && isCustom(mon[1].id) && mon[1].blocks == QStringList({"monitor"}) && mon[1].style == Accent &&
          mon[1].span == 4 && hiddenBlocks(mon) == QStringList({"rules", "armed"}));
    const Layout one = templateLayout(3, &side);
    for (const Card& cc : one) CHECK(cc.id == kHidden || cc.span == 4);
    // раскладка в файл и обратно (вместе с боковой панелью)
    Layout back;
    int backSide = -1;
    CHECK(importJson(exportJson(mon, 2), &back, &backSide) && back == mon && backSide == 2);
    CHECK(!importJson("[]", &back, &backSide) && !importJson(R"({"autoq":"profile","cards":[]})", &back, &backSide));
    CHECK(!importJson("{не json", &back, &backSide));
    CHECK(importJson(R"({"autoq":"layout","sidebar":9,"cards":[]})", &back, &backSide) && backSide == 3 && back == defaults());
    backSide = 1;      // в файле нет панели — остаётся как сейчас
    CHECK(importJson(R"({"autoq":"layout","cards":[]})", &back, &backSide) && backSide == 1);
    AppSettings::dropScope("AutoQTest", "cl_" + pid);
}

// --- 1.14: режим «По шагам» (нажать ↓ / отпустить ↑) и «старт через N мс» ---
static void testV114() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "v114_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    CHECK(Games::addBuiltin("genshin"));
    CHECK(gameModeFromInt(3) == GameMode::Steps && gameModeFromInt(4) == GameMode::Sequence);
    CHECK(gameModeName(GameMode::Steps) == "По шагам");

    auto st = [](const QString& item, int press, int after) {
        ActionStep a = ActionStep::fromItem(item);
        a.press = press; a.afterMs = after;
        return a;
    };
    const int D = ActionStep::Down, U = ActionStep::Up;
    // действия -> шаги: нажать, «держать», отпустить, пауза после
    ActionStep a1 = ActionStep::fromItem("mouse:1"); a1.holdMs = 40; a1.afterMs = 120;
    ActionStep a2 = ActionStep::fromItem("f");       a2.holdMs = 0;  a2.afterMs = 7;
    const QList<ActionStep> conv = Steps::fromActions({a1, a2});
    CHECK(conv.size() == 4);
    CHECK(conv[0].press == D && conv[0].mouse && conv[0].afterMs == 40);
    CHECK(conv[1].press == U && conv[1].mouse && conv[1].afterMs == 120);
    CHECK(conv[2].press == D && !conv[2].mouse && conv[2].afterMs == 0);
    CHECK(conv[3].press == U && conv[3].key == "f" && conv[3].afterMs == 7);
    for (const ActionStep& x : conv) CHECK(x.holdMs == 0);

    // кикфлип: ЛКМ↓ ПКМ↓ ПКМ↑ ЛКМ↑ — две клавиши, ничего не остаётся нажатым
    const QList<ActionStep> kick{st("mouse:1", D, 30), st("mouse:3", D, 50), st("mouse:3", U, 10), st("mouse:1", U, 0)};
    const QList<ActionStep> in = Steps::pressedInputs(kick);
    CHECK(in.size() == 2 && in[0].button == 1 && in[1].button == 3 && in[0].press == ActionStep::Click);
    CHECK(Steps::inputIndex(in, kick[1]) == 1 && Steps::inputIndex(in, kick[0]) == 0);
    CHECK(Steps::inputIndex(in, kick[2]) == -1);                 // «отпустить» не считается
    CHECK(Steps::strayUps(kick).isEmpty() && Steps::leftDown(kick).isEmpty());
    // «отпустить» без «нажать» и то, что осталось нажатым
    const QList<ActionStep> bad{st("f", U, 0), st("e", D, 0), st("e", D, 0), st("q", D, 0), st("e", U, 0)};
    CHECK(Steps::strayUps(bad) == QList<int>{0});
    CHECK(Steps::leftDown(bad).size() == 1 && Steps::leftDown(bad)[0].key == "q");
    CHECK(Steps::pressedInputs(bad).size() == 2);                // e и q, f не нажимается
    // «клик» (нажать и отпустить) — нажимает, но зажатым ничего не оставляет; зажатое раньше — отпускает
    const int C = ActionStep::Click;
    const QList<ActionStep> taps{st("e", C, 0), st("q", D, 0), st("q", C, 0), st("q", U, 0), st("f", D, 0)};
    CHECK(Steps::pressedInputs(taps).size() == 3 && Steps::inputIndex(Steps::pressedInputs(taps), taps[0]) == 0);
    CHECK(Steps::strayUps(taps) == QList<int>{3});              // q уже отпустил «клик»
    CHECK(Steps::leftDown(taps).size() == 1 && Steps::leftDown(taps)[0].key == "f");
    CHECK(st("e", C, 0).pressMark() == "↕" && st("e", D, 0).pressMark() == "↓" && st("e", U, 0).pressMark() == "↑");
    // клавиша в разном регистре — та же клавиша
    CHECK(st("F", D, 0).sameInput(st("f", U, 0)) && !st("f", D, 0).sameInput(st("mouse:1", D, 0)));

    // runList / pressList: пустые шаги — из действий, в других режимах — действия как есть
    GameState g;
    g.actions = {a1, a2};
    CHECK(g.runList(GameMode::Steps) == conv && g.runList(GameMode::Sequence) == g.actions);
    g.steps = kick;
    CHECK(g.runList(GameMode::Steps) == kick && g.runList(GameMode::Combo) == g.actions);
    CHECK(g.pressList(GameMode::Steps).size() == 2 && g.pressList(GameMode::Sequence) == g.actions);
    GameState keysOnly; keysOnly.actions = {a2}; keysOnly.steps = kick;
    CHECK(keysOnly.usesMouse(GameMode::Steps) && !keysOnly.usesMouse(GameMode::Sequence));
    // длина круга: паузы между шагами + «повтор через»
    CHECK(FrameSync::cycleMs(kick, 100, false) == 30 + 50 + 10 + 100);

    // запись и чтение: шаги и «старт через» сохраняются, старые версии их не видят
    GameState save = Games::defaultState(Games::get("genshin"));
    save.steps = kick;
    save.startDelayMs = 1500;
    Games::saveState("genshin", save);
    GameState back = Games::loadState(Games::get("genshin"));
    CHECK(back == save && back.steps.size() == 4 && back.steps[2].press == U && back.startDelayMs == 1500);
    {   // действия — без направления (даже если в записи оно есть), шаги — всегда с направлением
        auto s = AppSettings::open();
        CHECK(s->value("games/genshin/state/steps").toStringList().value(0).endsWith(":d"));
        CHECK(s->value("games/genshin/state/steps").toStringList().value(3).endsWith(":u"));
        s->setValue("games/genshin/state/actions", QStringList{"k:f:0:0:u"});
        s->setValue("games/genshin/state/steps", QStringList{"m:1:500:20", "k:nosuchkey:0:0:d", "k:e:0:5:u"});
        s->setValue("games/genshin/state/startDelay", 999999);
        s->sync();
    }
    back = Games::loadState(Games::get("genshin"));
    CHECK(back.actions.size() == 1 && back.actions[0].press == ActionStep::Click);
    CHECK(back.steps.size() == 2 && back.steps[0].press == D && back.steps[0].holdMs == 0 && back.steps[1].press == U);
    {   // шаг «клик» пишется с «:c» (без поля — «нажать», как в 1.14)
        GameState c = back;
        c.steps = {st("e", ActionStep::Click, 10), st("f", D, 0), st("f", U, 0)};
        Games::saveState("genshin", c);
        auto s = AppSettings::open();
        CHECK(s->value("games/genshin/state/steps").toStringList().value(0).endsWith(":c"));
        s.reset();
        const GameState cb = Games::loadState(Games::get("genshin"));
        CHECK(cb.steps.size() == 3 && cb.steps[0].press == ActionStep::Click && cb.steps[0].afterMs == 10);
        // «повтор через» в «По шагам» — от 0 (пауза между клавишами), у одного действия — от минимума игры
        CHECK(cb.repeatMin(GameMode::Steps, 20) == 0 && cb.repeatMin(GameMode::Sequence, 20) == 20);
        GameState two = cb; two.actions = {st("e", ActionStep::Click, 0), st("f", ActionStep::Click, 0)};
        CHECK(two.repeatMin(GameMode::Sequence, 20) == 0 && two.repeatMin(GameMode::Combo, 20) == 20 &&
              two.repeatMin(GameMode::Hold, 20) == 20);
        // круг целиком: где «повтор через» от 0 — от 1 мс (один шаг тоже), иначе — от минимума игры
        GameState one = cb; one.steps = {st("e", D, 0)}; one.actions = {st("e", ActionStep::Click, 0)};
        CHECK(one.minCycle(GameMode::Steps, 20) == 1 && cb.minCycle(GameMode::Steps, 20) == 1);
        CHECK(two.minCycle(GameMode::Sequence, 20) == 1 && one.minCycle(GameMode::Sequence, 20) == 20 &&
              two.minCycle(GameMode::Combo, 20) == 20 && minCycleMs(GameMode::Hold, 3, 20) == 20);
        Games::saveState("genshin", back);
    }
    CHECK(back.startDelayMs == kMaxStartDelayMs);
    { auto s = AppSettings::open(); s->setValue("games/genshin/state/startDelay", -5); s->sync(); }
    CHECK(Games::loadState(Games::get("genshin")).startDelayMs == 0);
    // без шагов и без задержки — ключей нет (файл как у 1.13)
    save.steps.clear(); save.startDelayMs = 0;
    Games::saveState("genshin", save);
    { auto s = AppSettings::open();
      CHECK(!s->contains("games/genshin/state/steps") && !s->contains("games/genshin/state/startDelay")); }
    // больше kMaxSteps — обрезается
    {
        QStringList many;
        for (int i = 0; i < kMaxSteps + 10; ++i) many << (i % 2 ? "k:f:0:1:u" : "k:f:0:1:d");
        auto s = AppSettings::open(); s->setValue("games/genshin/state/steps", many); s->sync();
    }
    CHECK(Games::loadState(Games::get("genshin")).steps.size() == kMaxSteps);

    // профиль в режиме «По шагам»: режим и шаги не теряются ни при сохранении, ни при экспорте
    GameState prof = Games::defaultState(Games::get("genshin"));
    prof.steps = kick;
    prof.startDelayMs = 250;
    Games::saveProfile("genshin", "Кикфлип", prof, static_cast<int>(GameMode::Steps));
    int mode = -1;
    CHECK(Games::loadProfile("genshin", "Кикфлип", &mode) == prof && mode == 3);
    const QString out = AppSettings::dataDir() + "/kick.ini";
    CHECK(Games::exportProfile("genshin", "Кикфлип", out));
    const QString imported = Games::importProfile("genshin", out);
    CHECK(imported == "Кикфлип (2)");
    mode = -1;
    CHECK(Games::loadProfile("genshin", imported, &mode) == prof && mode == 3);
    CHECK(Games::renameProfile("genshin", imported, "Кик 2"));
    mode = -1;
    CHECK(Games::loadProfile("genshin", "Кик 2", &mode).steps == kick && mode == 3);

    // клавиша старт/стоп из двух клавиш: «держу Tab, жму F»
    const HotkeySpec tf = HotkeySpec::parse("tab+f");
    CHECK(tf.isValid() && tf.isGlobalSafe() && tf.isChord() && tf.hold == "tab" && tf.key == "f" && tf.mods == 0);
    CHECK(tf.toString() == "tab+f" && tf.display() == "Tab+F" && tf.passthrough());
    CHECK(HotkeySpec::parse(tf.toString()) == tf && tf != HotkeySpec::parse("f") && tf != HotkeySpec::parse("e+f"));
    const HotkeySpec stf = HotkeySpec::parse("Shift+Tab+F");
    CHECK(stf.mods == ModShift && stf.hold == "tab" && stf.toString() == "shift+tab+f" && stf.display() == "Shift+Tab+F");
    CHECK(HotkeySpec::parse("tab+ctrl+f6").toString() == "ctrl+tab+f6" && HotkeySpec::parse("tab+f6").passthrough());
    const HotkeySpec ct = HotkeySpec::parse("ctrl+tab");                    // Ctrl — модификатор, не сочетание
    CHECK(ct.isValid() && !ct.isChord() && ct.mods == ModCtrl && ct.key == "tab");
    for (const char* bad : {"tab+tab", "tab+e+f", "tab+shift", "mouse:8+f", "tab+mouse:8", "capslock+f", "nosuch+f"})
        CHECK(!HotkeySpec::parse(bad).isValid());
    CHECK(HotkeySpec::parse("tab+shift").hold.isEmpty() && HotkeySpec::parse("tab+mouse:8").mods == 0);
    HotkeySpec manual; manual.key = "f"; manual.hold = "f";
    CHECK(!manual.isValid() && !manual.isGlobalSafe());
    manual.hold = "shift";
    CHECK(!manual.isValid());
    manual.hold = "space";
    CHECK(manual.isValid() && manual.display() == "Пробел+F");
    // сочетание сохраняется у игры и в профиле
    Games::setStartKey("genshin", tf);
    CHECK(Games::startKey("genshin") == tf);
    Games::setProfileKey("genshin", "Кик 2", stf);
    CHECK(Games::profileKey("genshin", "Кик 2") == stf);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
}

// 1.17: группы действий — как функции
static void testGroups() {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    AppSettings::setScope("AutoQTest", "grp_" + pid);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    CHECK(Games::addBuiltin("genshin"));
    CHECK(Games::addBuiltin("minecraft"));
    const int D = ActionStep::Down, U = ActionStep::Up, C = ActionStep::Click;
    auto act = [](const QString& item, int hold, int after) {
        ActionStep a = ActionStep::fromItem(item); a.holdMs = hold; a.afterMs = after; return a;
    };
    auto stp = [](const QString& item, int press, int after) {
        ActionStep a = ActionStep::fromItem(item); a.press = press; a.afterMs = after; return a;
    };
    auto call = [](const QString& id, int rep, int after) {
        ActionStep a; a.group = id; a.repeat = rep; a.afterMs = after; a.mouse = false; a.key.clear(); return a;
    };
    // id: 8 знаков 0-9a-f, новые не совпадают с занятыми
    QList<ActionGroup> gs;
    for (int i = 0; i < 30; ++i) {
        ActionGroup g; g.id = Groups::newId(gs); g.name = Groups::freeName(gs);
        CHECK(Groups::validId(g.id));
        gs << g;
    }
    CHECK(gs[0].name == "Группа 1" && gs[29].name == "Группа 30");
    CHECK(!Groups::validId("G1234567") && !Groups::validId("abc") && !Groups::validId("0123456789"));
    CHECK(Groups::validName(" Кик ") && !Groups::validName("  ") && !Groups::validName(QString(41, 'x')));

    // группа действий: ЛКМ (держать 40), пауза 15, F
    ActionGroup combo; combo.id = "0a0b0c0d"; combo.name = "Комбо";
    combo.items = {act("mouse:1", 40, 15), act("f", 0, 999)};      // пауза у последнего — не используется
    combo.betweenMs = 25;                                          // между повторами ×N
    // ряд: E, пауза 10, [Комбо ×3, пауза 70], Q
    const QList<ActionStep> chain{act("e", 0, 10), call(combo.id, 3, 70), act("q", 0, 5)};
    QList<int> owner;
    const QList<ActionStep> run = Groups::expand(chain, {combo}, false, false, &owner);
    CHECK(run.size() == 1 + 2 * 3 + 1);
    CHECK(run[0].key == "e" && run[1].mouse && run[1].holdMs == 40 && run[1].afterMs == 15);
    // между повторами — пауза группы, после последнего — пауза вызова (она не меняет скорость группы)
    CHECK(run[2].key == "f" && run[2].afterMs == 25 && run[4].key == "f" && run[4].afterMs == 25);
    CHECK(run[6].key == "f" && run[6].afterMs == 70 && run[7].key == "q");
    CHECK(Groups::betweenMs(combo, chain[1]) == 25);
    {   // ×1 — только пауза вызова; группа из 1.17.0 (-1) — между повторами пауза вызова, как тогда
        const QList<ActionStep> r1 = Groups::expand({call(combo.id, 1, 70)}, {combo}, false);
        CHECK(r1.size() == 2 && r1[1].afterMs == 70);
        ActionGroup old = combo; old.betweenMs = -1;
        const QList<ActionStep> ro = Groups::expand(chain, {old}, false);
        CHECK(ro[2].afterMs == 70 && ro[4].afterMs == 70 && ro[6].afterMs == 70);
        CHECK(Groups::betweenMs(old, chain[1]) == 70);
    }
    CHECK(owner == (QList<int>{0, 1, 1, 1, 1, 1, 1, 2}));
    for (const ActionStep& a : run) CHECK(!a.isGroup());
    // «Вместе» / «Зажать» — без повторов
    CHECK(Groups::expand(chain, {combo}, false, true).size() == 4);
    // группы нет — вызов ничего не жмёт
    CHECK(Groups::expand(chain, {}, false).size() == 2);
    // в ряду «По шагам» группа из действий — шагами
    const QList<ActionStep> srun = Groups::expand({call(combo.id, 1, 30)}, {combo}, true);
    CHECK(srun.size() == 4);
    CHECK(srun[0].press == D && srun[0].mouse && srun[0].afterMs == 40);
    CHECK(srun[1].press == U && srun[1].afterMs == 15);
    CHECK(srun[3].press == U && srun[3].key == "f" && srun[3].afterMs == 30);
    // группа из шагов (кикфлип) в ряду действий (файл правили руками) — «нажать» кликом, «отпустить» — нет
    ActionGroup kick; kick.id = "deadbeef"; kick.name = "Кикфлип"; kick.steps = true;
    kick.items = {stp("mouse:1", D, 30), stp("mouse:3", C, 50), stp("mouse:1", U, 0)};
    const QList<ActionStep> kr = Groups::expand({call(kick.id, 1, 0)}, {kick}, false);
    CHECK(kr.size() == 2 && kr[0].press == C && kr[1].button == 3);
    // разгруппировать: те же шаги, что жал кликер
    CHECK(Groups::inlineCall(call(combo.id, 2, 70), combo, false) == Groups::expand({call(combo.id, 2, 70)}, {combo}, false));
    // удалили группу — её вызовы становятся действиями, остальное на месте
    const QList<ActionStep> flat = Groups::inlineAll(chain, combo, false, kMaxActions);
    CHECK(flat == run);
    CHECK(Groups::inlineAll(chain, kick, false, kMaxActions) == chain);          // чужая группа — без изменений
    CHECK(Groups::inlineAll(chain, combo, false, 3).size() == 3);
    CHECK(Groups::usedIds(chain) == QStringList{combo.id});
    CHECK(Groups::uses(chain + chain, combo.id) == 2);
    CHECK(Groups::preview(kick) == "ЛКМ ↓ · ПКМ ↕ · ЛКМ ↑");
    CHECK(Groups::preview(combo, 1) == "ЛКМ · +1");
    // вызов — не кнопка: с ЛКМ не путается
    CHECK(!call(combo.id, 1, 0).sameInput(ActionStep::fromItem("mouse:1")));
    CHECK(call(combo.id, 1, 0) != call(combo.id, 2, 0));

    // состояние: ряд с вызовом, кликер жмёт развёрнутое
    GameState st;
    st.actions = chain;
    st.groups = {combo};
    CHECK(st.multi());
    CHECK(st.chain(GameMode::Sequence) == chain);
    CHECK(st.runList(GameMode::Sequence) == run);
    CHECK(st.runOwners(GameMode::Sequence) == owner);
    CHECK(st.pressList(GameMode::Sequence).size() == run.size());
    CHECK(st.repeatMin(GameMode::Sequence, 20) == 0);        // в круге 8 нажатий — «повтор через» от 0
    GameState one; one.actions = {call(combo.id, 1, 0)}; one.groups = {combo};
    CHECK(one.multi() && one.repeatMin(GameMode::Sequence, 20) == 0);
    // «По шагам» без своих шагов: из действий, вызов остаётся вызовом
    const QList<ActionStep> sc = st.chain(GameMode::Steps);
    CHECK(sc.size() == 2 + 1 + 2 && sc[2].isGroup() && sc[2].press == C);
    CHECK(st.runList(GameMode::Steps).size() == 2 + 4 * 3 + 2);
    CHECK(st.usesMouse(GameMode::Sequence));
    // в сравнении групп нет: поправил группу — профиль не «несохранённый»
    GameState st2 = st; st2.groups.clear();
    CHECK(st2 == st);

    // хранение у игры: группы и вызовы переживают запись
    Games::saveGroups("genshin", {combo, kick});
    const QList<ActionGroup> back = Games::groups("genshin");
    CHECK(back.size() == 2 && back[0] == combo && back[1].items == kick.items && back[1].steps);
    CHECK(Games::groups("minecraft").isEmpty());
    GameState gs1 = Games::loadState(Games::get("genshin"));
    CHECK(gs1.groups.size() == 2);
    gs1.actions = chain;
    gs1.steps = {stp("e", D, 5), call(kick.id, 2, 40), stp("e", U, 0)};
    Games::saveState("genshin", gs1);
    const GameState gs2 = Games::loadState(Games::get("genshin"));
    CHECK(gs2.actions == chain && gs2.steps == gs1.steps);
    CHECK(gs2.steps[1].isGroup() && gs2.steps[1].repeat == 2 && gs2.steps[1].press == C);
    CHECK(gs2.runList(GameMode::Steps).size() == 1 + 3 * 2 + 1);

    // профиль: копии вызванных групп — в файле; на другом ПК (группы у игры нет) — приходят с ним
    Games::saveProfile("genshin", "С группой", gs2, 3);
    {
        QSettings f(Games::profileFile("genshin", "С группой"), QSettings::IniFormat);
        CHECK(f.value("profile/groups").toStringList() == (QStringList{combo.id, kick.id}));
        CHECK(f.value("profile/group/deadbeef/name").toString() == "Кикфлип");
    }
    const QString file = QDir::temp().filePath("aq_grp_" + pid + ".ini");
    CHECK(Games::exportProfile("genshin", "С группой", file));
    Games::saveGroups("genshin", {});                         // «другой ПК»: групп нет
    CHECK(Games::groups("genshin").isEmpty());
    int mode = -1;
    const GameState lp = Games::loadProfile("genshin", "С группой", &mode);
    CHECK(mode == 3 && lp.groups.size() == 2);                // профиль принёс группы
    CHECK(Games::groups("genshin").size() == 2);              // и они теперь у игры
    // своя (поправленная) группа важнее копии в профиле
    ActionGroup edited = combo; edited.items = {act("r", 0, 0)};
    Games::saveGroups("genshin", {edited, kick});
    CHECK(Games::loadProfile("genshin", "С группой").groups[0].items == edited.items);
    // импорт в другую игру: группы — к ней
    QString from;
    const QString name = Games::importProfile("minecraft", file, nullptr, &from);
    CHECK(!name.isEmpty() && Games::groups("minecraft").size() == 2);
    CHECK(Games::loadProfile("minecraft", name).actions == chain);
    QFile::remove(file);

    // группа из 1.17.0 (без паузы между повторами): становится паузой после её вызова в ряду —
    // круг жмётся как раньше; запись — уже с паузой
    {
        auto s = AppSettings::open();
        s->remove("games/genshin/group");
        s->setValue("games/genshin/groups", QStringList{"0a0b0c0d", "deadbeef"});
        s->setValue("games/genshin/group/0a0b0c0d/name", "Комбо");
        s->setValue("games/genshin/group/0a0b0c0d/items", QStringList{"m:1:40:15", "k:f:0:0"});
        s->setValue("games/genshin/group/deadbeef/name", "Не в ряду");
        s->setValue("games/genshin/group/deadbeef/items", QStringList{"k:r:0:0"});
        s->setValue("games/genshin/state/actions", QStringList{"k:e:0:10", "g:0a0b0c0d:3:70", "k:q:0:5"});
        s->setValue("games/genshin/state/steps", QStringList{});
        s->sync();
    }
    CHECK(Games::groups("genshin").value(0).betweenMs == -1);
    const GameState legacy = Games::loadState(Games::get("genshin"));
    CHECK(legacy.groups.size() == 2 && legacy.groups[0].betweenMs == 70);
    CHECK(legacy.groups[1].betweenMs == -1);                  // не стоит в ряду — пока как было
    CHECK(Games::groups("genshin")[0].betweenMs == 70);       // записано
    {
        const QList<ActionStep> lr = legacy.runList(GameMode::Sequence);
        CHECK(lr.size() == 8 && lr[2].afterMs == 70 && lr[4].afterMs == 70 && lr[6].afterMs == 70);
    }

    // старая запись без групп и мусор — не ломают чтение
    {
        auto s = AppSettings::open();
        s->setValue("games/genshin/state/actions", QStringList{"g:XYZ:1:0", "k:f:0:0", "g:0a0b0c0d:500:-5"});
        s->setValue("games/genshin/groups", QStringList{"0a0b0c0d", "bad", "0a0b0c0d"});
        s->sync();
    }
    const GameState gs3 = Games::loadState(Games::get("genshin"));
    CHECK(gs3.actions.size() == 2 && !gs3.actions[0].isGroup());
    CHECK(gs3.actions[1].repeat == kMaxRepeat && gs3.actions[1].afterMs == 0);
    CHECK(gs3.groups.size() == 1);
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
}

// --- автообновление: ответ GitHub, версии, файл под систему, контрольные суммы ---
static void testUpdates() {
    using namespace Updates;
    CHECK(versionNumbers("1.17.0 beta") == QList<int>({1, 17, 0}));
    CHECK(versionNumbers("v1.17.1") == QList<int>({1, 17, 1}));
    CHECK(versionNumbers("Version 2.0 beta") == QList<int>({2, 0}));
    CHECK(versionNumbers("beta").isEmpty());
    CHECK(compareVersions("v1.17.1", "1.17.0 beta") > 0);
    CHECK(compareVersions("1.17", "1.17.0") == 0);
    CHECK(compareVersions("1.9.0", "1.10.0") < 0);                 // числами, а не строкой
    CHECK(compareVersions("v1.16.2", "1.17.0 beta") < 0);
    CHECK(isNewer("v1.17.1", "1.17.0 beta"));
    CHECK(!isNewer("v1.17.0", "1.17.0 beta"));
    CHECK(!isNewer("v1.16.2", "1.17.0 beta"));
    CHECK(!isNewer("", "1.17.0 beta"));
    CHECK(latestApiUrl() == "https://api.github.com/repos/QIISIT/Autoq/releases/latest");

    // как отвечает GitHub (сокращённо, по релизу v1.17.0)
    const QByteArray json = R"({
        "tag_name": "v1.17.1", "name": "Version 1.17.1 beta", "draft": false, "prerelease": false,
        "html_url": "https://github.com/QIISIT/Autoq/releases/tag/v1.17.1",
        "assets": [
          {"name": "AutoQ-1.17.1-beta-macOS.dmg", "size": 25453647,
           "browser_download_url": "https://github.com/QIISIT/Autoq/releases/download/v1.17.1/AutoQ-1.17.1-beta-macOS.dmg",
           "digest": "sha256:262E56CF7241A9DA13789076AE9525F8306F50533492936D9C13F53C6572D492"},
          {"name": "AutoQ-1.17.1-beta-Windows-Portable.zip", "size": 10098726,
           "browser_download_url": "https://github.com/QIISIT/Autoq/releases/download/v1.17.1/AutoQ-1.17.1-beta-Windows-Portable.zip",
           "digest": "sha256:eb0983244fd2bc5154e6c3218d9f48671aebda592309f65ce2ec67db2d245c06"},
          {"name": "AutoQ-1.17.1-beta-Windows-Setup.exe", "size": 7998920,
           "browser_download_url": "https://github.com/QIISIT/Autoq/releases/download/v1.17.1/AutoQ-1.17.1-beta-Windows-Setup.exe"},
          {"name": "autoq-v1171.tar.gz", "size": 750002,
           "browser_download_url": "https://github.com/QIISIT/Autoq/releases/download/v1.17.1/autoq-v1171.tar.gz",
           "digest": "sha256:a7fd033f27e2acc12f4c3529d45c066b45022768b7a9a28e5b886bbb1662a5b8"},
          {"name": "autoq-v1171.tar.gz.sig", "size": 10,
           "browser_download_url": "https://github.com/QIISIT/Autoq/releases/download/v1.17.1/autoq-v1171.tar.gz.sig"},
          {"name": "evil.exe", "size": 1, "browser_download_url": "http://example.com/evil.exe"}
        ]})";
    QString err;
    const Release r = parseRelease(json, &err);
    CHECK(r.valid());
    CHECK(r.tag == "v1.17.1");
    CHECK(r.version == "1.17.1 beta");
    CHECK(r.page.endsWith("/releases/tag/v1.17.1"));
    CHECK(r.assets.size() == 5);                                    // без https — не берём
    const Asset* setup = pickAsset(r, Package::Setup);
    const Asset* portable = pickAsset(r, Package::Portable);
    const Asset* source = pickAsset(r, Package::Source);
    const Asset* dmg = pickAsset(r, Package::Dmg);
    CHECK(setup && setup->name == "AutoQ-1.17.1-beta-Windows-Setup.exe" && setup->sha256.isEmpty());
    CHECK(portable && portable->size == 10098726 &&
          portable->sha256 == "eb0983244fd2bc5154e6c3218d9f48671aebda592309f65ce2ec67db2d245c06");
    CHECK(source && source->name == "autoq-v1171.tar.gz");          // не .sig
    CHECK(dmg && dmg->sha256 == "262e56cf7241a9da13789076ae9525f8306f50533492936d9c13f53c6572d492");   // строчными
    CHECK(pickAsset(r, Package::None) == nullptr);
    for (Package p : {Package::Setup, Package::Portable, Package::Source, Package::Dmg})
        CHECK(packageFromKey(packageKey(p)) == p);
    CHECK(packageFromKey("") == Package::None && packageKey(Package::None).isEmpty());

    // название без номера — версия из тега; пререлиз, черновик и мусор — не релиз
    QByteArray noName = json;
    noName.replace("\"Version 1.17.1 beta\"", "\"Big update\"");
    CHECK(parseRelease(noName).version == "1.17.1");
    QByteArray pre = json;
    pre.replace("\"prerelease\": false", "\"prerelease\": true");
    CHECK(!parseRelease(pre).valid());
    CHECK(!parseRelease("{\"message\": \"API rate limit exceeded\"}").valid());
    CHECK(!parseRelease("<html>").valid());

    const QByteArray sums = "64cc08bf42b5e744e544ae2894000dd65402d54eec5c238e52de55875880ad05  AutoQ-1.17.1-beta-Windows-Setup.exe\n"
                            "A7FD033F27E2ACC12F4C3529D45C066B45022768B7A9A28E5B886BBB1662A5B8 *autoq-v1171.tar.gz\r\n";
    CHECK(sumFor(sums, "AutoQ-1.17.1-beta-Windows-Setup.exe") == "64cc08bf42b5e744e544ae2894000dd65402d54eec5c238e52de55875880ad05");
    CHECK(sumFor(sums, "autoq-v1171.tar.gz") == "a7fd033f27e2acc12f4c3529d45c066b45022768b7a9a28e5b886bbb1662a5b8");
    CHECK(sumFor(sums, "AutoQ-Setup.exe").isEmpty());

    QTemporaryDir tmp;
    QFile f(tmp.filePath("abc.bin"));
    CHECK(f.open(QIODevice::WriteOnly) && f.write("abc") == 3);
    f.close();
    CHECK(sha256OfFile(f.fileName()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256OfFile(tmp.filePath("none.bin")).isEmpty());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testKeys();
    testHotkeys();
    testMatching();
    testGames();
    testV15();
    testModes();
    testV17();
    testV19();
    testV112();
    testV113();
    testV114();
    testGroups();
    testDataDir();
    testCardLayout();
    testUpdates();
    testI18n();
    {
        const QString pid = QString::number(QCoreApplication::applicationPid());
        for (const char* sc : {"t", "v15_", "imp_", "empty_", "modes_", "v17_", "v19_", "v112_", "v113_", "v114_", "snd_", "fs_", "grp_"})
            AppSettings::dropScope("AutoQTest", QString::fromLatin1(sc) + pid);
    }
    std::printf("\n%d проверок пройдено, %d провалено\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
