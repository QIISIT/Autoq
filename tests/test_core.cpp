// Юнит-тесты ядра (без интерфейса). Запуск: test_core
// Собирается и под Linux, и под Windows (проверяется в Wine).
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QSet>
#include <QRegularExpression>
#include <QFile>
#include "i18n.h"
#include "sounds.h"
#include "framesync.h"
#include <thread>
#include <cstdio>
#include "keys.h"
#include "winmatch.h"
#include "games.h"
#include "clicker.h"

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
    // мышь
    CHECK(MouseButtons::buttonForIndex(0) == 1);
    CHECK(MouseButtons::buttonForIndex(1) == 3);
    CHECK(MouseButtons::buttonForIndex(2) == 2);
    for (int b : {1, 2, 3}) CHECK(MouseButtons::buttonForIndex(MouseButtons::indexForButton(b)) == b);
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
    // минимум интервала игры соблюдается при чтении
    st.intervalMs = 5; Games::saveState("genshin", st);
    CHECK(Games::loadState(Games::get("genshin")).intervalMs == 20);
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
    CHECK(Prefs::captureChoices() == (QList<int>{3, 5, 7, 10}));
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
        QSettings s("AutoQTest", sc); s.clear(); s.sync();
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
    { auto s = AppSettings::open(); s->remove("games/minecraft/profiles/Без режима"); s->sync(); }
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
    CHECK(back.usesMouse() && back.multi());
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

    { QSettings s("AutoQTest", "v112_" + pid); s.clear(); s.sync(); }
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
    Sounds::setSound(Sounds::Off, "file:/tmp/x.wav");
    CHECK(Sounds::sound(Sounds::On) == "coin" && Sounds::sound(Sounds::Off) == "file:/tmp/x.wav");
    CHECK(Sounds::label("file:/tmp/x.wav") == "x.wav");
    Sounds::setSound(Sounds::On, "нет-такого");
    CHECK(Sounds::sound(Sounds::On) == "ticks-up");     // неизвестный — по умолчанию
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    for (const QString& sc : {"v113_" + pid, "snd_" + pid}) { QSettings s("AutoQTest", sc); s.clear(); s.sync(); }

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
    { QSettings s("AutoQTest", "fs_" + pid); s.clear(); s.sync(); }

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
    testI18n();
    std::printf("\n%d проверок пройдено, %d провалено\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
