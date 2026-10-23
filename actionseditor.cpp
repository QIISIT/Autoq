#include "actionseditor.h"
#include "i18n.h"
#include "widgets.h"
#include "keypicker.h"
#include "icons.h"
#include "theme.h"
#include <QApplication>
#include <QSpinBox>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QTimer>
#include <QMenu>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include "animations.h"

namespace {
const char* kStepMime = "application/x-autoq-step";

// Кнопка постоянного размера: min-height из стиля (QSS) её не сжимает
class SizedButton : public QPushButton {
public:
    explicit SizedButton(const QSize& s) : size_(s) { setFixedSize(s); }
    void setSize(const QSize& s) { size_ = s; setFixedSize(s); }
    QSize sizeHint() const override { return size_; }
    QSize minimumSizeHint() const override { return size_; }
private:
    QSize size_;
};

// размеры шагов и клавиш (редактор → вид элементов): пересобрать ряд, если сменились
QString lookKey() { return QString("%1/%2").arg(Theme::element("slot").size).arg(Theme::element("key").size); }

// «повтор»: стрелка, которая возвращается к началу цепочки
class LoopLine : public QWidget {
public:
    LoopLine() { setFixedSize(92, 14); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QString pc = Theme::element("pause").color;
        const QColor c = pc.isEmpty() ? Theme::t().redHi : Theme::textColor(QColor(pc));
        p.setPen(QPen(c, 2, Qt::DashLine, Qt::RoundCap));
        const qreal y = height() / 2.0;
        p.drawLine(QPointF(2, y), QPointF(width() - 22, y));
        AqIcons::paint(p, "loop", QRectF(width() - 17, y - 7, 14, 14), c, 2.2);
    }
};

QSpinBox* smallSpin(int lo, int hi, const QString& tip) {
    auto* s = new QSpinBox();
    s->setRange(lo, hi);
    s->setSuffix(T(" мс"));
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(Qt::AlignCenter);
    s->setAccelerated(true);
    s->setToolTip(tip);
    s->setAccessibleName(tip);
    return s;
}

// подпись сверху, число, стрелка снизу — пауза между карточками, «старт через», «повтор через»
QWidget* chip(const QString& caption, QSpinBox* spin, QWidget* arrow) {
    auto* w = new QWidget();
    w->setProperty("aqEl", "pause");    // вид — редактор интерфейса, «Плашки пауз»
    w->setFixedWidth(92);               // как стрелка: ширина ряда не скачет от подписи
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    auto* l = new QLabel(caption);
    l->setObjectName("small");
    l->setAlignment(Qt::AlignCenter);
    v->addWidget(l);
    spin->setFixedWidth(84);
    v->addWidget(spin, 0, Qt::AlignHCenter);
    v->addWidget(arrow, 0, Qt::AlignHCenter);
    return w;
}
}

ActionsEditor::ActionsEditor(QWidget* parent) : QWidget(parent) {
    flow_ = new FlowLayout(this, 10, 10);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setAcceptDrops(true);
    dropMark_ = new QWidget(this);
    dropMark_->setObjectName("dropMarker");
    dropMark_->setAttribute(Qt::WA_StyledBackground);
    dropMark_->setAttribute(Qt::WA_TransparentForMouseEvents);
    dropMark_->hide();
}

int ActionsEditor::heightForWidth(int w) const { return flow_->heightForWidth(w); }
QSize ActionsEditor::sizeHint() const { return QSize(640, flow_->heightForWidth(640)); }
QSize ActionsEditor::minimumSizeHint() const { return QSize(200, flow_->heightForWidth(qMax(width(), 200))); }

void ActionsEditor::setState(const QList<ActionStep>& list, GameMode mode, int startMs, int intervalMs, int minIntervalMs) {
    mode_ = mode;
    actions_ = list;
    if (actions_.isEmpty()) {
        ActionStep a;
        if (steps()) a.press = ActionStep::Down;
        actions_ << a;
    }
    while (actions_.size() > maxItems()) actions_.removeLast();
    startMs_ = qBound(0, startMs, kMaxStartDelayMs);
    minIntervalMs_ = qMax(1, minIntervalMs);
    intervalMs_ = qBound(repeatMin(), intervalMs, 60000);
    if (builtCount_ != actions_.size() || builtMode_ != mode_) {
        rebuild();
    } else {
        // ряд не пересобирается, а минимум интервала у другой игры (или после правки игры) другой
        if (repeatSpin_ && repeatSpin_->minimum() != repeatMin()) {
            QSignalBlocker b(repeatSpin_);
            repeatSpin_->setMinimum(repeatMin());
        }
        updateValues();
    }
}

int ActionsEditor::repeatMin() const { return repeatMinMs(mode_, int(actions_.size()), minIntervalMs_); }

void ActionsEditor::setTimes(int startMs, int intervalMs) {
    startMs_ = qBound(0, startMs, kMaxStartDelayMs);
    intervalMs_ = qBound(repeatMin(), intervalMs, 60000);
    if (startSpin_ && startSpin_->value() != startMs_) { QSignalBlocker b(startSpin_); startSpin_->setValue(startMs_); }
    if (repeatSpin_ && repeatSpin_->value() != intervalMs_) { QSignalBlocker b(repeatSpin_); repeatSpin_->setValue(intervalMs_); }
}

void ActionsEditor::edited() {
    if (loading_) return;
    emit changed();
}

// Правки, после которых ряд собирается заново, — не из обработчика кнопки внутри
// ряда (кнопка удалится), а сразу после. Пока правка ждёт — вторую не берём:
// двойной щелчок по «убрать» не должен убрать два действия.
void ActionsEditor::later(const std::function<void()>& f) {
    if (busy_) return;
    busy_ = true;
    const int gen = buildGen_;
    QTimer::singleShot(0, this, [this, f, gen]() {
        busy_ = false;
        if (gen == buildGen_) f();      // ряд за это время пересобрали (сменили игру) — правка устарела
    });
}

void ActionsEditor::rebuild() {
    stopAnimations();
    flow_->clear();
    slots_.clear();
    units_.clear();
    add_ = nullptr;
    startUnit_ = nullptr;
    startSpin_ = repeatSpin_ = nullptr;
    dropMark_->hide();
    ++buildGen_;
    builtCount_ = actions_.size();
    builtMode_ = mode_;
    active_ = -1;
    const int n = actions_.size();
    const bool hold = mode_ == GameMode::Hold;
    const bool paused = mode_ == GameMode::Sequence || steps();     // между карточками — пауза
    // у действия рядом с клавишей — «держать»; «Уже» / «Шире» — вид шагов в редакторе
    const int slotSize = Theme::element("slot").size;
    const int slotW = (steps() || hold ? 176 : 206) + (slotSize == 0 ? -20 : slotSize == 2 ? 36 : 0);
    builtLook_ = lookKey();

    // старт через N мс
    startSpin_ = smallSpin(0, kMaxStartDelayMs, T("Старт через столько мс после нажатия «Старт» или клавиши (0 — сразу)"));
    startUnit_ = chip(T("старт через"), startSpin_, new ArrowLine());
    flow_->addWidget(startUnit_);
    connect(startSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (loading_) return;
        startMs_ = v;
        edited();
    });

    for (int i = 0; i < n; ++i) {
        auto* unit = new QWidget();
        auto* uh = new QHBoxLayout(unit);
        uh->setContentsMargins(0, 0, 0, 0);
        uh->setSpacing(10);

        SlotUi ui;
        auto* f = new QFrame();
        ui.frame = f;
        f->setObjectName("slot");
        f->setProperty("aqEl", "slot");
        f->setProperty("slotIndex", i);
        f->setFixedSize(slotW, slotH_);
        f->setContextMenuPolicy(Qt::CustomContextMenu);
        f->installEventFilter(this);
        auto* g = new QGridLayout(f);
        g->setContentsMargins(8, 8, 8, 10);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(4);
        ui.grip = new QToolButton();
        ui.grip->setObjectName("slotGrip");
        ui.grip->setFixedSize(14, 20);
        ui.grip->setCursor(Qt::OpenHandCursor);
        ui.grip->setToolTip(steps() ? T("Перетащи шаг на другое место") : T("Перетащи действие на другое место"));
        ui.grip->setProperty("slotIndex", i);
        ui.grip->installEventFilter(this);
        g->addWidget(ui.grip, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
        if (hold) {
            ui.caption = new QLabel(T("зажать"));
            ui.caption->setObjectName("small");
            ui.caption->setToolTip(T("Пока кликер включён — зажата"));
            g->addWidget(ui.caption, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
        } else {
            // режим действия — над клавишей; щелчок открывает плашку с режимами
            ui.mode = new ChoiceButton();
            ui.mode->setProperty("aqEl", "mode");
            ui.mode->setProperty("aqElMore", "plate");     // плашка, которую она открывает
            ui.mode->setToolTip(steps() ? T("Шаг %1: нажать, отпустить или клик — щёлкни, чтобы выбрать").arg(i + 1)
                                        : T("Действие %1: клик или держать нажатым — щёлкни, чтобы выбрать").arg(i + 1));
            g->addWidget(ui.mode, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
        }
        if (!hold && !steps()) {
            // подпись над полем «держать» — чтобы не спутать с паузой
            auto* hc = new QLabel(T("держать"));
            hc->setObjectName("small");
            hc->setToolTip(T("Сколько держать нажатым (0 — клик: нажать и сразу отпустить)"));
            g->addWidget(hc, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
        }
        if (n > 1) {
            ui.remove = new QToolButton();
            ui.remove->setObjectName("icon");
            ui.remove->setFixedSize(22, 22);
            ui.remove->setToolTip(steps() ? T("Убрать шаг %1").arg(i + 1) : T("Убрать действие %1").arg(i + 1));
            ui.remove->setAccessibleName(ui.remove->toolTip());
            ui.remove->setCursor(Qt::PointingHandCursor);
            g->addWidget(ui.remove, 0, 3, Qt::AlignRight | Qt::AlignTop);
        }
        g->setColumnStretch(1, 1);
        auto* vr = new QHBoxLayout();
        vr->setSpacing(6);
        ui.value = new KeyPicker();
        ui.value->setObjectName("keyPickBig");
        ui.value->setProperty("aqEl", "key");
        ui.value->setKeys(KeyPicker::actionItems(false), KeyPicker::actionCommon());
        ui.value->setToolTip(T("Кнопка мыши или клавиша — список с поиском"));
        ui.value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        vr->addWidget(ui.value, 1);
        if (steps()) {
            ui.dir = new QToolButton();
            ui.dir->setObjectName("stepDir");
            ui.dir->setProperty("aqEl", "key");
            ui.dir->setFixedSize(36, 36);
            ui.dir->setCursor(Qt::PointingHandCursor);
            vr->addWidget(ui.dir);
        } else if (!hold) {
            // «держать N мс» — всегда рядом с клавишей: 0 — клик, больше — «держать»
            ui.hold = smallSpin(0, 10000, T("Сколько держать нажатым (0 — клик: нажать и сразу отпустить)"));
            ui.hold->setFixedWidth(64);
            vr->addWidget(ui.hold);
        }
        g->addLayout(vr, 1, 0, 1, 4);
        g->setRowStretch(2, 1);
        uh->addWidget(f);

        if (i + 1 < n) {
            QWidget* conn;
            if (paused) {
                ui.after = smallSpin(0, 60000, steps()
                    ? T("Пауза после шага %1 перед шагом %2 (щёлкни по полю — и колёсиком быстро менять)").arg(i + 1).arg(i + 2)
                    : T("Пауза после действия %1 перед действием %2 (щёлкни по полю — и колёсиком быстро менять)").arg(i + 1).arg(i + 2));
                conn = chip(T("пауза"), ui.after, new ArrowLine());
            } else {
                conn = new QWidget();
                auto* cv = new QVBoxLayout(conn);
                cv->setContentsMargins(0, 0, 0, 0);
                cv->setSpacing(3);
                cv->addWidget(new PlusBadge(), 0, Qt::AlignHCenter);
                auto* l = new QLabel(hold ? T("держать вместе") : T("одновременно"));
                l->setObjectName("small");
                cv->addWidget(l, 0, Qt::AlignHCenter);
                conn->setFixedWidth(92);
            }
            uh->addWidget(conn, 0, Qt::AlignVCenter);
        } else if (!hold) {
            // после последнего — «повтор через»: тот же интервал, что в «Скорости»; между
            // клавишами («По шагам», несколько действий по очереди) — от 0
            intervalMs_ = qMax(intervalMs_, repeatMin());
            repeatSpin_ = smallSpin(repeatMin(), 60000, repeatMin() == 0
                ? T("Пауза после последнего — и всё сначала. Можно 0: круг целиком всё равно не быстрее %1 мс. "
                    "Это интервал из карточки «Скорость»").arg(minIntervalMs_)
                : T("Пауза после последнего — и всё сначала. Это интервал из карточки «Скорость»"));
            uh->addWidget(chip(T("повтор через"), repeatSpin_, new LoopLine()), 0, Qt::AlignVCenter);
            connect(repeatSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
                if (loading_) return;
                intervalMs_ = qMax(repeatMin(), v);
                edited();
            });
        }
        flow_->addWidget(unit);
        slots_ << ui;
        units_ << unit;

        connect(f, &QWidget::customContextMenuRequested, this, [this, f, i](const QPoint& pos) {
            showMenu(i, f->mapToGlobal(pos));
        });
        if (ui.mode)
            connect(ui.mode, &QAbstractButton::clicked, this, [this, i]() { openModePlate(i); });
        connect(ui.value, &KeyPicker::picked, this, [this, i](const QString& name) {
            if (i >= actions_.size()) return;
            ActionStep& a = actions_[i];
            const ActionStep p = ActionStep::fromItem(name);
            a.mouse = p.mouse;
            if (p.mouse) a.button = p.button; else a.key = p.key;
            updateValues();
            edited();
        });
        if (ui.dir)
            connect(ui.dir, &QToolButton::clicked, this, [this, i]() {
                if (loading_ || i >= actions_.size()) return;
                ActionStep& a = actions_[i];
                // ↓ ⇄ ↑; клик (нажать и отпустить) — становится «нажать»
                a.press = a.press == ActionStep::Down ? ActionStep::Up : ActionStep::Down;
                updateValues();
                edited();
            });
        if (ui.hold)
            connect(ui.hold, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
                if (loading_ || i >= actions_.size()) return;
                actions_[i].holdMs = v;
                if (i < slots_.size()) updateMode(slots_[i], actions_[i]);    // клик ⇄ держать
                edited();
            });
        if (ui.after)
            connect(ui.after, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
                if (loading_ || i >= actions_.size()) return;
                actions_[i].afterMs = v;
                edited();
            });
        if (ui.remove)
            connect(ui.remove, &QToolButton::clicked, this, [this, i]() { later([this, i]() { removeAt(i); }); });
    }
    if (n < maxItems()) {
        add_ = new SizedButton(QSize(n == 1 ? slotW : 132, slotH_));
        add_->setObjectName("addSecond");
        add_->setProperty("aqEl", "add");
        add_->setCursor(Qt::PointingHandCursor);
        add_->setText(steps() ? T("Шаг") : n == 1 ? (mode_ == GameMode::Sequence ? T("Действие 2") : T("вместе с…")) : T("Ещё"));
        add_->setToolTip(steps() ? T("Добавить шаг: нажать или отпустить кнопку / клавишу")
                         : mode_ == GameMode::Sequence ? T("Добавить действие — по очереди после остальных")
                                                       : T("Добавить клавишу или кнопку — вместе с остальными"));
        add_->setAccessibleName(steps() ? T("Добавить шаг") : T("Добавить действие"));
        connect(add_, &QPushButton::clicked, this, [this]() {
            later([this]() { insertAt(actions_.size(), newItem()); });
        });
        flow_->addWidget(add_);
    }
    // сразу видны: addWidget показывает их только на следующем круге событий,
    // и переход при смене игры снял бы пустой ряд
    for (int i = 0; i < flow_->count(); ++i)
        if (QWidget* w = flow_->itemAt(i)->widget()) w->show();
    refreshIcons();
    updateValues();
    // высота карточек — по содержимому (крупный шрифт или масштаб — ничего не наезжает),
    // у всех одна: ряд ровный
    int h = 88;
    for (const SlotUi& ui : slots_) {
        ui.frame->ensurePolished();
        h = qMax(h, ui.frame->layout()->totalMinimumSize().height());
    }
    if (h != slotH_) {
        slotH_ = h;
        for (const SlotUi& ui : slots_) ui.frame->setFixedHeight(h);
        if (add_) static_cast<SizedButton*>(add_)->setSize(QSize(add_->width(), h));
    }
    updateGeometry();
}

ActionStep ActionsEditor::newItem() const {
    ActionStep a;
    if (steps()) {
        // что-то осталось нажатым — следующий шаг его отпускает
        const QList<ActionStep> left = Steps::leftDown(actions_);
        if (!left.isEmpty()) {
            a = left.last();
            a.press = ActionStep::Up;
            a.holdMs = a.afterMs = 0;
            return a;
        }
        const QList<ActionStep> used = Steps::pressedInputs(actions_);
        const QStringList pool = {"mouse:1", "mouse:3", "f", "e", "space", "q", "r"};
        a = ActionStep::fromItem(pool.first());
        for (const QString& k : pool) {
            const ActionStep c = ActionStep::fromItem(k);
            bool taken = false;
            for (const ActionStep& u : used) taken = taken || u.sameInput(c);
            if (!taken) { a = c; break; }
        }
        a.press = ActionStep::Down;
        return a;
    }
    a.mouse = false;
    // новая клавиша — не такая же, как уже есть
    const QStringList pool = {"f", "e", "space", "q", "r", "1", "2", "3"};
    for (const QString& k : pool) {
        bool used = false;
        for (const ActionStep& x : actions_) used = used || (!x.mouse && x.key == k);
        if (!used) { a.key = k; break; }
    }
    return a;
}

void ActionsEditor::applyEdit(const QList<ActionStep>& next, const QList<int>& oldOf, int removedOld) {
    const Snapshot before = snapshot();
    const QPixmap pm = (removedOld >= 0 && units_.value(removedOld)) ? units_[removedOld]->grab() : QPixmap();
    actions_ = next;
    setUpdatesEnabled(false);           // без промежуточного кадра «всё в куче»
    rebuild();
    animateFrom(before, oldOf, removedOld, pm);
    setUpdatesEnabled(true);
    edited();
}

void ActionsEditor::insertAt(int at, const ActionStep& a) {
    if (actions_.size() >= maxItems()) return;
    at = qBound(0, at, int(actions_.size()));
    QList<ActionStep> next = actions_;
    next.insert(at, a);
    QList<int> oldOf;
    for (int j = 0; j < next.size(); ++j) oldOf << (j < at ? j : j == at ? -1 : j - 1);
    applyEdit(next, oldOf, -1);
}

void ActionsEditor::removeAt(int i) {
    if (actions_.size() <= 1 || i < 0 || i >= actions_.size()) return;
    QList<ActionStep> next = actions_;
    next.removeAt(i);
    QList<int> oldOf;
    for (int j = 0; j < next.size(); ++j) oldOf << (j < i ? j : j + 1);
    applyEdit(next, oldOf, i);
}

// to — место среди карточек до того, как карточку вынули (0…n)
void ActionsEditor::moveTo(int from, int to) {
    const int n = actions_.size();
    if (from < 0 || from >= n) return;
    to = qBound(0, to, n);
    if (to > from) --to;
    if (to == from) return;
    QList<ActionStep> next = actions_;
    QList<int> oldOf;
    for (int j = 0; j < n; ++j) oldOf << j;
    next.move(from, to);
    oldOf.move(from, to);
    applyEdit(next, oldOf, -1);
}

void ActionsEditor::showMenu(int i, const QPoint& global) {
    if (i < 0 || i >= actions_.size()) return;
    const int n = actions_.size();
    const bool canAdd = n < maxItems();
    QMenu m(this);
    QAction* before = m.addAction(steps() ? T("Добавить шаг перед") : T("Добавить действие перед"));
    QAction* after = m.addAction(steps() ? T("Добавить шаг после") : T("Добавить действие после"));
    QAction* dup = m.addAction(T("Дублировать"));
    for (QAction* a : {before, after, dup}) a->setEnabled(canAdd);
    QAction* flip = nullptr;
    if (steps())
        flip = m.addAction(actions_[i].press == ActionStep::Down ? T("Сделать «отпустить» ↑") : T("Сделать «нажать» ↓"));
    m.addSeparator();
    QAction* left = m.addAction(T("Переместить влево"));
    QAction* right = m.addAction(T("Переместить вправо"));
    left->setEnabled(i > 0);
    right->setEnabled(i + 1 < n);
    m.addSeparator();
    QAction* del = m.addAction(steps() ? T("Убрать шаг") : T("Убрать действие"));
    del->setEnabled(n > 1);
    QAction* got = m.exec(global);
    if (!got) return;
    if (got == flip) {
        ActionStep& a = actions_[i];
        a.press = a.press == ActionStep::Down ? ActionStep::Up : ActionStep::Down;
        updateValues();
        edited();
        return;
    }
    // меню удалится вместе с пунктами — запоминаем, что выбрали, числом
    const int what = got == before ? 1 : got == after ? 2 : got == dup ? 3 : got == left ? 4 : got == right ? 5 : got == del ? 6 : 0;
    later([this, i, what]() {
        if (i >= actions_.size()) return;
        switch (what) {
            case 1: insertAt(i, newItem()); break;
            case 2: insertAt(i + 1, newItem()); break;
            case 3: { const ActionStep c = actions_[i]; insertAt(i + 1, c); break; }
            case 4: moveTo(i, i - 1); break;
            case 5: moveTo(i, i + 2); break;
            case 6: removeAt(i); break;
            default: break;
        }
    });
}

// ---------------------------------------------------------------------------
// Перетаскивание: за ручку (или пустое место карточки) — на новое место
// ---------------------------------------------------------------------------
bool ActionsEditor::eventFilter(QObject* o, QEvent* e) {
    auto* w = qobject_cast<QWidget*>(o);
    if (!w || !w->property("slotIndex").isValid()) return QWidget::eventFilter(o, e);
    if (e->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() != Qt::LeftButton) return QWidget::eventFilter(o, e);
        pressSlot_ = w->property("slotIndex").toInt();
        pressPos_ = me->globalPosition().toPoint();
        if (w->objectName() == "slotGrip") w->setCursor(Qt::ClosedHandCursor);
        return true;
    }
    if (e->type() == QEvent::MouseMove && pressSlot_ >= 0) {
        auto* me = static_cast<QMouseEvent*>(e);
        if ((me->globalPosition().toPoint() - pressPos_).manhattanLength() < QApplication::startDragDistance())
            return true;
        const int from = pressSlot_;
        pressSlot_ = -1;
        if (from >= slots_.size() || busy_) return true;
        QFrame* f = slots_[from].frame;
        auto* data = new QMimeData();
        data->setData(kStepMime, QByteArray::number(from));
        auto* drag = new QDrag(f);
        drag->setMimeData(data);
        QPixmap pm = f->grab();
        {
            QPixmap faded(pm.size());
            faded.setDevicePixelRatio(pm.devicePixelRatio());
            faded.fill(Qt::transparent);
            QPainter p(&faded);
            p.setOpacity(0.85);
            p.drawPixmap(0, 0, pm);
            pm = faded;
        }
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(16, 16));
        dragFrom_ = from;
        drag->exec(Qt::MoveAction);
        dragFrom_ = -1;
        dropMark_->hide();
        if (QWidget* g = slots_.value(from).grip) g->setCursor(Qt::OpenHandCursor);
        return true;
    }
    if (e->type() == QEvent::MouseButtonRelease) {
        pressSlot_ = -1;
        if (w->objectName() == "slotGrip") w->setCursor(Qt::OpenHandCursor);
    }
    return QWidget::eventFilter(o, e);
}

int ActionsEditor::dropIndex(const QPoint& p) const {
    int best = -1;
    long long bestD = -1;
    QRect bestR;
    for (int j = 0; j < slots_.size(); ++j) {
        const QRect r(slots_[j].frame->mapTo(this, QPoint(0, 0)), slots_[j].frame->size());
        const long long dx = qMax(0, qMax(r.left() - p.x(), p.x() - r.right()));
        const long long dy = qMax(0, qMax(r.top() - p.y(), p.y() - r.bottom()));
        const long long d = dx * dx + dy * dy * 4;      // ряд важнее: сначала ближайший по высоте
        if (best < 0 || d < bestD) { best = j; bestD = d; bestR = r; }
    }
    if (best < 0) return -1;
    return p.x() < bestR.center().x() ? best : best + 1;
}

void ActionsEditor::dragEnterEvent(QDragEnterEvent* e) {
    if (dragFrom_ >= 0 && e->mimeData()->hasFormat(kStepMime)) e->acceptProposedAction();
    else e->ignore();
}

void ActionsEditor::dragMoveEvent(QDragMoveEvent* e) {
    if (dragFrom_ < 0) { e->ignore(); return; }
    const int idx = dropIndex(e->position().toPoint());
    dropAt_ = idx;
    if (idx < 0) { dropMark_->hide(); e->ignore(); return; }
    QRect r;
    if (idx < slots_.size()) {
        const QFrame* f = slots_[idx].frame;
        r = QRect(f->mapTo(this, QPoint(0, 0)) - QPoint(7, 0), QSize(4, f->height()));
    } else {
        const QFrame* f = slots_.last().frame;
        r = QRect(f->mapTo(this, QPoint(f->width() + 3, 0)), QSize(4, f->height()));
    }
    r.moveLeft(qBound(0, r.left(), qMax(0, width() - r.width())));
    dropMark_->setGeometry(r);
    dropMark_->show();
    dropMark_->raise();
    e->acceptProposedAction();
}

void ActionsEditor::dragLeaveEvent(QDragLeaveEvent*) { dropMark_->hide(); }

void ActionsEditor::dropEvent(QDropEvent* e) {
    dropMark_->hide();
    const int from = dragFrom_, to = dropIndex(e->position().toPoint());
    if (from < 0 || to < 0) { e->ignore(); return; }
    e->acceptProposedAction();
    // раскладку меняем после возврата из exec(): источник перетаскивания ещё жив
    later([this, from, to]() { moveTo(from, to); });
}

// ---------------------------------------------------------------------------
ActionsEditor::Snapshot ActionsEditor::snapshot() const {
    Snapshot sn;
    for (QWidget* u : units_) sn.units << u->geometry();
    if (add_) sn.add = add_->geometry();
    sn.height = height();
    return sn;
}

void ActionsEditor::stopAnimations() {
    if (heightAnim_) {
        heightAnim_->stop();
        heightAnim_->deleteLater();
        heightAnim_ = nullptr;
    }
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    flow_->setFrozen(false);
}

void ActionsEditor::animateFrom(const Snapshot& before, const QList<int>& oldOf, int removedOld, const QPixmap& removedPm) {
    if (!Anim::enabled() || !isVisible() || width() < 50) return;
    const int dur = Anim::ms(230);
    const int gen = ++animGen_;
    // новые виджеты Qt показывает только на следующем шаге — показываем сразу, чтобы
    // раскладка их учла (кадр «в куче» не виден: обновление окна выключено)
    for (QWidget* u : units_) if (u->isHidden()) u->show();
    if (add_ && add_->isHidden()) add_->show();
    if (startUnit_ && startUnit_->isHidden()) startUnit_->show();
    // новые места считаем сразу, не дожидаясь раскладки
    const int newH = heightForWidth(width());
    flow_->setGeometry(QRect(0, 0, width(), newH));
    flow_->setFrozen(true);
    // размораживает только последняя анимация: прошлая не должна отпустить раскладку посреди новой
    QTimer::singleShot(dur + 30, this, [this, gen]() { if (gen == animGen_) flow_->setFrozen(false); });
    auto move = [dur](QWidget* w, const QPoint& from) {
        const QPoint to = w->pos();
        if (from == to) return;
        w->move(from);
        auto* a = new QPropertyAnimation(w, "pos", w);
        a->setDuration(dur);
        a->setStartValue(from);
        a->setEndValue(to);
        a->setEasingCurve(QEasingCurve::OutCubic);
        a->start(QAbstractAnimation::DeleteWhenStopped);
    };
    for (int j = 0; j < units_.size(); ++j) {
        QWidget* u = units_[j];
        const int old = oldOf.value(j, -1);
        if (old < 0) {                          // новое — проявляется и чуть подъезжает
            auto* eff = new QGraphicsOpacityEffect(u);
            eff->setOpacity(0);
            u->setGraphicsEffect(eff);
            auto* f = new QPropertyAnimation(eff, "opacity", u);
            f->setDuration(dur);
            f->setStartValue(0.0);
            f->setEndValue(1.0);
            connect(f, &QPropertyAnimation::finished, u, [u]() { u->setGraphicsEffect(nullptr); });
            f->start(QAbstractAnimation::DeleteWhenStopped);
            // добавили в конец — из места кнопки «добавить», иначе — чуть снизу
            const bool appended = j == units_.size() - 1 && before.add.isValid();
            move(u, appended ? before.add.topLeft() : u->pos() + QPoint(0, 12));
            continue;
        }
        if (old < before.units.size()) move(u, before.units[old].topLeft());
    }
    if (add_ && before.add.isValid()) move(add_, before.add.topLeft());
    // убранное тает на своём месте
    if (removedOld >= 0 && removedOld < before.units.size() && !removedPm.isNull()) {
        auto* ghost = new QLabel(this);
        ghost->setPixmap(removedPm);
        ghost->setGeometry(before.units[removedOld]);
        ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* eff = new QGraphicsOpacityEffect(ghost);
        ghost->setGraphicsEffect(eff);
        ghost->show();
        ghost->raise();
        auto* f = new QPropertyAnimation(eff, "opacity", ghost);
        f->setDuration(dur * 3 / 4);
        f->setStartValue(1.0);
        f->setEndValue(0.0);
        auto* m = new QPropertyAnimation(ghost, "pos", ghost);
        m->setDuration(dur * 3 / 4);
        m->setStartValue(ghost->pos());
        m->setEndValue(ghost->pos() + QPoint(0, 10));
        auto* g = new QParallelAnimationGroup(ghost);
        g->addAnimation(f);
        g->addAnimation(m);
        connect(g, &QParallelAnimationGroup::finished, ghost, &QObject::deleteLater);
        g->start(QAbstractAnimation::DeleteWhenStopped);
    }
    // высота — постепенно (иначе карточки ниже прыгают)
    if (before.height > 0 && before.height != newH) {
        auto* ha = new QVariantAnimation(this);
        heightAnim_ = ha;
        ha->setDuration(dur);
        ha->setStartValue(before.height);
        ha->setEndValue(newH);
        ha->setEasingCurve(QEasingCurve::OutCubic);
        setFixedHeight(before.height);
        connect(ha, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { setFixedHeight(v.toInt()); });
        connect(ha, &QVariantAnimation::finished, this, [this, ha]() {
            if (heightAnim_ != ha) return;      // её уже сменила новая
            setMinimumHeight(0);
            setMaximumHeight(QWIDGETSIZE_MAX);
            updateGeometry();
            heightAnim_ = nullptr;
        });
        ha->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void ActionsEditor::updateDir(SlotUi& ui, const ActionStep& a) {
    if (!ui.dir) return;
    const auto& t = Theme::t();
    const bool up = a.press == ActionStep::Up, click = a.press == ActionStep::Click;
    ui.dir->setIcon(AqIcons::ui(up ? "arrow-up" : click ? "press-tap" : "arrow-down", up ? t.text : t.redHi, 20, 2.4));
    ui.dir->setIconSize(QSize(20, 20));
    ui.dir->setProperty("up", up);
    ui.dir->setToolTip(up ? T("Отпустить ↑ — щёлкни, чтобы стало «нажать»")
                     : click ? T("Клик ↕ — нажать и сразу отпустить. Щёлкни, чтобы стало «нажать»")
                             : T("Нажать ↓ — щёлкни, чтобы стало «отпустить»"));
    ui.dir->setAccessibleName(up ? T("Отпустить") : click ? T("Клик") : T("Нажать"));
    Theme::repolish(ui.dir);
}

// Режимы: у шага — нажать / отпустить / клик, у действия — клик / держать N мс
namespace {
QList<Choice> modeChoices(bool steps) {
    if (steps)
        return {{"arrow-down", T("Нажать ↓"), T("Зажать и не отпускать — отпустит шаг «Отпустить»")},
                {"arrow-up", T("Отпустить ↑"), T("Отпустить то, что зажал шаг «Нажать»")},
                {"press-tap", T("Клик ↕"), T("Нажать и сразу отпустить — за один шаг")}};
    return {{"click", T("Клик"), T("Нажать и сразу отпустить")},
            {"press-hold", T("Держать"), T("Держать нажатым столько, сколько указано рядом с клавишей")}};
}
int modeIndex(bool steps, const ActionStep& a) {
    if (steps) return a.press == ActionStep::Up ? 1 : a.press == ActionStep::Click ? 2 : 0;
    return a.holdMs > 0 ? 1 : 0;
}
}

void ActionsEditor::updateMode(SlotUi& ui, const ActionStep& a) {
    if (ui.mode) {
        const Choice c = modeChoices(steps()).value(modeIndex(steps(), a));
        // на кнопке — коротко, без стрелок: они есть на значке
        QString title = c.title;
        title.remove(QStringLiteral(" ↓")).remove(QStringLiteral(" ↑")).remove(QStringLiteral(" ↕"));
        ui.mode->setChoice(c.icon, title);
    }
    if (ui.hold && ui.hold->value() != a.holdMs) { QSignalBlocker b(ui.hold); ui.hold->setValue(a.holdMs); }
}

void ActionsEditor::openModePlate(int i) {
    if (i < 0 || i >= actions_.size() || i >= slots_.size() || !slots_[i].mode) return;
    const int gen = buildGen_;
    ChoicePlate::open(slots_[i].mode, steps() ? T("Режим шага %1").arg(i + 1) : T("Режим действия %1").arg(i + 1),
                      modeChoices(steps()), modeIndex(steps(), actions_[i]), [this, i, gen](int k) {
        if (gen != buildGen_ || i >= actions_.size()) return;      // ряд за это время пересобрали
        ActionStep& a = actions_[i];
        if (steps()) {
            a.press = k == 1 ? ActionStep::Up : k == 2 ? ActionStep::Click : ActionStep::Down;
        } else if ((k == 1) != (a.holdMs > 0)) {
            a.holdMs = k == 1 ? 100 : 0;
        } else {
            return;
        }
        updateValues();
        edited();
        if (k == 1 && !steps() && i < slots_.size() && slots_[i].hold) {
            // выбрали «держать» — сразу можно вписать, сколько
            slots_[i].hold->setFocus(Qt::OtherFocusReason);
            slots_[i].hold->selectAll();
        }
    });
}

void ActionsEditor::updateValues() {
    loading_ = true;
    const QList<int> stray = steps() ? Steps::strayUps(actions_) : QList<int>{};
    for (int i = 0; i < slots_.size() && i < actions_.size(); ++i) {
        const ActionStep& a = actions_[i];
        SlotUi& ui = slots_[i];
        QString item = a.itemName();
        // клавиша не из списка действий (F13–F24 из старых настроек или профиля) — показываем
        // как есть: кликер жмёт именно её, и подпись не должна врать
        if (!a.mouse && !Keys::byName(a.key)) item = Keys::actionKeyNames().value(0);
        ui.value->setCurrentItem(item);
        updateMode(ui, a);
        if (ui.after && ui.after->value() != a.afterMs) { QSignalBlocker b(ui.after); ui.after->setValue(a.afterMs); }
        updateDir(ui, a);
        // «отпустить» без «нажать» раньше — шаг пропускается: видно сразу
        const bool warn = stray.contains(i);
        if (ui.frame->property("warn").toBool() != warn) {
            ui.frame->setProperty("warn", warn);
            Theme::repolish(ui.frame);
        }
        ui.frame->setToolTip(warn ? T("«Отпустить» без «нажать» раньше в круге — этот шаг пропускается") : QString());
    }
    if (startSpin_ && startSpin_->value() != startMs_) { QSignalBlocker b(startSpin_); startSpin_->setValue(startMs_); }
    if (repeatSpin_ && repeatSpin_->value() != intervalMs_) { QSignalBlocker b(repeatSpin_); repeatSpin_->setValue(intervalMs_); }
    loading_ = false;
}

void ActionsEditor::setActiveStep(int i) {
    if (i >= slots_.size()) i = -1;
    if (i == active_) return;
    auto mark = [this](int k, bool on) {
        if (k < 0 || k >= slots_.size()) return;
        slots_[k].frame->setProperty("active", on);
        Theme::repolish(slots_[k].frame);
    };
    mark(active_, false);
    active_ = i;
    mark(active_, true);
}

void ActionsEditor::refreshLook() {
    if (builtLook_ == lookKey()) return;
    rebuild();
    updateValues();
}

void ActionsEditor::refreshIcons() {
    const auto& t = Theme::t();
    for (SlotUi& ui : slots_) {
        if (ui.remove) ui.remove->setIcon(AqIcons::ui("x", t.muted, 12, 2.4));
        if (ui.grip) ui.grip->setIcon(AqIcons::ui("grip", t.faint, 12));
        if (ui.grip) ui.grip->setIconSize(QSize(12, 12));
        if (ui.mode) ui.mode->update();
    }
    if (add_) add_->setIcon(AqIcons::ui("plus", t.muted, 18));
    for (int i = 0; i < slots_.size() && i < actions_.size(); ++i) {
        slots_[i].value->setCurrent(slots_[i].value->currentKey(), slots_[i].value->text());
        updateDir(slots_[i], actions_[i]);
    }
}
