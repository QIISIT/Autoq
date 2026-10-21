#include "actionseditor.h"
#include "i18n.h"
#include "widgets.h"
#include "keypicker.h"
#include "icons.h"
#include "theme.h"
#include <QComboBox>
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
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include "animations.h"

// Кнопка постоянного размера: min-height из стиля (QSS) её не сжимает
class SizedButton : public QPushButton {
public:
    explicit SizedButton(const QSize& s) : size_(s) { setFixedSize(s); }
    QSize sizeHint() const override { return size_; }
    QSize minimumSizeHint() const override { return size_; }
private:
    QSize size_;
};

static QSpinBox* smallSpin(int lo, int hi, const QString& tip) {
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

ActionsEditor::ActionsEditor(QWidget* parent) : QWidget(parent) {
    flow_ = new FlowLayout(this, 10, 10);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

int ActionsEditor::heightForWidth(int w) const { return flow_->heightForWidth(w); }
QSize ActionsEditor::sizeHint() const { return QSize(640, flow_->heightForWidth(640)); }
QSize ActionsEditor::minimumSizeHint() const { return QSize(200, flow_->heightForWidth(qMax(width(), 200))); }

void ActionsEditor::setActions(const QList<ActionStep>& actions, GameMode mode) {
    actions_ = actions.isEmpty() ? QList<ActionStep>{ActionStep{}} : actions;
    while (actions_.size() > kMaxActions) actions_.removeLast();
    mode_ = mode;
    if (builtCount_ != actions_.size() || builtMode_ != mode_) rebuild();
    else updateValues();
}

void ActionsEditor::edited() {
    if (loading_) return;
    emit changed();
}

void ActionsEditor::rebuild() {
    // не пересобирать прямо из обработчика кнопки внутри ряда — откладываем удаление
    flow_->clear();
    slots_.clear();
    units_.clear();
    add_ = nullptr;
    builtCount_ = actions_.size();
    builtMode_ = mode_;
    const int n = actions_.size();
    const bool seq = mode_ == GameMode::Sequence;
    const bool hold = mode_ == GameMode::Hold;
    for (int i = 0; i < n; ++i) {
        auto* unit = new QWidget();
        auto* uh = new QHBoxLayout(unit);
        uh->setContentsMargins(0, 0, 0, 0);
        uh->setSpacing(10);

        auto* f = new QFrame();
        f->setObjectName("slot");
        f->setFixedSize(176, hold ? 88 : 110);
        auto* g = new QGridLayout(f);
        g->setContentsMargins(12, 8, 8, 10);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(4);
        SlotUi ui;
        ui.type = new QComboBox();
        ui.type->setObjectName("kind");
        ui.type->addItems({T("Клик мышью"), T("Клавиша")});
        ui.type->setCursor(Qt::PointingHandCursor);
        ui.type->setToolTip(T("Действие %1: кнопка мыши или клавиша").arg(i + 1));
        ui.type->setAccessibleName(ui.type->toolTip());
        g->addWidget(ui.type, 0, 0, Qt::AlignLeft);
        if (n > 1) {
            ui.remove = new QToolButton();
            ui.remove->setObjectName("icon");
            ui.remove->setFixedSize(22, 22);
            ui.remove->setToolTip(T("Убрать действие %1").arg(i + 1));
            ui.remove->setAccessibleName(ui.remove->toolTip());
            ui.remove->setCursor(Qt::PointingHandCursor);
            g->addWidget(ui.remove, 0, 1, Qt::AlignRight | Qt::AlignTop);
        }
        ui.value = new KeyPicker();
        ui.value->setObjectName("keyPickBig");
        ui.value->setKeys(KeyPicker::actionItems(false), KeyPicker::actionCommon());
        ui.value->setToolTip(T("Кнопка мыши или клавиша — список с поиском"));
        ui.value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        g->addWidget(ui.value, 1, 0, 1, 2);
        if (!hold) {
            auto* hr = new QHBoxLayout();
            hr->setSpacing(6);
            ui.holdLabel = new QLabel(T("держать"));
            ui.holdLabel->setObjectName("small");
            hr->addWidget(ui.holdLabel);
            ui.hold = smallSpin(0, 10000, T("Сколько держать нажатым (0 — обычный щелчок)"));
            ui.hold->setFixedWidth(74);
            hr->addWidget(ui.hold);
            hr->addStretch(1);
            g->addLayout(hr, 2, 0, 1, 2);
        }
        uh->addWidget(f);

        if (i + 1 < n) {
            auto* conn = new QWidget();
            auto* cv = new QVBoxLayout(conn);
            cv->setContentsMargins(0, 0, 0, 0);
            cv->setSpacing(3);
            if (seq) {
                auto* l = new QLabel(T("пауза"));
                l->setObjectName("small");
                cv->addWidget(l, 0, Qt::AlignHCenter);
                ui.after = smallSpin(0, 60000, T("Пауза после действия %1 перед действием %2 (колёсико мыши — быстро менять)").arg(i + 1).arg(i + 2));
                ui.after->setFixedWidth(84);
                cv->addWidget(ui.after, 0, Qt::AlignHCenter);
                cv->addWidget(new ArrowLine(), 0, Qt::AlignHCenter);
            } else {
                cv->addWidget(new PlusBadge(), 0, Qt::AlignHCenter);
                auto* l = new QLabel(hold ? T("держать вместе") : T("одновременно"));
                l->setObjectName("small");
                cv->addWidget(l, 0, Qt::AlignHCenter);
                conn->setFixedWidth(92);
            }
            uh->addWidget(conn, 0, Qt::AlignVCenter);
        }
        flow_->addWidget(unit);
        slots_ << ui;
        units_ << unit;

        connect(ui.type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int idx) {
            if (loading_ || i >= actions_.size()) return;
            ActionStep& a = actions_[i];
            a.mouse = idx == 0;
            if (a.mouse && !MouseButtons::valid(a.button)) a.button = 1;
            if (!a.mouse && !Keys::actionKeyNames().contains(a.key)) a.key = "f";
            updateValues();
            edited();
        });
        connect(ui.value, &KeyPicker::picked, this, [this, i](const QString& name) {
            if (i >= actions_.size()) return;
            ActionStep& a = actions_[i];
            const ActionStep p = ActionStep::fromItem(name);
            a.mouse = p.mouse;
            if (p.mouse) a.button = p.button; else a.key = p.key;
            updateValues();
            edited();
        });
        if (ui.hold)
            connect(ui.hold, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
                if (loading_ || i >= actions_.size()) return;
                actions_[i].holdMs = v;
                edited();
            });
        if (ui.after)
            connect(ui.after, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
                if (loading_ || i >= actions_.size()) return;
                actions_[i].afterMs = v;
                edited();
            });
        if (ui.remove)
            connect(ui.remove, &QToolButton::clicked, this, [this, i]() {
                QTimer::singleShot(0, this, [this, i]() {
                    if (actions_.size() <= 1 || i >= actions_.size()) return;
                    const Snapshot before = snapshot();
                    const QPixmap pm = units_.value(i) ? units_[i]->grab() : QPixmap();
                    actions_.removeAt(i);
                    setUpdatesEnabled(false);       // без промежуточного кадра «всё в куче»
                    rebuild();
                    animateFrom(before, i, -1, pm);
                    setUpdatesEnabled(true);
                    edited();
                });
            });
    }
    if (n < kMaxActions) {
        add_ = new SizedButton(QSize(n == 1 ? 176 : 132, hold ? 88 : 110));
        add_->setObjectName("addSecond");
        add_->setCursor(Qt::PointingHandCursor);
        add_->setText(n == 1 ? (seq ? T("Действие 2") : T("вместе с…")) : T("Ещё"));
        add_->setToolTip(seq ? T("Добавить действие — по очереди после остальных")
                             : T("Добавить клавишу или кнопку — вместе с остальными"));
        add_->setAccessibleName(T("Добавить действие"));
        connect(add_, &QPushButton::clicked, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                if (actions_.size() >= kMaxActions) return;
                ActionStep a;
                a.mouse = false;
                // новая клавиша — не такая же, как уже есть
                const QStringList pool = {"f", "e", "space", "q", "r", "1", "2", "3"};
                for (const QString& k : pool) {
                    bool used = false;
                    for (const ActionStep& x : actions_) used = used || (!x.mouse && x.key == k);
                    if (!used) { a.key = k; break; }
                }
                const Snapshot before = snapshot();
                actions_ << a;
                setUpdatesEnabled(false);
                rebuild();
                animateFrom(before, -1, actions_.size() - 1, QPixmap());
                setUpdatesEnabled(true);
                edited();
            });
        });
        flow_->addWidget(add_);
    }
    refreshIcons();
    updateValues();
    updateGeometry();
}

ActionsEditor::Snapshot ActionsEditor::snapshot() const {
    Snapshot sn;
    for (QWidget* u : units_) sn.units << u->geometry();
    if (add_) sn.add = add_->geometry();
    sn.height = height();
    return sn;
}

void ActionsEditor::animateFrom(const Snapshot& before, int removed, int added, const QPixmap& removedPm) {
    if (!Anim::enabled() || !isVisible() || width() < 50) return;
    const int dur = Anim::ms(230);
    // новые виджеты Qt показывает только на следующем шаге — показываем сразу, чтобы
    // раскладка их учла (кадр «в куче» не виден: обновление окна выключено)
    for (QWidget* u : units_) if (u->isHidden()) u->show();
    if (add_ && add_->isHidden()) add_->show();
    // новые места считаем сразу, не дожидаясь раскладки
    const int newH = heightForWidth(width());
    flow_->setGeometry(QRect(0, 0, width(), newH));
    flow_->setFrozen(true);
    QTimer::singleShot(dur + 30, this, [this]() { flow_->setFrozen(false); });
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
        if (j == added) {                       // новое действие — проявляется и чуть подъезжает
            auto* eff = new QGraphicsOpacityEffect(u);
            eff->setOpacity(0);
            u->setGraphicsEffect(eff);
            auto* f = new QPropertyAnimation(eff, "opacity", u);
            f->setDuration(dur);
            f->setStartValue(0.0);
            f->setEndValue(1.0);
            connect(f, &QPropertyAnimation::finished, u, [u]() { u->setGraphicsEffect(nullptr); });
            f->start(QAbstractAnimation::DeleteWhenStopped);
            // из места кнопки «добавить»
            move(u, before.add.isValid() ? before.add.topLeft() : u->pos() + QPoint(0, 12));
            continue;
        }
        const int old = (removed >= 0 && j >= removed) ? j + 1 : j;
        if (old < before.units.size()) move(u, before.units[old].topLeft());
    }
    if (add_ && before.add.isValid()) move(add_, before.add.topLeft());
    // убранное действие тает на своём месте
    if (removed >= 0 && removed < before.units.size() && !removedPm.isNull()) {
        auto* ghost = new QLabel(this);
        ghost->setPixmap(removedPm);
        ghost->setGeometry(before.units[removed]);
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
    // высота карточки — постепенно (иначе карточки ниже прыгают)
    if (before.height > 0 && before.height != newH) {
        if (heightAnim_) heightAnim_->stop();
        heightAnim_ = new QVariantAnimation(this);
        heightAnim_->setDuration(dur);
        heightAnim_->setStartValue(before.height);
        heightAnim_->setEndValue(newH);
        heightAnim_->setEasingCurve(QEasingCurve::OutCubic);
        setFixedHeight(before.height);
        connect(heightAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { setFixedHeight(v.toInt()); });
        connect(heightAnim_, &QVariantAnimation::finished, this, [this]() {
            setMinimumHeight(0);
            setMaximumHeight(QWIDGETSIZE_MAX);
            updateGeometry();
            heightAnim_ = nullptr;
        });
        heightAnim_->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void ActionsEditor::updateValues() {
    loading_ = true;
    for (int i = 0; i < slots_.size() && i < actions_.size(); ++i) {
        const ActionStep& a = actions_[i];
        SlotUi& ui = slots_[i];
        {
            QSignalBlocker b(ui.type);
            ui.type->setCurrentIndex(a.mouse ? 0 : 1);
        }
        QString item = a.itemName();
        // клавиша не из списка действий (F13–F24 из старых настроек или профиля) — показываем
        // как есть: кликер жмёт именно её, и подпись не должна врать
        if (!a.mouse && !Keys::byName(a.key)) item = Keys::actionKeyNames().value(0);
        ui.value->setCurrentItem(item);
        if (ui.hold && ui.hold->value() != a.holdMs) { QSignalBlocker b(ui.hold); ui.hold->setValue(a.holdMs); }
        if (ui.after && ui.after->value() != a.afterMs) { QSignalBlocker b(ui.after); ui.after->setValue(a.afterMs); }
    }
    loading_ = false;
}

void ActionsEditor::refreshIcons() {
    const auto& t = Theme::t();
    for (SlotUi& ui : slots_)
        if (ui.remove) ui.remove->setIcon(AqIcons::ui("x", t.muted, 12, 2.4));
    if (add_) add_->setIcon(AqIcons::ui("plus", t.muted, 18));
    for (SlotUi& ui : slots_) ui.value->setCurrent(ui.value->currentKey(), ui.value->text());
}
