#include "overlay.h"
#include "i18n.h"
#include "games.h"
#include "theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>
#include <QLocale>
#include <QFontMetricsF>
#include <QtMath>
#ifdef __APPLE__
#include "macsupport.h"
#endif

// macOS: окно на всех рабочих столах и поверх полноэкранных игр
static void keepOverlayOnTop(QWidget* w) {
#ifdef __APPLE__
    if (w->isVisible()) AqMac::keepOnTop(static_cast<unsigned long long>(w->winId()), true);
#else
    Q_UNUSED(w);
#endif
}

// ===========================================================================
// Настройки
// ===========================================================================
OverlaySettings OverlaySettings::defaults() { return OverlaySettings{}; }

static QColor readColor(QSettings& s, const QString& key, const QColor& def) {
    const QString v = s.value(key).toString();
    if (v.isEmpty()) return def;
    const QColor c(v);
    return c.isValid() ? c : def;
}

OverlaySettings OverlaySettings::load() {
    auto s = AppSettings::open();
    OverlaySettings o;
    const QString p = "overlay/";
    o.enabled = s->value(p + "enabled", o.enabled).toBool();
    o.when = qBound(0, s->value(p + "when", o.when).toInt(), 1);
    o.showCps = s->value(p + "showCps", o.showCps).toBool();
    o.showLabel = s->value(p + "showLabel", o.showLabel).toBool();
    o.showActions = s->value(p + "showActions", o.showActions).toBool();
    o.showTotal = s->value(p + "showTotal", o.showTotal).toBool();
    o.showStatus = s->value(p + "showStatus", o.showStatus).toBool();
    o.showGame = s->value(p + "showGame", o.showGame).toBool();
    o.showGraph = s->value(p + "showGraph", o.showGraph).toBool();
    o.label = s->value(p + "label", o.label).toString().left(16);
    o.anchor = qBound(0, s->value(p + "anchor", o.anchor).toInt(), 8);
    o.offsetX = qBound(-4000, s->value(p + "offsetX", o.offsetX).toInt(), 8000);
    o.offsetY = qBound(-4000, s->value(p + "offsetY", o.offsetY).toInt(), 8000);
    o.screen = qBound(0, s->value(p + "screen", o.screen).toInt(), 16);
    o.textColor = readColor(*s, p + "textColor", o.textColor);
    o.numberColor = readColor(*s, p + "numberColor", QColor());
    o.bgColor = readColor(*s, p + "bgColor", o.bgColor);
    o.bgOpacity = qBound(0, s->value(p + "bgOpacity", o.bgOpacity).toInt(), 100);
    o.opacity = qBound(15, s->value(p + "opacity", o.opacity).toInt(), 100);
    o.scale = qBound(50, s->value(p + "scale", o.scale).toInt(), 300);
    o.font = qBound(0, s->value(p + "font", o.font).toInt(), 2);
    o.radius = qBound(0, s->value(p + "radius", o.radius).toInt(), 40);
    o.vertical = s->value(p + "vertical", o.vertical).toBool();
    o.outline = s->value(p + "outline", o.outline).toBool();
    const int w = s->value(p + "windowMs", o.windowMs).toInt();
    o.windowMs = (w == 500 || w == 1000 || w == 2000) ? w : 1000;
    return o;
}

void OverlaySettings::save() const {
    auto s = AppSettings::open();
    const QString p = "overlay/";
    s->setValue(p + "enabled", enabled);
    s->setValue(p + "when", when);
    s->setValue(p + "showCps", showCps);
    s->setValue(p + "showLabel", showLabel);
    s->setValue(p + "showActions", showActions);
    s->setValue(p + "showTotal", showTotal);
    s->setValue(p + "showStatus", showStatus);
    s->setValue(p + "showGame", showGame);
    s->setValue(p + "showGraph", showGraph);
    s->setValue(p + "label", label);
    s->setValue(p + "anchor", anchor);
    s->setValue(p + "offsetX", offsetX);
    s->setValue(p + "offsetY", offsetY);
    s->setValue(p + "screen", screen);
    s->setValue(p + "textColor", textColor.name().toUpper());
    s->setValue(p + "numberColor", numberColor.isValid() ? numberColor.name().toUpper() : QString());
    s->setValue(p + "bgColor", bgColor.name().toUpper());
    s->setValue(p + "bgOpacity", bgOpacity);
    s->setValue(p + "opacity", opacity);
    s->setValue(p + "scale", scale);
    s->setValue(p + "font", font);
    s->setValue(p + "radius", radius);
    s->setValue(p + "vertical", vertical);
    s->setValue(p + "outline", outline);
    s->setValue(p + "windowMs", windowMs);
}

// ===========================================================================
// Рисование (общее для оверлея и образца в настройках)
// ===========================================================================
namespace {

QString fmtCps(double v) {
    return I18n::locale().toString(v, 'f', v < 9.95 ? 1 : 0);
}

QString fmtNum(long long n) {
    QString s = QString::number(n);
    for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, I18n::english() ? QChar(',') : QChar(0x00A0));
    return s;
}

QColor numberColorOf(const OverlaySettings& s) {
    if (s.numberColor.isValid()) return s.numberColor;
    return Theme::tokensFor([] { Theme::Appearance a = Theme::current(); a.dark = true; return a; }()).redHi;
}

QFont makeFont(const QString& family, qreal px, int weight) {
    QFont f(family);
    f.setPixelSize(qMax(6, qRound(px)));
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

struct Fonts {
    QFont number, label, mono, ui;
    qreal k;
};

Fonts fontsFor(const OverlaySettings& s) {
    Fonts f;
    f.k = s.scale / 100.0;
    const QString fam = s.font == 1 ? Theme::uiFamily() : s.font == 2 ? Theme::monoFamily() : Theme::displayFamily();
    f.number = makeFont(fam, 30 * f.k, s.font == 0 ? QFont::Black : QFont::Bold);
    f.label = makeFont(Theme::uiFamily(), 12 * f.k, QFont::DemiBold);
    f.mono = makeFont(Theme::monoFamily(), 12.5 * f.k, QFont::Bold);
    f.ui = makeFont(Theme::uiFamily(), 12 * f.k, QFont::DemiBold);
    return f;
}

// Кусочек текста своего цвета (одна строка может состоять из нескольких)
struct Run { QString text; QFont font; QColor color; };
using Line = QVector<Run>;

qreal lineWidth(const Line& l) {
    qreal w = 0;
    for (const Run& r : l) w += QFontMetricsF(r.font).horizontalAdvance(r.text);
    return w;
}
qreal lineHeight(const Line& l) {
    qreal h = 0;
    for (const Run& r : l) h = qMax(h, QFontMetricsF(r.font).height());
    return h;
}
qreal lineAscent(const Line& l) {
    qreal a = 0;
    for (const Run& r : l) a = qMax(a, QFontMetricsF(r.font).ascent());
    return a;
}

// Что показывает блок — по этому блоки разных игр встают в одну колонку
enum Kind { kName, kCps, kActions, kTotal, kStatus, kGame, kGraph, kKinds };

// Блок оверлея: несколько строк текста или график
struct Block {
    int kind = kCps;
    QVector<Line> lines;
    bool graph = false;
    bool dot = false;             // точка статуса перед первой строкой
    QColor dotColor;
    QSizeF size;
};

QVector<Block> buildBlocks(const OverlaySettings& s, const OverlayView& v, const Fonts& f) {
    const QColor text = s.textColor;
    QColor dim = text;
    dim.setAlphaF(0.72);
    const QColor num = numberColorOf(s);
    QVector<Block> out;
    const bool anything = s.showCps || s.showActions || s.showTotal || s.showStatus || s.showGame || s.showGraph;
    // несколько игр сразу: у каждой строки сначала название — иначе не понять, где какая
    // название игры, под ним — профиль
    if (v.named && !v.game.isEmpty()) {
        Block b;
        b.kind = kName;
        Line l;
        l << Run{QFontMetricsF(f.ui).elidedText(v.game, Qt::ElideRight, 180 * f.k), f.ui, text};
        b.lines << l;
        if (!v.profile.isEmpty()) {
            Line pl;
            pl << Run{QFontMetricsF(f.label).elidedText(v.profile, Qt::ElideRight, 180 * f.k), f.label, dim};
            b.lines << pl;
        }
        out << b;
    }

    if (s.showCps || !anything) {
        Block b;
        b.kind = kCps;
        Line l;
        if (v.hold) {
            l << Run{QStringLiteral("—"), f.number, num};
            if (s.showLabel) l << Run{T("  зажато"), f.label, dim};
        } else {
            l << Run{fmtCps(v.cps), f.number, num};
            if (s.showLabel && !s.label.trimmed().isEmpty()) l << Run{"  " + s.label.trimmed(), f.label, dim};
        }
        b.lines << l;
        out << b;
    }
    if (s.showActions && !v.a1.isEmpty()) {
        Block b;
        b.kind = kActions;
        if (v.hold) {
            Line l;
            l << Run{v.a2.isEmpty() ? v.a1 : v.a1 + " + " + v.a2, f.mono, num};
            l << Run{T("  держу"), f.ui, dim};
            b.lines << l;
        } else {
            auto act = [&](const QString& name, double cps) {
                Line l;
                l << Run{name + " ", f.mono, dim} << Run{fmtCps(cps), f.mono, text};
                return l;
            };
            if (s.vertical || v.a2.isEmpty()) {
                b.lines << act(v.a1, v.cps1);
                if (!v.a2.isEmpty()) b.lines << act(v.a2, v.cps2);
            } else {
                Line l = act(v.a1, v.cps1);
                l << Run{QStringLiteral("  ·  "), f.mono, dim};
                l += act(v.a2, v.cps2);
                b.lines << l;
            }
        }
        out << b;
    }
    if (s.showTotal) {
        Block b;
        b.kind = kTotal;
        Line l;
        l << Run{T("всего "), f.ui, dim} << Run{fmtNum(v.total), f.mono, text};
        b.lines << l;
        out << b;
    }
    if (s.showStatus) {
        Block b;
        b.kind = kStatus;
        b.dot = true;
        const QString names[] = {T("Остановлен"), T("Работает"), T("Пауза: не окно игры"), T("Выбери окно"),
                                      T("Пауза: курсор не над игрой"), T("Ждёт старта"), T("Пауза: нет кадров игры")};
        const int st = qBound(0, v.status, 6);
        b.dotColor = st == 1 ? QColor("#3DDC84") : (st == 2 || st == 4 || st == 6) ? QColor("#F0B341")
                   : (st == 3 || st == 5) ? num : QColor("#8A8A93");
        Line l;
        l << Run{names[st], f.ui, text};
        b.lines << l;
        out << b;
    }
    if (s.showGame && !v.named && !v.game.isEmpty()) {
        Block b;
        b.kind = kGame;
        Line l;
        const QString g = v.profile.isEmpty() ? v.game : v.game + " · " + v.profile;
        l << Run{QFontMetricsF(f.ui).elidedText(g, Qt::ElideRight, 180 * f.k), f.ui, dim};
        b.lines << l;
        out << b;
    }
    if (s.showGraph) {
        Block b;
        b.kind = kGraph;
        b.graph = true;
        b.size = QSizeF(96 * f.k, 26 * f.k);
        out << b;
    }
    for (Block& b : out) {
        if (b.graph) continue;
        qreal w = 0, h = 0;
        const qreal lineGap = 2 * f.k;
        for (int i = 0; i < b.lines.size(); ++i) {
            w = qMax(w, lineWidth(b.lines[i]));
            h += lineHeight(b.lines[i]) + (i ? lineGap : 0);
        }
        if (b.dot) w += 12 * f.k;
        b.size = QSizeF(w, h);
    }
    return out;
}

void drawRun(QPainter& p, const QPointF& baseline, const Run& r, bool outline, qreal k) {
    if (!outline) {
        p.setFont(r.font);
        p.setPen(r.color);
        p.drawText(baseline, r.text);
        return;
    }
    QPainterPath path;
    path.addText(baseline, r.font, r.text);
    QColor shade(0, 0, 0, 200);
    shade.setAlphaF(0.78 * r.color.alphaF());
    p.strokePath(path, QPen(shade, qMax(2.0, 3.2 * k), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(path, r.color);
}

struct Metrics { qreal padX, padY, gap; };
Metrics metricsFor(const Fonts& f) { return {14 * f.k, 9 * f.k, 14 * f.k}; }

// Плашка целиком: строка на каждую игру. В строку блоки идут колонками,
// одинаковые блоки разных игр — друг под другом (колонки общие на все строки).
// «Столбиком» — у каждой игры блоки друг под другом, игры разделены линией.
struct Panel {
    QVector<QVector<Block>> rows;
    QVector<qreal> rowH;
    qreal colW[kKinds] = {};
    bool used[kKinds] = {};
    qreal rowGap = 0;
    QSizeF inner;                 // без полей
};

Panel layoutPanel(const OverlaySettings& s, const QList<OverlayView>& views, const Fonts& f, const Metrics& m) {
    Panel pn;
    pn.rowGap = m.gap * 0.9;
    for (const OverlayView& v : views) pn.rows << buildBlocks(s, v, f);
    qreal w = 0, h = 0;
    if (!s.vertical) {
        for (const QVector<Block>& r : pn.rows)
            for (const Block& b : r) {
                pn.used[b.kind] = true;
                pn.colW[b.kind] = qMax(pn.colW[b.kind], b.size.width());
            }
        int n = 0;
        for (int k = 0; k < kKinds; ++k)
            if (pn.used[k]) w += pn.colW[k] + (n++ ? m.gap : 0);
    }
    for (int r = 0; r < pn.rows.size(); ++r) {
        const QVector<Block>& bl = pn.rows[r];
        qreal rh = 0;
        for (int i = 0; i < bl.size(); ++i) {
            if (s.vertical) {
                w = qMax(w, bl[i].size.width());
                rh += bl[i].size.height() + (i ? m.gap * 0.6 : 0);
            } else {
                rh = qMax(rh, bl[i].size.height());
            }
        }
        pn.rowH << rh;
        h += rh + (r ? pn.rowGap : 0);
    }
    pn.inner = QSizeF(w, h);
    return pn;
}

QSize panelSize(const Panel& pn, const Metrics& m) {
    return QSize(qCeil(pn.inner.width() + 2 * m.padX) + 4, qCeil(pn.inner.height() + 2 * m.padY) + 4);
}

void drawBlock(QPainter& p, const QPointF& at, const Block& b, const OverlayView& v, const OverlaySettings& s,
               const Fonts& f, const QColor& num) {
    if (b.graph) {
        const QRectF g(at, b.size);
        QColor base = s.textColor;
        base.setAlphaF(0.18);
        p.setPen(QPen(base, 1));
        p.drawLine(QPointF(g.left(), g.bottom()), QPointF(g.right(), g.bottom()));
        const QVector<double>& d = v.graph;
        if (d.size() < 2) return;
        double mx = 1;
        for (double val : d) mx = qMax(mx, val);
        QPainterPath line, fill;
        for (int i = 0; i < d.size(); ++i) {
            const QPointF pt(g.left() + g.width() * i / (d.size() - 1), g.bottom() - (g.height() - 2) * d[i] / mx);
            if (i == 0) { line.moveTo(pt); fill.moveTo(g.left(), g.bottom()); fill.lineTo(pt); }
            else { line.lineTo(pt); fill.lineTo(pt); }
        }
        fill.lineTo(g.right(), g.bottom());
        fill.closeSubpath();
        QColor fc = num;
        fc.setAlphaF(0.22);
        p.fillPath(fill, fc);
        p.setPen(QPen(num, qMax(1.5, 2 * f.k), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
        return;
    }
    qreal ly = at.y();
    for (int i = 0; i < b.lines.size(); ++i) {
        const Line& l = b.lines[i];
        qreal lx = at.x();
        if (b.dot && i == 0) {
            const qreal r = 4 * f.k;
            p.setPen(Qt::NoPen);
            p.setBrush(b.dotColor);
            p.drawEllipse(QPointF(lx + r, ly + lineHeight(l) / 2), r, r);
            lx += 12 * f.k;
        }
        const qreal base = ly + lineAscent(l);
        for (const Run& r : l) {
            drawRun(p, QPointF(lx, base), r, s.outline, f.k);
            lx += QFontMetricsF(r.font).horizontalAdvance(r.text);
        }
        ly += lineHeight(l) + 2 * f.k;
    }
}

} // namespace

QSize OverlayPaint::size(const OverlaySettings& s, const OverlayView& v) { return size(s, QList<OverlayView>{v}); }

void OverlayPaint::paint(QPainter& p, const QPointF& tl, const OverlaySettings& s, const OverlayView& v,
                         bool moveHint) {
    paint(p, tl, s, QList<OverlayView>{v}, moveHint);
}

QSize OverlayPaint::size(const OverlaySettings& s, const QList<OverlayView>& views) {
    const Fonts f = fontsFor(s);
    const Metrics m = metricsFor(f);
    return panelSize(layoutPanel(s, views, f, m), m);
}

void OverlayPaint::paint(QPainter& p, const QPointF& tl, const OverlaySettings& s, const QList<OverlayView>& views,
                         bool moveHint) {
    const Fonts f = fontsFor(s);
    const Metrics m = metricsFor(f);
    const Panel pn = layoutPanel(s, views, f, m);
    const QSize total = panelSize(pn, m);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setOpacity(s.opacity / 100.0);
    const QRectF box(tl.x() + 2, tl.y() + 2, total.width() - 4, total.height() - 4);
    if (s.bgOpacity > 0) {
        QColor bg = s.bgColor;
        bg.setAlphaF(s.bgOpacity / 100.0);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(box, s.radius * f.k, s.radius * f.k);
    }
    if (moveHint) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(numberColorOf(s), 2, Qt::DashLine));
        p.drawRoundedRect(box.adjusted(1, 1, -1, -1), s.radius * f.k, s.radius * f.k);
    }
    const QColor num = numberColorOf(s);
    QColor sep = s.textColor;
    sep.setAlphaF(0.22);
    const qreal left = box.left() + m.padX;
    qreal y = box.top() + m.padY;
    for (int r = 0; r < pn.rows.size(); ++r) {
        const QVector<Block>& bl = pn.rows[r];
        const qreal rh = pn.rowH[r];
        const OverlayView& v = views[r];
        if (r) {                       // линия между играми
            const qreal ly = qRound(y - pn.rowGap / 2) + 0.5;
            p.setPen(QPen(sep, 1));
            p.drawLine(QPointF(left, ly), QPointF(left + pn.inner.width(), ly));
        }
        if (s.vertical) {
            qreal by = y;
            for (const Block& b : bl) {
                drawBlock(p, QPointF(left, by), b, v, s, f, num);
                by += b.size.height() + m.gap * 0.6;
            }
        } else {
            qreal x = left;
            bool drawn = false;
            for (int k = 0; k < kKinds; ++k) {
                if (!pn.used[k]) continue;
                for (const Block& b : bl) {
                    if (b.kind != k) continue;
                    if (drawn) {       // тонкий разделитель перед блоком
                        p.setPen(QPen(sep, 1));
                        p.drawLine(QPointF(x - m.gap / 2, y + 2), QPointF(x - m.gap / 2, y + rh - 2));
                    }
                    drawBlock(p, QPointF(x, y + (rh - b.size.height()) / 2), b, v, s, f, num);
                    drawn = true;
                }
                x += pn.colW[k] + m.gap;
            }
        }
        y += rh + pn.rowGap;
    }
    p.restore();
}

OverlayView OverlayPaint::sample() {
    OverlayView v;
    v.cps = 20; v.cps1 = 10; v.cps2 = 10;
    v.a1 = T("ЛКМ"); v.a2 = "F";
    v.total = 1248;
    v.status = 1;
    v.game = "Genshin Impact";
    for (int i = 0; i < 40; ++i) v.graph << 17 + 3 * qSin(i / 4.0) + (i % 7 == 0 ? 2 : 0);
    return v;
}

// ===========================================================================
// Окно оверлея
// ===========================================================================
OverlayWindow::OverlayWindow(std::function<OverlayData()> source) : QWidget(nullptr), source_(std::move(source)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);   // macOS: не прятать, когда активна игра
    // по слову «оверлей» в заголовке AutoQ на macOS узнаёт своё сквозное окно
    setWindowTitle(T("AutoQ — оверлей"));
    applyFlags();
    timer_ = startTimer(100);
}

void OverlayWindow::applyFlags() {
    Qt::WindowFlags f = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                        Qt::WindowDoesNotAcceptFocus | Qt::X11BypassWindowManagerHint | Qt::NoDropShadowWindowHint;
    if (!move_) f |= Qt::WindowTransparentForInput;   // клики проходят сквозь оверлей в игру
    const bool vis = isVisible();
    setWindowFlags(f);
    setCursor(move_ ? Qt::SizeAllCursor : Qt::ArrowCursor);
    if (vis) { show(); keepOverlayOnTop(this); }
}

void OverlayWindow::setSettings(const OverlaySettings& s) {
    s_ = s;
    for (auto& t : tracks_) t.second.hist.clear();
    relayout();
    refresh();
    update();
}

void OverlayWindow::setMoveMode(bool on) {
    if (on == move_) return;
    move_ = on;
    dragging_ = false;
    applyFlags();
    refresh();
    update();
}

QRect OverlayWindow::screenRect() const {
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* scr = screens.value(s_.screen, QGuiApplication::primaryScreen());
    return scr ? scr->geometry() : QRect(0, 0, 1920, 1080);
}

void OverlayWindow::relayout() {
    if (views_.isEmpty()) views_ << OverlayView{};
    const QSize sz = OverlayPaint::size(s_, views_);
    if (size() != sz) setFixedSize(sz);
    if (dragging_) return;
    const QRect g = screenRect();
    const int col = s_.anchor % 3, row = s_.anchor / 3;
    const int x = col == 0 ? g.left() + s_.offsetX
                : col == 1 ? g.center().x() - sz.width() / 2 + s_.offsetX
                           : g.right() + 1 - sz.width() - s_.offsetX;
    const int y = row == 0 ? g.top() + s_.offsetY
                : row == 1 ? g.center().y() - sz.height() / 2 + s_.offsetY
                           : g.bottom() + 1 - sz.height() - s_.offsetY;
    if (pos() != QPoint(x, y)) move(x, y);
}

bool OverlayData::anyActive() const {
    for (const OverlayEntry& e : entries)
        if (e.running || e.pending) return true;
    return false;
}

void OverlayWindow::refresh() { refresh(source_ ? source_() : OverlayData{}); }

void OverlayWindow::refresh(const OverlayData& d) {
    const bool want = move_ || (s_.enabled && d.hasGame && !d.entries.isEmpty() && (s_.when == 1 || d.anyActive()));
    if (want && !isVisible()) { relayout(); show(); raise(); keepOverlayOnTop(this); }
    else if (!want && isVisible()) hide();
}

void OverlayWindow::tick() {
    const OverlayData d = source_ ? source_() : OverlayData{};
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool graphTick = now - lastGraph_ >= 250;
    if (graphTick) lastGraph_ = now;
    QList<OverlayView> views;
    for (const OverlayEntry& e : d.entries) {
        Track& t = tracks_[e.key];
        t.seen = now;
        // клики в секунду — по приросту счётчиков за последние windowMs
        if (!e.running) {
            t.hist.clear();
            t.cps = t.cps1 = t.cps2 = 0;
        } else {
            if (!t.hist.empty() && (e.c1 < t.hist.back().c1 || e.c2 < t.hist.back().c2)) t.hist.clear();
            t.hist.push_back({now, e.c1, e.c2});
            while (t.hist.size() > 1 && t.hist[1].t <= now - s_.windowMs) t.hist.pop_front();
            const Sample& a = t.hist.front();
            const double dt = (now - a.t) / 1000.0;
            if (dt >= 0.15) {
                t.cps1 = (e.c1 - a.c1) / dt;
                t.cps2 = (e.c2 - a.c2) / dt;
            } else {
                t.cps1 = t.cps2 = 0;
            }
            t.cps = t.cps1 + t.cps2;
        }
        if (graphTick) {
            t.graph << t.cps;
            while (t.graph.size() > 40) t.graph.removeFirst();
        }
        OverlayView v;
        v.cps = t.cps; v.cps1 = t.cps1; v.cps2 = t.cps2;
        v.graph = t.graph;
        v.a1 = e.a1;
        v.a2 = e.a2;
        v.total = static_cast<long long>(e.total);
        v.hold = e.hold && e.running;
        v.game = e.game;
        v.profile = e.profile;
        v.named = d.entries.size() > 1;
        v.status = e.pending ? 3 : !e.running ? 0 : e.starting ? 5 : e.allowed ? 1 : e.pauseReason == 2 ? 4 : e.pauseReason == 3 ? 6 : 2;
        views << v;
    }
    // игры, которых больше нет в оверлее, — забыть их историю
    for (auto it = tracks_.begin(); it != tracks_.end();)
        it = it->second.seen == now ? std::next(it) : tracks_.erase(it);
    if (views.isEmpty()) views << OverlayView{};
    if (move_ && !d.anyActive()) {    // в режиме перетаскивания — пример, чтобы было что двигать
        const OverlayView ex = OverlayPaint::sample();
        OverlayView& v = views.first();
        v.cps = ex.cps; v.cps1 = ex.cps1; v.cps2 = ex.cps2;
        if (v.a1.isEmpty()) { v.a1 = ex.a1; v.a2 = ex.a2; }
        if (v.graph.size() < 2) v.graph = ex.graph;
        if (v.game.isEmpty()) v.game = ex.game;
    }
    views_ = views;
    refresh(d);
    if (!isVisible()) return;
    relayout();
    if (++raiseTick_ >= 10) {         // раз в секунду — снова поверх (игра могла подняться выше)
        raiseTick_ = 0;
        if (!dragging_) { raise(); keepOverlayOnTop(this); }
    }
    update();
}

void OverlayWindow::timerEvent(QTimerEvent*) { tick(); }

void OverlayWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    OverlayPaint::paint(p, QPointF(0, 0), s_, views_, move_);
}

void OverlayWindow::mousePressEvent(QMouseEvent* e) {
    if (!move_ || e->button() != Qt::LeftButton) return;
    dragging_ = true;
    dragOffset_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
}

void OverlayWindow::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    move(e->globalPosition().toPoint() - dragOffset_);
}

void OverlayWindow::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_ || e->button() != Qt::LeftButton) return;
    dragging_ = false;
    // монитор — тот, где центр оверлея; отступ — от ближайшего края по привязке
    const QPoint c = geometry().center();
    const QList<QScreen*> screens = QGuiApplication::screens();
    int idx = 0;
    for (int i = 0; i < screens.size(); ++i)
        if (screens[i]->geometry().contains(c)) { idx = i; break; }
    s_.screen = idx;
    const QRect g = screenRect();
    const QRect r = geometry();
    const int col = s_.anchor % 3, row = s_.anchor / 3;
    s_.offsetX = col == 0 ? r.left() - g.left() : col == 1 ? r.center().x() - g.center().x() : g.right() - r.right();
    s_.offsetY = row == 0 ? r.top() - g.top() : row == 1 ? r.center().y() - g.center().y() : g.bottom() - r.bottom();
    emit moved(s_.screen, s_.offsetX, s_.offsetY);
}

// ===========================================================================
// Образец в настройках: «игра» и оверлей поверх неё
// ===========================================================================
OverlayPreview::OverlayPreview(QWidget* parent) : QWidget(parent) {
    setFixedSize(440, 248);
}

void OverlayPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(r, 12, 12);
    p.setClipPath(clip);
    // «игра»: небо, горы, трава
    QLinearGradient sky(0, 0, 0, height());
    sky.setColorAt(0, QColor("#2A3B63"));
    sky.setColorAt(0.55, QColor("#7FA6C9"));
    sky.setColorAt(1, QColor("#B9D4E0"));
    p.fillRect(rect(), sky);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 244, 214, 220));
    p.drawEllipse(QPointF(width() * 0.78, height() * 0.24), 18, 18);
    QPainterPath hills;
    hills.moveTo(0, height() * 0.70);
    hills.cubicTo(width() * 0.2, height() * 0.48, width() * 0.35, height() * 0.62, width() * 0.5, height() * 0.55);
    hills.cubicTo(width() * 0.68, height() * 0.47, width() * 0.82, height() * 0.66, width(), height() * 0.58);
    hills.lineTo(width(), height());
    hills.lineTo(0, height());
    hills.closeSubpath();
    p.setBrush(QColor("#4E7A5A"));
    p.drawPath(hills);
    p.setBrush(QColor("#36573F"));
    p.drawRect(QRectF(0, height() * 0.82, width(), height()));
    // перекрестие прицела — «игра»
    p.setPen(QPen(QColor(255, 255, 255, 150), 1.5));
    const QPointF c(width() / 2.0, height() / 2.0);
    p.drawLine(c + QPointF(-6, 0), c + QPointF(6, 0));
    p.drawLine(c + QPointF(0, -6), c + QPointF(0, 6));

    // оверлей в уменьшенном виде, на своём месте экрана
    OverlaySettings s = s_;
    const qreal shrink = 0.62;
    const OverlayView v = OverlayPaint::sample();
    const QSize sz = OverlayPaint::size(s, v);
    const QSize full = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->geometry().size()
                                                        : QSize(1920, 1080);
    const qreal kx = width() / qreal(full.width()), ky = height() / qreal(full.height());
    const qreal w = sz.width() * shrink, h = sz.height() * shrink;
    const int col = s.anchor % 3, row = s.anchor / 3;
    const qreal ox = qBound(-width() / 2.0, s.offsetX * kx, width() / 2.0);
    const qreal oy = qBound(-height() / 2.0, s.offsetY * ky, height() / 2.0);
    qreal x = col == 0 ? ox : col == 1 ? (width() - w) / 2 + ox : width() - w - ox;
    qreal y = row == 0 ? oy : row == 1 ? (height() - h) / 2 + oy : height() - h - oy;
    x = qBound(2.0, x, width() - w - 2);
    y = qBound(2.0, y, height() - h - 2);
    p.save();
    p.translate(x, y);
    p.scale(shrink, shrink);
    OverlayPaint::paint(p, QPointF(0, 0), s, v);
    p.restore();
    p.setClipping(false);
    p.setPen(QPen(Theme::t().cardBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 12, 12);
}
