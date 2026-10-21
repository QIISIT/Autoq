#include "widgets.h"
#include "i18n.h"
#include <QSignalBlocker>
#include <QSpinBox>
#include "games.h"
#include "theme.h"
#include "icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTimerEvent>
#include <QWidget>
#include <QDateTime>
#include <QLineEdit>
#include <QToolButton>
#include <QCompleter>
#include <QAbstractItemView>
#include <QStringListModel>
#include <QKeyEvent>
#include <QTimer>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QSlider>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QCoreApplication>
#include <QGridLayout>
#include <QResizeEvent>
#include <QCursor>
#include <QScreen>
#include <QFileDialog>
#include <QStandardPaths>

// ---------------------------------------------------------------------------
ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(40, 24);
    setFocusPolicy(Qt::TabFocus);
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    const auto& t = Theme::t();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const bool on = isChecked();
    QColor track = on ? t.red : t.fieldBorder;
    if (!isEnabled()) track.setAlpha(110);
    p.setPen(hasFocus() ? QPen(t.redHi, 1.5) : Qt::NoPen);
    p.setBrush(track);
    const qreal rr = qMin<qreal>(r.height() / 2, Theme::r(12));
    p.drawRoundedRect(r, rr, rr);
    const qreal d = height() - 6;
    const qreal x = on ? width() - 3 - d : 3;
    p.setPen(Qt::NoPen);
    p.setBrush(on ? QColor(Qt::white) : t.muted);
    const qreal kr = qMin<qreal>(d / 2, Theme::r(9));
    p.drawRoundedRect(QRectF(x, 3, d, d), kr, kr);
}

// ---------------------------------------------------------------------------
Segmented::Segmented(const QStringList& labels, bool mono, const QString& suffix, QWidget* parent)
    : QFrame(parent) {
    setObjectName("segBox");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(3, 3, 3, 3);
    lay->setSpacing(4);
    group_.setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = new QPushButton(labels[i]);
        b->setObjectName(mono ? "seg" : "segText");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        if (mono) b->setMinimumWidth(36);
        group_.addButton(b, i);
        buttons_ << b;
        lay->addWidget(b, mono ? 0 : 1);
    }
    if (!suffix.isEmpty()) {
        auto* l = new QLabel(suffix);
        l->setObjectName("small");
        lay->addStretch(1);
        lay->addWidget(l);
        lay->addSpacing(6);
    }
    connect(&group_, &QButtonGroup::idClicked, this, &Segmented::changed);
}

void Segmented::setCurrent(int index) {
    if (QAbstractButton* b = group_.button(index)) { b->setChecked(true); return; }
    // нет такого пункта (например, своё значение) — ничего не выделено
    group_.setExclusive(false);
    for (QAbstractButton* b : group_.buttons()) b->setChecked(false);
    group_.setExclusive(true);
}

// ---------------------------------------------------------------------------
TimeChoice::TimeChoice(QWidget* parent) : QWidget(parent) {
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    QStringList labels;
    for (int c : Prefs::captureChoices()) labels << QString::number(c);
    seg_ = new Segmented(labels, true);
    h->addWidget(seg_);
    spin_ = new QSpinBox();
    spin_->setObjectName("timeCustom");
    spin_->setRange(1, 120);
    spin_->setSuffix(T(" сек"));
    spin_->setAlignment(Qt::AlignCenter);
    spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin_->setAccelerated(true);
    spin_->setFixedWidth(74);
    spin_->setToolTip(T("Своё время — любое от 1 до 120 секунд (колёсико мыши — быстро менять)"));
    spin_->setAccessibleName(T("Своё время, секунд"));
    h->addWidget(spin_);
    h->addStretch(1);
    setSeconds(Prefs::captureSeconds());
    connect(seg_, &Segmented::changed, this, [this](int i) {
        const int v = Prefs::captureChoices().value(i, 3);
        { QSignalBlocker b(spin_); spin_->setValue(v); }
        emit changed(v);
    });
    connect(spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        { QSignalBlocker b(seg_); seg_->setCurrent(Prefs::captureChoices().indexOf(v)); }
        emit changed(v);
    });
}

void TimeChoice::setSeconds(int s) {
    QSignalBlocker b1(seg_), b2(spin_);
    seg_->setCurrent(Prefs::captureChoices().indexOf(s));
    spin_->setValue(s);
}
int Segmented::current() const { return group_.checkedId(); }

// ---------------------------------------------------------------------------
FlowLayout::FlowLayout(QWidget* parent, int h, int v) : QLayout(parent), h_(h), v_(v) {
    setContentsMargins(0, 0, 0, 0);
}
FlowLayout::~FlowLayout() { clear(); }
void FlowLayout::addItem(QLayoutItem* item) { items_.append(item); }
QLayoutItem* FlowLayout::takeAt(int i) { return (i >= 0 && i < items_.size()) ? items_.takeAt(i) : nullptr; }
void FlowLayout::clear() {
    while (QLayoutItem* it = takeAt(0)) {
        // прячем сразу: удаление отложено, и старое не должно мелькать поверх нового
        if (QWidget* w = it->widget()) { w->hide(); w->deleteLater(); }
        delete it;
    }
}
QSize FlowLayout::minimumSize() const {
    QSize s;
    for (QLayoutItem* it : items_) s = s.expandedTo(it->minimumSize());
    return s;
}
int FlowLayout::doLayout(const QRect& r, bool testOnly) const {
    int x = r.x(), y = r.y(), lineH = 0;
    for (QLayoutItem* it : items_) {
        const QSize sz = it->sizeHint();
        int nextX = x + sz.width() + h_;
        if (nextX - h_ > r.right() + 1 && lineH > 0) {
            x = r.x();
            y += lineH + v_;
            nextX = x + sz.width() + h_;
            lineH = 0;
        }
        if (!testOnly) it->setGeometry(QRect(QPoint(x, y), QSize(qMin(sz.width(), r.width()), sz.height())));
        x = nextX;
        lineH = qMax(lineH, sz.height());
    }
    return y + lineH - r.y();
}

// ---------------------------------------------------------------------------
StartButton::StartButton(QWidget* parent) : QPushButton(parent) {
    setObjectName("start");
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(56);
    setMinimumWidth(176);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(18, 0, 18, 0);
    lay->setSpacing(12);
    icon_ = new QLabel();
    text_ = new QLabel(T("Старт"));
    text_->setObjectName("startText");
    chip_ = new QLabel("F6");
    chip_->setObjectName("startChip");
    for (QLabel* l : {icon_, text_, chip_}) l->setAttribute(Qt::WA_TransparentForMouseEvents);
    chip_->setFixedHeight(22);
    chip_->setAlignment(Qt::AlignCenter);
    lay->addStretch(1);
    lay->addWidget(icon_, 0, Qt::AlignVCenter);
    lay->addWidget(text_, 0, Qt::AlignVCenter);
    lay->addWidget(chip_, 0, Qt::AlignVCenter);
    lay->addStretch(1);
    setProperty("running", false);
    refreshIcon();
}

void StartButton::setState(bool running, const QString& hotkey, bool pending, bool unused) {
    running_ = running || pending || unused;
    pending_ = pending;
    unused_ = unused && !running && !pending;
    text_->setText(pending ? T("Отмена") : running ? T("Стоп") : unused_ ? T("Включить") : T("Старт"));
    chip_->setText(hotkey);
    // длинное сочетание (Ctrl+Alt+F7) — мельче, чтобы кнопка не раздувалась
    chip_->setProperty("len", hotkey.size() > 10 ? "l" : hotkey.size() > 6 ? "m" : "s");
    updateGeometry();
    setProperty("running", running_);
    Theme::repolish(this);
    for (QLabel* l : {text_, chip_}) Theme::repolish(l);
    chip_->ensurePolished();
    chip_->setMinimumWidth(chip_->fontMetrics().horizontalAdvance(hotkey) + 16);
    chip_->updateGeometry();
    refreshIcon();
    setToolTip(pending ? T("Ждём, пока выберешь окно игры. Нажми, чтобы отменить запуск")
               : unused_ ? T("Категория не используется — запустить её нельзя. Нажми, чтобы включить (как кружок в списке)")
                         : QString());
    setAccessibleName(QString("%1 (%2)").arg(text_->text(), hotkey));
}

QSize StartButton::sizeHint() const {
    // размер по содержимому (надпись + плашка клавиши), не меньше 176×56
    return layout() ? layout()->sizeHint().expandedTo(QSize(176, 56)).boundedTo(QSize(10000, 56)).expandedTo(QSize(176, 56))
                    : QSize(176, 56);
}

void StartButton::refreshIcon() {
    const qreal dpr = devicePixelRatioF();
    icon_->setPixmap(pending_ ? AqIcons::uiPixmap("x", Theme::t().redHi, 14, dpr, 2.6)
                     : unused_ ? AqIcons::uiPixmap("power", Theme::t().redHi, 15, dpr, 2.2)
                     : running_ ? AqIcons::uiPixmap("stop", Theme::t().redHi, 14, dpr)
                                : AqIcons::uiPixmap("play", Theme::t().onRed, 16, dpr));
}

// ---------------------------------------------------------------------------
ElideLabel::ElideLabel(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAttribute(Qt::WA_StyledBackground, false);
}
QSize ElideLabel::sizeHint() const {
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(text_) + 4, fm.height() + 4);
}
QSize ElideLabel::minimumSizeHint() const {
    const QFontMetrics fm(font());
    return QSize(qMin(sizeHint().width(), fm.horizontalAdvance("Wwwww…")), fm.height() + 4);
}
void ElideLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(palette().color(QPalette::WindowText));
    const QString t = fontMetrics().elidedText(text_, Qt::ElideRight, width());
    p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, t);
}

// ---------------------------------------------------------------------------
void ArrowLine::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(Theme::t().redHi, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    const qreal y = height() / 2.0, w = width();
    p.drawLine(QPointF(2, y), QPointF(w - 4, y));
    p.drawLine(QPointF(w - 10, y - 5), QPointF(w - 4, y));
    p.drawLine(QPointF(w - 10, y + 5), QPointF(w - 4, y));
}

void PlusBadge::paintEvent(QPaintEvent*) {
    const auto& t = Theme::t();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(t.redLine, 1));
    p.setBrush(t.redTint);
    p.drawEllipse(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    AqIcons::paint(p, "plus", QRectF(9, 9, 16, 16), t.redHi, 2.6);
}

// ---------------------------------------------------------------------------
InfoBadge::InfoBadge(const QString& title, const QString& text, QWidget* parent)
    : QAbstractButton(parent), title_(title), text_(text) {
    setFixedSize(18, 18);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setToolTip(T("Подробнее"));
    setAccessibleName(title);
    setAttribute(Qt::WA_Hover);
    connect(this, &QAbstractButton::clicked, this, &InfoBadge::showInfo);
}

void InfoBadge::paintEvent(QPaintEvent*) {
    const auto& t = Theme::t();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool hot = underMouse() || hasFocus() || isDown();
    AqIcons::paint(p, "help", QRectF(rect()).adjusted(1, 1, -1, -1), hot ? t.redHi : t.muted, 1.9);
}

void InfoBadge::showInfo() {
    auto* pop = new QFrame(this, Qt::Popup);
    pop->setObjectName("popup");
    pop->setAttribute(Qt::WA_DeleteOnClose);
    auto* lay = new QVBoxLayout(pop);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(8);
    auto* head = new QLabel(title_);
    head->setObjectName("cardTitle");
    head->setWordWrap(true);
    lay->addWidget(head);
    auto* body = new QLabel(text_);
    body->setObjectName("muted");
    body->setWordWrap(true);
    body->setTextFormat(Qt::PlainText);
    lay->addWidget(body);
    // ширина задана — высоту считаем по переносам строк (иначе текст обрезался бы)
    pop->ensurePolished();
    for (QLabel* l : {head, body}) {
        l->ensurePolished();
        l->setFixedWidth(328);
        l->setFixedHeight(l->heightForWidth(328));
    }
    pop->adjustSize();
    QPoint pos = mapToGlobal(QPoint(width() / 2 - 24, height() + 6));
    if (QScreen* scr = screen()) {
        const QRect g = scr->availableGeometry();
        if (pos.y() + pop->height() > g.bottom()) pos.setY(mapToGlobal(QPoint(0, 0)).y() - pop->height() - 6);
        pos.setX(qBound(g.left() + 4, pos.x(), g.right() - pop->width() - 4));
    }
    pop->move(pos);
    pop->show();
}

// ---------------------------------------------------------------------------
StatusDot::StatusDot(QWidget* parent) : QWidget(parent) { setFixedSize(10, 10); }

void StatusDot::setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    if (on && !timer_) timer_ = startTimer(700);
    if (!on && timer_) { killTimer(timer_); timer_ = 0; }
    phase_ = false;
    update();
}

void StatusDot::timerEvent(QTimerEvent*) { phase_ = !phase_; update(); }

void StatusDot::paintEvent(QPaintEvent*) {
    const auto& t = Theme::t();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(1, 1, 8, 8);
    if (active_) {
        p.setPen(Qt::NoPen);
        QColor c = t.redHi;
        if (phase_) c.setAlpha(110);
        p.setBrush(c);
        p.drawEllipse(r);
    } else {
        p.setPen(QPen(t.faint, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

// ---------------------------------------------------------------------------
CaptureCountdown::CaptureCountdown(QPushButton* button, QObject* parent)
    : QObject(parent), btn_(button) {
    idle_ = btn_->text();
}

void CaptureCountdown::setIdleText(const QString& text) {
    idle_ = text;
    if (!active()) btn_->setText(text);
}

void CaptureCountdown::start(int seconds) {
    if (active()) return;
    totalMs_ = qMax(1, seconds) * 1000;
    endMs_ = QDateTime::currentMSecsSinceEpoch() + totalMs_;
    btn_->setProperty("counting", true);
    Theme::repolish(btn_);
    timer_ = startTimer(100);
    timerEvent(nullptr);
}

void CaptureCountdown::cancel() {
    if (!active()) return;
    restore();
    emit cancelled();
}

void CaptureCountdown::restore() {
    if (timer_) killTimer(timer_);
    timer_ = 0;
    btn_->setStyleSheet(QString());
    btn_->setProperty("counting", false);
    Theme::repolish(btn_);
    btn_->setText(idle_);
    btn_->setToolTip(QString());
}

void CaptureCountdown::timerEvent(QTimerEvent*) {
    const qint64 left = endMs_ - QDateTime::currentMSecsSinceEpoch();
    if (left <= 0) {
        restore();
        emit finished();
        return;
    }
    const int sec = static_cast<int>((left + 999) / 1000);
    btn_->setText(T("Переключись в игру… %1").arg(sec));
    btn_->setToolTip(T("Нажми ещё раз, чтобы отменить"));
    // полоска: сколько времени осталось
    const double f = qBound(0.0, double(left) / totalMs_, 1.0);
    QColor c = Theme::t().red;
    c.setAlpha(Theme::t().light ? 50 : 70);
    const QString rgba = QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    btn_->setStyleSheet(QString("QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
                                "stop:0 %1, stop:%2 %1, stop:%3 transparent, stop:1 transparent); }")
                            .arg(rgba).arg(f, 0, 'f', 3).arg(qMin(1.0, f + 0.001), 0, 'f', 3));
}

// ---------------------------------------------------------------------------
QRect GameListDelegate::circleRect(const QRect& r) {
    return QRect(r.right() - 30, r.center().y() - 11, 22, 22);
}

void GameListDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    const auto& t = Theme::t();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const bool sel = opt.state & QStyle::State_Selected;
    const bool hover = opt.state & QStyle::State_MouseOver;
    const bool armed = idx.data(ArmedRole).toBool();
    const bool running = idx.data(RunningRole).toBool();
    const QRectF r = QRectF(opt.rect).adjusted(1, 1, -1, -1);
    if (sel) {
        p->setPen(QPen(t.redLine, 1));
        p->setBrush(t.redTint);
        p->drawRoundedRect(r, Theme::r(10), Theme::r(10));
    } else if (hover) {
        p->setPen(Qt::NoPen);
        p->setBrush(t.field);
        p->drawRoundedRect(r, Theme::r(10), Theme::r(10));
    }
    const QPixmap tile = idx.data(sel ? TileActiveRole : TileRole).value<QPixmap>();
    const int tileSize = 34;
    const QRect tileRect(opt.rect.left() + 10, opt.rect.top() + (opt.rect.height() - tileSize) / 2, tileSize, tileSize);
    // не используется (пустой кружок) — значок и название приглушены
    const bool unused = !armed && !running;
    p->setOpacity(unused ? 0.42 : 1.0);
    if (!tile.isNull()) p->drawPixmap(tileRect, tile);

    // клавиша запуска — плашкой на значке
    const QString key = idx.data(KeyRole).toString();
    if (!key.isEmpty()) {
        QFont kf(Theme::monoFamily());
        kf.setPixelSize(9);
        kf.setWeight(QFont::Bold);
        const QFontMetrics fm(kf);
        const QString k = fm.elidedText(key, Qt::ElideRight, 40);
        const int kw = qMax(14, fm.horizontalAdvance(k) + 7), kh = 13;
        const QRectF kr(tileRect.right() - kw + 7, tileRect.bottom() - kh + 5, kw, kh);
        p->setPen(QPen(running ? t.red : t.fieldBorder2, 1));
        p->setBrush(running ? t.red : t.keycap);
        p->drawRoundedRect(kr, qMin(4, Theme::r(4)), qMin(4, Theme::r(4)));
        p->setFont(kf);
        p->setPen(running ? t.onRed : t.text);
        p->drawText(kr, Qt::AlignCenter, k);
    }
    p->setOpacity(1.0);

    const QRect circle = circleRect(opt.rect);
    const int textX = tileRect.right() + 13;
    const QRect textRect(textX, opt.rect.top() + 9, circle.left() - textX - 4, opt.rect.height() - 18);
    QFont f1(Theme::uiFamily());
    f1.setPixelSize(13);
    f1.setWeight(sel ? QFont::DemiBold : QFont::Normal);
    QFont f2(Theme::uiFamily());
    f2.setPixelSize(11);
    f2.setWeight(running ? QFont::DemiBold : QFont::Normal);
    p->setFont(f1);
    p->setPen(unused ? t.faint : sel ? t.text : t.text2);
    const QString name = QFontMetrics(f1).elidedText(idx.data(Qt::DisplayRole).toString(),
                                                      Qt::ElideRight, textRect.width());
    p->drawText(textRect, Qt::AlignLeft | Qt::AlignTop, name);
    p->setFont(f2);
    p->setPen(running ? t.redHi : sel ? t.redSoft : t.faint);
    const QString sub = running ? T("▶ работает") : unused ? T("не используется") : idx.data(SubRole).toString();
    p->drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, QFontMetrics(f2).elidedText(sub, Qt::ElideRight, textRect.width()));

    // кружок «используется»: закрашен — категорию можно запустить, пустой — нельзя никак
    const QRectF c = QRectF(circle).adjusted(4, 4, -4, -4);
    const auto* view = qobject_cast<const QAbstractItemView*>(opt.widget);
    const bool overCircle = hover && view && circle.contains(view->viewport()->mapFromGlobal(QCursor::pos()));
    if (armed) {
        p->setPen(Qt::NoPen);
        p->setBrush(t.red);
        p->drawEllipse(c);
        AqIcons::paint(*p, "check", c.adjusted(3, 3, -3, -3), t.onRed, 3.2);
    } else {
        p->setPen(QPen(overCircle ? t.redHi : (t.light ? t.muted : QColor("#E6E6EA")), overCircle ? 2.0 : 1.6));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(c.adjusted(0.8, 0.8, -0.8, -0.8));
    }
    p->restore();
}

// ---------------------------------------------------------------------------
CardFlow::CardFlow(QWidget* parent) : QWidget(parent) {
    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(14);
    grid_->setSizeConstraint(QLayout::SetNoConstraint);
}

void CardFlow::setCards(const QList<QWidget*>& cards) {
    cards_ = cards;
    cols_ = 0;
    relayout(columnsFor(width() > 0 ? width() : 900));
}

int CardFlow::columnsFor(int w) const {
    int need = 0;
    for (QWidget* c : cards_) need = qMax(need, c->minimumSizeHint().width());

    need = qMax(need, 250);
    const int sp = grid_->horizontalSpacing() < 0 ? 14 : grid_->horizontalSpacing();
    for (int cols = qMax(1, int(cards_.size())); cols > 1; --cols)
        if (w >= cols * need + (cols - 1) * sp) return cols;
    return 1;
}

QSize CardFlow::minimumSizeHint() const {
    int w = 0;
    for (QWidget* c : cards_) w = qMax(w, c->minimumSizeHint().width());
    return QSize(w, minimumHeight());
}

QSize CardFlow::sizeHint() const { return QSize(900, minimumHeight()); }

void CardFlow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    const int c = columnsFor(e->size().width());
    if (c != cols_) relayout(c);
}

void CardFlow::relayout(int cols) {
    cols_ = cols;
    while (grid_->count() > 0) delete grid_->takeAt(0);
    for (int i = 0; i < 4; ++i) { grid_->setColumnStretch(i, 0); grid_->setRowStretch(i, 0); }
    const int n = cards_.size();
    for (int i = 0; i < n; ++i) {
        const int row = i / cols, col = i % cols;
        // последняя карточка одна в ряду — на всю ширину
        const int span = (i == n - 1 && col == 0 && cols > 1) ? cols : 1;
        grid_->addWidget(cards_[i], row, col, 1, span);
    }
    for (int i = 0; i < cols; ++i) grid_->setColumnStretch(i, 1);
    grid_->activate();
    // высота — сколько нужно рядам (раскладка без ограничений сама её не задаёт)
    int h = 0;
    const int rows = (n + cols - 1) / cols;
    for (int r = 0; r < rows; ++r) {
        int rowH = 0;
        for (int i = r * cols; i < qMin(n, (r + 1) * cols); ++i)
            rowH = qMax(rowH, cards_[i]->minimumSizeHint().height());
        h += rowH;
    }
    h += (rows - 1) * (grid_->verticalSpacing() < 0 ? 14 : grid_->verticalSpacing());
    setMinimumHeight(h);
    updateGeometry();
}
// ---------------------------------------------------------------------------
ChipEditor::ChipEditor(bool mono, const QString& addText, const QString& addTip, QWidget* parent)
    : QWidget(parent), mono_(mono), addText_(addText), addTip_(addTip) {
    flow_ = new FlowLayout(this, 5, 5);
    rebuild();
}

void ChipEditor::setItems(const QStringList& items) {
    if (edit_) return;            // не мешаем дописывать
    items_ = items;
    rebuild();
}

void ChipEditor::rebuild() {
    flow_->clear();
    edit_ = nullptr;
    const auto& t = Theme::t();
    QFont f = mono_ ? QFont(Theme::monoFamily()) : QFont(Theme::uiFamily());
    f.setPixelSize(11);
    for (const QString& it : items_) {
        auto* chip = new QFrame();
        chip->setObjectName("ruleChip");
        auto* h = new QHBoxLayout(chip);
        h->setContentsMargins(7, 1, 2, 1);
        h->setSpacing(2);
        auto* l = new QLabel(QFontMetrics(f).elidedText(it, Qt::ElideMiddle, 120));
        l->setObjectName(mono_ ? "chipMonoText" : "chipUiText");
        l->setToolTip(it);
        h->addWidget(l);
        auto* x = new QToolButton();
        x->setObjectName("chipX");
        x->setFixedSize(16, 16);
        x->setIcon(AqIcons::ui("x", t.faint, 9, 2.4));
        x->setToolTip(T("Убрать «") + it + "»");
        x->setAccessibleName(x->toolTip());
        x->setCursor(Qt::PointingHandCursor);
        connect(x, &QToolButton::clicked, this, [this, it]() {
            items_.removeAll(it);
            emit itemsChanged(items_);
            QTimer::singleShot(0, this, [this]() { rebuild(); });
        });
        h->addWidget(x);
        flow_->addWidget(chip);
    }
    auto* add = new QToolButton();
    add->setObjectName("chipAdd");
    add->setText(addText_);
    add->setIcon(AqIcons::ui("plus", t.muted, 10, 2.4));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setToolTip(addTip_);
    add->setAccessibleName(addTip_);
    add->setCursor(Qt::PointingHandCursor);
    connect(add, &QToolButton::clicked, this, &ChipEditor::startAdd);
    flow_->addWidget(add);
    if (!pickText_.isEmpty()) {
        auto* pick = new QToolButton();
        pick->setObjectName("chipAdd");
        pick->setText(pickText_);
        pick->setIcon(AqIcons::ui("window", t.muted, 11, 2.0));
        pick->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        pick->setToolTip(pickTip_);
        pick->setAccessibleName(pickTip_);
        pick->setCursor(Qt::PointingHandCursor);
        connect(pick, &QToolButton::clicked, this, [this, pick]() { emit pickRequested(pick); });
        flow_->addWidget(pick);
    }
    updateGeometry();
}

void ChipEditor::startAdd() {
    if (edit_) return;
    // кнопки «+» и «выбрать» меняем на поле ввода
    for (int n = pickText_.isEmpty() ? 1 : 2; n > 0 && flow_->count() > 0; --n) {
        QLayoutItem* last = flow_->takeAt(flow_->count() - 1);
        if (last) { if (last->widget()) last->widget()->deleteLater(); delete last; }
    }
    edit_ = new QLineEdit();
    edit_->setObjectName("chipEdit");
    edit_->setFixedWidth(150);
    edit_->setPlaceholderText(mono_ ? "game.exe" : T("часть заголовка"));
    edit_->setProperty("mono", mono_);
    if (!suggestions_.isEmpty()) {
        auto* c = new QCompleter(suggestions_, edit_);
        c->setCaseSensitivity(Qt::CaseInsensitive);
        c->setFilterMode(Qt::MatchContains);
        edit_->setCompleter(c);
    }
    edit_->installEventFilter(this);
    QLineEdit* e = edit_;
    connect(e, &QLineEdit::returnPressed, this, [this, e]() { if (e == edit_) finishAdd(true); });
    connect(e, &QLineEdit::editingFinished, this, [this, e]() {
        // потеря фокуса: если что-то написано — добавить, иначе отменить
        if (e == edit_ && !(e->completer() && e->completer()->popup() && e->completer()->popup()->isVisible()))
            finishAdd(!e->text().trimmed().isEmpty());
    });
    flow_->addWidget(edit_);
    // × — передумал (мисклик): закрыть поле, ничего не добавляя
    auto* cancel = new QToolButton();
    cancel->setObjectName("chipX");
    cancel->setFixedSize(20, 20);
    cancel->setIcon(AqIcons::ui("x", Theme::t().muted, 10, 2.4));
    cancel->setToolTip(T("Закрыть без изменений (Esc)"));
    cancel->setAccessibleName(cancel->toolTip());
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setFocusPolicy(Qt::NoFocus);
    connect(cancel, &QToolButton::pressed, this, [this, e]() { if (e == edit_) finishAdd(false); });
    flow_->addWidget(cancel);
    updateGeometry();
    edit_->setFocus();
}

void ChipEditor::finishAdd(bool commit) {
    if (!edit_) return;
    const QString text = edit_->text();
    edit_ = nullptr;
    bool changed = false;
    if (commit) {
        for (const QString& p : text.split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts)) {
            const QString v = p.trimmed();
            if (v.isEmpty() || items_.contains(v, Qt::CaseInsensitive)) continue;
            items_ << v;
            changed = true;
        }
    }
    if (changed) emit itemsChanged(items_);
    QTimer::singleShot(0, this, [this]() { rebuild(); });
}

bool ChipEditor::eventFilter(QObject* o, QEvent* e) {
    if (o == edit_ && e->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        finishAdd(false);
        return true;
    }
    return QWidget::eventFilter(o, e);
}

// ---------------------------------------------------------------------------
TemplateCard::TemplateCard(const QPixmap& tile, const QString& name, const QString& sub, QWidget* parent)
    : QPushButton(parent) {
    setObjectName("templateCard");
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(210, 66);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(name);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(14, 10, 14, 10);
    h->setSpacing(12);
    auto* icon = new QLabel();
    icon->setPixmap(tile);
    auto* col = new QVBoxLayout();
    col->setSpacing(2);
    auto* n = new QLabel(name);
    n->setObjectName("gameName");
    auto* s = new QLabel(sub);
    s->setObjectName("small");
    col->addWidget(n);
    col->addWidget(s);
    h->addWidget(icon);
    h->addLayout(col, 1);
    for (QLabel* l : {icon, n, s}) l->setAttribute(Qt::WA_TransparentForMouseEvents);
}

QSize TemplateCard::sizeHint() const {
    return layout() ? layout()->sizeHint().expandedTo(QSize(210, 66)) : QSize(210, 66);
}

// ---------------------------------------------------------------------------
bool askYesNo(QWidget* parent, const QString& title, const QString& text,
              const QString& yes, const QString& no, bool defaultYes) {
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, parent);
    QPushButton* y = box.addButton(yes, QMessageBox::YesRole);
    QPushButton* n = box.addButton(no, QMessageBox::NoRole);
    box.setDefaultButton(defaultYes ? y : n);
    box.setEscapeButton(n);
    box.exec();
    return box.clickedButton() == y;
}

// ---------------------------------------------------------------------------
void WheelGuard::install(QWidget* root) {
    auto* g = new WheelGuard(root);
    for (QWidget* w : root->findChildren<QWidget*>()) {
        if (qobject_cast<QSlider*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w)) {
            w->setFocusPolicy(Qt::StrongFocus);
            w->installEventFilter(g);
        }
    }
}

bool WheelGuard::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Wheel) {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && !w->hasFocus()) {
            // колёсико — странице: ищем прокрутку выше по иерархии
            for (QWidget* p = w->parentWidget(); p; p = p->parentWidget())
                if (auto* area = qobject_cast<QAbstractScrollArea*>(p)) {
                    QCoreApplication::sendEvent(area->verticalScrollBar(), e);
                    break;
                }
            return true;
        }
    }
    return QObject::eventFilter(o, e);
}

// ---------------------------------------------------------------------------
SoundBox::SoundBox(Sounds::Kind kind, bool inherit, QWidget* parent)
    : QComboBox(parent), kind_(kind), inherit_(inherit) {
    setMinimumWidth(160);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(12);
    cur_ = inherit ? QString() : Sounds::sound(kind);
    refill();
    connect(this, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        QString id = itemData(i).toString();
        if (id == QLatin1String("add")) {
            const QString f = QFileDialog::getOpenFileName(window(), T("Свой звук — файл WAV"),
                                                           QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                                                           T("Звук WAV (*.wav)"));
            if (f.isEmpty()) { refill(); return; }
            QString err;
            id = Sounds::addUserSound(f, &err);
            if (id.isEmpty()) {
                QMessageBox::warning(window(), T("Свой звук"), err);
                refill();
                return;
            }
        }
        cur_ = id;
        refill();
        emit soundChosen(id);
        Sounds::preview(inherit_ && id.isEmpty() ? Sounds::sound(kind_) : id);
    });
}

void SoundBox::setSound(const QString& id) {
    cur_ = id;
    refill();
}

void SoundBox::refill() {
    QSignalBlocker b(this);
    clear();
    if (inherit_) addItem(T("Как в настройках · %1").arg(Sounds::label(Sounds::sound(kind_))), QString());
    for (const QString& id : Sounds::ids())
        if (id != QLatin1String("none")) addItem(Sounds::label(id), id);
    const QStringList mine = Sounds::userSounds();
    if (!mine.isEmpty()) {
        insertSeparator(count());
        for (const QString& id : mine) addItem(T("Мой: %1").arg(Sounds::label(id)), id);
    }
    // выбранный раньше файл не из «Моих звуков» (до 1.13) или пропавший — тоже в списке
    if (!cur_.isEmpty() && findData(cur_) < 0 && cur_ != QLatin1String("none"))
        addItem((Sounds::exists(cur_) ? T("Свой: %1") : T("Нет файла: %1")).arg(Sounds::label(cur_)), cur_);
    addItem(Sounds::label("none"), QStringLiteral("none"));
    insertSeparator(count());
    addItem(T("Добавить свой звук…"), QStringLiteral("add"));
    setCurrentIndex(qMax(0, findData(cur_)));
    setToolTip(currentText());
}

void SoundBox::showPopup() {
    refill();
    QComboBox::showPopup();
}
