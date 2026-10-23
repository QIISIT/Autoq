// Оверлей КПС: настройки сохраняются, размер и рисование (без экрана — offscreen)
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include "overlay.h"
#include "games.h"
#include "theme.h"
#include "keycapturebutton.h"
#include "styles.h"
#include "cardlayout.h"
#include <QKeyEvent>
#include <QFileInfo>
#include <QJsonObject>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Qt пишет «Could not parse application stylesheet», если правила стиля с ошибкой
static int g_qssErrors = 0;
static void qssMessages(QtMsgType, const QMessageLogContext&, const QString& msg) {
    if (msg.contains("Could not parse")) ++g_qssErrors;
}

static int painted(const OverlaySettings& s, const OverlayView& v) {
    const QSize sz = OverlayPaint::size(s, v);
    QImage img(sz, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    OverlayPaint::paint(p, QPointF(0, 0), s, v);
    p.end();
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 0) ++n;
    return n;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppSettings::setScope("AutoQTest", QString("ov_%1").arg(QCoreApplication::applicationPid()));
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    Theme::apply(app, true);

    // по умолчанию выключен, справа сверху, «КПС»
    OverlaySettings d = OverlaySettings::load();
    CHECK(!d.enabled);
    CHECK(d.anchor == 2);
    CHECK(d.label == "КПС");
    CHECK(!d.numberColor.isValid());
    CHECK(d.windowMs == 1000);

    // всё сохраняется и читается
    OverlaySettings s = d;
    s.enabled = true; s.when = 1; s.showGraph = true; s.showTotal = true; s.showStatus = false;
    s.label = "CPS"; s.anchor = 6; s.offsetX = 40; s.offsetY = -12; s.screen = 1;
    s.textColor = QColor("#112233"); s.numberColor = QColor("#AABBCC"); s.bgColor = QColor("#334455");
    s.bgOpacity = 30; s.opacity = 80; s.scale = 150; s.font = 2; s.radius = 4; s.vertical = true;
    s.outline = false; s.windowMs = 2000;
    s.save();
    OverlaySettings r = OverlaySettings::load();
    CHECK(r.enabled && r.when == 1 && r.showGraph && r.showTotal && !r.showStatus);
    CHECK(r.label == "CPS" && r.anchor == 6 && r.offsetX == 40 && r.offsetY == -12 && r.screen == 1);
    CHECK(r.textColor == QColor("#112233") && r.numberColor == QColor("#AABBCC") && r.bgColor == QColor("#334455"));
    CHECK(r.bgOpacity == 30 && r.opacity == 80 && r.scale == 150 && r.font == 2 && r.radius == 4);
    CHECK(r.vertical && !r.outline && r.windowMs == 2000);
    // «как акцент» — пустой цвет числа
    s.numberColor = QColor(); s.save();
    CHECK(!OverlaySettings::load().numberColor.isValid());
    // мусор в настройках — в допустимые пределы
    { auto st = AppSettings::open(); st->setValue("overlay/scale", 9999); st->setValue("overlay/anchor", 42);
      st->setValue("overlay/windowMs", 777); st->setValue("overlay/opacity", 0);
      st->setValue("overlay/textColor", "нет такого"); st->sync(); }
    OverlaySettings g = OverlaySettings::load();
    CHECK(g.scale == 300 && g.anchor == 8 && g.windowMs == 1000 && g.opacity == 15);
    CHECK(g.textColor == QColor("#FFFFFF"));

    // размер: больше масштаб — больше; столбиком — выше и уже
    OverlaySettings a = OverlaySettings::defaults();
    const OverlayView v = OverlayPaint::sample();
    const QSize s100 = OverlayPaint::size(a, v);
    a.scale = 200;
    const QSize s200 = OverlayPaint::size(a, v);
    CHECK(s200.width() > s100.width() * 1.6 && s200.height() > s100.height() * 1.6);
    a.scale = 100; a.vertical = true;
    const QSize vert = OverlayPaint::size(a, v);
    CHECK(vert.height() > s100.height() && vert.width() < s100.width());
    // меньше элементов — меньше оверлей; без всего — всё равно число
    OverlaySettings only = OverlaySettings::defaults();
    only.showActions = false; only.showStatus = false;
    CHECK(OverlayPaint::size(only, v).width() < s100.width());
    OverlaySettings none = only; none.showCps = false;
    CHECK(OverlayPaint::size(none, v).width() > 20);
    // график и «всего» добавляют места
    OverlaySettings more = OverlaySettings::defaults();
    more.showGraph = true; more.showTotal = true;
    CHECK(OverlayPaint::size(more, v).width() > s100.width());
    // рисуется что-то видимое; без подложки — меньше закрашенных точек
    OverlaySettings bg = OverlaySettings::defaults();
    const int withBg = painted(bg, v);
    bg.bgOpacity = 0;
    const int noBg = painted(bg, v);
    CHECK(withBg > 0 && noBg > 0 && noBg < withBg);
    // режим «Зажать» и одно действие
    OverlayView h = v; h.hold = true; h.a2.clear();
    CHECK(painted(OverlaySettings::defaults(), h) > 0);
    OverlayView one = v; one.a2.clear();
    CHECK(OverlayPaint::size(OverlaySettings::defaults(), one).width() < s100.width());

    // И2: несколько игр — одна плашка, по строке на игру, у каждой строки название первым
    {
        const OverlaySettings d = OverlaySettings::defaults();
        CHECK(OverlayPaint::size(d, QList<OverlayView>{v}) == OverlayPaint::size(d, v));
        OverlayView named = v; named.named = true;
        CHECK(OverlayPaint::size(d, named).width() > OverlayPaint::size(d, v).width());
        OverlayView second = one; second.named = true; second.game = "Minecraft (Java)"; second.profile = "Быстро";
        const QSize a = OverlayPaint::size(d, named), b = OverlayPaint::size(d, second);
        const QSize two = OverlayPaint::size(d, QList<OverlayView>{named, second});
        // общие поля: ниже, чем две плашки отдельно, но выше любой одной
        CHECK(two.height() < a.height() + b.height() && two.height() > qMax(a.height(), b.height()));
        // колонки общие: ширина — по самой широкой строке
        CHECK(two.width() >= qMax(a.width(), b.width()) && two.width() <= a.width() + b.width());
        QImage img(two, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            OverlayPaint::paint(p, QPointF(0, 0), d, QList<OverlayView>{named, second});
        }
        // одна сплошная плашка: фон без щелей сверху донизу и по всей ширине
        bool solid = true;
        for (int y = 16; y < two.height() - 16; ++y)
            if (qAlpha(img.pixel(4, y)) == 0 || qAlpha(img.pixel(two.width() - 5, y)) == 0) solid = false;
        CHECK(solid);
        CHECK(qAlpha(img.pixel(two.width() - 8, two.height() - 8)) > 0 && qAlpha(img.pixel(8, two.height() - 8)) > 0);
        // столбиком — тоже одна плашка: строки друг под другом
        OverlaySettings vd = d; vd.vertical = true;
        const QSize vtwo = OverlayPaint::size(vd, QList<OverlayView>{named, second});
        CHECK(vtwo.height() > OverlayPaint::size(vd, named).height());
        CHECK(vtwo.height() < OverlayPaint::size(vd, named).height() + OverlayPaint::size(vd, second).height());
    }

    // вид мелких элементов (редактор интерфейса): пределы, JSON, сохранение, правила стиля
    {
        Theme::Elements e;
        Theme::ElementStyle k; k.look = 1; k.color = "#3b82f6"; k.radius = 30; k.size = 2;
        e["key"] = k;
        Theme::ElementStyle m; m.hidden = true; e["mode"] = m;             // кнопку режима не спрятать
        Theme::ElementStyle h; h.hidden = true; h.size = 0; e["hint"] = h;
        Theme::ElementStyle x; x.look = 3; e["nope"] = x;                  // такой группы нет
        Theme::ElementStyle st; st.look = 7; e["start"] = st;              // такого вида у «Старта» нет
        const Theme::Elements n = Theme::normalizeElements(e);
        CHECK(n.size() == 2 && n.contains("key") && n.contains("hint"));
        CHECK(n.value("key").color == "#3B82F6" && n.value("key").radius == 24 && n.value("key").size == 2 && n.value("key").look == 1);
        CHECK(n.value("hint").hidden && n.value("hint").size == 0);
        CHECK(Theme::elementsFromJson(Theme::elementsToJson(e)) == n);
        CHECK(Theme::elementsToJson({}).isEmpty());
        Theme::Appearance a = Theme::current();
        a.elements = n;
        Theme::apply(app, a);
        Theme::saveAppearance(a);
        CHECK(Theme::loadAppearance().elements == n);
        CHECK(app.styleSheet().contains("QPushButton#keyPickBig, QToolButton#stepDir {"));
        CHECK(app.styleSheet().contains("QLabel#small[aqEl=\"hint\"] { font-size: 10px; }"));
        CHECK(Theme::element("key").look == 1 && Theme::elementColor("key", Qt::red) == QColor("#3B82F6"));
        CHECK(Theme::elementColor("slot", Qt::red) == QColor(Qt::red));
        CHECK(Theme::elementRadius("slot", 12) == 12 && Theme::elementRadius("key", 8) == 24);
        // у каждой группы есть название, пояснение, варианты; все виды с цветом, скруглением
        // и размером — правила, которые Qt разбирает без ошибок
        const QtMessageHandler prev = qInstallMessageHandler(qssMessages);
        for (const QString& id : Theme::elementIds()) {
            const int caps = Theme::elementCaps(id);
            CHECK(caps != 0 && !Theme::elementLabel(id).isEmpty() && !Theme::elementNote(id).isEmpty());
            CHECK(Theme::elementLooks(id).size() >= ((caps & Theme::ElLook) ? 3 : 1));
            CHECK(Theme::elementSizes(id).size() == 3);
            for (int look = 0; look < Theme::elementLooks(id).size(); ++look) {
                Theme::ElementStyle es;
                es.look = look; es.color = "#22C55E"; es.radius = 4; es.size = look % 3;
                es.hidden = (caps & Theme::ElHide) && look == 0;
                a.elements = {{id, es}};
                Theme::apply(app, a);
                CHECK(Theme::element(id).look == look);
                QWidget probe;                     // стиль применяется к виджету — тут Qt и разбирает правила
                probe.ensurePolished();
            }
        }
        qInstallMessageHandler(prev);
        CHECK(g_qssErrors == 0);
        a.elements.clear();
        Theme::apply(app, a);
        Theme::saveAppearance(a);
        CHECK(Theme::loadAppearance().elements.isEmpty());
    }

    // профили стиля: файл в папке данных, имя без учёта регистра, раскладка, переименование
    {
        CHECK(Styles::names().isEmpty());
        Styles::Profile p;
        p.name = "Синий неон";
        p.look = Theme::current();
        p.look.accent = QColor("#3B82F6");
        p.look.dark = false;
        p.look.radius = 20;
        p.look.elements["slot"].look = 2;
        CHECK(Styles::save(p));
        CHECK(Styles::exists("синий НЕОН"));
        Styles::Profile q;
        CHECK(Styles::load("Синий неон", &q));
        CHECK(q.name == "Синий неон" && Styles::sameLook(q.look, p.look) && !q.hasLayout());
        CHECK(q.look.elements.value("slot").look == 2 && !q.look.dark && q.look.radius == 20);
        CHECK(!Styles::sameLook(q.look, Theme::current()));
        // с раскладкой: «/» в имени — «_» в имени файла
        const CardLayout::Layout l = CardLayout::templateLayout(1);
        p.name = "Компакт/тёмный";
        p.layout = CardLayout::exportJson(l, 2);
        CHECK(Styles::save(p));
        CHECK(QFileInfo::exists(Styles::dir() + "/Компакт_тёмный.aqstyle"));
        CHECK(Styles::load("компакт/тёмный", &q) && q.hasLayout());
        CardLayout::Layout l2;
        int side = 0;
        CHECK(CardLayout::importJson(q.layout, &l2, &side) && l2 == CardLayout::normalize(l) && side == 2);
        CHECK(Styles::names() == QStringList({"Компакт/тёмный", "Синий неон"}));
        CHECK(Styles::uniqueName("Синий неон") == "Синий неон (2)" && Styles::uniqueName("Новый") == "Новый");
        // то же имя — замена, а не второй файл
        p.look.radius = 6;
        CHECK(Styles::save(p) && Styles::names().size() == 2);
        CHECK(Styles::load("Компакт/тёмный", &q) && q.look.radius == 6);
        // переименовать: занято — нельзя, свободно — и файл под новым именем
        QString why;
        CHECK(!Styles::rename("Синий неон", "компакт/ТЁМНЫЙ", &why) && !why.isEmpty());
        CHECK(Styles::rename("Синий неон", "Морской"));
        CHECK(Styles::exists("Морской") && !Styles::exists("Синий неон"));
        CHECK(QFileInfo::exists(Styles::dir() + "/Морской.aqstyle"));
        CHECK(Styles::rename("Морской", "МОРСКОЙ") && Styles::names().contains("МОРСКОЙ"));
        // не стиль AutoQ — не читается; мусор — в пределах
        CHECK(!Styles::fromJson("{}", nullptr) && !Styles::fromJson("not json", nullptr));
        CHECK(!Styles::fromJson(R"({"format":"autoq-style","name":"","look":{}})", nullptr));
        Styles::Profile g;
        CHECK(Styles::fromJson(R"({"format":"autoq-style","name":"X","look":{"accent":"red","radius":99,"density":-4,)"
                               R"("variant":"zzz","theme":"light","elements":{"key":{"look":9,"radius":-7}}},"layout":{"x":1}})", &g));
        CHECK(g.look.accent == Theme::defaultAccent() && g.look.radius == 24 && g.look.density == 0);
        CHECK(g.look.variant == "studio" && !g.look.dark && g.look.elements.isEmpty() && !g.hasLayout());
        CHECK(Styles::fromJson(Styles::toJson(q), &g) && g.name == q.name && Styles::sameLook(g.look, q.look) && g.layout == q.layout);
        CHECK(Styles::validName(QString(40, 'a')) && !Styles::validName(QString(41, 'a')) && !Styles::validName("  "));
        CHECK(Styles::uniqueName(QString(40, 'a')) == QString(40, 'a'));      // свободное длинное имя — как есть
        p.name = QString(40, 'b');
        p.layout.clear();
        CHECK(Styles::save(p) && Styles::uniqueName(QString(40, 'B')) == QString(34, 'B') + " (2)");
        CHECK(Styles::validName(Styles::uniqueName(QString(40, 'b'))) && Styles::remove(QString(40, 'b')));
        CHECK(Styles::remove("морской") && !Styles::exists("Морской"));
        CHECK(Styles::remove("Компакт/тёмный") && Styles::names().isEmpty());
        CHECK(!Styles::remove("Компакт/тёмный"));
    }

    AppSettings::dropScope("AutoQTest", QString("ov_%1").arg(QCoreApplication::applicationPid()));
    // «Своё сочетание»: одна клавиша — по отпусканию, две — «держу Tab, жму F»
    {
        KeyCaptureButton btn;
        HotkeySpec got;
        bool acc = false;
        int fin = 0;
        QObject::connect(&btn, &KeyCaptureButton::captureFinished, [&](bool a, const HotkeySpec& sp) { acc = a; got = sp; ++fin; });
        auto key = [&](QEvent::Type t, int k, quint32 scan, Qt::KeyboardModifiers m = Qt::NoModifier, bool rep = false) {
            QKeyEvent e(t, k, m, scan, 0, 0, QString(), rep);
            QCoreApplication::sendEvent(&btn, &e);
        };
        auto start = [&]() { fin = 0; acc = false; got = HotkeySpec{}; btn.click(); CHECK(btn.isCapturing()); };
        start();
        key(QEvent::KeyPress, Qt::Key_Tab, 23);
        key(QEvent::KeyPress, Qt::Key_Tab, 23, Qt::NoModifier, true);      // повтор — ждём дальше
        CHECK(fin == 0 && btn.isCapturing());
        key(QEvent::KeyPress, Qt::Key_F, 41);
        CHECK(fin == 1 && acc && got.toString() == "tab+f" && got.display() == "Tab+F");
        key(QEvent::KeyRelease, Qt::Key_F, 41);
        key(QEvent::KeyRelease, Qt::Key_Tab, 23);
        // одна клавиша — когда её отпустят
        start();
        key(QEvent::KeyPress, Qt::Key_F, 41);
        CHECK(fin == 0);
        key(QEvent::KeyRelease, Qt::Key_F, 41, Qt::NoModifier, true);      // повтор — не отпускание
        CHECK(fin == 0);
        key(QEvent::KeyRelease, Qt::Key_F, 41);
        CHECK(fin == 1 && acc && got.toString() == "f" && !got.isChord());
        // с Ctrl — сразу, как раньше
        start();
        key(QEvent::KeyPress, Qt::Key_Control, 37, Qt::ControlModifier);
        key(QEvent::KeyPress, Qt::Key_F, 41, Qt::ControlModifier);
        CHECK(fin == 1 && got.toString() == "ctrl+f");
        key(QEvent::KeyRelease, Qt::Key_F, 41, Qt::ControlModifier);
        key(QEvent::KeyRelease, Qt::Key_Control, 37);
        // Shift+Tab, потом F — «shift+tab+f»; Esc — отмена
        start();
        key(QEvent::KeyPress, Qt::Key_Shift, 50, Qt::ShiftModifier);
        key(QEvent::KeyPress, Qt::Key_Backtab, 23, Qt::ShiftModifier);
        key(QEvent::KeyPress, Qt::Key_F, 41, Qt::ShiftModifier);
        CHECK(fin == 1 && got.toString() == "shift+tab+f" && got.display() == "Shift+Tab+F");
        key(QEvent::KeyRelease, Qt::Key_F, 41, Qt::ShiftModifier);
        key(QEvent::KeyRelease, Qt::Key_Tab, 23, Qt::ShiftModifier);
        start();
        key(QEvent::KeyPress, Qt::Key_Escape, 9);
        CHECK(fin == 1 && !acc && !btn.isCapturing());
        // Shift+Tab держат, а отпускание пришло как Tab — тоже одна клавиша
        start();
        key(QEvent::KeyPress, Qt::Key_Backtab, 23, Qt::ShiftModifier);
        key(QEvent::KeyRelease, Qt::Key_Tab, 0);
        CHECK(fin == 1 && got.toString() == "shift+tab");
    }

    std::printf("\n%d проверок пройдено, %d провалено\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
