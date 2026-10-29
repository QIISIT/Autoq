#include "ui/style/widgets.h"
#include "core/i18n.h"
#include <QSignalBlocker>
#include <QSpinBox>
#include "core/games.h"
#include "ui/style/theme.h"
#include "ui/style/icons.h"
#include "core/tips.h"
#include <QPushButton>
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
#include <QInputDialog>
#include <QMessageBox>
#include <QSlider>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QCoreApplication>
#include <QResizeEvent>
#include <QCursor>
#include <QScreen>
#include <QFileDialog>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QGraphicsDropShadowEffect>
#include "ui/style/animations.h"

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
    setProperty("aqEl", "seg");          // вид — редактор интерфейса, «Переключатели»
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
    spin_ = new QSpinBox();
    spin_->setObjectName("timeCustom");
    spin_->setRange(1, 120);
    spin_->setPrefix(T("через "));
    spin_->setSuffix(T(" сек"));
    spin_->setAlignment(Qt::AlignCenter);
    spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin_->setAccelerated(true);
    spin_->setFixedWidth(108);
    spin_->setToolTip(T("Сколько секунд дать себе, чтобы переключиться в игру: от 1 до 120 (колёсико мыши — быстро менять)"));
    spin_->setAccessibleName(T("Время на переключение в игру, секунд"));
    h->addWidget(spin_);
    setSeconds(Prefs::captureSeconds());
    connect(spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TimeChoice::changed);
}

void TimeChoice::setSeconds(int s) {
    QSignalBlocker b(spin_);
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
        // спрятанное нарочно (hide()) — места не занимает; ещё не показанное (Qt покажет
        // его на следующем круге событий) — занимает
        if (QWidget* w = it->widget(); w && w->isHidden() && w->testAttribute(Qt::WA_WState_ExplicitShowHide)) continue;
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
    const QString pc = Theme::element("pause").color;
    QPen pen(pc.isEmpty() ? Theme::t().redHi : Theme::textColor(QColor(pc)), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
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

static std::function<void()>& allTipsHandler() { static std::function<void()> f; return f; }
void InfoBadge::setAllTipsHandler(const std::function<void()>& f) { allTipsHandler() = f; }

InfoBadge::InfoBadge(const QStringList& tipIds, QWidget* parent)
    : InfoBadge(T("Подсказки"), QString(), parent) {
    tips_ = tipIds;
    setToolTip(T("Подсказки: как и что делать"));
    setProperty("aqTips", true);        // главное окно прячет такие, если подсказки выключены
}

void InfoBadge::showInfo() {
    auto* pop = new QFrame(this, Qt::Popup);
    pop->setObjectName("popup");
    pop->setAttribute(Qt::WA_DeleteOnClose);
    auto* lay = new QVBoxLayout(pop);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(8);
    // заголовок и текст (у подсказок — по паре на каждую)
    QList<QPair<QString, QString>> parts;
    if (tips_.isEmpty()) parts << qMakePair(title_, text_);
    for (const QString& id : tips_) {
        const Tips::Tip t = Tips::get(id);
        if (!t.isNull()) parts << qMakePair(t.title, t.text);
    }
    const int w = tips_.isEmpty() ? 328 : 360;
    QList<QLabel*> labels;
    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0) lay->addSpacing(4);
        auto* head = new QLabel(parts[i].first);
        head->setObjectName(tips_.isEmpty() ? "cardTitle" : "fieldLabel");
        head->setWordWrap(true);
        lay->addWidget(head);
        auto* body = new QLabel(parts[i].second);
        body->setObjectName("muted");
        body->setWordWrap(true);
        body->setTextFormat(Qt::PlainText);
        lay->addWidget(body);
        labels << head << body;
    }
    if (!tips_.isEmpty() && allTipsHandler()) {
        auto* more = new QPushButton(T("Все подсказки…"));
        more->setObjectName("ghost");
        more->setCursor(Qt::PointingHandCursor);
        lay->addWidget(more, 0, Qt::AlignLeft);
        connect(more, &QPushButton::clicked, pop, [pop]() {
            pop->close();
            const std::function<void()> f = allTipsHandler();
            QTimer::singleShot(0, f);
        });
    }
    // ширина задана — высоту считаем по переносам строк (иначе текст обрезался бы)
    pop->ensurePolished();
    for (QLabel* l : labels) {
        l->ensurePolished();
        l->setFixedWidth(w);
        l->setFixedHeight(l->heightForWidth(w));
    }
    pop->adjustSize();
    QPoint pos = mapToGlobal(QPoint(width() / 2 - 24, height() + 6));
    if (QScreen* scr = screen()) {
        const QRect g = scr->availableGeometry();
        if (pos.y() + pop->height() > g.bottom()) pos.setY(mapToGlobal(QPoint(0, 0)).y() - pop->height() - 6);
        pos.setY(qMax(g.top() + 4, pos.y()));       // длинный список подсказок — не выше экрана
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
QRect GameListDelegate::circleRect(const QRect& r, bool compact) {
    if (compact) {
        // точка в правом верхнем углу значка (значок — по центру строки)
        const int tileRight = r.center().x() + 17;
        return QRect(tileRight - 8, r.top() + 2, 16, 16);
    }
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
    const int tileX = compact ? opt.rect.center().x() - tileSize / 2 + 1 : opt.rect.left() + 10;
    const QRect tileRect(tileX, opt.rect.top() + (opt.rect.height() - tileSize) / 2, tileSize, tileSize);
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

    const auto* view = qobject_cast<const QAbstractItemView*>(opt.widget);
    if (compact) {
        // узкая панель: только значок; «работает» — красная рамка, «используется» — точка в углу
        if (running) {
            p->setPen(QPen(t.red, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(tileRect).adjusted(-3, -3, 3, 3), Theme::r(10), Theme::r(10));
        }
        const QRect cr = circleRect(opt.rect, true);
        const QRectF c = QRectF(cr).adjusted(3, 3, -3, -3);
        const bool overDot = hover && view && cr.contains(view->viewport()->mapFromGlobal(QCursor::pos()));
        p->setPen(QPen(armed ? t.card : (overDot ? t.redHi : (t.light ? t.muted : QColor("#E6E6EA"))), armed ? 2.0 : 1.6));
        p->setBrush(armed ? t.red : t.card);
        p->drawEllipse(c);
        p->restore();
        return;
    }

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
    // показать сразу: addWidget показывает отложенно, и переход при смене игры снял бы пустые ряды
    for (int i = 0; i < flow_->count(); ++i)
        if (QWidget* w = flow_->itemAt(i)->widget()) w->show();
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

QLabel* mkLabel(const QString& text, const char* objectName) {
    auto* l = new QLabel(text);
    l->setObjectName(objectName);
    return l;
}

namespace {
// мс или — «По кадрам» — десятые кадра: 15 видно как «1,5», вводить можно «1,5» и «1.5»
class MsSpin : public QSpinBox {
public:
    bool tenths = false;
protected:
    QString textFromValue(int v) const override {
        if (!tenths) return QSpinBox::textFromValue(v);
        return locale().toString(v / double(kFrameUnit), 'f', v % kFrameUnit ? 1 : 0);
    }
    QString number(QString t) const {
        if (t.startsWith(prefix())) t.remove(0, prefix().size());
        if (t.endsWith(suffix())) t.chop(suffix().size());
        t = t.trimmed();
        t.replace(',', '.');
        if (locale().decimalPoint() != ".") t.replace(locale().decimalPoint(), ".");
        return t;
    }
    int valueFromText(const QString& text) const override {
        if (!tenths) return QSpinBox::valueFromText(text);
        return qRound(number(text).toDouble() * kFrameUnit);
    }
    QValidator::State validate(QString& input, int& pos) const override {
        if (!tenths) return QSpinBox::validate(input, pos);
        static const QRegularExpression re("^\\d*(\\.\\d?)?$");
        const QString t = number(input);
        if (!re.match(t).hasMatch()) return QValidator::Invalid;
        if (t.isEmpty() || t == ".") return QValidator::Intermediate;
        const int v = qRound(t.toDouble() * kFrameUnit);
        return v >= minimum() && v <= maximum() ? QValidator::Acceptable : QValidator::Intermediate;
    }
};

class TrimSpin : public QDoubleSpinBox {
protected:
    QString textFromValue(double v) const override {
        QString t = QDoubleSpinBox::textFromValue(v);
        const QString dp = locale().decimalPoint();
        if (property("aqTrim").toBool() && decimals() > 0 && t.contains(dp)) {
            while (t.endsWith('0')) t.chop(1);
            if (t.endsWith(dp)) t.chop(dp.size());
        }
        return t;
    }
};
}

void setFrameTenths(QSpinBox* s, bool on) {
    auto* m = dynamic_cast<MsSpin*>(s);
    if (!m || m->tenths == on) return;
    m->tenths = on;
    m->setSingleStep(on ? kFrameUnit / 2 : 1);
    m->setSuffix(m->suffix());                 // текст поля — заново, в новых единицах
}

QDoubleSpinBox* trimSpin() { return new TrimSpin(); }

QSpinBox* msSpin(int lo, int hi) {
    auto* s = new MsSpin();
    s->setRange(lo, hi);
    s->setSuffix(T(" мс"));
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(Qt::AlignCenter);
    s->setAccelerated(true);
    return s;
}

void addPopupSearch(QBoxLayout* lay, QWidget* popup, QLineEdit* search, const QString& placeholder) {
    search->setObjectName("search");
    search->setPlaceholderText(placeholder);
    search->setClearButtonEnabled(true);
    search->addAction(AqIcons::ui("search", Theme::t().muted, 14), QLineEdit::LeadingPosition);
    auto* top = new QHBoxLayout();
    top->setSpacing(4);
    top->addWidget(search, 1);
    auto* x = new QToolButton();
    x->setObjectName("popupX");
    x->setFixedSize(26, 26);
    x->setIcon(AqIcons::ui("x", Theme::t().muted, 12, 2.4));
    x->setToolTip(T("Закрыть"));
    x->setAccessibleName(T("Закрыть"));
    x->setCursor(Qt::PointingHandCursor);
    QObject::connect(x, &QToolButton::clicked, popup, &QWidget::close);
    top->addWidget(x);
    lay->addLayout(top);
}

QString askText(QWidget* parent, const QString& title, const QString& label, const QString& value, bool* ok) {
    QInputDialog d(parent);
    d.setWindowTitle(title);
    d.setLabelText(label);
    d.setTextValue(value);
    d.setOkButtonText(T("Готово"));
    d.setCancelButtonText(T("Отмена"));
    const bool accepted = d.exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? d.textValue() : QString();
}

// ---------------------------------------------------------------------------
namespace {
// поле, которое колёсико меняет (полоса прокрутки — не поле: она листает)
bool wheelField(QObject* o) {
    return qobject_cast<QAbstractSpinBox*>(o) || qobject_cast<QComboBox*>(o) ||
           (qobject_cast<QAbstractSlider*>(o) && !qobject_cast<QScrollBar*>(o));
}

bool inScrollArea(const QWidget* w) {
    for (const QWidget* p = w->parentWidget(); p && !p->isWindow(); p = p->parentWidget())
        if (qobject_cast<const QAbstractScrollArea*>(p)) return true;
    return false;
}

// курсор числа — между приставкой и подписью («50| мс», а не «50 мс|»)
void keepCursorInNumber(QAbstractSpinBox* sb) {
    if (sb->property("aqCursorGuard").toBool()) return;
    auto* le = sb->findChild<QLineEdit*>();
    if (!le) return;
    sb->setProperty("aqCursorGuard", true);
    QObject::connect(le, &QLineEdit::cursorPositionChanged, sb, [sb, le](int, int) {
        if (le->hasSelectedText()) return;
        QString pre, suf;
        if (auto* s = qobject_cast<QSpinBox*>(sb)) { pre = s->prefix(); suf = s->suffix(); }
        else if (auto* d = qobject_cast<QDoubleSpinBox*>(sb)) { pre = d->prefix(); suf = d->suffix(); }
        if (pre.isEmpty() && suf.isEmpty()) return;
        const QString text = le->text();
        const int lo = !pre.isEmpty() && text.startsWith(pre) ? int(pre.size()) : 0;
        const int hi = !suf.isEmpty() && text.endsWith(suf) ? int(text.size() - suf.size()) : int(text.size());
        const int pos = le->cursorPosition();
        if (lo <= hi && (pos < lo || pos > hi)) {
            const QSignalBlocker b(le);
            le->setCursorPosition(qBound(lo, pos, hi));
        }
    });
}
}

void InputGuard::install() {
    if (!qApp) return;
    qApp->installEventFilter(new InputGuard(qApp));
}

bool InputGuard::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Polish && wheelField(o)) {
        auto* w = static_cast<QWidget*>(o);
        // фокус от колёсика: иначе первый же оборот колёсика над полем «выбирал» бы его
        if (w->focusPolicy() == Qt::WheelFocus) w->setFocusPolicy(Qt::StrongFocus);
        if (auto* sb = qobject_cast<QAbstractSpinBox*>(o)) {
            keepCursorInNumber(sb);
            // правый щелчок по числу — не меню правки текста («Вырезать», «Шаг вверх»…),
            // а меню того, что вокруг (шаг, карточка)
            if (sb->contextMenuPolicy() == Qt::DefaultContextMenu) sb->setContextMenuPolicy(Qt::NoContextMenu);
        }
    } else if (e->type() == QEvent::Wheel) {
        static QElapsedTimer clock;
        if (!clock.isValid()) clock.start();
        const qint64 now = clock.elapsed();
        if (!wheelField(o)) {
            // колёсико дошло до прокрутки — страницу листают
            if (auto* area = qobject_cast<QAbstractScrollArea*>(o->parent()))
                if (area->viewport() == o) lastPageWheel_ = now;
            if (qobject_cast<QScrollBar*>(o)) lastPageWheel_ = now;
            return QObject::eventFilter(o, e);
        }
        auto* w = static_cast<QWidget*>(o);
        if (!inScrollArea(w)) return QObject::eventFilter(o, e);    // листать нечего (мини-окно) — как раньше
        if (w->hasFocus() && now - lastPageWheel_ > 600) return QObject::eventFilter(o, e);
        // мимо выбранного поля — событие идёт дальше вверх, до прокрутки страницы
        e->ignore();
        return true;
    }
    return QObject::eventFilter(o, e);
}

// ---------------------------------------------------------------------------
ChoiceButton::ChoiceButton(QWidget* parent) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
}

void ChoiceButton::setChoice(const QString& icon, const QString& text) {
    icon_ = icon;
    setText(text);
    setAccessibleName(text);
    updateGeometry();
    update();
}

static QFont choiceFont(int px, QFont::Weight weight) {
    QFont f(Theme::uiFamily());
    f.setPixelSize(px);
    f.setWeight(weight);
    return f;
}

QSize ChoiceButton::sizeHint() const {
    const QFontMetrics fm(choiceFont(11, QFont::DemiBold));
    return QSize(6 + 13 + 4 + fm.horizontalAdvance(text()) + 3 + 10 + 5, qMax(22, fm.height() + 8));
}

void ChoiceButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& t = Theme::t();
    const bool hot = underMouse() || hasFocus() || open_ || isDown();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    // вид — редактор интерфейса («Кнопка режима»): с подложкой или с рамкой всегда
    const Theme::ElementStyle st = Theme::element("mode");
    const QColor c = Theme::elementColor("mode", t.red);
    const qreal rr = Theme::elementRadius("mode", 7);
    if (hot || st.look) {
        QColor fill = t.keycap;
        if (st.look == 1) fill = Theme::mix(c, t.field, 0.16);
        else if (st.look == 2 && !hot) fill = Qt::transparent;
        QColor pen = open_ || hasFocus() ? c : st.look ? c : t.fieldBorder2;
        if (st.look == 1 && !(open_ || hasFocus() || hot)) pen = Qt::transparent;
        p.setPen(pen.alpha() ? QPen(pen, 1) : QPen(Qt::NoPen));
        p.setBrush(fill);
        p.drawRoundedRect(r, rr, rr);
    }
    const qreal cy = height() / 2.0;
    AqIcons::paint(p, icon_, QRectF(6, cy - 6.5, 13, 13), st.color.isEmpty() ? t.redHi : Theme::textColor(c), 2.3);
    p.setFont(choiceFont(11, QFont::DemiBold));
    p.setPen(hot ? t.text : t.text2);
    const int tx = 6 + 13 + 4;
    p.drawText(QRect(tx, 0, width() - tx - 15, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    AqIcons::paint(p, open_ ? "chevron-up" : "chevron", QRectF(width() - 15, cy - 5, 10, 10), t.muted, 2.6);
}

// ---------------------------------------------------------------------------
namespace {
// пункт плашки: значок в плитке, название, пояснение; текущий — с галочкой
class ChoiceRow : public QAbstractButton {
public:
    ChoiceRow(const Choice& c, bool current, QWidget* parent) : QAbstractButton(parent), c_(c), current_(current) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
        setText(c.title);
        setAccessibleName(c.title);
        setAccessibleDescription(c.text);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override {
        const QFontMetrics tf(choiceFont(13, QFont::DemiBold)), df(choiceFont(11, QFont::Normal));
        const int dh = c_.text.isEmpty() ? 0
                     : df.boundingRect(QRect(0, 0, qMax(40, w - kText - 30), 1000), Qt::TextWordWrap, c_.text).height();
        return qMax(52, 9 + tf.height() + (dh ? 1 + dh : 0) + 9);
    }
    QSize sizeHint() const override { return QSize(256, heightForWidth(256)); }
protected:
    static constexpr int kText = 54;
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& t = Theme::t();
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool hot = underMouse() || isDown();
        // свой цвет плашки (редактор интерфейса → «Плашка выбора») — у выбранного пункта
        const bool own = !Theme::element("plate").color.isEmpty();
        const QColor c = Theme::elementColor("plate", t.red);
        const QColor tint = own ? Theme::mix(c, t.card, 0.15) : t.redTint;
        if (current_ || hot || hasFocus()) {
            p.setPen(hasFocus() ? QPen(c, 1) : Qt::NoPen);
            p.setBrush(current_ ? tint : t.field);
            p.drawRoundedRect(r, 10, 10);
        }
        const QRectF tile(10, 9, 34, 34);
        p.setPen(current_ ? Qt::NoPen : QPen(t.fieldBorder2, 1));
        p.setBrush(current_ ? c : t.keycap);
        p.drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);
        AqIcons::paint(p, c_.icon, tile.adjusted(8, 8, -8, -8), current_ ? (own ? Theme::onColor(c) : t.onRed) : t.text2, 2.3);
        const QFont tf = choiceFont(13, QFont::DemiBold), df = choiceFont(11, QFont::Normal);
        const QFontMetrics tm(tf);
        const int right = width() - (current_ ? 30 : 12);
        p.setFont(tf);
        p.setPen(t.text);
        p.drawText(QRect(kText, 9, right - kText, tm.height()), Qt::AlignLeft | Qt::AlignVCenter, c_.title);
        if (!c_.text.isEmpty()) {
            p.setFont(df);
            p.setPen(t.muted);
            p.drawText(QRect(kText, 9 + tm.height() + 1, width() - kText - 30, height()), Qt::TextWordWrap, c_.text);
        }
        if (current_) AqIcons::paint(p, "check", QRectF(width() - 25, 9 + tm.height() / 2.0 - 7, 14, 14), own ? Theme::textColor(c) : t.redHi, 2.6);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { click(); return; }
        if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down || e->key() == Qt::Key_Escape) { e->ignore(); return; }
        QAbstractButton::keyPressEvent(e);
    }
private:
    Choice c_;
    bool current_;
};

constexpr int kPlateShadow = 14;        // поле вокруг плашки под тень
}

ChoicePlate* ChoicePlate::open(QWidget* anchor, const QString& title, const QList<Choice>& items, int current,
                               const std::function<void(int)>& picked) {
    if (!anchor || !anchor->window() || items.isEmpty()) return nullptr;
    return new ChoicePlate(anchor, title, items, current, picked);
}

ChoicePlate::ChoicePlate(QWidget* anchor, const QString& title, const QList<Choice>& items, int current,
                         const std::function<void(int)>& picked)
    : QWidget(anchor->window()), anchor_(anchor), picked_(picked) {
    setAttribute(Qt::WA_DeleteOnClose);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPlateShadow, kPlateShadow - 4, kPlateShadow, kPlateShadow + 4);
    auto* box = new QFrame();
    box->setObjectName("choicePlate");
    box->setAttribute(Qt::WA_StyledBackground);
    auto* shadow = new QGraphicsDropShadowEffect(box);
    shadow->setBlurRadius(26);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, Theme::isDark() ? 140 : 60));
    box->setGraphicsEffect(shadow);
    outer->addWidget(box);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(2);
    if (!title.isEmpty()) {
        auto* head = new QLabel(title.toUpper());
        head->setObjectName("plateTitle");
        head->setTextFormat(Qt::PlainText);
        lay->addWidget(head);
    }
    for (int i = 0; i < items.size(); ++i) {
        auto* row = new ChoiceRow(items[i], i == current, box);
        connect(row, &QAbstractButton::clicked, this, [this, i]() {
            if (done_) return;
            const auto cb = picked_;
            dismiss();
            if (cb) cb(i);
        });
        lay->addWidget(row);
        rows_ << row;
    }
    setFixedWidth(288 + 2 * kPlateShadow);
    if (auto* cb = qobject_cast<ChoiceButton*>(anchor)) cb->setOpen(true);
    connect(anchor, &QObject::destroyed, this, [this]() { dismiss(); });     // ряд пересобрали
    qApp->installEventFilter(this);
    place();
    show();
    raise();
    Anim::appear(this);
    if (QAbstractButton* f = rows_.value(qBound(0, current, int(rows_.size()) - 1))) f->setFocus(Qt::PopupFocusReason);
}

ChoicePlate::~ChoicePlate() {
    if (qApp) qApp->removeEventFilter(this);
    if (auto* cb = qobject_cast<ChoiceButton*>(anchor_.data())) cb->setOpen(false);
}

void ChoicePlate::place() {
    QWidget* win = parentWidget();
    if (!win || !anchor_) return;
    const int h = heightForWidth(width()) > 0 ? heightForWidth(width()) : sizeHint().height();
    resize(width(), h);
    const QPoint a = anchor_->mapTo(win, QPoint(0, 0));
    int x = a.x() - kPlateShadow - 2;
    int y = a.y() + anchor_->height() + 2 - (kPlateShadow - 4);
    if (y + h - kPlateShadow > win->height() - 6) y = a.y() - h + kPlateShadow;     // снизу не влезает — над кнопкой
    x = qBound(-kPlateShadow + 6, x, win->width() - width() + kPlateShadow - 6);
    y = qMax(-kPlateShadow + 6, y);
    move(x, y);
}

void ChoicePlate::dismiss() {
    if (done_) return;
    done_ = true;
    const bool hadFocus = isAncestorOf(QApplication::focusWidget());
    hide();
    if (hadFocus && anchor_ && anchor_->isVisible()) anchor_->setFocus(Qt::PopupFocusReason);
    close();
}

void ChoicePlate::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { dismiss(); return; }
    if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
        int i = 0;
        for (int k = 0; k < rows_.size(); ++k) if (rows_[k]->hasFocus()) i = k;
        i = (i + (e->key() == Qt::Key_Down ? 1 : int(rows_.size()) - 1)) % int(rows_.size());
        rows_[i]->setFocus(Qt::TabFocusReason);
        return;
    }
    QWidget::keyPressEvent(e);
}

bool ChoicePlate::eventFilter(QObject* o, QEvent* e) {
    if (done_) return false;
    switch (e->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            auto* w = qobject_cast<QWidget*>(o);
            if (!w || w == this || isAncestorOf(w)) break;
            // по самой кнопке — только закрыть (иначе она тут же открыла бы плашку снова)
            const bool onAnchor = anchor_ && (w == anchor_ || anchor_->isAncestorOf(w));
            dismiss();
            return onAnchor;
        }
        case QEvent::Wheel: {
            auto* w = qobject_cast<QWidget*>(o);
            if (w && w != this && !isAncestorOf(w)) dismiss();
            break;
        }
        case QEvent::KeyPress:
            if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) { dismiss(); return true; }
            break;
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Hide:
        case QEvent::WindowDeactivate:
            // окно или кнопка сдвинулись / пропали — плашка висела бы не там
            if (o == parentWidget() || o == anchor_.data()) dismiss();
            break;
        default:
            break;
    }
    return false;
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
