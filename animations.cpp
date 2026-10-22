#include "animations.h"
#include "theme.h"
#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QTimer>
#include <QTimerEvent>
#include <QBasicTimer>
#include <QtMath>
#include <cmath>
#include <QEnterEvent>

namespace {
qreal easeOut(qreal t) { t = qBound<qreal>(0, t, 1); return 1 - (1 - t) * (1 - t) * (1 - t); }

// Переход поверх области: снимки «до» и «после», живые виджеты под ним не трогаются
class TransitionOverlay : public QWidget {
public:
    struct Piece { QRect r; QPixmap pm; int delay; };
    TransitionOverlay(QWidget* area, const QPixmap& before, const QPixmap& after,
                      const QList<Piece>& pieces, const QString& style)
        : QWidget(area), before_(before), after_(after), pieces_(pieces), style_(style) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(area->rect());
        total_ = style == "cascade" ? 260 + 70 * qMax(0, int(pieces.size()) - 1) : 240;
        speed_ = Anim::speed();
        clock_.start();
        timer_.start(15, this);
        raise();
        show();
    }
protected:
    void timerEvent(QTimerEvent* e) override {
        if (e->timerId() != timer_.timerId()) return;
        if (clock_.elapsed() * speed_ >= total_) { timer_.stop(); hide(); deleteLater(); return; }
        update();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal ms = clock_.elapsed() * speed_;       // время с учётом скорости анимаций
        if (style_ == "cascade") {
            const qreal e = easeOut(ms / 160.0);
            p.setOpacity(1 - e);
            p.drawPixmap(0, 0, before_);
            p.setOpacity(e);
            p.drawPixmap(0, 0, after_);          // «после» без карточек (их места залиты фоном)
            for (const Piece& pc : pieces_) {
                const qreal k = easeOut((ms - pc.delay) / 260.0);
                if (k <= 0) continue;
                p.setOpacity(k);
                p.save();
                p.setClipRect(pc.r);              // выезжает в своём месте — не на строку статуса и полосу редактора
                p.drawPixmap(pc.r.topLeft() + QPoint(0, qRound((1 - k) * 28)), pc.pm);
                p.restore();
            }
            return;
        }
        const qreal e = easeOut(ms / total_);
        if (style_ == "slide") {
            p.setOpacity(qMax<qreal>(0, 1 - e * 1.6));
            p.drawPixmap(0, qRound(-e * 10), before_);
            p.setOpacity(e);
            p.drawPixmap(0, qRound((1 - e) * 22), after_);
        } else {
            p.setOpacity(1 - e);
            p.drawPixmap(0, 0, before_);
            p.setOpacity(e);
            p.drawPixmap(0, 0, after_);
        }
    }
private:
    QPixmap before_, after_;
    QList<Piece> pieces_;
    QString style_;
    int total_ = 240;
    qreal speed_ = 1;
    QElapsedTimer clock_;
    QBasicTimer timer_;
};
} // namespace

QString Anim::style() { return Theme::normalizeAnim(Theme::current().anim); }
bool Anim::enabled() { return style() != "none"; }
qreal Anim::speed() { return qBound(25, Theme::current().animSpeed, 400) / 100.0; }
int Anim::ms(int base) { return qMax(1, qRound(base / speed())); }

void Anim::transition(QWidget* area, const std::function<void()>& change, const QList<QWidget*>& cards,
                      const QString& styleOverride) {
    const QString st = styleOverride.isEmpty() ? style() : Theme::normalizeAnim(styleOverride);
    if (!area || st == "none" || !area->isVisible() || area->width() < 10 || area->height() < 10) {
        change();
        return;
    }
    // прошлый переход ещё идёт — убираем его: здесь, внутри (на странице) и снаружи
    // (по всему окну — его снимок лежал бы поверх нового перехода)
    auto drop = [](QObject* o) {
        if (auto* ov = dynamic_cast<TransitionOverlay*>(o)) { ov->hide(); ov->deleteLater(); }
    };
    for (QWidget* w : area->findChildren<QWidget*>()) drop(w);
    for (QWidget* up = area->parentWidget(); up; up = up->isWindow() ? nullptr : up->parentWidget())
        for (QObject* o : up->children()) drop(o);
    const QPixmap before = area->grab();
    change();
    // раскладки, которые change() только запросил (карточки, боковая панель, ряд действий), —
    // применить сейчас, иначе «после» снимется со старым расположением и в конце всё прыгнет
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        if (area->layout()) area->layout()->activate();
    }
    QPixmap after = area->grab();
    QList<TransitionOverlay::Piece> pieces;
    if (st == "cascade") {
        QPainter p(&after);
        // только видимая часть карточки (прокрутка обрезает её снизу и под полосой редактора)
        QList<QRect> rects;
        for (QWidget* c : cards) {
            if (!c || !c->isVisible()) continue;
            QRect vis = area->rect();
            for (QWidget* up = c->parentWidget(); up && up != area; up = up->parentWidget())
                vis &= QRect(up->mapTo(area, QPoint(0, 0)), up->size());
            const QRect r = QRect(c->mapTo(area, QPoint(0, 0)), c->size()) & vis;
            if (!r.isEmpty()) rects << r;
        }
        // по очереди сверху вниз, слева направо — как карточки стоят после смены
        std::sort(rects.begin(), rects.end(), [](const QRect& a, const QRect& b) {
            return a.top() != b.top() ? a.top() < b.top() : a.left() < b.left();
        });
        int i = 0;
        for (const QRect& r : rects) {
            pieces << TransitionOverlay::Piece{r, after.copy(QRect(r.topLeft() * after.devicePixelRatio(),
                                                                    r.size() * after.devicePixelRatio())), 40 + 70 * i++};
            pieces.last().pm.setDevicePixelRatio(after.devicePixelRatio());
        }
        // места карточек — фоном: они появятся по очереди
        p.setCompositionMode(QPainter::CompositionMode_Source);
        for (const auto& pc : pieces) p.fillRect(pc.r, Theme::t().bg);
    }
    new TransitionOverlay(area, before, after, pieces, st);
}

void Anim::appear(QWidget* w, const QString& styleOverride) {
    const QString st = styleOverride.isEmpty() ? style() : Theme::normalizeAnim(styleOverride);
    if (!w || st == "none") return;
    if (w->isWindow()) {
        auto* a = new QPropertyAnimation(w, "windowOpacity", w);
        a->setDuration(Anim::ms(170));
        a->setStartValue(0.0);
        a->setEndValue(1.0);
        a->start(QAbstractAnimation::DeleteWhenStopped);
        return;
    }
    auto* eff = new QGraphicsOpacityEffect(w);
    eff->setOpacity(0);
    w->setGraphicsEffect(eff);
    auto* fade = new QPropertyAnimation(eff, "opacity", w);
    fade->setDuration(Anim::ms(220));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    QObject::connect(fade, &QPropertyAnimation::finished, w, [w]() { w->setGraphicsEffect(nullptr); });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
    if (st != "fade") {
        auto* move = new QPropertyAnimation(w, "pos", w);
        move->setDuration(Anim::ms(st == "cascade" ? 300 : 240));
        move->setStartValue(w->pos() + QPoint(0, 14));
        move->setEndValue(w->pos());
        move->setEasingCurve(st == "cascade" ? QEasingCurve::OutBack : QEasingCurve::OutCubic);
        move->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

// ===========================================================================
Toast::Toast(QWidget* host) : QFrame(host), host_(host) {
    setObjectName("toast");
    setAttribute(Qt::WA_StyledBackground);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 9, 14, 9);
    text_ = new QLabel();
    text_->setTextFormat(Qt::PlainText);      // в тексте бывают названия игр и файлов
    text_->setWordWrap(true);
    lay->addWidget(text_);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, [this]() {
        if (!Anim::enabled()) { deleteLater(); return; }
        auto* eff = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(eff);
        auto* a = new QPropertyAnimation(eff, "opacity", this);
        a->setDuration(Anim::ms(220));
        a->setStartValue(1.0);
        a->setEndValue(0.0);
        connect(a, &QPropertyAnimation::finished, this, &QObject::deleteLater);
        a->start(QAbstractAnimation::DeleteWhenStopped);
    });
    host->installEventFilter(this);
    setCursor(Qt::PointingHandCursor);
}

void Toast::place() {
    const int w = qMin(480, qMax(200, host_->width() - 48));
    // одна строка, если влезает; иначе — перенос по ширине
    const int natural = text_->fontMetrics().horizontalAdvance(text_->text()) + 6;
    text_->setFixedWidth(qMin(natural, w - 28));
    adjustSize();
    // над строкой состояния (если она есть) по центру
    const int bottomGap = host_->property("toastBottom").isValid() ? host_->property("toastBottom").toInt() : 20;
    move((host_->width() - width()) / 2, host_->height() - height() - bottomGap);
}

bool Toast::eventFilter(QObject* o, QEvent* e) {
    if (o == host_ && e->type() == QEvent::Resize) place();
    return QFrame::eventFilter(o, e);
}

void Toast::hideAll(QWidget* host) {
    if (!host) return;
    for (Toast* t : host->findChildren<Toast*>(QString(), Qt::FindDirectChildrenOnly)) { t->hide(); t->deleteLater(); }
}

void Toast::show(QWidget* host, const QString& text, int ms) {
    if (!host) return;
    hideAll(host);                          // старое сообщение сразу уходит
    auto* t = new Toast(host);
    t->text_->setText(text);
    t->place();
    t->raise();
    t->QFrame::show();
    Anim::appear(t);
    t->timer_->start(qMax(1200, ms));
}

// ===========================================================================
AnimDemo::AnimDemo(const QString& style, QWidget* parent) : QWidget(parent), style_(style) {
    setMinimumSize(200, 110);
    setMouseTracking(true);
}

void AnimDemo::setHoverHost(QWidget* host) {
    host_ = host;
    if (host) host->installEventFilter(this);
}

qreal AnimDemo::speedNow() const {
    return speedOverride_ > 0 ? speedOverride_ / 100.0 : Anim::speed();
}

void AnimDemo::setPlaying(bool on) {
    if (on && !timer_) {
        last_ = QDateTime::currentMSecsSinceEpoch();
        if (t_ >= 2200) t_ = 0;          // с начала
        timer_ = startTimer(16);
    } else if (!on && timer_ && !once_) {
        killTimer(timer_);
        timer_ = 0;
        t_ = 2200;                        // готовый кадр
        update();
    }
}

void AnimDemo::replay() {
    once_ = true;
    t_ = 0;
    if (timer_) { killTimer(timer_); timer_ = 0; }
    setPlaying(true);
}

bool AnimDemo::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Enter) { hover_ = true; setPlaying(true); }
    else if (e->type() == QEvent::Leave) { hover_ = false; setPlaying(false); }
    return QWidget::eventFilter(o, e);
}

void AnimDemo::enterEvent(QEnterEvent*) { hover_ = true; setPlaying(true); }
void AnimDemo::leaveEvent(QEvent*) {
    if (host_ && host_->underMouse()) return;   // курсор ещё на карточке
    hover_ = false;
    setPlaying(false);
}

void AnimDemo::timerEvent(QTimerEvent*) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    t_ += (now - last_) * speedNow();
    last_ = now;
    if (t_ >= 2200) {
        if (hover_) t_ = std::fmod(t_, 2200.0);    // пока навели — по кругу
        else { once_ = false; t_ = 2200; killTimer(timer_); timer_ = 0; }
    }
    update();
}

void AnimDemo::paintEvent(QPaintEvent*) {
    const auto& t = Theme::t();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const int rr = Theme::r(10);
    p.setPen(QPen(t.cardBorder, 1));
    p.setBrush(t.bg);
    p.drawRoundedRect(r, rr, rr);
    // цикл 2,2 с: 0–0,7 появление, держим, 1,9–2,2 исчезновение; без наведения — готовый кадр
    const bool still = t_ >= 2200;
    const qreal ms = still ? 1500 : t_;
    const qreal fadeOut = ms > 1900 ? 1 - (ms - 1900) / 300.0 : 1.0;
    const qreal w = (r.width() - 40) / 3.0;
    for (int i = 0; i < 3; ++i) {
        qreal k = 1, dy = 0;
        if (style_ == "fade") k = easeOut(ms / 450.0);
        else if (style_ == "slide") { k = easeOut(ms / 450.0); dy = (1 - k) * 18; }
        else if (style_ == "cascade") { k = easeOut((ms - i * 140) / 380.0); dy = (1 - k) * 22; }
        p.setOpacity(qBound<qreal>(0, k, 1) * fadeOut);
        const QRectF c(r.left() + 10 + i * (w + 10), r.top() + 28 + dy, w, r.height() - 40);
        p.setPen(QPen(t.cardBorder, 1));
        p.setBrush(t.card);
        p.drawRoundedRect(c, Theme::r(8), Theme::r(8));
        p.setPen(Qt::NoPen);
        p.setBrush(i == 0 ? t.red : t.fieldBorder2);
        p.drawRoundedRect(QRectF(c.left() + 8, c.top() + 9, c.width() * 0.5, 6), 3, 3);
        p.setBrush(t.fieldBorder);
        p.drawRoundedRect(QRectF(c.left() + 8, c.top() + 22, c.width() - 16, 5), 2.5, 2.5);
        p.drawRoundedRect(QRectF(c.left() + 8, c.top() + 32, c.width() * 0.7, 5), 2.5, 2.5);
    }
    p.setOpacity(1);
    p.setPen(Qt::NoPen);
    p.setBrush(t.fieldBorder);
    p.drawRoundedRect(QRectF(r.left() + 10, r.top() + 10, r.width() * 0.35, 8), 4, 4);
}
