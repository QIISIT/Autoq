#include "onboarding.h"
#include "i18n.h"
#include "theme.h"
#include "widgets.h"
#include "icons.h"
#include "games.h"
#include "appinfo.h"
#include "colorpicker.h"
#include "animations.h"
#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QScrollArea>

static QLabel* lbl(const QString& text, const char* obj) {
    auto* l = new QLabel(text);
    l->setObjectName(obj);
    return l;
}

// Карточка выбора: картинка сверху, название и пояснение снизу
static QPushButton* choiceCard(QWidget* picture, const QString& name, const QString& note) {
    auto* b = new CardButton();
    b->setObjectName("choiceCard");
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    // Minimum, а не Fixed: у QPushButton своя маленькая высота, а нам нужна высота карточки
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    b->setAccessibleName(name);
    b->setToolTip(name + " — " + note);
    auto* v = new QVBoxLayout(b);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);
    picture->setAttribute(Qt::WA_TransparentForMouseEvents);
    v->addWidget(picture, 1);
    auto* n = lbl(name, "gameName");
    auto* s = lbl(note, "small");
    s->setWordWrap(true);
    s->setMinimumHeight(2 * s->fontMetrics().height() + 4);   // у всех карточек одна высота
    s->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    for (QLabel* l : {n, s}) l->setAttribute(Qt::WA_TransparentForMouseEvents);
    v->addWidget(n);
    v->addWidget(s);
    return b;
}

OnboardingPage::OnboardingPage(QWidget* parent) : QWidget(parent) {
    setObjectName("mainInner");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(40, 30, 40, 26);
    outer->setSpacing(14);

    auto* head = new QHBoxLayout();
    head->setSpacing(14);
    logo_ = new QLabel();
    head->addWidget(logo_, 0, Qt::AlignTop);
    auto* col = new QVBoxLayout();
    col->setSpacing(4);
    col->addWidget(lbl(T("Добро пожаловать в AutoQ"), "small"));
    stepTitle_ = lbl(QString(), "welcomeTitle");
    col->addWidget(stepTitle_);
    stepNote_ = lbl(QString(), "muted");
    stepNote_->setWordWrap(true);
    col->addWidget(stepNote_);
    head->addLayout(col, 1);
    auto* dots = new QHBoxLayout();
    dots->setSpacing(6);
    for (int i = 0; i < 2; ++i) {
        auto* d = new QLabel();
        d->setObjectName("stepDot");
        dots->addWidget(d);
        dots_ << d;
    }
    head->addLayout(dots);
    outer->addLayout(head);

    steps_ = new QStackedWidget();
    // --- шаг 1: стиль интерфейса
    {
        auto* page = new QWidget();
        auto* g = new QGridLayout(page);
        g->setContentsMargins(0, 6, 0, 0);
        g->setSpacing(14);
        int i = 0;
        for (const QString& id : Theme::variants()) {
            auto* pv = new ThemePreview();
            pv->setMinimumSize(220, 150);
            pv->setMaximumSize(QWIDGETSIZE_MAX, 230);
            pv->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            pv->setAppearance(Theme::variantPreset(id));
            previews_ << pv;
            auto* card = choiceCard(pv, Theme::variantLabel(id), Theme::variantNote(id));
            card->setMinimumHeight(300);
            card->setProperty("variant", id);
            connect(card, &QPushButton::clicked, this, [this, id]() { chooseVariant(id); });
            variantCards_ << card;
            g->addWidget(card, 0, i++);
        }
        g->setRowStretch(1, 1);
        steps_->addWidget(page);
    }
    // --- шаг 2: анимации
    {
        auto* page = new QWidget();
        auto* g = new QGridLayout(page);
        g->setContentsMargins(0, 6, 0, 0);
        g->setSpacing(14);
        int i = 0;
        for (const QString& id : Theme::animations()) {
            auto* demo = new AnimDemo(id);
            demo->setMinimumSize(180, 110);
            auto* card = choiceCard(demo, Theme::animLabel(id), Theme::animNote(id));
            card->setMinimumHeight(196);
            card->setProperty("anim", id);
            connect(card, &QPushButton::clicked, this, [this, id]() { chooseAnim(id); });
            animCards_ << card;
            g->addWidget(card, i / 2, i % 2);
            ++i;
        }
        g->setRowStretch(2, 1);
        steps_->addWidget(page);
    }
    auto* scroll = new QScrollArea();
    scroll->setObjectName("mainScroll");
    scroll->setWidget(steps_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll, 1);

    auto* foot = new QHBoxLayout();
    foot->setSpacing(8);
    foot->addWidget(lbl(T("Всё это можно поменять потом: Настройки → Оформление."), "faint"), 1);
    back_ = new QPushButton(T("Назад"));
    back_->setCursor(Qt::PointingHandCursor);
    next_ = new QPushButton();
    next_->setObjectName("primary");
    next_->setCursor(Qt::PointingHandCursor);
    next_->setDefault(true);
    foot->addWidget(back_);
    foot->addWidget(next_);
    outer->addLayout(foot);

    connect(back_, &QPushButton::clicked, this, [this]() { goStep(0); });
    connect(next_, &QPushButton::clicked, this, [this]() {
        if (step_ == 0) { goStep(1); return; }
        Prefs::setOnboardingDone(true);
        emit finished();
    });
    goStep(0);
}

void OnboardingPage::restart() {
    // повторный проход: текущий выбор уже отмечен
    variant_ = Theme::current().variant;
    anim_ = Theme::current().anim;
    goStep(0);
}

void OnboardingPage::goStep(int i) {
    step_ = i;
    Anim::transition(steps_, [this, i]() { steps_->setCurrentIndex(i); });
    if (i == 0) {
        stepTitle_->setText(T("Выбери оформление"));
        stepNote_->setText(T("Три стиля интерфейса — у каждого свои цвета, шрифты и форма. Нажми на понравившийся: окно сразу примерит его."));
    } else {
        stepTitle_->setText(T("Выбери анимации"));
        stepNote_->setText(T("Как появляются экраны, карточки и сообщения. «Без анимаций» — если ПК слабый или так спокойнее."));
    }
    refresh();
}

void OnboardingPage::chooseVariant(const QString& id) {
    variant_ = id;
    Theme::Appearance a = Theme::variantPreset(id);
    a.anim = anim_.isEmpty() ? Theme::current().anim : anim_;
    Theme::apply(*qApp, a);
    Theme::saveAppearance(a);
    emit appearanceChanged();
    refresh();
}

void OnboardingPage::chooseAnim(const QString& id) {
    anim_ = id;
    Theme::Appearance a = Theme::current();
    a.anim = id;
    Theme::apply(*qApp, a);
    Theme::saveAppearance(a);
    refresh();
}

void OnboardingPage::refresh() {
    logo_->setPixmap(AqIcons::logo(52, devicePixelRatioF()));
    for (int i = 0; i < dots_.size(); ++i) {
        dots_[i]->setProperty("on", i == step_);
        Theme::repolish(dots_[i]);
    }
    for (QPushButton* b : variantCards_) b->setChecked(b->property("variant").toString() == variant_);
    for (QPushButton* b : animCards_) b->setChecked(b->property("anim").toString() == anim_);
    for (int i = 0; i < previews_.size(); ++i) {
        // образец — в цветах стиля, но со светлой/тёмной темой как сейчас
        Theme::Appearance a = Theme::variantPreset(Theme::variants().value(i));
        previews_[i]->setAppearance(a);
    }
    back_->setVisible(step_ > 0);
    next_->setText(step_ == 0 ? T("Дальше") : T("Готово — выбрать игру"));
    next_->setEnabled(step_ == 0 ? !variant_.isEmpty() : !anim_.isEmpty());
    next_->setIcon(AqIcons::ui(step_ == 0 ? "chevron-right" : "check", Theme::t().onRed, 14, 2.4));
}
