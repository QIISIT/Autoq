// Оверлей КПС: настройки сохраняются, размер и рисование (без экрана — offscreen)
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include "overlay.h"
#include "games.h"
#include "theme.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

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

    AppSettings::dropScope("AutoQTest", QString("ov_%1").arg(QCoreApplication::applicationPid()));
    std::printf("\n%d проверок пройдено, %d провалено\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
