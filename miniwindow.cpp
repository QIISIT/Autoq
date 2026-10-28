#include "miniwindow.h"
#include "i18n.h"
#include "widgets.h"
#include "icons.h"
#include "theme.h"
#include "appinfo.h"
#include "keys.h"
#include "keypicker.h"
#include "animations.h"
#include "games.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QToolButton>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QFrame>
#include <QSignalBlocker>
#include <QShowEvent>
#ifdef __APPLE__
#include "macsupport.h"
#endif

static QToolButton* iconBtn(const QString& tip, int size = 30) {
    auto* b = new QToolButton();
    b->setObjectName("icon");
    b->setFixedSize(size, size);
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// Обычное окно (не «инструмент»): у него есть «Свернуть» в заголовке на любой ОС
// и кнопка на панели задач — свёрнутое окно всегда можно вернуть
MiniWindow::MiniWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                      Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint) {
    setObjectName("miniRoot");
    setAttribute(Qt::WA_StyledBackground);
    setWindowTitle(T("AutoQ — мини"));
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);   // macOS: не прятать, когда активна игра
    setProperty("toastBottom", 14);
    setFixedWidth(364);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 14, 18, 12);
    lay->setSpacing(10);

    // --- статус, оформление, «развернуть»
    auto* top = new QHBoxLayout();
    top->setSpacing(8);
    pill_ = new QFrame();
    pill_->setObjectName("pill");
    auto* pillLay = new QHBoxLayout(pill_);
    pillLay->setContentsMargins(10, 0, 12, 0);
    pillLay->setSpacing(7);
    dot_ = new StatusDot();
    pillText_ = new QLabel(T("Остановлен"));
    pillLay->addWidget(dot_);
    pillLay->addWidget(pillText_);
    pill_->setFixedHeight(28);
    top->addWidget(pill_);
    top->addStretch(1);
    overlayBtn_ = iconBtn(T("Оверлей КПС поверх игры: когда показывать, настройки"));
    overlayBtn_->setCheckable(true);
    overlayBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    top->addWidget(overlayBtn_);
    settingsBtn_ = iconBtn(T("Настройки AutoQ"));
    top->addWidget(settingsBtn_);
    collapseBtn_ = iconBtn(T("Свернуть: оставить только статус, счётчик и Старт"));
    top->addWidget(collapseBtn_);
    expandBtn_ = iconBtn(T("Открыть большое окно"));
    top->addWidget(expandBtn_);
    lay->addLayout(top);

    // --- игра
    gameBox_ = new QComboBox();
    gameBox_->setIconSize(QSize(22, 22));
    gameBox_->setMaxVisibleItems(12);
    gameBox_->setToolTip(T("Игра"));
    gameBox_->setAccessibleName(T("Игра"));
    lay->addWidget(gameBox_);
    full_ << gameBox_;

    // --- счётчик
    auto* cnt = new QVBoxLayout();
    cnt->setSpacing(0);
    counter_ = mkLabel("0", "miniCounter");
    caption_ = mkLabel(T("нажатий за сессию"), "muted");
    cnt->addWidget(counter_);
    cnt->addWidget(caption_);
    lay->addLayout(cnt);

    // --- действия, режим, скорость
    auto* summary = new QFrame();
    summary->setObjectName("summary");
    auto* sl = new QVBoxLayout(summary);
    sl->setContentsMargins(12, 10, 12, 10);
    sl->setSpacing(8);
    auto* acts = new QHBoxLayout();
    acts->setSpacing(8);
    act1_ = new KeyPicker();
    act1_->setObjectName("keyPickMini");
    act1_->setKeys(KeyPicker::actionItems(false), KeyPicker::actionCommon());
    act1_->setToolTip(T("Действие 1 — кнопка мыши или клавиша"));
    act1_->setAccessibleName(T("Действие 1"));
    sep_ = new QLabel("→");
    act2_ = new KeyPicker();
    act2_->setObjectName("keyPickMini");
    act2_->setKeys(KeyPicker::actionItems(true), QStringList{"none"} + KeyPicker::actionCommon());
    act2_->setToolTip(T("Действие 2 («—» — без второго действия)"));
    act2_->setAccessibleName(T("Действие 2"));
    moreActs_ = new QLabel();
    moreActs_->setObjectName("small");
    moreActs_->setToolTip(T("Остальные действия — в большом окне"));
    acts->addWidget(act1_);
    acts->addWidget(sep_);
    acts->addWidget(act2_);
    acts->addWidget(moreActs_);
    acts->addStretch(1);
    interval_ = new QSpinBox();
    interval_->setRange(5, 60000);
    interval_->setSuffix(T(" мс"));
    interval_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    interval_->setAlignment(Qt::AlignCenter);
    interval_->setAccelerated(true);
    interval_->setFixedWidth(84);
    interval_->setToolTip(T("Интервал: пауза между нажатиями (колёсико мыши — быстро менять)"));
    interval_->setAccessibleName(T("Интервал"));
    acts->addWidget(interval_);
    sl->addLayout(acts);
    modeSeg_ = new Segmented({T("По очереди"), T("Вместе"), T("Зажать"), T("Шаги")}, false);
    modeSeg_->setToolTip(T("По очереди — 1, затем 2.\nВместе — нажать обе сразу (F3+A).\nЗажать — держать нажатыми, пока кликер включён.\nШаги — отдельно «нажать ↓» и «отпустить ↑» (шаги меняются в большом окне)."));
    sl->addWidget(modeSeg_);
    singleSeg_ = new Segmented({T("Повторять"), T("Зажать"), T("Шаги")}, false);
    singleSeg_->setToolTip(T("Одно действие: повторять с интервалом или держать нажатым.\nШаги — отдельно «нажать ↓» и «отпустить ↑».\nВыбери второе действие — появятся «По очереди» и «Вместе»."));
    sl->addWidget(singleSeg_);
    lay->addWidget(summary);
    full_ << summary;

    // --- профиль и клавиша старт/стоп
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    grid->addWidget(mkLabel(T("Профиль"), "muted"), 0, 0);
    profileBox_ = new QComboBox();
    profileBox_->setAccessibleName(T("Профиль"));
    profileBox_->setToolTip(T("Профиль — выбери, и он сразу загрузится"));
    auto* profRow = new QHBoxLayout();
    profRow->setSpacing(6);
    profRow->addWidget(profileBox_, 1);
    saveProfileBtn_ = iconBtn(T("Сохранить настройки в профиль"));
    resetProfileBtn_ = iconBtn(T("Сбросить: вернуть сохранённое или стандартные настройки"));
    profRow->addWidget(saveProfileBtn_);
    profRow->addWidget(resetProfileBtn_);
    grid->addLayout(profRow, 0, 1);
    grid->setColumnStretch(1, 1);
    auto* gridBox = new QWidget();
    gridBox->setLayout(grid);
    lay->addWidget(gridBox);
    full_ << gridBox;

    // --- окно игры
    auto* win = new QHBoxLayout();
    win->setSpacing(8);
    winIcon_ = new QLabel();
    winText_ = mkLabel(QString(), "muted");
    winText_->setWordWrap(true);
    winProc_ = mkLabel(QString(), "small");
    win->addWidget(winIcon_, 0, Qt::AlignTop);
    win->addWidget(winText_, 1);
    win->addWidget(winProc_, 0, Qt::AlignTop);
    auto* winBox = new QWidget();
    winBox->setLayout(win);
    lay->addWidget(winBox);
    full_ << winBox;
    captureBtn_ = new QPushButton();
    captureBtn_->setObjectName("outline");
    captureBtn_->setMinimumHeight(36);
    captureBtn_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    captureBtn_->setToolTip(T("Нажми и переключись в игру — AutoQ выберет её окно и будет кликать только в нём"));
    countdown_ = new CaptureCountdown(captureBtn_, this);
    countdown_->setIdleText(T("Выбрать окно игры (3 сек)"));
    lay->addWidget(captureBtn_);
    full_ << captureBtn_;

    lay->addSpacing(2);
    startBtn_ = new StartButton();
    startBtn_->setMinimumWidth(0);
    startBtn_->setFixedHeight(52);
    lay->addWidget(startBtn_);
    footer_ = mkLabel(QString(), "footer");
    footer_->setAlignment(Qt::AlignCenter);
    lay->addWidget(footer_);

    // --- сигналы
    connect(startBtn_, &QPushButton::clicked, this, &MiniWindow::toggleRequested);
    connect(expandBtn_, &QToolButton::clicked, this, &MiniWindow::expandRequested);
    connect(settingsBtn_, &QToolButton::clicked, this, &MiniWindow::settingsRequested);
    connect(collapseBtn_, &QToolButton::clicked, this, [this]() {
        setCompact(!compact_);
        auto s = AppSettings::open();
        s->setValue("miniCompact", compact_);
    });
    connect(overlayBtn_, &QToolButton::clicked, this, [this]() { emit overlayMenuRequested(overlayBtn_); });
    connect(overlayBtn_, &QToolButton::customContextMenuRequested, this, &MiniWindow::overlaySettingsRequested);
    connect(gameBox_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        emit gameChosen(gameBox_->itemData(i).toString());
    });
    connect(act1_, &KeyPicker::picked, this, [this](const QString& n) { emitAction(1, n); });
    connect(act2_, &KeyPicker::picked, this, [this](const QString& n) { emitAction(2, n); });
    connect(interval_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MiniWindow::intervalChosen);
    connect(modeSeg_, &Segmented::changed, this, &MiniWindow::modeChosen);
    connect(singleSeg_, &Segmented::changed, this, [this](int i) {
        if (i == 1) emit modeChosen(static_cast<int>(GameMode::Hold));
        else if (i == 2) emit modeChosen(static_cast<int>(GameMode::Steps));
        else if (mode_ == GameMode::Hold || mode_ == GameMode::Steps) emit modeChosen(static_cast<int>(GameMode::Sequence));
    });
    connect(profileBox_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        emit profileChosen(profileBox_->itemData(i).toString());
    });
    connect(saveProfileBtn_, &QToolButton::clicked, this, &MiniWindow::profileSaveRequested);
    connect(resetProfileBtn_, &QToolButton::clicked, this, [this]() { emit profileResetRequested(resetProfileBtn_); });
    connect(captureBtn_, &QPushButton::clicked, this, [this]() {
        if (countdown_->active()) { countdown_->cancel(); return; }
        startCapture();
    });
    connect(countdown_, &CaptureCountdown::finished, this, &MiniWindow::captureFinished);
    connect(countdown_, &CaptureCountdown::cancelled, this, &MiniWindow::captureCancelled);

    {
        auto s = AppSettings::open();
        compact_ = s->value("miniCompact", false).toBool();
    }
    refreshTheme();
    setCompact(compact_);
    applyOnTop();
}

void MiniWindow::setCompact(bool on) {
    compact_ = on;
    for (QWidget* w : full_) w->setVisible(!on);
    caption_->setVisible(!on);
    collapseBtn_->setToolTip(on ? T("Развернуть мини-окно: игра, действия, профиль, окно игры")
                                : T("Свернуть: оставить только статус, счётчик и Старт"));
    collapseBtn_->setIcon(AqIcons::ui(on ? "chevron" : "minus", Theme::t().muted, 15, 2.2));
    adjustSize();
    // высота по содержимому: свёрнутое окно не остаётся большим
    resize(width(), sizeHint().height());
}

void MiniWindow::applyOnTop() {
    const bool on = Prefs::miniOnTop();
    if (bool(windowFlags() & Qt::WindowStaysOnTopHint) == on) return;
    const bool vis = isVisible();
    const QPoint pos = this->pos();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (vis) { move(pos); show(); }
}

void MiniWindow::toast(const QString& text) { Toast::show(this, text); }

void MiniWindow::emitAction(int slot, const QString& name) {
    if (name == "none") { emit actionChosen(slot, false, false, 1, QString()); return; }
    if (const int b = MouseButtons::fromItemName(name)) emit actionChosen(slot, true, true, b, QString());
    else emit actionChosen(slot, true, false, 1, name);
}

void MiniWindow::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
#ifdef __APPLE__
    // поверх полноэкранной игры и на всех рабочих столах
    if (Prefs::miniOnTop()) AqMac::keepOnTop(static_cast<unsigned long long>(winId()), false);
#endif
}

void MiniWindow::refreshTheme() {
    const auto& t = Theme::t();
    expandBtn_->setIcon(AqIcons::ui("expand", t.muted, 15));
    settingsBtn_->setIcon(AqIcons::ui("settings", t.muted, 15, 1.8));
    collapseBtn_->setIcon(AqIcons::ui(compact_ ? "chevron" : "minus", t.muted, 15, 2.2));
    overlayBtn_->setIcon(AqIcons::ui("gauge", overlayBtn_->isChecked() ? t.redHi : t.muted, 17, 1.9));
    resetProfileBtn_->setIcon(AqIcons::ui("undo", t.muted, 15));
    saveProfileBtn_->setIcon(AqIcons::ui("save", profileDirty_ ? t.redHi : t.muted, 15));
    captureBtn_->setIcon(AqIcons::ui("target", t.redHi, 15));
    footer_->setText(QString("created by <span style=\"color:%1; font-weight:600\">" AQ_AUTHOR "</span>")
                         .arg(t.redHi.name()));
    sep_->setStyleSheet(QString("color: %1; font-weight: 700; font-size: 15px; background: transparent;")
                            .arg(t.redHi.name()));
    startBtn_->refreshIcon();
    setWindowStatus(winState_, winText_->text(), winProc_->text());
    setRunning(running_, hotkeyDisplay_, pending_, unused_);
    dot_->update();
}

void MiniWindow::setGames(const QList<GameItem>& games, const QString& currentId) {
    QSignalBlocker b(gameBox_);
    gameBox_->clear();
    for (const GameItem& g : games) gameBox_->addItem(QIcon(g.tile), g.name, g.id);
    const int i = gameBox_->findData(currentId);
    gameBox_->setCurrentIndex(i >= 0 ? i : 0);
}

void MiniWindow::setState(const GameState& st, GameMode mode, int minInterval, bool frames) {
    mode_ = mode;
    const bool steps = mode == GameMode::Steps;
    // шаги и группы здесь не правятся — коротко, что жмётся (править — в большом окне)
    const bool fixed = steps || st.actions.value(0).isGroup() || (st.multi() && st.actions.value(1).isGroup());
    if (fixed) {
        const QList<ActionStep> list = st.chain(mode);
        QStringList parts;
        for (int i = 0; i < list.size(); ++i) {
            if (parts.size() == 3) { parts << QString("+%1").arg(list.size() - i); break; }
            const ActionStep& a = list[i];
            const ActionGroup* g = a.isGroup() ? Groups::find(st.groups, a.group) : nullptr;
            parts << (a.isGroup() ? QStringLiteral("▣ ") + (g ? g->name : QStringLiteral("?"))
                                  : a.label() + (steps ? a.pressMark() : QString()));
        }
        moreActs_->setText(parts.join(steps ? QStringLiteral(" ") : QStringLiteral(" · ")));
        moreActs_->setToolTip(steps ? T("Шаги меняются в большом окне") : T("Действия с группами меняются в большом окне"));
    } else {
        act1_->setCurrentItem(st.actions.value(0).itemName());
        act2_->setCurrentItem(st.multi() ? st.actions.value(1).itemName() : QString("none"));
        moreActs_->setText(st.actions.size() > 2 ? QString("+%1").arg(st.actions.size() - 2) : QString());
        moreActs_->setToolTip(T("Остальные действия — в большом окне"));
    }
    act1_->setVisible(!fixed);
    act2_->setVisible(!fixed);
    moreActs_->setVisible(fixed || st.actions.size() > 2);
    {
        QSignalBlocker b(interval_);
        interval_->setMinimum(minInterval);
        interval_->setSuffix(frames ? T(" кадр.") : T(" мс"));
        interval_->setValue(st.intervalMs);
    }
    interval_->setEnabled(mode != GameMode::Hold);
    {
        QSignalBlocker b(modeSeg_);
        modeSeg_->setCurrent(static_cast<int>(mode));
        QSignalBlocker b2(singleSeg_);
        singleSeg_->setCurrent(mode == GameMode::Hold ? 1 : steps ? 2 : 0);
    }
    modeSeg_->setVisible(st.multi() || steps);
    singleSeg_->setVisible(!st.multi() && !steps);
    sep_->setText(mode == GameMode::Sequence ? "→" : "+");
    sep_->setVisible(st.multi() && !steps);
}

void MiniWindow::setProfiles(const QStringList& names, const QString& active, bool dirty) {
    {
        QSignalBlocker b(profileBox_);
        profileBox_->clear();
        profileBox_->addItem(T("без профиля"), QString());
        for (const QString& n : names) profileBox_->addItem(dirty && n == active ? n + " •" : n, n);
        const int i = profileBox_->findData(active);
        profileBox_->setCurrentIndex(i >= 0 ? i : 0);
    }
    profileDirty_ = dirty;
    saveProfileBtn_->setIcon(AqIcons::ui("save", dirty ? Theme::t().redHi : Theme::t().muted, 15));
    saveProfileBtn_->setToolTip(active.isEmpty() ? T("Сохранить настройки как новый профиль")
                                : dirty ? T("Сохранить изменения в «%1»").arg(active)
                                        : T("Профиль «%1» сохранён").arg(active));
}

void MiniWindow::setRunning(bool running, const QString& hotkey, bool pending, bool unused) {
    running_ = running;
    pending_ = pending;
    unused_ = unused;
    if (!hotkey.isEmpty()) hotkeyDisplay_ = hotkey;
    startBtn_->setState(running, hotkeyDisplay_, pending, unused);
    pillText_->setText(pending ? T("Выбери окно") : running ? T("Работает") : unused ? T("Не используется") : T("Остановлен"));
    pill_->setProperty("state", running || pending ? "run" : "idle");
    Theme::repolish(pill_);
    pillText_->setStyleSheet(QString("color: %1; font-weight: 600; font-size: 12px; background: transparent;")
                                 .arg(running || pending ? Theme::t().redSoft.name() : Theme::t().muted.name()));
    dot_->setActive(running);
}

void MiniWindow::setCount(const QString& count, const QString& caption) {
    counter_->setText(count);
    caption_->setText(caption);
}

void MiniWindow::setWindowStatus(int state, const QString& text, const QString& process) {
    winState_ = state;
    const auto& t = Theme::t();
    const qreal dpr = devicePixelRatioF();
    if (state == 1) winIcon_->setPixmap(AqIcons::uiPixmap("check", t.ok, 15, dpr, 2.8));
    else if (state == 2) winIcon_->setPixmap(AqIcons::uiPixmap("ring", t.warn, 15, dpr, 2.4));
    else if (state == 3) winIcon_->setPixmap(AqIcons::uiPixmap("dot", t.redHi, 15, dpr));
    else winIcon_->setPixmap(AqIcons::uiPixmap("ring", t.faint, 15, dpr, 2.0));
    if (winText_->text() != text) winText_->setText(text);
    winProc_->setText(QFontMetrics(winProc_->font()).elidedText(process, Qt::ElideMiddle, 120));
    winProc_->setToolTip(process);
}

void MiniWindow::setCaptureSeconds(int sec) {
    captureSec_ = sec;
    countdown_->setIdleText(T("Выбрать окно игры (%1 сек)").arg(sec));
}

void MiniWindow::startCapture() {
    if (countdown_->active()) return;
    countdown_->start(captureSec_);
    emit captureStarted();
}

void MiniWindow::cancelCapture() { countdown_->cancel(); }
bool MiniWindow::capturing() const { return countdown_->active(); }

void MiniWindow::setOverlayOn(bool on) {
    QSignalBlocker b(overlayBtn_);
    overlayBtn_->setChecked(on);
    overlayBtn_->setIcon(AqIcons::ui("gauge", on ? Theme::t().redHi : Theme::t().muted, 17, 1.9));
}
