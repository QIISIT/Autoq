#include "colorpicker.h"
#include "i18n.h"
#include "widgets.h"
#include "icons.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QSignalBlocker>

namespace {
QFont mkFont(const QString& family, int px, int weight = QFont::Normal) {
    QFont f(family);
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}
void styleSwatch(QToolButton* b, const QString& hex) {
    const auto& t = Theme::t();
    b->setStyleSheet(QString("QToolButton#swatch { background: %1; border: 2px solid %2; border-radius: %4px; }"
                             "QToolButton#swatch:checked { border: 3px solid %3; }")
                         .arg(hex, t.fieldBorder2.name(), t.text.name())
                         .arg(b->width() / 2));
}
} // namespace

// ===========================================================================
// Образец окна AutoQ в выбранных цветах
// ===========================================================================
ThemePreview::ThemePreview(QWidget* parent) : QWidget(parent) {
    setFixedSize(316, 196);
    a_ = Theme::current();
}

void ThemePreview::paintEvent(QPaintEvent*) {
    const Theme::Tokens t = Theme::tokensFor(a_);
    const QColor accent = a_.accent.isValid() ? a_.accent : Theme::defaultAccent();
    const QString dispF = Theme::displayFamilyFor(a_), uiF = Theme::uiFamilyFor(a_);
    const Theme::Appearance ap = a_;
    // скругления — как у стиля (в образце всё мельче: база ×0.7)
    auto R = [&ap](qreal base) { return qreal(Theme::radiusFor(ap, qRound(base / 0.7))) * 0.7; };
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    // рисуем макет 316×196 и растягиваем под размер виджета
    const qreal sc = qMin(width() / 316.0, height() / 196.0);
    p.translate((width() - 316 * sc) / 2, (height() - 196 * sc) / 2);
    p.scale(sc, sc);
    const QRect area(0, 0, 316, 196);
    const QRectF r = QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(r, R(12), R(12));
    p.setClipPath(clip);
    p.fillRect(area, t.bg);

    // --- боковая панель
    const qreal sideW = 92;
    p.fillRect(QRectF(0, 0, sideW, area.height()), t.side);
    p.setPen(QPen(t.cardBorder, 1));
    p.drawLine(QPointF(sideW, 0), QPointF(sideW, area.height()));
    p.drawPixmap(10, 10, AqIcons::logoWithAccent(18, devicePixelRatioF(), accent));
    p.setFont(mkFont(dispF, 11, QFont::Bold));
    p.setPen(t.text);
    p.drawText(QPointF(33, 24), "Auto");
    p.setPen(t.redHi);
    p.drawText(QPointF(33 + QFontMetrics(p.font()).horizontalAdvance("Auto"), 24), "Q");

    auto item = [&](qreal y, bool sel, const QColor& gameColor, const QString& name, const QString& sub) {
        const QRectF box(6, y, sideW - 12, 30);
        if (sel) {
            p.setPen(QPen(t.redLine, 1));
            p.setBrush(t.redTint);
            p.drawRoundedRect(box, R(8), R(8));
        }
        const QColor base = t.light ? QColor("#FFFFFF") : QColor("#141417");
        const QRectF tile(12, y + 6, 18, 18);
        p.setPen(QPen(sel ? accent : Theme::mix(gameColor, base, 0.3), sel ? 1.6 : 1));
        p.setBrush(Theme::mix(gameColor, base, 0.16));
        p.drawRoundedRect(tile, R(5), R(5));
        p.setPen(Qt::NoPen);
        p.setBrush(t.light ? Theme::mix(gameColor, QColor("#000000"), 0.55) : gameColor);
        p.drawEllipse(tile.center(), 3.2, 3.2);
        p.setFont(mkFont(uiF, 9, sel ? QFont::DemiBold : QFont::Normal));
        p.setPen(sel ? t.text : t.text2);
        p.drawText(QPointF(35, y + 13), name);
        p.setFont(mkFont(uiF, 8));
        p.setPen(sel ? t.redSoft : t.faint);
        p.drawText(QPointF(35, y + 25), sub);
    };
    item(40, true, QColor("#F2C66D"), "Genshin", T("по очереди"));
    item(74, false, QColor("#6EDC8C"), "Minecraft", T("вместе"));
    item(108, false, QColor("#B9A8FF"), "Star Rail", T("по очереди"));

    // --- шапка
    p.setFont(mkFont(dispF, 12, QFont::Bold));
    p.setPen(t.text);
    p.drawText(QPointF(104, 27), "Genshin");
    const QRectF chip(104, 35, 66, 16);
    p.setPen(QPen(t.redLine, 1));
    p.setBrush(t.redTint);
    p.drawRoundedRect(chip, R(8), R(8));
    p.setFont(mkFont(uiF, 8, QFont::DemiBold));
    p.setPen(t.redSoft);
    p.drawText(chip, Qt::AlignCenter, T("По очереди"));

    const QRectF start(222, 12, 84, 32);
    p.setPen(Qt::NoPen);
    p.setBrush(t.red);
    p.drawRoundedRect(start, R(9), R(9));
    p.setBrush(t.onRed);
    QPainterPath tri;
    tri.moveTo(start.left() + 14, start.top() + 10);
    tri.lineTo(start.left() + 23, start.center().y());
    tri.lineTo(start.left() + 14, start.bottom() - 10);
    tri.closeSubpath();
    p.drawPath(tri);
    p.setFont(mkFont(dispF, 10, QFont::Bold));
    p.setPen(t.onRed);
    p.drawText(start.adjusted(28, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, T("Старт"));

    // --- карточка действий
    const QRectF card(102, 58, 204, 128);
    const QString variant = Theme::normalizeVariant(ap.variant);
    if (variant == "glass") {
        QLinearGradient g(card.topLeft(), card.bottomLeft());
        g.setColorAt(0, Theme::mix(QColor("#FFFFFF"), t.card, t.light ? 0.6 : 0.06));
        g.setColorAt(1, t.card);
        p.setPen(QPen(Theme::mix(t.red, t.cardBorder, 0.3), 1));
        p.setBrush(g);
    } else {
        p.setPen(QPen(t.cardBorder, 1));
        p.setBrush(t.card);
    }
    p.drawRoundedRect(card, R(10), R(10));
    if (variant == "sharp") {
        p.setPen(Qt::NoPen);
        p.setBrush(t.red);
        p.drawRect(QRectF(card.left(), card.top(), 3, card.height()));
    }
    p.setFont(mkFont(Theme::monoFamily(), 8, QFont::Bold));
    p.setPen(t.redHi);
    p.drawText(QPointF(112, 76), "01");
    p.setFont(mkFont(uiF, 9, QFont::DemiBold));
    p.setPen(t.text);
    p.drawText(QPointF(127, 76), T("Действия"));
    // сегменты
    const QRectF seg(226, 65, 72, 18);
    p.setPen(QPen(t.fieldBorder, 1));
    p.setBrush(t.fieldDeep);
    p.drawRoundedRect(seg, R(6), R(6));
    p.setFont(mkFont(Theme::monoFamily(), 8, QFont::Bold));
    for (int i = 0; i < 3; ++i) {
        const QRectF s(seg.left() + 2 + i * 23, seg.top() + 2, 22, 14);
        if (i == 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.red);
            p.drawRoundedRect(s, R(4), R(4));
        }
        p.setPen(i == 0 ? t.onRed : t.text2);
        p.drawText(s, Qt::AlignCenter, QString::number(3 + 2 * i));
    }
    // клавиши
    auto keycap = [&](const QRectF& k, const QString& text) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.fieldBorder2);
        p.drawRoundedRect(k.adjusted(0, 0, 0, 3), R(6), R(6));
        p.setPen(QPen(t.fieldBorder2, 1));
        p.setBrush(t.keycap);
        p.drawRoundedRect(k, R(6), R(6));
        p.setFont(mkFont(Theme::monoFamily(), 10, QFont::Bold));
        p.setPen(t.text);
        p.drawText(k, Qt::AlignCenter, text);
    };
    keycap(QRectF(112, 88, 54, 28), T("ЛКМ"));
    p.setPen(QPen(t.redHi, 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(172, 103), QPointF(194, 103));
    p.drawLine(QPointF(190, 99), QPointF(194, 103));
    p.drawLine(QPointF(190, 107), QPointF(194, 103));
    keycap(QRectF(200, 88, 40, 28), "F");
    // тумблер
    const QRectF tg(258, 92, 34, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(t.red);
    p.drawRoundedRect(tg, R(10), R(10));
    p.setBrush(QColor(Qt::white));
    p.drawEllipse(QRectF(tg.right() - 17, tg.top() + 3, 14, 14));
    // ползунок скорости
    p.setBrush(t.track);
    p.drawRoundedRect(QRectF(112, 134, 180, 5), 2.5, 2.5);
    p.setBrush(t.red);
    p.drawRoundedRect(QRectF(112, 134, 110, 5), 2.5, 2.5);
    p.setPen(QPen(t.red, 3));
    p.setBrush(QColor(Qt::white));
    p.drawEllipse(QPointF(222, 136.5), 5, 5);
    p.setFont(mkFont(Theme::monoFamily(), 12, QFont::Bold));
    p.setPen(t.redHi);
    p.drawText(QPointF(112, 166), "≈ 20");
    p.setFont(mkFont(uiF, 8));
    p.setPen(t.muted);
    p.drawText(QPointF(154, 165), T("нажатий в секунду"));

    p.setClipping(false);
    p.setPen(QPen(t.cardBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, R(12), R(12));
}

// ===========================================================================
SvPlane::SvPlane(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 150);
    setCursor(Qt::CrossCursor);
}

void SvPlane::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QLinearGradient gx(r.topLeft(), r.topRight());
    gx.setColorAt(0, Qt::white);
    gx.setColorAt(1, QColor::fromHsvF(h_, 1, 1));
    p.setPen(Qt::NoPen);
    p.setBrush(gx);
    p.drawRoundedRect(r, 8, 8);
    QLinearGradient gy(r.topLeft(), r.bottomLeft());
    gy.setColorAt(0, QColor(0, 0, 0, 0));
    gy.setColorAt(1, QColor(0, 0, 0, 255));
    p.setBrush(gy);
    p.drawRoundedRect(r, 8, 8);
    const QPointF m(r.left() + s_ * r.width(), r.top() + (1 - v_) * r.height());
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 160), 3));
    p.drawEllipse(m, 7, 7);
    p.setPen(QPen(Qt::white, 2));
    p.drawEllipse(m, 7, 7);
}

void SvPlane::pickAt(const QPoint& pt) {
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    s_ = static_cast<float>(qBound(0.0, (pt.x() - r.left()) / r.width(), 1.0));
    v_ = static_cast<float>(qBound(0.0, 1.0 - (pt.y() - r.top()) / r.height(), 1.0));
    update();
    emit picked(s_, v_);
}
void SvPlane::mousePressEvent(QMouseEvent* e) { pickAt(e->position().toPoint()); }
void SvPlane::mouseMoveEvent(QMouseEvent* e) { if (e->buttons() & Qt::LeftButton) pickAt(e->position().toPoint()); }

HueStrip::HueStrip(QWidget* parent) : QWidget(parent) {
    setFixedWidth(20);
    setMinimumHeight(150);
    setCursor(Qt::PointingHandCursor);
}

void HueStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(2, 1, -2, -1);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    for (int i = 0; i <= 6; ++i) g.setColorAt(i / 6.0, QColor::fromHsvF(i == 6 ? 0.0 : i / 6.0, 1, 1));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(r, 6, 6);
    const qreal y = r.top() + h_ * r.height();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 160), 3));
    p.drawRoundedRect(QRectF(0.5, y - 3, width() - 1, 6), 3, 3);
    p.setPen(QPen(Qt::white, 2));
    p.drawRoundedRect(QRectF(0.5, y - 3, width() - 1, 6), 3, 3);
}

void HueStrip::pickAt(const QPoint& pt) {
    const QRectF r = QRectF(rect()).adjusted(2, 1, -2, -1);
    h_ = static_cast<float>(qBound(0.0, (pt.y() - r.top()) / r.height(), 0.9999));
    update();
    emit picked(h_);
}
void HueStrip::mousePressEvent(QMouseEvent* e) { pickAt(e->position().toPoint()); }
void HueStrip::mouseMoveEvent(QMouseEvent* e) { if (e->buttons() & Qt::LeftButton) pickAt(e->position().toPoint()); }

// ===========================================================================
// Свой цвет — с живым образцом
// ===========================================================================
ColorPickerDialog::ColorPickerDialog(Target target, const QColor& initial, QWidget* parent)
    : QDialog(parent), target_(target) {
    const bool accent = (target == Accent);
    const bool plain = (target == Plain);
    setWindowTitle(plain ? T("Цвет — AutoQ") : accent ? T("Свой цвет акцента — AutoQ") : T("Свой цвет фона — AutoQ"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(12);

    auto* head = new QHBoxLayout();
    auto* icon = new QLabel();
    icon->setPixmap(AqIcons::uiPixmap("palette", Theme::t().redHi, 20, devicePixelRatioF(), 1.8));
    head->addWidget(icon);
    head->addWidget(mkLabel(plain ? T("Цвет") : accent ? T("Свой цвет акцента") : T("Свой цвет фона"), "cardTitle"));
    head->addStretch(1);
    lay->addLayout(head);
    lay->addWidget(mkLabel(plain ? T("Двигай кружок и полосу — оверлей меняется сразу.")
                             : T("Двигай кружок и полосу — справа сразу видно, как будет выглядеть AutoQ."), "muted"));

    auto* body = new QHBoxLayout();
    body->setSpacing(10);
    plane_ = new SvPlane();
    plane_->setFixedSize(220, 196);
    hue_ = new HueStrip();
    hue_->setFixedHeight(196);
    body->addWidget(plane_);
    body->addWidget(hue_);
    body->addSpacing(8);
    auto* pcol = new QVBoxLayout();
    pcol->setSpacing(4);
    preview_ = new ThemePreview();
    pcol->addWidget(preview_);
    preview_->setVisible(!plain);
    pcol->addStretch(1);
    body->addLayout(pcol);
    lay->addLayout(body);

    auto* hexRow = new QHBoxLayout();
    hexRow->setSpacing(10);
    swatchNow_ = new QLabel();
    swatchNow_->setFixedSize(30, 30);
    hexRow->addWidget(swatchNow_);
    hex_ = new QLineEdit();
    hex_->setObjectName("mono");
    hex_->setFixedWidth(110);
    hex_->setMaxLength(7);
    hex_->setValidator(new QRegularExpressionValidator(QRegularExpression("#?[0-9A-Fa-f]{0,6}"), hex_));
    hex_->setToolTip(T("Цвет в виде #RRGGBB — можно вставить из любого редактора"));
    hexRow->addWidget(hex_);
    note_ = mkLabel(QString(), "small");
    note_->setWordWrap(true);
    hexRow->addWidget(note_, 1);
    lay->addLayout(hexRow);

    // готовые цвета
    auto* presetsRow = new QHBoxLayout();
    presetsRow->setSpacing(6);
    presetsRow->addWidget(mkLabel(T("Готовые"), "small"));
    const QStringList presets = plain ? QStringList{"#FFFFFF", "#0B0B0D", "#E11D2A", "#FF6A13", "#F5B700", "#22C55E",
                                                    "#14B8A6", "#3B82F6", "#A855F7", "#EC4899"}
                              : accent ? Theme::accentPresets() : Theme::bgPresets(Theme::current().dark);
    for (const QString& hx : presets) {
        auto* b = new QToolButton();
        b->setObjectName("swatch");
        b->setFixedSize(24, 24);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(Theme::colorLabel(QColor(hx)));
        styleSwatch(b, hx);
        connect(b, &QToolButton::clicked, this, [this, hx]() { setColor(QColor(hx)); });
        presetsRow->addWidget(b);
    }
    presetsRow->addStretch(1);
    lay->addLayout(presetsRow);

    auto* foot = new QHBoxLayout();
    foot->addStretch(1);
    auto* cancel = new QPushButton(T("Отмена"));
    auto* ok = new QPushButton(T("Применить"));
    ok->setObjectName("primary");
    ok->setDefault(true);
    foot->addWidget(cancel);
    foot->addWidget(ok);
    lay->addLayout(foot);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(plane_, &SvPlane::picked, this, [this](float s, float v) {
        s_ = s; v_ = v;
        setColor(QColor::fromHsvF(h_, s_, v_));
    });
    connect(hue_, &HueStrip::picked, this, [this](float h) {
        h_ = h;
        setColor(QColor::fromHsvF(h_, s_, v_));
    });
    connect(hex_, &QLineEdit::textEdited, this, [this](const QString& txt) {
        QString t = txt.trimmed();
        if (!t.startsWith('#')) t.prepend('#');
        if (t.size() == 7 && QColor(t).isValid()) setColor(QColor(t), true);
    });

    QColor c = initial.isValid() ? initial : (accent ? Theme::defaultAccent() : Theme::defaultBg(true));
    float h, s, v, a;
    c.getHsvF(&h, &s, &v, &a);
    h_ = h < 0 ? 0 : h; s_ = s; v_ = v;
    setColor(c);
}

void ColorPickerDialog::setColor(const QColor& c, bool fromHex) {
    color_ = c;
    float h, s, v, a;
    c.getHsvF(&h, &s, &v, &a);
    if (h >= 0) h_ = h;          // у серого тона нет — оставляем прежний
    s_ = s; v_ = v;
    plane_->setHsv(h_, s_, v_);
    hue_->setHue(h_);
    if (!fromHex) {
        QSignalBlocker b(hex_);
        hex_->setText(c.name().toUpper());
    }
    const auto& t = Theme::t();
    swatchNow_->setStyleSheet(QString("background: %1; border: 2px solid %2; border-radius: 15px;")
                                  .arg(c.name(), t.fieldBorder2.name()));
    emit colorChanged(c);
    if (target_ == Plain) { note_->setText(c.name().toUpper()); return; }
    Theme::Appearance ap = Theme::current();
    if (target_ == Accent) ap.accent = c;
    else if (ap.dark) ap.bgDark = c;
    else ap.bgLight = c;
    preview_->setAppearance(ap);
    const Theme::Tokens tk = Theme::tokensFor(ap);
    if (target_ == Accent) {
        note_->setText(tk.onRed.lightness() < 128 ? T("Надписи на кнопках будут тёмными — так их лучше видно.")
                                                  : T("Надписи на кнопках — белые."));
    } else {
        const bool adjusted = tk.bg.name() != c.name();
        note_->setText(adjusted ? T("Для %1 темы фон будет %2 — иначе текст плохо читается.")
                                      .arg(ap.dark ? T("тёмной") : T("светлой"), tk.bg.name().toUpper())
                                : T("Фон %1 темы.").arg(ap.dark ? T("тёмной") : T("светлой")));
    }
}

bool ColorPickerDialog::getColor(QWidget* parent, Target target, QColor& color) {
    ColorPickerDialog dlg(target, color, parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    color = dlg.color();
    return true;
}

// ===========================================================================
// Быстрое оформление (мини-окно)
// ===========================================================================
AppearancePopup::AppearancePopup(QWidget* parent) : QFrame(parent, Qt::Popup) {
    setObjectName("popup");
    setAttribute(Qt::WA_DeleteOnClose);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(9);
    lay->addWidget(mkLabel(T("Оформление"), "cardTitle"));
    themeSeg_ = new Segmented({T("Тёмная"), T("Светлая")}, false);
    lay->addWidget(themeSeg_);
    lay->addWidget(mkLabel(T("Цвет акцента"), "small"));
    auto* grid = new QGridLayout();
    grid->setSpacing(6);
    const QStringList presets = Theme::accentPresets();
    for (int i = 0; i < presets.size(); ++i) {
        auto* b = new QToolButton();
        b->setObjectName("swatch");
        b->setCheckable(true);
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("hex", presets[i]);
        b->setToolTip(Theme::colorName(presets[i]));
        const QString& hx = presets[i];
        connect(b, &QToolButton::clicked, this, [this, hx]() {
            Theme::Appearance a = Theme::current();
            a.accent = QColor(hx);
            apply(a);
        });
        swatches_ << b;
        grid->addWidget(b, i / 5, i % 5);
    }
    lay->addLayout(grid);
    auto* row = new QHBoxLayout();
    row->setSpacing(6);
    auto* custom = new QPushButton(T("Свой…"));
    custom->setToolTip(T("Любой цвет — с образцом интерфейса"));
    auto* more = new QPushButton(T("Все настройки…"));
    row->addWidget(custom);
    row->addWidget(more);
    lay->addLayout(row);

    connect(themeSeg_, &Segmented::changed, this, [this](int i) {
        Theme::Appearance a = Theme::current();
        a.dark = (i == 0);
        apply(a);
    });
    connect(custom, &QPushButton::clicked, this, [this]() {
        QWidget* owner = parentWidget();
        hide();
        QColor c = Theme::current().accent;
        if (ColorPickerDialog::getColor(owner, ColorPickerDialog::Accent, c)) {
            Theme::Appearance a = Theme::current();
            a.accent = c;
            Theme::apply(*qApp, a);
            Theme::saveAppearance(a);
            emit changed();
        }
        close();
    });
    connect(more, &QPushButton::clicked, this, [this]() {
        hide();
        emit moreRequested();
        close();
    });
    refresh();
}

void AppearancePopup::apply(const Theme::Appearance& a) {
    Theme::apply(*qApp, a);
    Theme::saveAppearance(a);
    refresh();
    emit changed();
}

void AppearancePopup::refresh() {
    const Theme::Appearance& a = Theme::current();
    {
        QSignalBlocker b(themeSeg_);
        themeSeg_->setCurrent(a.dark ? 0 : 1);
    }
    for (QToolButton* b : swatches_) {
        const QString hx = b->property("hex").toString();
        styleSwatch(b, hx);
        b->setChecked(hx.compare(a.accent.name(), Qt::CaseInsensitive) == 0);
    }
}

void AppearancePopup::popupBelow(QWidget* anchor) {
    adjustSize();
    QPoint pos = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height() + 6));
    if (QScreen* scr = anchor->screen()) {
        const QRect g = scr->availableGeometry();
        pos.setX(qBound(g.left() + 4, pos.x(), g.right() - width() - 4));
        if (pos.y() + height() > g.bottom()) pos.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 6);
    }
    move(pos);
    show();
}
