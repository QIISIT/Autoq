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
#include "groupdialog.h"
#include <QMessageBox>

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
    auto* s = msSpin(lo, hi);
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
    if (builtCount_ != actions_.size() || builtMode_ != mode_ || builtKey_ != buildKey()) {
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

// в окне группы «повтор через» — пауза между её повторами: от 0
int ActionsEditor::repeatMin() const { return groupMode_ ? 0 : repeatMinMs(mode_, int(run().size()), minIntervalMs_); }

QList<ActionStep> ActionsEditor::run(QList<int>* owner) const {
    return Groups::expand(actions_, groups_, steps(), mode_ == GameMode::Combo || mode_ == GameMode::Hold, owner);
}

QString ActionsEditor::buildKey() const {
    QString k;
    for (const ActionStep& a : actions_) k += a.isGroup() ? 'g' : 'a';
    return k;
}

// группы — в рядах «По очереди» и «По шагам» (в «Вместе» и «Зажать» порядка нет). Одно
// действие в «Вместе» — это «Повторять»: группа и там, а ряд станет «По очереди» (главное окно)
bool ActionsEditor::canGroup() const {
    if (groupMode_) return false;
    if (mode_ == GameMode::Combo) return actions_.size() == 1 && !actions_[0].isGroup();
    return mode_ == GameMode::Sequence || steps();
}

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
    add_ = groupAdd_ = selMake_ = nullptr;
    selTile_ = nullptr;
    sel_.clear();
    selAnchor_ = -1;
    startUnit_ = nullptr;
    startSpin_ = repeatSpin_ = nullptr;
    dropMark_->hide();
    ++buildGen_;
    builtCount_ = actions_.size();
    builtMode_ = mode_;
    builtKey_ = buildKey();
    active_ = -1;
    const int n = actions_.size();
    const bool hold = mode_ == GameMode::Hold;
    const bool paused = mode_ == GameMode::Sequence || steps();     // между карточками — пауза
    const bool repeats = mode_ == GameMode::Sequence || steps();    // вызов группы — «×N»
    // у действия рядом с клавишей — «держать»; «Уже» / «Шире» — вид шагов в редакторе
    const int slotSize = Theme::element("slot").size;
    const int slotW = (steps() || hold ? 176 : 206) + (slotSize == 0 ? -20 : slotSize == 2 ? 36 : 0);
    // вызов группы — не уже карточки действия: имени и «×N» нужно место и в ряду шагов
    const int callW = qMax(slotW, 206 + (slotSize == 0 ? -20 : slotSize == 2 ? 36 : 0));
    builtLook_ = lookKey();

    // старт через N мс (у группы — нет: она начинается там, где стоит в ряду)
    if (!groupMode_) {
        startSpin_ = smallSpin(0, kMaxStartDelayMs, T("Старт через столько мс после нажатия «Старт» или клавиши (0 — сразу)"));
        startUnit_ = chip(T("старт через"), startSpin_, new ArrowLine());
        flow_->addWidget(startUnit_);
        connect(startSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
            if (loading_) return;
            startMs_ = v;
            edited();
        });
    }

    for (int i = 0; i < n; ++i) {
        const bool call = actions_[i].isGroup();
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
        f->setProperty("group", call);
        f->setFixedSize(call ? callW : slotW, slotH_);
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
        ui.grip->setToolTip(call ? T("Перетащи группу на другое место")
                                 : steps() ? T("Перетащи шаг на другое место") : T("Перетащи действие на другое место"));
        ui.grip->setProperty("slotIndex", i);
        ui.grip->installEventFilter(this);
        g->addWidget(ui.grip, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
        if (call) {
            // состав группы — мелко над именем (полностью — в подсказке)
            ui.caption = new QLabel();
            ui.caption->setObjectName("small");
            ui.caption->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            g->addWidget(ui.caption, 0, 1, Qt::AlignVCenter);
        } else if (hold) {
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
        if (!hold && !steps() && !call) {
            // подпись над полем «держать» — чтобы не спутать с паузой
            auto* hc = new QLabel(T("держать"));
            hc->setObjectName("small");
            hc->setToolTip(T("Сколько держать нажатым (0 — клик: нажать и сразу отпустить)"));
            g->addWidget(hc, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
        }
        if (n > 1 || call) {
            ui.remove = new QToolButton();
            ui.remove->setObjectName("icon");
            ui.remove->setFixedSize(22, 22);
            ui.remove->setToolTip(call ? T("Убрать группу из ряда (сама группа останется)")
                                       : steps() ? T("Убрать шаг %1").arg(i + 1) : T("Убрать действие %1").arg(i + 1));
            ui.remove->setAccessibleName(ui.remove->toolTip());
            ui.remove->setCursor(Qt::PointingHandCursor);
            g->addWidget(ui.remove, 0, 3, Qt::AlignRight | Qt::AlignTop);
        }
        g->setColumnStretch(1, 1);
        auto* vr = new QHBoxLayout();
        vr->setSpacing(6);
        if (call) {
            ui.groupName = new QPushButton();
            ui.groupName->setObjectName("groupName");
            ui.groupName->setProperty("aqEl", "key");
            ui.groupName->setCursor(Qt::PointingHandCursor);
            ui.groupName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            ui.groupName->setToolTip(T("Изменить группу: имя и что в ней"));
            vr->addWidget(ui.groupName, 1);
            if (repeats) {
                ui.repeat = new QSpinBox();
                ui.repeat->setRange(1, kMaxRepeat);
                ui.repeat->setPrefix(QStringLiteral("×"));
                ui.repeat->setFixedWidth(64);
                ui.repeat->setToolTip(T("Сколько раз подряд выполнить группу"));
                ui.repeat->setAccessibleName(ui.repeat->toolTip());
                vr->addWidget(ui.repeat);
            }
        } else {
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
        }
        g->addLayout(vr, 1, 0, 1, 4);
        g->setRowStretch(2, 1);
        uh->addWidget(f);

        if (i + 1 < n) {
            QWidget* conn;
            if (paused) {
                ui.after = smallSpin(0, 60000, call
                    ? T("Пауза после группы — после её последнего повтора, перед следующим в ряду. Скорость самой группы не меняет: "
                        "паузу между повторами задаёт «повтор через» в окне группы")
                    : steps()
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
        } else if (!hold && !groupMode_) {
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
        } else if (groupMode_) {
            // окно группы: после последнего — пауза между повторами (×N у карточки группы)
            repeatSpin_ = smallSpin(0, 60000, T("Пауза между повторами группы (×N на её карточке). После последнего повтора — "
                                                "пауза, что стоит в ряду после группы"));
            uh->addWidget(chip(T("повтор через"), repeatSpin_, new LoopLine()), 0, Qt::AlignVCenter);
            connect(repeatSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
                if (loading_) return;
                intervalMs_ = qMax(0, v);
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
        if (ui.value)
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
        if (ui.groupName)
            connect(ui.groupName, &QPushButton::clicked, this, [this, i]() { later([this, i]() { editGroup(i); }); });
        if (ui.repeat)
            connect(ui.repeat, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
                if (loading_ || i >= actions_.size()) return;
                actions_[i].repeat = v;
                if (repeatSpin_ && repeatSpin_->minimum() != repeatMin()) {
                    QSignalBlocker b(repeatSpin_);
                    repeatSpin_->setMinimum(repeatMin());
                }
                edited();
            });
        if (ui.remove)
            connect(ui.remove, &QToolButton::clicked, this, [this, i]() { later([this, i]() { removeAt(i); }); });
    }
    if (n < maxItems()) {
        add_ = new SizedButton(QSize(n == 1 ? slotW : 96, slotH_));
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
        // группы — рядом с «добавить», всегда: создать новую или поставить готовую (из любой игры)
        if (canGroup()) {
            groupAdd_ = new SizedButton(QSize(96, slotH_));
            groupAdd_->setObjectName("addSecond");
            groupAdd_->setProperty("aqEl", "add");
            groupAdd_->setCursor(Qt::PointingHandCursor);
            groupAdd_->setText(T("Группа"));
            groupAdd_->setToolTip(T("Создать группу или поставить в конец ряда готовую — группы общие для всех игр"));
            groupAdd_->setAccessibleName(T("Вставить группу"));
            connect(groupAdd_, &QPushButton::clicked, this, [this]() {
                QMenu m(this);
                fillGroupMenu(&m, int(actions_.size()));
                m.exec(groupAdd_->mapToGlobal(QPoint(0, groupAdd_->height())));
            });
            flow_->addWidget(groupAdd_);
        }
    }
    if (canGroup()) {
        // выделили карточки — вместо «добавить»: сделать из них группу
        selTile_ = new QWidget();
        selTile_->setFixedSize(96, slotH_);      // как «добавить»: ряд не переносится
        auto* sv = new QVBoxLayout(selTile_);
        sv->setContentsMargins(0, 0, 0, 0);
        sv->setSpacing(4);
        selMake_ = new QPushButton();
        selMake_->setObjectName("accent");
        selMake_->setCursor(Qt::PointingHandCursor);
        auto* unsel = new QPushButton(T("Отмена"));
        unsel->setToolTip(T("Снять выделение"));
        unsel->setObjectName("ghost");
        unsel->setCursor(Qt::PointingHandCursor);
        sv->addStretch(1);
        sv->addWidget(selMake_);
        sv->addWidget(unsel);
        sv->addStretch(1);
        connect(selMake_, &QPushButton::clicked, this, [this]() { later([this]() { makeGroup(); }); });
        connect(unsel, &QPushButton::clicked, this, [this]() { setSelection({}); });
        flow_->addWidget(selTile_);
    }
    // сразу видны: addWidget показывает их только на следующем круге событий,
    // и переход при смене игры снял бы пустой ряд
    for (int i = 0; i < flow_->count(); ++i)
        if (QWidget* w = flow_->itemAt(i)->widget()) w->show();
    if (selTile_) selTile_->hide();     // пока ничего не выделено
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
        if (groupAdd_) static_cast<SizedButton*>(groupAdd_)->setSize(QSize(groupAdd_->width(), h));
        if (selTile_) selTile_->setFixedHeight(h);
    }
    updateGeometry();
}

ActionStep ActionsEditor::newItem() const {
    ActionStep a;
    if (steps()) {
        // что-то осталось нажатым — следующий шаг его отпускает
        const QList<ActionStep> left = Steps::leftDown(run());
        if (!left.isEmpty()) {
            a = left.last();
            a.press = ActionStep::Up;
            a.holdMs = a.afterMs = 0;
            return a;
        }
        const QList<ActionStep> used = Steps::pressedInputs(run());
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
        for (const ActionStep& x : run()) used = used || (!x.mouse && x.key == k);
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
    if (i < 0 || i >= actions_.size()) return;
    if (actions_.size() == 1) {
        // одна карточка-группа: убрать её из ряда можно — вместо неё новое действие
        if (!actions_[i].isGroup()) return;
        applyEdit({newItem()}, {-1}, i);
        return;
    }
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
    const bool call = actions_[i].isGroup();
    QMenu m(this);
    QAction* edit = nullptr;
    QAction* split = nullptr;
    if (call) {
        const ActionGroup* g = Groups::find(groups_, actions_[i].group);
        edit = m.addAction(T("Изменить группу…"));
        split = m.addAction(T("Разгруппировать"));
        split->setToolTip(T("Вместо группы — её действия, здесь же"));
        edit->setEnabled(g);
        split->setEnabled(g);
        m.addSeparator();
    }
    QAction* before = m.addAction(steps() ? T("Добавить шаг перед") : T("Добавить действие перед"));
    QAction* after = m.addAction(steps() ? T("Добавить шаг после") : T("Добавить действие после"));
    QAction* dup = m.addAction(T("Дублировать"));
    for (QAction* a : {before, after, dup}) a->setEnabled(canAdd);
    if (canGroup()) {
        QMenu* sub = m.addMenu(T("Вставить группу после"));
        fillGroupMenu(sub, i + 1);
        sub->setEnabled(canAdd);
    }
    QAction* flip = nullptr;
    if (steps() && !call)
        flip = m.addAction(actions_[i].press == ActionStep::Down ? T("Сделать «отпустить» ↑") : T("Сделать «нажать» ↓"));
    QAction* select = nullptr;
    QAction* group = nullptr;
    if (canGroup() && !call) {
        m.addSeparator();
        select = m.addAction(sel_.contains(i) ? T("Снять выделение") : T("Выделить (Ctrl + щелчок)"));
        const bool inSel = sel_.contains(i) && sel_.size() > 1;
        group = m.addAction(inSel ? T("Сделать группой (%1)").arg(sel_.size()) : T("Сделать группой"));
        if (inSel && !selectionSolid()) {
            group->setEnabled(false);
            group->setText(T("Сделать группой — выдели карточки подряд"));
        }
    }
    m.addSeparator();
    QAction* left = m.addAction(T("Переместить влево"));
    QAction* right = m.addAction(T("Переместить вправо"));
    left->setEnabled(i > 0);
    right->setEnabled(i + 1 < n);
    m.addSeparator();
    QAction* del = m.addAction(call ? T("Убрать из ряда") : steps() ? T("Убрать шаг") : T("Убрать действие"));
    del->setEnabled(n > 1 || call);     // одна группа в ряду — вместо неё станет новое действие
    QAction* drop = nullptr;
    if (call && Groups::find(groups_, actions_[i].group))
        drop = m.addAction(AqIcons::ui("trash", Theme::t().muted, 14), T("Удалить группу…"));
    QAction* got = m.exec(global);
    if (!got) return;
    if (got == flip) {
        ActionStep& a = actions_[i];
        a.press = a.press == ActionStep::Down ? ActionStep::Up : ActionStep::Down;
        updateValues();
        edited();
        return;
    }
    if (got == select) { toggleSelect(i, false); return; }
    // меню удалится вместе с пунктами — запоминаем, что выбрали, числом
    // (вставка группы из подменю — уже сделана: её пункты сами вызывают insertGroup)
    const int what = got == before ? 1 : got == after ? 2 : got == dup ? 3 : got == left ? 4 : got == right ? 5
                   : got == del ? 6 : got == edit ? 7 : got == split ? 8 : got == group ? 9 : got == drop ? 10 : 0;
    if (!what) return;
    later([this, i, what]() {
        if (i >= actions_.size()) return;
        switch (what) {
            case 1: insertAt(i, newItem()); break;
            case 2: insertAt(i + 1, newItem()); break;
            case 3: { const ActionStep c = actions_[i]; insertAt(i + 1, c); break; }
            case 4: moveTo(i, i - 1); break;
            case 5: moveTo(i, i + 2); break;
            case 6: removeAt(i); break;
            case 7: editGroup(i); break;
            case 8: ungroup(i); break;
            case 9:
                if (!sel_.contains(i)) setSelection({i});
                makeGroup();
                break;
            case 10: deleteGroup(i); break;
            default: break;
        }
    });
}

// ---------------------------------------------------------------------------
// Группы
// ---------------------------------------------------------------------------
void ActionsEditor::fillGroupMenu(QMenu* m, int at) {
    QAction* make = m->addAction(AqIcons::ui("plus", Theme::t().muted, 14), T("Создать группу…"));
    make->setToolTip(T("Новая группа: имя и что в ней — встанет сюда"));
    connect(make, &QAction::triggered, this, [this, at]() { later([this, at]() { createGroup(at); }); });
    if (!groups_.isEmpty()) m->addSeparator();
    for (const ActionGroup& g : groups_) {
        const QString pv = Groups::preview(Groups::inRow(g, steps()), 3);
        QAction* a = m->addAction(AqIcons::ui("layers", Theme::t().muted, 14),
                                  pv.isEmpty() ? g.name : QStringLiteral("%1   %2").arg(g.name, pv));
        const QString id = g.id;
        connect(a, &QAction::triggered, this, [this, at, id]() { later([this, at, id]() { insertGroup(at, id); }); });
    }
}

void ActionsEditor::createGroup(int at) {
    if (!canGroup() || actions_.size() >= maxItems()) return;
    if (groups_.size() >= kMaxGroups) {
        QMessageBox::information(this, T("Группы"), T("Групп уже %1 — больше нельзя. Удалить лишнюю можно в окне группы.").arg(kMaxGroups));
        return;
    }
    ActionGroup g = Groups::blank(groups_, steps());
    if (GroupDialog::edit(this, g, 0, true, steps()) != GroupDialog::Saved) return;
    groups_ << g;
    insertGroup(at, g.id);
}

void ActionsEditor::insertGroup(int at, const QString& id) {
    if (!Groups::find(groups_, id)) return;
    ActionStep c;
    c.group = id;
    c.mouse = false;
    c.key.clear();
    c.press = ActionStep::Click;
    insertAt(at, c);
}

void ActionsEditor::toggleSelect(int i, bool range) {
    if (!canGroup() || i < 0 || i >= actions_.size() || actions_[i].isGroup()) return;
    QList<int> next = sel_;
    if (range && selAnchor_ >= 0 && selAnchor_ < actions_.size()) {
        next.clear();
        for (int k = qMin(selAnchor_, i); k <= qMax(selAnchor_, i); ++k)
            if (!actions_[k].isGroup()) next << k;
    } else {
        if (next.contains(i)) next.removeAll(i); else next << i;
        selAnchor_ = i;
    }
    setSelection(next);
}

void ActionsEditor::setSelection(const QList<int>& sel) {
    sel_ = sel;
    std::sort(sel_.begin(), sel_.end());
    if (sel_.isEmpty()) selAnchor_ = -1;
    updateSelection();
}

bool ActionsEditor::selectionSolid() const {
    return !sel_.isEmpty() && sel_.last() - sel_.first() + 1 == sel_.size();
}

void ActionsEditor::updateSelection() {
    for (int k = 0; k < slots_.size(); ++k) {
        const bool on = sel_.contains(k);
        if (slots_[k].frame->property("selected").toBool() == on) continue;
        slots_[k].frame->setProperty("selected", on);
        Theme::repolish(slots_[k].frame);
    }
    const bool any = !sel_.isEmpty();
    if (selTile_) selTile_->setVisible(any);
    if (add_) add_->setVisible(!any);
    if (groupAdd_) groupAdd_->setVisible(!any);
    if (selMake_) {
        selMake_->setText(T("Сделать\nгруппой"));
        selMake_->setEnabled(selectionSolid());
        selMake_->setToolTip(selectionSolid() ? T("Выделено: %1. Станет одной карточкой-группой — её можно поставить ещё раз и повторить ×N").arg(sel_.size())
                                              : T("Группа — из карточек подряд: выдели их Shift + щелчком"));
    }
    flow_->invalidate();
    updateGeometry();
}

void ActionsEditor::makeGroup() {
    if (!canGroup() || !selectionSolid() || sel_.last() >= actions_.size()) return;
    if (groups_.size() >= kMaxGroups) {
        QMessageBox::information(this, T("Группы"), T("Групп уже %1 — больше нельзя. Удалить лишнюю можно в окне группы.").arg(kMaxGroups));
        return;
    }
    const int first = sel_.first(), count = int(sel_.size()), last = sel_.last();
    ActionGroup g;
    g.id = Groups::newId(groups_);
    g.name = Groups::freeName(groups_);
    g.steps = steps();
    g.items = actions_.mid(first, count);
    g.items.last().afterMs = 0;             // пауза после группы — у вызова
    ActionStep c;
    c.group = g.id;
    c.mouse = false;
    c.key.clear();
    c.press = ActionStep::Click;
    c.afterMs = actions_[last].afterMs;
    g.betweenMs = c.afterMs;                // между повторами — та же пауза: ритм ряда не меняется
    groups_ << g;
    QList<ActionStep> next = actions_;
    next.erase(next.begin() + first, next.begin() + first + count);
    next.insert(first, c);
    QList<int> oldOf;
    for (int j = 0; j < next.size(); ++j) oldOf << (j < first ? j : j == first ? -1 : j + count - 1);
    applyEdit(next, oldOf, -1);
}

void ActionsEditor::ungroup(int i) {
    if (i < 0 || i >= actions_.size() || !actions_[i].isGroup()) return;
    const ActionGroup* g = Groups::find(groups_, actions_[i].group);
    if (!g) return;
    QList<ActionStep> part = Groups::inlineCall(actions_[i], *g, steps());
    if (actions_.size() - 1 + part.size() > maxItems()) {
        QMessageBox::information(this, T("Группа"),
            T("Не поместится: в ряду можно до %1, а у группы с повтором — %2.").arg(maxItems()).arg(part.size()));
        return;
    }
    if (part.isEmpty() && actions_.size() == 1) part << newItem();
    QList<ActionStep> next = actions_;
    next.removeAt(i);
    for (int k = 0; k < part.size(); ++k) next.insert(i + k, part[k]);
    QList<int> oldOf;
    for (int j = 0; j < next.size(); ++j)
        oldOf << (j < i ? j : j < i + part.size() ? -1 : j - int(part.size()) + 1);
    applyEdit(next, oldOf, i);
}

void ActionsEditor::editGroup(int i) {
    if (i < 0 || i >= actions_.size() || !actions_[i].isGroup()) return;
    const QString id = actions_[i].group;
    const ActionGroup* found = Groups::find(groups_, id);
    if (!found) return;
    ActionGroup g = *found;
    g.betweenMs = Groups::betweenMs(g, actions_[i]);    // группа из 1.17.0 — как жмётся сейчас
    const auto res = GroupDialog::edit(this, g, Groups::uses(actions_, id), false, steps());
    if (res == GroupDialog::Cancelled) return;
    if (res != GroupDialog::Saved) { removeGroup(id, res == GroupDialog::DeletedWithCards); return; }
    for (ActionGroup& x : groups_)
        if (x.id == id) { x = g; break; }
    if (repeatSpin_ && repeatSpin_->minimum() != repeatMin()) {
        QSignalBlocker b(repeatSpin_);
        repeatSpin_->setMinimum(repeatMin());
    }
    if (builtKey_ != buildKey()) rebuild(); else updateValues();
    edited();
}

void ActionsEditor::deleteGroup(int i) {
    if (i < 0 || i >= actions_.size() || !actions_[i].isGroup()) return;
    const QString id = actions_[i].group;
    const ActionGroup* g = Groups::find(groups_, id);
    if (!g) return;
    const auto res = GroupDialog::askDelete(this, *g, Groups::uses(actions_, id));
    if (res == GroupDialog::Cancelled) return;
    removeGroup(id, res == GroupDialog::DeletedWithCards);
}

// Группу удалили: её вызовы в ряду — обычными карточками на тех же местах (withCards —
// убираются из ряда). Ряд без карточек не бывает: останется одно новое действие
void ActionsEditor::removeGroup(const QString& id, bool withCards) {
    const ActionGroup* found = Groups::find(groups_, id);
    if (!found) return;
    const ActionGroup g = *found;
    for (int k = 0; k < groups_.size(); ++k)
        if (groups_[k].id == id) { groups_.removeAt(k); break; }
    if (withCards) {
        QList<ActionStep> next;
        for (const ActionStep& a : actions_)
            if (a.group != id) next << a;
        actions_ = next;
    } else {
        actions_ = Groups::inlineAll(actions_, g, steps(), maxItems());
    }
    if (actions_.isEmpty()) actions_ << newItem();
    setUpdatesEnabled(false);
    rebuild();
    setUpdatesEnabled(true);
    edited();
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
        // Ctrl / Shift + щелчок — выделить для группы (не перетаскивание)
        if (canGroup() && (me->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            pressSlot_ = -1;
            toggleSelect(w->property("slotIndex").toInt(), me->modifiers() & Qt::ShiftModifier);
            return true;
        }
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
        // простой щелчок по карточке (не перетащили) — выделение снимается
        if (pressSlot_ >= 0 && !sel_.isEmpty()) setSelection({});
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
    if (add_ && add_->isVisible()) sn.add = add_->geometry();
    if (groupAdd_ && groupAdd_->isVisible()) sn.groupAdd = groupAdd_->geometry();
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
    if (groupAdd_ && groupAdd_->isHidden()) groupAdd_->show();
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
    if (groupAdd_ && before.groupAdd.isValid()) move(groupAdd_, before.groupAdd.topLeft());
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
    // «отпустить» без «нажать» — по тому, что жмёт кликер (внутри групп тоже), на карточке-хозяине
    QList<int> stray;
    if (steps()) {
        QList<int> owner;
        const QList<ActionStep> r = run(&owner);
        for (int k : Steps::strayUps(r))
            if (!stray.contains(owner.value(k, -1))) stray << owner.value(k, -1);
    }
    for (int i = 0; i < slots_.size() && i < actions_.size(); ++i) {
        const ActionStep& a = actions_[i];
        SlotUi& ui = slots_[i];
        if (ui.groupName) {
            const ActionGroup* g = Groups::find(groups_, a.group);
            const QString name = g ? g->name : T("Группы нет");
            // состав — как его жмут в этом ряду: в «По шагам» действия группы — «нажать» и «отпустить»
            const ActionGroup view = g ? Groups::inRow(*g, steps()) : ActionGroup{};
            const QString pv = g ? Groups::preview(view, 6) : QString();
            // кнопка и подпись сами не сокращают текст — по ширине карточки
            const int cardW = ui.frame->width();
            const int nameW = cardW - 16 - (ui.repeat ? 70 : 0) - 40;
            ui.groupName->setText(ui.groupName->fontMetrics().elidedText(name, Qt::ElideRight, qMax(30, nameW)));
            ui.caption->setText(ui.caption->fontMetrics().elidedText(pv.isEmpty() ? T("пусто") : pv, Qt::ElideRight,
                                                                     qMax(30, cardW - 16 - 14 - 22 - 12)));
            if (ui.repeat && ui.repeat->value() != a.repeat) { QSignalBlocker b(ui.repeat); ui.repeat->setValue(a.repeat); }
            if (ui.repeat && g) {
                const QString tip = T("Сколько раз подряд выполнить группу. Между повторами — %1 мс (меняется в окне группы)")
                                        .arg(Groups::betweenMs(*g, a));
                ui.repeat->setToolTip(tip);
                ui.repeat->setAccessibleName(tip);
            }
            if (ui.after && ui.after->value() != a.afterMs) { QSignalBlocker b(ui.after); ui.after->setValue(a.afterMs); }
            const bool warn = stray.contains(i);
            if (ui.frame->property("warn").toBool() != warn) {
                ui.frame->setProperty("warn", warn);
                Theme::repolish(ui.frame);
            }
            ui.frame->setToolTip(!g ? T("Такой группы нет — карточка ничего не жмёт, её можно убрать из ряда.")
                                 : T("Группа «%1»: %2").arg(name.toHtmlEscaped(), Groups::preview(view, 16).toHtmlEscaped())
                                   + (warn ? "<br>" + T("Внутри — «отпустить» без «нажать» раньше: этот шаг пропускается") : QString()));
            continue;
        }
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
    if (groupAdd_) groupAdd_->setIcon(AqIcons::ui("layers", t.muted, 18));
    for (int i = 0; i < slots_.size() && i < actions_.size(); ++i) {
        if (slots_[i].groupName) {
            slots_[i].groupName->setIcon(AqIcons::ui("layers", t.redHi, 18));
            continue;
        }
        slots_[i].value->setCurrent(slots_[i].value->currentKey(), slots_[i].value->text());
        updateDir(slots_[i], actions_[i]);
    }
}
