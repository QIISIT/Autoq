#include "mainwindow.h"
#include "board.h"
#include "cardlayout.h"
#include "i18n.h"
#include "appinfo.h"
#include "autostart.h"
#include "keycapturebutton.h"
#include "keypicker.h"
#include "windowpicker.h"
#include "gameeditdialog.h"
#include "settingsdialog.h"
#include "hotkeydialog.h"
#include "miniwindow.h"
#include "overlay.h"
#include "widgets.h"
#include "actionseditor.h"
#include "onboarding.h"
#include "animations.h"
#include "icons.h"
#include "theme.h"
#include "sounds.h"
#include "framesync.h"
#include <QWidget>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QMessageBox>
#include <QDialog>
#include <QLineEdit>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QBoxLayout>
#include <QApplication>
#include <QTimer>
#include <QScrollArea>
#include <QScreen>
#include <QSlider>
#include <QListWidget>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QDesktopServices>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QtMath>

static const char* kAppTitle = AQ_NAME;

static QString num(qint64 n) {
    QString s = QString::number(n);
    // 1 248 (неразрывный пробел) или 1,248 по-английски
    for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, I18n::english() ? QChar(',') : QChar(0x00A0));
    return s;
}

static QString dec(double v) {
    return I18n::locale().toString(v, 'f', v < 10 ? 1 : 0);
}

static qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

static QString elide(const QString& s, int n = 42) {
    return s.size() > n ? s.left(n - 1) + "…" : s;
}

static QLabel* mkLabel(const QString& text, const char* obj) {
    auto* l = new QLabel(text);
    l->setObjectName(obj);
    return l;
}

static QSpinBox* mkSpin(int lo, int hi, const char* obj = nullptr) {
    auto* s = new QSpinBox();
    s->setRange(lo, hi);
    s->setSuffix(T(" мс"));
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(Qt::AlignCenter);
    s->setAccelerated(true);
    if (obj) s->setObjectName(obj);
    return s;
}

// single — у игры одно действие: тогда «по очереди» и «вместе» — одно и то же (повтор)
static QString modeSubtitle(GameMode m, bool single = false) {
    if (m == GameMode::Steps) return T("по шагам");
    if (single) return m == GameMode::Hold ? T("зажать") : T("повтор");
    switch (m) {
        case GameMode::Combo: return T("вместе");
        case GameMode::Hold:  return T("зажать");
        default:              return T("по очереди");
    }
}

static QString presetsWord(int n) {
    if (I18n::english()) return n == 1 ? QString("1 preset") : QString("%1 presets").arg(n);
    const int a = n % 100, b = n % 10;
    if (a >= 11 && a <= 14) return T("%1 пресетов").arg(n);
    if (b == 1) return T("%1 пресет").arg(n);
    if (b >= 2 && b <= 4) return T("%1 пресета").arg(n);
    return T("%1 пресетов").arg(n);
}

// «каждый кадр», «каждые 2 кадра», «каждые 5 кадров»
static QString everyFrames(int n) {
    if (I18n::english()) return n == 1 ? QString("every frame") : QString("every %1 frames").arg(n);
    const int a = n % 100, b = n % 10;
    if (n == 1) return T("каждый кадр");
    if (a >= 11 && a <= 14) return T("каждые %1 кадров").arg(n);
    if (b == 1) return T("каждый %1 кадр").arg(n);
    if (b >= 2 && b <= 4) return T("каждые %1 кадра").arg(n);
    return T("каждые %1 кадров").arg(n);
}

static bool titleMatches(const QStringList& titles, const QString& title) {
    for (const QString& t : titles)
        if (!t.trimmed().isEmpty() && title.contains(t.trimmed(), Qt::CaseInsensitive)) return true;
    return false;
}

// короткая подпись клавиши — для плашки на значке игры
static QString chipText(const HotkeySpec& sp) {
    if (sp.isMouse()) {
        const int b = sp.mouseButton();
        return b == 8 ? QStringLiteral("M4") : b == 9 ? QStringLiteral("M5") : T("СКМ");
    }
    return sp.display();
}

static bool ownWindow(const WindowInfo& w) {
    return w.processKnown && processMatches({ownProcessName()}, w);
}

// ===========================================================================
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(AQ_NAME);
    qApp->setWindowIcon(AqIcons::appIcon());
    probe_ = std::make_unique<ActiveWindowProbe>();

    buildUi();
    buildTray();
    mini_ = new MiniWindow();

    loading_ = true;
    timeSeg_->setSeconds(Prefs::captureSeconds());
    loading_ = false;
    mini_->setCaptureSeconds(Prefs::captureSeconds());

    // оверлей КПС: берёт счётчики кликера и подписи действий
    overlay_ = new OverlayWindow([this]() { return overlayData(); });
    overlay_->setSettings(OverlaySettings::load());

    connectSignals();
    connectMini();
    {
        // боковая панель — общая для всех игр (редактор интерфейса)
        const int side = qBound(0, AppSettings::open()->value("sidebarMode", 0).toInt(), 3);
        board_->setSidebarMode(side);
        applySidebarMode(side);
    }
    reloadGames(Games::currentId());
    refreshIcons();
    syncOverlayUi();

    liveTimer_ = new QTimer(this);
    connect(liveTimer_, &QTimer::timeout, this, &MainWindow::refreshLive);
    liveTimer_->start(500);
    // подсветка шага, который кликер жмёт сейчас, — чаще остального «живого»
    stepTimer_ = new QTimer(this);
    stepTimer_->setInterval(50);
    connect(stepTimer_, &QTimer::timeout, this, [this]() {
        const bool run = hasGame() && clk().isRunning();
        actionsEdit_->setActiveStep(run ? clk().currentStep() : -1);
        updateMonitor(false);
        if (!run) stepTimer_->stop();
    });
    refreshLive();

    // первый запуск — сначала выбор оформления и анимаций
    if (!Prefs::onboardingDone()) rootStack_->setCurrentWidget(onboarding_);   // ничего не выбрано заранее
}

MainWindow::~MainWindow() {
    for (auto& r : runners_) r.second->clicker.stop();
    delete overlay_;
    delete mini_;
}

void MainWindow::attachHotkey(GlobalHotkey* hk) {
    hotkey_ = hk;
    rebuildBindings();
}

// ===========================================================================
// Интерфейс
// ===========================================================================
void MainWindow::buildUi() {
    rootStack_ = new QStackedWidget(this);
    rootStack_->setProperty("toastBottom", 50);       // сообщения — над строкой состояния
    auto* root = new QWidget();
    root->setObjectName("root");
    root_ = root;
    auto* rootLay = new QVBoxLayout(root);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    auto* body = new QHBoxLayout();
    bodyLay_ = body;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(buildSidebar());

    inner_ = new QWidget();
    inner_->setObjectName("mainInner");
    auto* lay = new QVBoxLayout(inner_);
    lay->setContentsMargins(24, 20, 24, 20);
    lay->setSpacing(14);
    // карточки — из блоков; раскладка у каждой игры своя (редактор интерфейса)
    board_ = new Board();
    buildHeader();
    buildActionsCard();
    buildSpeedCard();
    buildWindowCard();
    buildProfileCard();
    lay->addWidget(board_, 1);

    // на низких экранах — прокрутка
    auto* scroll = new QScrollArea();
    mainScroll_ = scroll;
    scroll->setObjectName("mainScroll");
    scroll->setWidget(inner_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);   // если окно совсем узкое

    // 0 — «добавь первую игру», 1 — настройки выбранной игры
    pages_ = new QStackedWidget();
    pages_->addWidget(buildWelcome());
    // полоса режима редактирования — над прокруткой, всегда на виду
    auto* gamePage = new QWidget();
    auto* gpl = new QVBoxLayout(gamePage);
    gpl->setContentsMargins(0, 0, 0, 0);
    gpl->setSpacing(0);
    auto* barWrap = new QWidget();
    auto* bwl = new QVBoxLayout(barWrap);
    bwl->setContentsMargins(24, 14, 24, 0);
    bwl->addWidget(board_->editBar());
    barWrap->setVisible(false);
    connect(board_, &Board::editModeChanged, barWrap, &QWidget::setVisible);
    gpl->addWidget(barWrap);
    gpl->addWidget(scroll, 1);
    pages_->addWidget(gamePage);
    board_->setAnimArea(pages_);
    board_->setSideAnimArea(root);              // боковая панель меняется — переход по всему окну
    body->addWidget(pages_, 1);

    rootLay->addLayout(body, 1);
    rootLay->addWidget(buildStatusBar());

    onboarding_ = new OnboardingPage();
    rootStack_->addWidget(root);
    rootStack_->addWidget(onboarding_);
    setCentralWidget(rootStack_);
    setMinimumSize(900, 540);
}

QWidget* MainWindow::buildSidebar() {
    const qreal dpr = devicePixelRatioF();
    auto* side = new QFrame();
    sidebar_ = side;
    side->setObjectName("sidebar");
    side->setFixedWidth(236);
    auto* lay = new QVBoxLayout(side);
    lay->setProperty("aqManual", true);        // поля и отступы ставит applySidebarMode (с учётом плотности)
    lay->setContentsMargins(12, 14, 12, 14);
    lay->setSpacing(4);

    auto* brand = new QBoxLayout(QBoxLayout::LeftToRight);
    brandLay_ = brand;
    brand->setProperty("aqManual", true);
    brand->setContentsMargins(8, 0, 2, 12);
    brand->setSpacing(9);
    brandLogo_ = new QLabel();
    brandLogo_->setPixmap(AqIcons::logo(26, dpr));
    auto* name = mkLabel(QString(), "brand");
    name->setProperty("brandName", true);
    auto* by = mkLabel("by " AQ_AUTHOR, "brandBy");
    brand->addWidget(brandLogo_);
    // «by Qisit» — под названием
    brandNames_ = new QWidget();
    auto* names = new QVBoxLayout(brandNames_);
    names->setContentsMargins(0, 0, 0, 0);
    names->setSpacing(0);
    names->addWidget(name);
    names->addWidget(by);
    brand->addWidget(brandNames_);
    brand->addStretch(1);
    // редактор интерфейса — рядом с настройками: он про всё окно, а не про игру
    layoutBtn_ = new QToolButton();
    layoutBtn_->setObjectName("icon");
    layoutBtn_->setFixedSize(32, 32);
    layoutBtn_->setCheckable(true);
    layoutBtn_->setCursor(Qt::PointingHandCursor);
    layoutBtn_->setToolTip(T("Редактор интерфейса: карточки, блоки, размеры, цвета, шаблоны"));
    layoutBtn_->setAccessibleName(T("Редактор интерфейса"));
    brand->addWidget(layoutBtn_);
    settingsBtn_ = new QToolButton();
    settingsBtn_->setObjectName("icon");
    settingsBtn_->setFixedSize(32, 32);
    settingsBtn_->setToolTip(T("Настройки: основные, оформление, оверлей КПС"));
    settingsBtn_->setAccessibleName(T("Настройки"));
    brand->addWidget(settingsBtn_);
    lay->addLayout(brand);

    sideSection_ = new QWidget();
    auto* sec = new QHBoxLayout(sideSection_);
    sec->setContentsMargins(8, 2, 8, 6);
    sec->addWidget(mkLabel(T("МОИ ИГРЫ"), "section"));
    sec->addStretch(1);
    gameCount_ = mkLabel("0", "sectionCount");
    sec->addWidget(gameCount_);
    lay->addWidget(sideSection_);

    gameList_ = new QListWidget();
    gameList_->setObjectName("gameList");
    gameDelegate_ = new GameListDelegate(gameList_);
    gameList_->setItemDelegate(gameDelegate_);
    gameList_->setMouseTracking(true);
    gameList_->setSpacing(1);
    gameList_->setFrameShape(QFrame::NoFrame);
    gameList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    gameList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gameList_->setAccessibleName(T("Мои игры"));
    gameList_->setContextMenuPolicy(Qt::CustomContextMenu);
    gameList_->setToolTip(T("Кружок справа — «используется»: закрашен — категорию можно запустить (кнопкой, клавишей, из трея), даже когда открыта другая. Пустой — запустить нельзя никак.\nПравая кнопка мыши — запустить, клавиши, изменить, удалить."));
    gameList_->viewport()->installEventFilter(this);
    lay->addWidget(gameList_, 1);
    emptyHint_ = mkLabel(T("Здесь будут твои игры.\nДобавь первую — кнопкой ниже\nили выбери из готовых."), "faint");
    emptyHint_->setAlignment(Qt::AlignCenter);
    emptyHint_->setWordWrap(true);
    emptyHint_->hide();
    lay->addWidget(emptyHint_);
    lay->addStretch(0);

    addGameBtn_ = new QPushButton(T("Добавить игру"));
    addGameBtn_->setObjectName("dashed");
    addGameBtn_->setMinimumHeight(42);
    addGameBtn_->setCursor(Qt::PointingHandCursor);
    addGameBtn_->setToolTip(T("Готовые игры (Genshin, Minecraft…) или своя"));
    lay->addWidget(addGameBtn_);
    return side;
}

QWidget* MainWindow::buildWelcome() {
    auto* w = new QWidget();
    w->setObjectName("mainInner");
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(40, 36, 40, 30);
    lay->setSpacing(14);
    auto* head = new QHBoxLayout();
    head->setSpacing(18);
    welcomeLogo_ = new QLabel();
    head->addWidget(welcomeLogo_, 0, Qt::AlignTop);
    auto* col = new QVBoxLayout();
    col->setSpacing(6);
    col->addWidget(mkLabel(T("Добавь первую игру"), "welcomeTitle"));
    auto* sub = mkLabel(T("Выбери из готовых — режим, правила окна и пресеты подставятся сами. Или добавь свою игру: AutoQ возьмёт процесс и заголовок прямо из её окна."), "muted");
    sub->setWordWrap(true);
    col->addWidget(sub);
    head->addLayout(col, 1);
    lay->addLayout(head);
    lay->addSpacing(10);
    lay->addWidget(mkLabel(T("ГОТОВЫЕ ИГРЫ"), "section"));
    welcomeGrid_ = new QGridLayout();
    welcomeGrid_->setSpacing(10);
    for (int c = 0; c < 3; ++c) welcomeGrid_->setColumnStretch(c, 1);
    lay->addLayout(welcomeGrid_);
    lay->addStretch(1);
    return w;
}

void MainWindow::buildHeader() {
    // игра: значок, название, «изменить», «убрать»; занимает всё свободное место в ряду
    BoardBlock* gblk = board_->addBlock("game", T("Игра"));
    gblk->wide = true;
    gblk->stretch = true;
    gblk->rowMin = 300;
    auto* h = new QHBoxLayout();
    h->setSpacing(14);
    headerIcon_ = new QLabel();
    headerIcon_->setFixedSize(56, 56);
    h->addWidget(headerIcon_);
    auto* nameRow = new QHBoxLayout();
    nameRow->setSpacing(8);
    nameLabel_ = new ElideLabel();
    nameLabel_->setObjectName("h1");
    auto iconBtn = [](int size, const QString& tip, const QString& name) {
        auto* b = new QToolButton();
        b->setObjectName("icon");
        b->setFixedSize(size, size);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setAccessibleName(name);
        return b;
    };
    editGameBtn_ = iconBtn(34, T("Изменить игру: значок, режим, процесс, заголовок окна"), T("Изменить игру"));
    delGameBtn_ = iconBtn(34, T("Убрать игру из списка"), T("Убрать игру"));
    // боковая панель скрыта — игры, «добавить» и настройки отсюда
    switchGameBtn_ = iconBtn(34, T("Другая игра, добавить игру, настройки"), T("Другая игра"));
    switchGameBtn_->hide();
    nameRow->addWidget(nameLabel_, 1);
    nameRow->addWidget(switchGameBtn_);
    nameRow->addWidget(editGameBtn_);
    nameRow->addWidget(delGameBtn_);
    h->addLayout(nameRow, 1);
    gblk->body()->addLayout(h);

    // счётчик нажатий — в рамке, ярко: видно сразу
    BoardBlock* cblk = board_->addBlock("counter", T("Счётчик нажатий"));
    counterBox_ = new QFrame();
    counterBox_->setObjectName("counterBox");
    counterBox_->setProperty("running", false);
    auto* cnt = new QVBoxLayout(counterBox_);
    cnt->setContentsMargins(14, 6, 14, 6);
    cnt->setSpacing(0);
    counterCaption_ = mkLabel(T("НАЖАТИЙ ЗА СЕССИЮ"), "counterCaption");
    cnt->addWidget(counterCaption_, 0, Qt::AlignRight);
    counterLabel_ = mkLabel("0", "counter");
    cnt->addWidget(counterLabel_, 0, Qt::AlignRight);
    counterRate_ = mkLabel(QString(), "small");
    counterRate_->hide();
    cnt->addWidget(counterRate_, 0, Qt::AlignRight);
    counterBox_->setMinimumWidth(130);
    cblk->body()->addWidget(counterBox_);

    // оверлей и мини-окно
    BoardBlock* tblk = board_->addBlock("tools", T("Оверлей и мини-окно"));
    auto* tools = new QHBoxLayout();
    tools->setSpacing(10);
    overlayBtn_ = iconBtn(40, T("Оверлей КПС поверх игры: когда показывать, настройки"), T("Оверлей КПС"));
    overlayBtn_->setCheckable(true);
    overlayBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    miniBtn_ = iconBtn(40, T("Мини-окно: всё нужное поверх игры"), T("Мини-окно"));
    tools->addWidget(overlayBtn_);
    tools->addWidget(miniBtn_);
    tools->addStretch(1);
    tblk->body()->addLayout(tools);

    BoardBlock* sblk = board_->addBlock("start", T("Кнопка «Старт»"));
    startBtn_ = new StartButton();
    sblk->body()->addWidget(startBtn_);
}

void MainWindow::buildActionsCard() {
    BoardBlock* blk = board_->addBlock("actions", T("Действия"));
    blk->wide = true;
    actionsTitle_ = board_->titleLabel("actions");
    auto* headW = new QWidget();
    auto* head = new QHBoxLayout(headW);
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(10);
    modeSeg_ = new Segmented({T("По очереди"), T("Вместе"), T("Зажать"), T("По шагам")}, false);
    modeSeg_->setToolTip(T("Режим работы:\nПо очереди — действия одно за другим (скип диалогов: ЛКМ, потом F).\nВместе — нажать все сразу, как F3+A.\nЗажать — держать нажатыми, пока кликер включён (копать, бежать).\nПо шагам — каждый шаг нажимает ↓, отпускает ↑ или делает клик ↕ (кикфлипы, отмены)."));
    modeSeg_->button(0)->setAccessibleName(T("Режим: по очереди"));
    modeSeg_->button(1)->setAccessibleName(T("Режим: вместе"));
    modeSeg_->button(2)->setAccessibleName(T("Режим: зажать"));
    modeSeg_->button(3)->setAccessibleName(T("Режим: по шагам"));
    // одно действие — свой выбор: повторять, зажать или расписать по шагам
    singleSeg_ = new Segmented({T("Повторять"), T("Зажать"), T("По шагам")}, false);
    singleSeg_->setToolTip(T("Одно действие:\nПовторять — нажимать снова и снова с выбранным интервалом.\nЗажать — держать нажатым, пока кликер включён.\nПо шагам — отдельно «нажать ↓» и «отпустить ↑», с паузами между шагами.\nДобавь ещё действие — появятся режимы «По очереди» и «Вместе»."));
    singleSeg_->button(0)->setAccessibleName(T("Режим: повторять"));
    singleSeg_->button(1)->setAccessibleName(T("Режим: зажать"));
    singleSeg_->button(2)->setAccessibleName(T("Режим: по шагам"));
    head->addWidget(mkLabel(T("Режим"), "small"));
    head->addWidget(modeSeg_);
    head->addWidget(singleSeg_);
    blk->setHead(headW);
    QVBoxLayout* lay = blk->body();

    actionsEdit_ = new ActionsEditor();
    lay->addWidget(actionsEdit_);
    actionsHint_ = mkLabel(QString(), "faint");
    actionsHint_->setWordWrap(true);
    lay->addWidget(actionsHint_);

    // монитор запуска: сколько идёт, сколько кругов и нажатий, какой шаг сейчас
    // (по умолчанию спрятан — вернуть: редактор → «Скрытые» или шаблон «Монитор»)
    BoardBlock* mblk = board_->addBlock("monitor", T("Монитор запуска"));
    lay = mblk->body();
    auto* top = new QHBoxLayout();
    top->setSpacing(10);
    monTime_ = mkLabel("00:00", "rate");
    monTime_->setToolTip(T("Сколько идёт запуск (после стопа — сколько шёл последний)"));
    monState_ = mkLabel(QString(), "monState");
    top->addWidget(monTime_, 0, Qt::AlignBottom);
    top->addWidget(monState_, 0, Qt::AlignVCenter);
    top->addStretch(1);
    lay->addLayout(top);
    auto* mg = new QGridLayout();
    mg->setHorizontalSpacing(18);
    mg->setVerticalSpacing(0);
    auto stat = [mg](int col, const QString& caption, const QString& tip) {
        auto* c = mkLabel(caption, "small");
        auto* v = mkLabel("0", "monValue");
        c->setToolTip(tip);
        v->setToolTip(tip);
        mg->addWidget(c, 0, col);
        mg->addWidget(v, 1, col);
        return v;
    };
    monCycles_ = stat(0, T("Кругов"), T("Сколько раз цепочка прошла целиком"));
    monPresses_ = stat(1, T("Нажатий"), T("Сколько нажатий за этот запуск"));
    monRate_ = stat(2, T("Сейчас /с"), T("Сколько нажатий в секунду на самом деле (за последние 2 секунды)"));
    mg->setColumnStretch(3, 1);
    lay->addLayout(mg);
    monStep_ = mkLabel(QString(), "muted");
    monStep_->setWordWrap(true);
    lay->addWidget(monStep_);
}

void MainWindow::buildSpeedCard() {
    BoardBlock* blk = board_->addBlock("speed", T("Скорость"));
    unitSeg_ = new Segmented({T("мс"), T("КПС"), T("КПМ")}, false);
    unitSeg_->setToolTip(T("В чём задавать скорость: интервал в миллисекундах, клики в секунду (КПС) или в минуту (КПМ)"));
    blk->setHead(unitSeg_);
    QVBoxLayout* lay = blk->body();
    auto* rate = new QHBoxLayout();
    rate->setSpacing(6);
    rateLabel_ = mkLabel("≈ 0", "rate");
    rateCaption_ = mkLabel(T("раз в секунду"), "muted");
    rateCaption_->setWordWrap(true);
    rate->addWidget(rateLabel_, 0, Qt::AlignBottom);
    rate->addWidget(rateCaption_, 1, Qt::AlignBottom);
    lay->addLayout(rate);
    slider_ = new QSlider(Qt::Horizontal);
    slider_->setRange(0, 1000);
    slider_->setToolTip(T("Медленнее ← → быстрее (щёлкни в любое место полосы)"));
    slider_->setAccessibleName(T("Интервал"));
    lay->addWidget(slider_);

    // точное число (интервал, КПС или КПМ — как выбрано у «Скорости»)
    BoardBlock* iblk = board_->addBlock("interval", T("Интервал — числом"));
    lay = iblk->body();
    auto* r1 = new QHBoxLayout();
    speedCaption_ = mkLabel(T("Интервал"), "muted");
    r1->addWidget(speedCaption_, 1);
    speedBox_ = new QDoubleSpinBox();
    speedBox_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    speedBox_->setAlignment(Qt::AlignCenter);
    speedBox_->setAccelerated(true);
    speedBox_->setFixedWidth(112);
    speedBox_->setToolTip(T("Скорость: пауза между циклами, или сколько раз в секунду / минуту"));
    speedBox_->setAccessibleName(T("Скорость"));
    r1->addWidget(speedBox_);
    lay->addLayout(r1);

    BoardBlock* jblk = board_->addBlock("jitter", T("Разброс"));
    lay = jblk->body();
    auto* r2 = new QHBoxLayout();
    r2->setSpacing(8);
    randomizeSwitch_ = new ToggleSwitch();
    randomizeSwitch_->setToolTip(T("Случайно меняет интервал — нажатия выглядят менее «роботными»"));
    randomizeSwitch_->setAccessibleName(T("Разброс интервала"));
    r2->addWidget(randomizeSwitch_);
    r2->addWidget(mkLabel(T("Разброс ±"), "muted"), 1);
    jitterBox_ = mkSpin(0, 1000);
    jitterBox_->setFixedWidth(80);
    jitterBox_->setAccessibleName(T("Разброс"));
    r2->addWidget(jitterBox_);
    lay->addLayout(r2);

    // в такт с монитором: каждый клик — в свой кадр (отдельный блок)
    BoardBlock* fblk = board_->addBlock("framesync", T("Такт монитора"));
    lay = fblk->body();
    auto* r3 = new QHBoxLayout();
    r3->setSpacing(8);
    frameSyncSwitch_ = new ToggleSwitch();
    frameSyncSwitch_->setToolTip(T("Клики ровно раз в 1, 2, 3… кадра монитора — каждый попадает в свой кадр"));
    frameSyncSwitch_->setAccessibleName(T("В такт с монитором"));
    r3->addWidget(frameSyncSwitch_);
    r3->addWidget(mkLabel(T("В такт с монитором"), "muted"));
    r3->addWidget(new InfoBadge(T("Почему КПС зависит от герцовки"),
        T("Монитор обновляет картинку N раз в секунду: 60 Гц — кадр каждые 16,7 мс, 144 Гц — каждые 6,9 мс. "
          "Многие игры тоже читают нажатия раз за кадр.\n\n"
          "Если кликать чаще, чем идут кадры, несколько кликов попадают в один кадр — игра засчитает один, "
          "остальные пропадут. А клики «не в такт» ложатся неровно: в один кадр два, в соседний ни одного.\n\n"
          "В такте AutoQ жмёт ровно раз в 1, 2, 3… кадра: каждый клик попадает в свой кадр, ничего не теряется, "
          "ритм ровный. Поэтому скорость округляется до целого числа кадров, а самое быстрое — один клик за кадр "
          "(при 144 Гц — 144 в секунду).\n\n"
          "Если в игре ограничен FPS (например, 60), выбери 60 Гц вместо «Авто»: тогда такт — по кадрам игры.")),
        0, Qt::AlignVCenter);
    r3->addStretch(1);
    lay->addLayout(r3);
    auto* r4 = new QHBoxLayout();
    r4->setSpacing(8);
    hzCaption_ = mkLabel(T("Частота кадров"), "muted");
    r4->addWidget(hzCaption_, 1);
    hzBox_ = new QComboBox();
    hzBox_->setFixedWidth(130);
    hzBox_->setToolTip(T("«Авто» — частота монитора, на котором игра. Своё значение — если в игре ограничен FPS."));
    hzBox_->setAccessibleName(T("Частота кадров"));
    r4->addWidget(hzBox_);
    lay->addLayout(r4);
    perMinuteLabel_ = mkLabel(QString(), "faint");
    perMinuteLabel_->setWordWrap(true);
    lay->addWidget(perMinuteLabel_);
    {
        auto s = AppSettings::open();
        unitSeg_->setCurrent(qBound(0, s->value("speedUnit", 0).toInt(), 2));
    }
}

void MainWindow::buildWindowCard() {
    BoardBlock* blk = board_->addBlock("window", T("Окно игры"));
    lockSwitch_ = new ToggleSwitch();
    lockSwitch_->setToolTip(T("Кликать только в выбранном окне игры.\nПока окно не выбрано — кликер не нажимает нигде."));
    lockSwitch_->setAccessibleName(T("Кликать только в окне игры"));
    blk->setHead(lockSwitch_);
    QVBoxLayout* lay = blk->body();

    statusBox_ = mkLabel(QString(), "status");
    statusBox_->setWordWrap(true);
    statusBox_->setProperty("state", "none");
    statusBox_->setMinimumHeight(34);
    lay->addWidget(statusBox_);

    BoardBlock* rblk = board_->addBlock("rules", T("Процесс и заголовок"));
    lay = rblk->body();
    auto* g = new QGridLayout();
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(6);
    g->setColumnStretch(1, 1);
    procEdit_ = new ChipEditor(true, T("процесс"), T("Вписать имя процесса игры (например game.exe или java)"));
    titleEdit_ = new ChipEditor(false, T("заголовок"), T("Вписать часть заголовка окна игры"));
    procEdit_->setPickButton(T("выбрать"), T("Выбрать процесс из открытых окон — вписывать не нужно"));
    titleEdit_->setPickButton(T("выбрать"), T("Выбрать заголовок из открытых окон"));
    g->addWidget(mkLabel(T("процесс"), "small"), 0, 0, Qt::AlignTop);
    g->addWidget(procEdit_, 0, 1);
    g->addWidget(mkLabel(T("заголовок"), "small"), 1, 0, Qt::AlignTop);
    g->addWidget(titleEdit_, 1, 1);
    lay->addLayout(g);

    BoardBlock* cblk = board_->addBlock("capture", T("Выбрать окно"));
    cblk->bottom = true;
    lay = cblk->body();
    lay->addWidget(mkLabel(T("Время на переключение в игру"), "small"));
    timeSeg_ = new TimeChoice();
    timeSeg_->setToolTip(T("Сколько секунд дать себе, чтобы переключиться в игру после «Выбрать окно»"));
    lay->addWidget(timeSeg_);
    checkBtn_ = new QPushButton();
    checkBtn_->setObjectName("outline");
    checkBtn_->setMinimumHeight(38);
    checkBtn_->setCursor(Qt::PointingHandCursor);
    checkBtn_->setToolTip(T("Нажми и переключись в игру — AutoQ выберет её окно и будет кликать только в нём"));
    checkBtn_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    countdown_ = new CaptureCountdown(checkBtn_, this);
    countdown_->setIdleText(T("Выбрать окно (%1 сек)").arg(Prefs::captureSeconds()));
    lay->addWidget(checkBtn_);
}

void MainWindow::buildProfileCard() {
    BoardBlock* blk = board_->addBlock("profile", T("Профиль"));
    QVBoxLayout* lay = blk->body();
    auto* prow = new QHBoxLayout();
    prow->setSpacing(6);
    profileBox_ = new QComboBox();
    profileBox_->setToolTip(T("Профиль — выбери, и он сразу загрузится"));
    profileBox_->setAccessibleName(T("Профиль"));
    prow->addWidget(profileBox_, 1);
    profileKeyBtn_ = new QToolButton();
    profileKeyBtn_->setObjectName("icon");
    profileKeyBtn_->setMinimumSize(30, 30);
    profileKeyBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    profileKeyBtn_->setAccessibleName(T("Клавиша профиля"));
    prow->addWidget(profileKeyBtn_);
    renameProfileBtn_ = new QToolButton();
    renameProfileBtn_->setObjectName("icon");
    renameProfileBtn_->setFixedSize(30, 30);
    renameProfileBtn_->setToolTip(T("Переименовать профиль"));
    renameProfileBtn_->setAccessibleName(T("Переименовать профиль"));
    prow->addWidget(renameProfileBtn_);
    profileOverlayBtn_ = new QToolButton();
    profileOverlayBtn_->setObjectName("icon");
    profileOverlayBtn_->setFixedSize(30, 30);
    profileOverlayBtn_->setCheckable(true);
    profileOverlayBtn_->setAccessibleName(T("Показывать в оверлее КПС"));
    prow->addWidget(profileOverlayBtn_);
    // ⋯ — профиль в файл и обратно: поделиться с другом, перенести на другой ПК
    profileMoreBtn_ = new QToolButton();
    profileMoreBtn_->setObjectName("icon");
    profileMoreBtn_->setFixedSize(30, 30);
    profileMoreBtn_->setPopupMode(QToolButton::InstantPopup);
    profileMoreBtn_->setToolTip(T("Профиль в файл и из файла"));
    profileMoreBtn_->setAccessibleName(T("Ещё: экспорт и импорт профиля"));
    {
        auto* m = new QMenu(profileMoreBtn_);
        exportProfileAction_ = m->addAction(T("Экспорт профиля в файл…"), this, &MainWindow::exportProfile);
        m->addAction(T("Импорт профиля из файла…"), this, &MainWindow::importProfile);
        m->addSeparator();
        m->addAction(T("Открыть папку профилей"), this, [this]() {
            if (!hasGame()) return;
            const QString dir = Games::profilesFolder(game_.id);
            QDir().mkpath(dir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        profileMoreBtn_->setMenu(m);
    }
    prow->addWidget(profileMoreBtn_);
    lay->addLayout(prow);
    profileState_ = mkLabel(QString(), "profileState");
    profileState_->setWordWrap(true);
    lay->addWidget(profileState_);
    auto* g = new QGridLayout();
    g->setSpacing(6);
    newProfileBtn_ = new QPushButton(T("Новый"));
    newProfileBtn_->setToolTip(T("Создать профиль: с нуля, из текущих настроек или из пресета"));
    saveProfileBtn_ = new QPushButton(T("Сохранить"));
    saveProfileBtn_->setObjectName("accent");
    resetProfileBtn_ = new QPushButton(T("Сбросить"));
    resetProfileBtn_->setToolTip(T("Вернуть сохранённое в профиле или стандартные настройки игры"));
    delProfileBtn_ = new QPushButton(T("Удалить"));
    delProfileBtn_->setObjectName("ghost");
    delProfileBtn_->setToolTip(T("Удалить профиль (настройки игры останутся)"));
    for (QPushButton* b : {newProfileBtn_, saveProfileBtn_, resetProfileBtn_, delProfileBtn_})
        b->setCursor(Qt::PointingHandCursor);
    g->addWidget(newProfileBtn_, 0, 0);
    g->addWidget(saveProfileBtn_, 0, 1);
    g->addWidget(resetProfileBtn_, 1, 0);
    g->addWidget(delProfileBtn_, 1, 1);
    lay->addLayout(g);

    // пресеты — готовые настройки игры
    BoardBlock* pblk = board_->addBlock("preset", T("Пресет"));
    lay = pblk->body();
    auto* presetRow = new QHBoxLayout();
    presetRow->setSpacing(8);
    presetRow->addWidget(mkLabel(T("Пресет"), "muted"));
    presetBtn_ = new QToolButton();
    presetBtn_->setObjectName("chip");
    presetBtn_->setPopupMode(QToolButton::InstantPopup);
    presetBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    presetBtn_->setLayoutDirection(Qt::RightToLeft);    // стрелка справа от текста
    presetBtn_->setToolTip(T("Готовые настройки для этой игры"));
    presetBtn_->setAccessibleName(T("Пресет"));
    presetMenu_ = new QMenu(presetBtn_);
    presetBtn_->setMenu(presetMenu_);
    presetRow->addWidget(presetBtn_);
    presetRow->addStretch(1);
    lay->addLayout(presetRow);

    BoardBlock* hblk = board_->addBlock("hotkey", T("Клавиша старт/стоп"));
    hblk->setDivider(true);
    lay = hblk->body();
    auto* hkCaption = mkLabel(T("Старт / стоп этой игры"), "muted");
    hkCaption->setToolTip(T("У каждой игры своя клавиша. Из списка (с поиском) или своё сочетание — любая буква, цифра, Ctrl+…, колёсико или Mouse 4/5"));
    lay->addWidget(hkCaption);
    auto* hk = new QHBoxLayout();
    hk->setSpacing(8);
    hotkeyPick_ = new KeyPicker();
    hotkeyPick_->setKeys(KeyPicker::hotkeyItems(), Keys::hotkeyPresetNames());
    hotkeyPick_->setAccessibleName(T("Клавиша старт/стоп"));
    hk->addWidget(hotkeyPick_);
    captureBtn_ = new KeyCaptureButton(false);
    captureBtn_->setObjectName("captureOval");
    hk->addWidget(captureBtn_);
    hk->addStretch(1);
    lay->addLayout(hk);
    // кнопка мыши или буква обычно доходят до игры; можно забрать их только под AutoQ
    blockRow_ = new QWidget();
    auto* br = new QHBoxLayout(blockRow_);
    br->setContentsMargins(0, 2, 0, 0);
    br->setSpacing(8);
    blockSwitch_ = new ToggleSwitch();
    blockSwitch_->setToolTip(T("Включено — клавиша только запускает и останавливает кликер: игра, Windows и другие программы её нажатие не получают.\n"
                               "Выключено — нажатие доходит до игры как обычно."));
    blockSwitch_->setAccessibleName(T("Не пропускать нажатие в игру"));
    br->addWidget(blockSwitch_);
    auto* bl = mkLabel(T("Не пропускать нажатие в игру"), "muted");
    bl->setWordWrap(true);
    bl->setToolTip(blockSwitch_->toolTip());
    br->addWidget(bl, 1);
    blockRow_->hide();
    lay->addWidget(blockRow_);
    hotkeyStatus_ = mkLabel(QString(), "hint");
    hotkeyStatus_->setWordWrap(true);
    hotkeyStatus_->hide();
    lay->addWidget(hotkeyStatus_);
    hotkeyInfo_ = mkLabel(QString(), "small");
    hotkeyInfo_->setWordWrap(true);
    hotkeyInfo_->hide();
    lay->addWidget(hotkeyInfo_);

    BoardBlock* kblk = board_->addBlock("keymode", T("Как работает клавиша"));
    lay = kblk->body();
    auto* km = new QHBoxLayout();
    km->setSpacing(8);
    keyModeSeg_ = new Segmented({T("Вкл / выкл"), T("Пока держу")}, false);
    keyModeSeg_->setToolTip(T("Вкл / выкл: первое нажатие включает, второе выключает.\nПока держу: кликер работает, только пока клавиша зажата."));
    km->addWidget(keyModeSeg_, 1);
    lay->addLayout(km);

    // свой звук включения и выключения — у игры и у каждого профиля
    BoardBlock* sblk = board_->addBlock("sounds", T("Звуки старт/стоп"));
    lay = sblk->body();
    auto* sg = new QGridLayout();
    sg->setHorizontalSpacing(8);
    sg->setVerticalSpacing(6);
    sg->setColumnStretch(1, 1);
    auto soundRow = [this, sg](int r, Sounds::Kind kind, const QString& caption) {
        sg->addWidget(mkLabel(caption, "muted"), r, 0);
        auto* box = new SoundBox(kind, true);
        box->setAccessibleName(caption);
        box->setToolTip(T("Свой звук этой игры (сохраняется и в профиле). «Как в настройках» — общий звук."));
        sg->addWidget(box, r, 1);
        auto* play = new QToolButton();
        play->setObjectName("icon");
        play->setFixedSize(30, 30);
        play->setToolTip(T("Послушать"));
        play->setAccessibleName(T("Послушать"));
        sg->addWidget(play, r, 2);
        soundPlayBtns_ << play;
        connect(play, &QToolButton::clicked, this, [this, kind]() {
            Sounds::preview(Sounds::resolve(kind, kind == Sounds::On ? cur_.soundOn : cur_.soundOff));
        });
        connect(box, &SoundBox::soundChosen, this, [this, kind](const QString& id) {
            if (loading_ || !hasGame()) return;
            (kind == Sounds::On ? cur_.soundOn : cur_.soundOff) = id;
            commit();
        });
        return box;
    };
    soundOnBox_ = soundRow(0, Sounds::On, T("Звук вкл"));
    soundOffBox_ = soundRow(1, Sounds::Off, T("Звук выкл"));
    lay->addLayout(sg);
    soundsOffHint_ = mkLabel(T("Звуки выключены: Настройки → Основные → Звук."), "faint");
    soundsOffHint_->setWordWrap(true);
    soundsOffHint_->hide();
    lay->addWidget(soundsOffHint_);

    BoardBlock* ablk = board_->addBlock("armed", T("Используется"));
    lay = ablk->body();
    auto* ar = new QHBoxLayout();
    ar->setSpacing(8);
    armedSwitch_ = new ToggleSwitch();
    armedSwitch_->setAccessibleName(T("Используется: категорию можно запустить"));
    armedSwitch_->setToolTip(T("Включено — категорию можно запустить кнопкой, клавишей или из трея, даже когда в AutoQ открыта другая.\nВыключено — не запускается никак.\nТо же — кружок справа от названия в списке."));
    ar->addWidget(armedSwitch_);
    auto* arl = mkLabel(T("Используется — можно запускать"), "muted");
    arl->setWordWrap(true);
    arl->setToolTip(armedSwitch_->toolTip());
    ar->addWidget(arl, 1);
    lay->addLayout(ar);
}

QWidget* MainWindow::buildStatusBar() {
    auto* bar = new QFrame();
    bar->setObjectName("statusbar");
    bar->setFixedHeight(34);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(16, 0, 16, 0);
    h->setSpacing(8);
    statusDot_ = new StatusDot();
    statusText_ = mkLabel(QString(), "muted");
    footerLabel_ = mkLabel(QString(), "footer");
    h->addWidget(statusDot_);
    h->addWidget(statusText_);
    h->addStretch(1);
    h->addWidget(footerLabel_);
    return bar;
}

void MainWindow::connectSignals() {
    connect(board_, &Board::edited, this, [this]() {
        if (hasGame()) CardLayout::save(game_.id, board_->cardLayout());
        updateMonitor(true);                  // монитор могли только что вернуть из «Скрытых»
        updateSidebarVisible();               // блок «Игра» спрятали при скрытой панели — панель нужна
    });
    connect(board_, &Board::applyToAllRequested, this, &MainWindow::applyLayoutToAll);
    connect(board_, &Board::message, this, &MainWindow::notify);
    connect(board_, &Board::sidebarModeChanged, this, [this](int mode) {
        applySidebarMode(mode);
        AppSettings::open()->setValue("sidebarMode", mode);
    });
    connect(board_, &Board::editModeChanged, this, [this](bool on) {
        layoutBtn_->setChecked(on);
        layoutBtn_->setIcon(AqIcons::ui("layout", on ? Theme::t().redHi : Theme::t().muted, 16));
        updateMonitor(true);
    });
    connect(layoutBtn_, &QToolButton::clicked, this, [this]() {
        layoutBtn_->setChecked(board_->editMode());   // переключает редактор, а не сама
        QTimer::singleShot(0, this, [this]() { setLayoutEditing(!board_->editMode()); });
    });
    connect(switchGameBtn_, &QToolButton::clicked, this, &MainWindow::showSwitchMenu);
    connect(gameList_, &QListWidget::currentRowChanged, this, &MainWindow::onGameRowChanged);
    connect(gameList_, &QListWidget::customContextMenuRequested, this, &MainWindow::showGameMenu);
    connect(addGameBtn_, &QPushButton::clicked, this, &MainWindow::addGame);
    connect(editGameBtn_, &QToolButton::clicked, this, &MainWindow::editGame);
    connect(delGameBtn_, &QToolButton::clicked, this, &MainWindow::removeGame);
    connect(miniBtn_, &QToolButton::clicked, this, &MainWindow::showMini);
    connect(overlayBtn_, &QToolButton::clicked, this, [this]() {
        syncOverlayUi();                 // нажатие не переключает само — выбор в меню
        showOverlayMenu(overlayBtn_);
    });
    connect(overlayBtn_, &QToolButton::customContextMenuRequested, this, &MainWindow::openOverlaySettings);
    connect(settingsBtn_, &QToolButton::clicked, this, &MainWindow::openSettings);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::startPressed);
    connect(onboarding_, &OnboardingPage::appearanceChanged, this, &MainWindow::appearanceChanged);
    connect(onboarding_, &OnboardingPage::finished, this, &MainWindow::finishOnboarding);

    connect(profileBox_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        switchProfile(profileBox_->itemData(i).toString());
    });
    connect(newProfileBtn_, &QPushButton::clicked, this, [this]() { newProfile(1); });
    connect(saveProfileBtn_, &QPushButton::clicked, this, &MainWindow::saveProfile);
    connect(resetProfileBtn_, &QPushButton::clicked, this, [this]() { showResetMenu(resetProfileBtn_); });
    connect(delProfileBtn_, &QPushButton::clicked, this, &MainWindow::deleteProfile);
    connect(renameProfileBtn_, &QToolButton::clicked, this, &MainWindow::renameProfile);
    connect(profileKeyBtn_, &QToolButton::clicked, this, &MainWindow::editProfileKey);
    connect(profileOverlayBtn_, &QToolButton::toggled, this, [this](bool on) {
        if (loading_ || !hasGame()) return;
        const QString active = Games::activeProfile(game_.id);
        Games::setProfileInOverlay(game_.id, active, on);
        updateProfileState();
        if (overlay_) overlay_->refresh();
        const QString who = active.isEmpty() ? game_.name : T("Профиль «%1»").arg(active);
        if (on && !(overlay_ && overlay_->settings().enabled))
            notify(T("%1 — в оверлее КПС. Сам оверлей выключен: включи его кнопкой со спидометром вверху.").arg(who));
        else
            notify(on ? T("%1 — показывается в оверлее КПС.").arg(who) : T("%1 — скрыт из оверлея КПС.").arg(who));
    });

    connect(modeSeg_, &Segmented::changed, this, [this](int i) {
        if (!loading_) setGameMode(gameModeFromInt(i));
    });
    connect(singleSeg_, &Segmented::changed, this, [this](int i) {
        if (loading_) return;
        if (i == 1) setGameMode(GameMode::Hold);
        else if (i == 2) setGameMode(GameMode::Steps);
        else if (game_.mode == GameMode::Hold || game_.mode == GameMode::Steps) setGameMode(GameMode::Sequence);
    });
    connect(actionsEdit_, &ActionsEditor::changed, this, [this]() {
        if (loading_) return;
        if (game_.mode == GameMode::Steps) {
            cur_.steps = actionsEdit_->actions();
        } else {
            const bool wasMulti = cur_.multi();
            cur_.actions = actionsEdit_->actions();
            // стало одно действие: «вместе» — то же, что «повторять»
            if (wasMulti && !cur_.multi() && game_.mode == GameMode::Combo) {
                game_.mode = GameMode::Sequence;
                Games::save(game_);
            }
        }
        cur_.startDelayMs = actionsEdit_->startDelay();
        // «повтор через» — тот же интервал, что в карточке «Скорость»
        const int iv = qBound(repeatMin(), actionsEdit_->interval(), 60000);
        if (iv != cur_.intervalMs) {
            cur_.intervalMs = iv;
            syncSlider();
        }
        syncSpeedBox();         // от числа действий зависит, можно ли интервал меньше минимума
        applyModeLayout();
        commit();
    });
    connect(speedBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (loading_ || v <= 0) return;
        const int unit = unitSeg_->current();
        const int iv = unit == 1 ? qRound(1000.0 / v) : unit == 2 ? qRound(60000.0 / v) : qRound(v);
        cur_.intervalMs = qBound(repeatMin(), iv, 60000);
        syncSlider();
        commit();
    });
    connect(unitSeg_, &Segmented::changed, this, [this](int i) {
        auto s = AppSettings::open();
        s->setValue("speedUnit", i);
        syncSpeedBox();
        updateRate();
    });
    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        if (loading_) return;
        // логарифмическая шкала: слева 5 секунд, справа — минимум игры (вправо — быстрее)
        const double lo = game_.minIntervalMs, hi = 5000.0;
        cur_.intervalMs = qBound(game_.minIntervalMs, int(qRound(lo * qPow(hi / lo, (1000 - v) / 1000.0))), 60000);
        syncSpeedBox();
        commit();
    });
    connect(randomizeSwitch_, &ToggleSwitch::toggled, this, [this](bool on) {
        if (loading_) return;
        cur_.randomize = on; commit();
    });
    connect(jitterBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (loading_) return;
        cur_.jitterMs = v; commit();
    });
    connect(lockSwitch_, &ToggleSwitch::toggled, this, [this](bool on) {
        if (loading_) return;
        cur_.lockWindow = on; commit(); refreshLive();
    });
    connect(frameSyncSwitch_, &ToggleSwitch::toggled, this, [this](bool on) {
        if (loading_) return;
        cur_.frameSync = on;
        fillHzBox();
        commit();
    });
    connect(hzBox_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (loading_) return;
        cur_.frameHz = hzBox_->itemData(i).toInt();
        commit();
    });
    // «Авто» показывает частоту монитора — открыли список: обновить (могли сменить монитор)
    hzBox_->installEventFilter(this);
    connect(procEdit_, &ChipEditor::itemsChanged, this, [this](const QStringList& l) { setRules(l, game_.titles); });
    connect(procEdit_, &ChipEditor::pickRequested, this, [this](QWidget* a) {
        WindowPickerPopup::open(a, WindowPickerPopup::Process, game_.processes, [this](const QString& v) {
            if (game_.processes.contains(v, Qt::CaseInsensitive)) return;
            setRules(game_.processes + QStringList{v}, game_.titles);
            updateRules();
        });
    });
    connect(titleEdit_, &ChipEditor::pickRequested, this, [this](QWidget* a) {
        WindowPickerPopup::open(a, WindowPickerPopup::Title, game_.titles, [this](const QString& v) {
            if (game_.titles.contains(v, Qt::CaseInsensitive)) return;
            setRules(game_.processes, game_.titles + QStringList{v});
            updateRules();
        });
    });
    connect(titleEdit_, &ChipEditor::itemsChanged, this, [this](const QStringList& l) { setRules(game_.processes, l); });
    connect(timeSeg_, &TimeChoice::changed, this, [this](int sec) {
        if (!loading_) setCaptureSeconds(sec);
    });
    connect(checkBtn_, &QPushButton::clicked, this, [this]() {
        if (countdown_->active()) { countdown_->cancel(); return; }
        startWindowCheck();
    });
    connect(countdown_, &CaptureCountdown::finished, this, &MainWindow::checkWindowDone);
    connect(countdown_, &CaptureCountdown::cancelled, this, [this]() {
        pendingStart_ = false;
        setStatusBox("none", QString());
        updateTrayState();
        refreshLive();
    });

    connect(hotkeyPick_, &KeyPicker::picked, this, [this](const QString& k) {
        HotkeySpec sp; sp.key = k;
        setHotkey(sp, true);
    });
    connect(blockSwitch_, &ToggleSwitch::toggled, this, [this](bool on) {
        if (loading_ || !hasGame() || on == hotkeySpec_.block) return;
        Games::setStartKeyBlock(game_.id, on);
        hotkeySpec_.block = on;
        updateHotkeyInfo();
        rebuildBindings();
        notify(on ? T("%1 — только для AutoQ: в игру нажатие не попадает").arg(hotkeySpec_.display())
                  : T("%1 — снова нажимается и в игре").arg(hotkeySpec_.display()));
    });
    connect(captureBtn_, &KeyCaptureButton::captureStarted, this, &MainWindow::onCaptureStarted);
    connect(captureBtn_, &KeyCaptureButton::captureFinished, this, &MainWindow::onCaptureFinished);
    connect(captureBtn_, &KeyCaptureButton::hint, this, [this](const QString& t) {
        hotkeyStatus_->setText(t);
        hotkeyStatus_->show();
    });
    connect(keyModeSeg_, &Segmented::changed, this, [this](int i) {
        if (loading_ || !hasGame()) return;
        Games::setKeyMode(game_.id, i);
        notify(i == 1 ? T("«%1»: работает, пока держишь %2").arg(game_.name, hotkeySpec_.display())
                      : T("«%1»: %2 — включить, ещё раз — выключить").arg(game_.name, hotkeySpec_.display()));
        updateTrayState();
    });
    connect(armedSwitch_, &ToggleSwitch::toggled, this, [this](bool on) {
        if (loading_ || !hasGame()) return;
        setArmed(game_.id, on, true);
    });
}

// Мини-окно управляет тем же состоянием, что и главное
void MainWindow::connectMini() {
    connect(mini_, &MiniWindow::toggleRequested, this, &MainWindow::startPressed);
    connect(mini_, &MiniWindow::expandRequested, this, &MainWindow::showMain);
    connect(mini_, &MiniWindow::gameChosen, this, [this](const QString& id) {
        QTimer::singleShot(0, this, [this, id]() { selectGame(id); });
    });
    connect(mini_, &MiniWindow::actionChosen, this,
            [this](int slot, bool use, bool mouse, int button, const QString& key) {
        const int i = slot - 1;
        if (i < 0) return;
        if (!use) {
            if (i > 0 && i < cur_.actions.size()) cur_.actions.removeAt(i);
        } else {
            while (cur_.actions.size() <= i) {
                ActionStep a; a.mouse = false; a.key = "f";
                cur_.actions << a;
            }
            ActionStep& a = cur_.actions[i];
            a.mouse = mouse;
            if (mouse) a.button = button; else a.key = key;
        }
        if (!cur_.multi() && game_.mode == GameMode::Combo) { game_.mode = GameMode::Sequence; Games::save(game_); }
        stateToUi();
        commit();
    });
    connect(mini_, &MiniWindow::modeChosen, this, [this](int m) { setGameMode(gameModeFromInt(m)); });
    connect(mini_, &MiniWindow::intervalChosen, this, [this](int ms) {
        if (ms == cur_.intervalMs) return;
        cur_.intervalMs = qMax(ms, repeatMin());
        stateToUi();
        commit();
    });
    connect(mini_, &MiniWindow::profileChosen, this, [this](const QString& n) {
        QTimer::singleShot(0, this, [this, n]() { switchProfile(n); });
    });
    connect(mini_, &MiniWindow::profileSaveRequested, this, &MainWindow::saveProfile);
    connect(mini_, &MiniWindow::profileResetRequested, this, &MainWindow::showResetMenu);
    connect(mini_, &MiniWindow::hotkeyPicked, this, [this](const QString& k) {
        HotkeySpec sp; sp.key = k;
        setHotkey(sp, true);
    });
    connect(mini_, &MiniWindow::hotkeyCaptureStarted, this, [this]() { suspendHotkeys(true); });
    connect(mini_, &MiniWindow::hotkeyCaptured, this, [this](bool ok, const HotkeySpec& sp) {
        suspendHotkeys(false);
        if (ok) setHotkey(sp, true);
    });
    connect(mini_, &MiniWindow::hotkeyHint, this, [this](const QString& t) {
        if (mini_->isVisible()) mini_->toast(t);
    });
    connect(mini_, &MiniWindow::captureStarted, this, [this]() {
        resultUntil_ = 0;
        countdown_->cancel();
        setStatusBox("wait", T("Переключись в окно игры «%1»…").arg(game_.name));
        updateTrayState();
        updateMiniLive();
    });
    connect(mini_, &MiniWindow::captureFinished, this, &MainWindow::checkWindowDone);
    connect(mini_, &MiniWindow::captureCancelled, this, [this]() {
        pendingStart_ = false;
        setStatusBox("none", QString());
        updateTrayState();
        refreshLive();
    });
    connect(mini_, &MiniWindow::appearanceChanged, this, &MainWindow::appearanceChanged);
    connect(mini_, &MiniWindow::settingsRequested, this, &MainWindow::openSettings);
    connect(mini_, &MiniWindow::overlayMenuRequested, this, [this](QWidget* a) { syncOverlayUi(); showOverlayMenu(a); });
    connect(mini_, &MiniWindow::overlaySettingsRequested, this, &MainWindow::openOverlaySettings);
}

// ===========================================================================
// Значки и цвета (перерисовываются при смене темы / акцента)
// ===========================================================================
void MainWindow::refreshIcons() {
    const auto& t = Theme::t();
    const qreal dpr = devicePixelRatioF();
    board_->refreshIcons();
    for (QToolButton* b : soundPlayBtns_) b->setIcon(AqIcons::ui("play", t.muted, 12));
    qApp->setWindowIcon(AqIcons::appIcon());
    brandLogo_->setPixmap(AqIcons::logo(26, dpr));
    for (QLabel* l : findChildren<QLabel*>())
        if (l->property("brandName").toBool())
            l->setText(QString("Auto<span style=\"color:%1\">Q</span>").arg(t.redHi.name()));
    footerLabel_->setText(QString(AQ_NAME " " AQ_VERSION_SHORT " · created by "
                                  "<span style=\"color:%1; font-weight:600\">" AQ_AUTHOR "</span>")
                              .arg(t.redHi.name()));
    addGameBtn_->setIcon(AqIcons::ui("plus", t.redHi, 16, 2.2));
    settingsBtn_->setIcon(AqIcons::ui("settings", t.muted, 17, 1.8));
    editGameBtn_->setIcon(AqIcons::ui("pencil", t.muted, 15));
    delGameBtn_->setIcon(AqIcons::ui("trash", t.muted, 15));
    miniBtn_->setIcon(AqIcons::ui("mini", t.muted, 17));
    layoutBtn_->setIcon(AqIcons::ui("layout", layoutBtn_->isChecked() ? t.redHi : t.muted, 16));
    switchGameBtn_->setIcon(AqIcons::ui("chevron", t.muted, 14, 2.2));
    overlayBtn_->setIcon(AqIcons::ui("gauge", overlayBtn_->isChecked() ? t.redHi : t.muted, 18, 1.9));
    if (overlay_) overlay_->update();
    actionsEdit_->refreshIcons();
    checkBtn_->setIcon(AqIcons::ui("target", t.redHi, 15));
    captureBtn_->setIcon(AqIcons::ui("keyboard", t.muted, 16));
    renameProfileBtn_->setIcon(AqIcons::ui("pencil", t.muted, 14));
    profileMoreBtn_->setIcon(AqIcons::ui("more", t.muted, 16));
    profileKeyBtn_->setIcon(AqIcons::ui("keyboard", t.muted, 15));
    newProfileBtn_->setIcon(AqIcons::ui("plus", t.muted, 13, 2.2));
    saveProfileBtn_->setIcon(AqIcons::ui("save", t.redHi, 14));
    resetProfileBtn_->setIcon(AqIcons::ui("undo", t.muted, 14));
    delProfileBtn_->setIcon(AqIcons::ui("trash", t.faint, 13));
    hotkeyPick_->setCurrent(hotkeySpec_.isChord() ? QString() : hotkeySpec_.key, hotkeySpec_.display());
    presetBtn_->setIcon(AqIcons::ui("chevron", t.muted, 12, 2.4));
    startBtn_->refreshIcon();
    statusDot_->update();
    if (trayCheckAction_) trayCheckAction_->setIcon(AqIcons::ui("target", t.redHi, 16));
    // плитки игр
    const QList<GameDef> games = Games::list();
    for (int i = 0; i < gameList_->count() && i < games.size(); ++i) {
        QListWidgetItem* it = gameList_->item(i);
        const GameDef& g = games[i];
        it->setData(GameListDelegate::TileRole,
                    AqIcons::gameTile(g.icon, QColor(g.color), g.image, 34, dpr, false, t.light));
        it->setData(GameListDelegate::TileActiveRole,
                    AqIcons::gameTile(g.icon, QColor(g.color), g.image, 34, dpr, true, t.light));
    }
    rebuildWelcome();
    procEdit_->setItems(game_.processes);
    titleEdit_->setItems(game_.titles);
    if (mini_) mini_->refreshTheme();
    rebuildTrayGames();
    if (hasGame()) updateHeader();
    updateProfileState();
    updateTrayState();
    syncMini();
}

void MainWindow::appearanceChanged() {
    statusBox_->setProperty("state", QString());   // перерисовать
    applySidebarMode(sideMode_);                   // плотность сменилась — поля боковой панели
    refreshIcons();
    refreshLive();
    gameList_->viewport()->update();
}

// ===========================================================================
// Первый запуск
// ===========================================================================
void MainWindow::startOnboarding() {
    showMain();
    onboarding_->restart();
    Anim::transition(rootStack_, [this]() { rootStack_->setCurrentWidget(onboarding_); });
}

void MainWindow::finishOnboarding() {
    Anim::transition(rootStack_, [this]() { rootStack_->setCurrentIndex(0); }, {}, "fade");
    appearanceChanged();
    if (Games::list().isEmpty()) notify(T("Готово! Теперь добавь игру — выбери из готовых или свою."));
}

// ===========================================================================
// Игры
// ===========================================================================
void MainWindow::reloadGames(const QString& selectId) {
    const QList<GameDef> games = Games::list();
    const auto& t = Theme::t();
    const qreal dpr = devicePixelRatioF();
    loading_ = true;
    gameList_->clear();
    int sel = 0;
    for (int i = 0; i < games.size(); ++i) {
        const GameDef& g = games[i];
        auto* it = new QListWidgetItem(g.name);
        it->setData(GameListDelegate::IdRole, g.id);
        it->setData(GameListDelegate::SubRole, modeSubtitle(g.mode, !Games::loadState(g).multi()));
        it->setData(GameListDelegate::TileRole, AqIcons::gameTile(g.icon, QColor(g.color), g.image, 34, dpr, false, t.light));
        it->setData(GameListDelegate::TileActiveRole, AqIcons::gameTile(g.icon, QColor(g.color), g.image, 34, dpr, true, t.light));
        it->setToolTip(g.name);
        gameList_->addItem(it);
        if (g.id == selectId) sel = i;
    }
    if (!games.isEmpty()) gameList_->setCurrentRow(sel);
    gameCount_->setText(QString::number(games.size()));
    gameList_->setVisible(!games.isEmpty());
    emptyHint_->setVisible(games.isEmpty() && sideMode_ != 2);
    loading_ = false;
    // игр больше нет — их кликеры не нужны
    for (auto it = runners_.begin(); it != runners_.end();) {
        bool keep = it->first.isEmpty();
        for (const GameDef& g : games) keep = keep || g.id == it->first;
        if (!keep) { it->second->clicker.stop(); it = runners_.erase(it); }
        else ++it;
    }
    QListWidgetItem* cur = gameList_->currentItem();
    switchToGame(cur ? cur->data(GameListDelegate::IdRole).toString() : QString());
    rebuildWelcome();
    rebuildTrayGames();
    updateGameItems();
    updateSidebarVisible();          // скрыта — но без игр её не спрятать
}

void MainWindow::updateGameItems() {
    for (int i = 0; i < gameList_->count(); ++i) {
        QListWidgetItem* it = gameList_->item(i);
        const QString id = it->data(GameListDelegate::IdRole).toString();
        it->setData(GameListDelegate::ArmedRole, Games::armed(id));
        it->setData(GameListDelegate::RunningRole, isRunning(id) || (id == game_.id && pendingStart_));
        it->setData(GameListDelegate::KeyRole, chipText(id == game_.id ? hotkeySpec_ : Games::startKey(id)));
        QString tip = it->text();
        const HotkeySpec key = id == game_.id ? hotkeySpec_ : Games::startKey(id);
        tip += "\n" + T("Старт / стоп: %1").arg(key.display());
        tip += "\n" + (Games::armed(id) ? T("● Используется — можно запустить, даже когда открыта другая")
                                       : T("○ Не используется — запустить нельзя (нажми кружок)"));
        it->setToolTip(tip);
    }
    gameList_->viewport()->update();
}

void MainWindow::rebuildWelcome() {
    if (!welcomeGrid_) return;
    while (QLayoutItem* it = welcomeGrid_->takeAt(0)) {
        if (it->widget()) { it->widget()->hide(); it->widget()->deleteLater(); }
        delete it;
    }
    const auto& t = Theme::t();
    const qreal dpr = devicePixelRatioF();
    welcomeLogo_->setPixmap(AqIcons::logo(60, dpr));
    int i = 0;
    for (const GameDef& d : Games::builtinDefaults()) {
        if (Games::contains(d.id)) continue;
        auto* card = new TemplateCard(AqIcons::gameTile(d.icon, QColor(d.color), {}, 40, dpr, false, t.light),
                                      d.name, modeSubtitle(d.mode, !Games::defaultState(d).multi()) + " · " + presetsWord(Games::presets(d).size()));
        card->setToolTip(T("Добавить «") + d.name + T("» с готовыми настройками"));
        const QString id = d.id;
        connect(card, &QPushButton::clicked, this, [this, id]() {
            QTimer::singleShot(0, this, [this, id]() { addTemplate(id); });
        });
        welcomeGrid_->addWidget(card, i / 3, i % 3);
        ++i;
    }
    auto* custom = new TemplateCard(AqIcons::gameTile("gamepad", QColor("#FF5A66"), {}, 40, dpr, false, t.light),
                                    T("Своя игра…"), T("любая игра — процесс и заголовок окна"));
    custom->setToolTip(T("Добавить свою игру"));
    connect(custom, &QPushButton::clicked, this, [this]() { QTimer::singleShot(0, this, &MainWindow::addGame); });
    welcomeGrid_->addWidget(custom, i / 3, i % 3);
}

void MainWindow::addTemplate(const QString& id) {
    if (!Games::addBuiltin(id)) return;
    Anim::transition(pageArea(), [this, id]() { reloadGames(id); }, board_->visibleCards());
    notify(T("Игра «%1» добавлена.").arg(Games::get(id).name));
}

void MainWindow::selectGame(const QString& id) {
    for (int i = 0; i < gameList_->count(); ++i)
        if (gameList_->item(i)->data(GameListDelegate::IdRole).toString() == id) {
            gameList_->setCurrentRow(i);
            return;
        }
}

void MainWindow::onGameRowChanged(int row) {
    if (loading_ || row < 0) return;
    const QString id = gameList_->item(row)->data(GameListDelegate::IdRole).toString();
    if (id == game_.id) return;
    cancelPending();
    countdown_->cancel();
    if (mini_) mini_->cancelCapture();
    Anim::transition(pageArea(), [this, id]() { switchToGame(id); }, board_->visibleCards());
    rebuildTrayGames();
}

void MainWindow::switchToGame(const QString& id) {
    bool ok = false;
    GameDef g = id.isEmpty() ? GameDef{} : Games::get(id, &ok);
    if (!ok) {
        // игр нет — экран «добавь первую игру»
        cancelPending();
        game_ = GameDef{};
        cur_ = GameState{};
        board_->setEditMode(false, false);    // редактировать нечего (без перехода: страница меняется сразу)
        pages_->setCurrentIndex(0);
        rebuildBindings();
        updateProfileState();
        updateTrayState();
        syncMini();
        if (mini_ && mini_->isVisible()) showMain();
        return;
    }
    pages_->setCurrentIndex(1);
    const bool sameGame = g.id == game_.id;
    if (!sameGame) cancelPending();            // отсчёт «выбрать окно» шёл для другой игры
    game_ = g;
    Games::setCurrentId(g.id);
    cur_ = Games::loadState(g);
    // та же игра (изменили её или убрали другую) — раскладка и история отмен остаются
    if (!sameGame) board_->setCardLayout(CardLayout::load(g.id));
    updateSidebarVisible();
    resultUntil_ = 0;
    rateHist_.clear();
    loading_ = true;
    stateToUi();
    reloadPresets();
    updateRules();
    keyModeSeg_->setCurrent(Games::keyMode(g.id));
    armedSwitch_->setChecked(Games::armed(g.id));
    loading_ = false;
    setHotkey(Games::startKey(g.id), false);
    updateHeader();
    pushConfig();
    updateRate();
    updatePresetLabel();
    updateProfileState();
    updateTrayState();
    syncMini();
    refreshLive();
}

void MainWindow::updateHeader() {
    const qreal dpr = devicePixelRatioF();
    headerIcon_->setPixmap(AqIcons::gameTile(game_.icon, QColor(game_.color), game_.image, 56, dpr,
                                             false, Theme::t().light));
    nameLabel_->setText(game_.name);
}

void MainWindow::addGame() {
    GameDef g;
    g.id = Games::newCustomId();
    g.mode = GameMode::Sequence;
    g.minIntervalMs = 5;
    g.icon = "gamepad";
    g.color = "#FF5A66";
    GameEditDialog dlg(g, true, this);
    if (dlg.exec() != QDialog::Accepted) return;
    GameDef res = dlg.result();
    if (res.builtin) Games::addBuiltin(res.id);   // шаблон: чистая запись, затем правки из окна
    Games::save(res);
    Anim::transition(pageArea(), [this, res]() { reloadGames(res.id); }, board_->visibleCards());
    notify(T("Игра «%1» добавлена.").arg(res.name));
}

void MainWindow::editGame() {
    if (!hasGame()) return;
    GameEditDialog dlg(game_, false, this);
    const int capBefore = Prefs::captureSeconds();
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (Prefs::captureSeconds() != capBefore) setCaptureSeconds(Prefs::captureSeconds());   // меняли в диалоге
    if (!accepted) return;
    GameDef res = dlg.result();
    Games::save(res);
    reloadGames(res.id);
}

void MainWindow::removeGame() {
    if (!hasGame()) return;
    if (!askYesNo(this, T("Убрать игру"),
            T("Убрать «%1» из списка вместе с её профилями?\n\nВстроенную игру можно вернуть кнопкой «Добавить игру».").arg(game_.name),
            T("Убрать"), T("Отмена"), false))
        return;
    stopGame(game_.id);
    cancelPending();
    const QString id = game_.id;
    const int idx = gameList_->currentRow();
    Games::remove(id);
    runners_.erase(id);
    game_ = GameDef{};
    const QList<GameDef> games = Games::list();
    reloadGames(games.isEmpty() ? QString() : games.value(qMin(idx, games.size() - 1)).id);
}

void MainWindow::removeGameById(const QString& id) {
    if (id == game_.id) { removeGame(); return; }
    bool ok = false;
    const GameDef g = Games::get(id, &ok);
    if (!ok) return;
    if (!askYesNo(this, T("Убрать игру"),
            T("Убрать «%1» из списка вместе с её профилями?\n\nВстроенную игру можно вернуть кнопкой «Добавить игру».").arg(g.name),
            T("Убрать"), T("Отмена"), false))
        return;
    stopGame(id);
    runners_.erase(id);
    Games::remove(id);
    reloadGames(game_.id);
}

void MainWindow::setArmed(const QString& id, bool on, bool announce) {
    if (id.isEmpty()) return;
    Games::setArmed(id, on);
    if (!on) {                           // не используется — и работать не может
        if (id == game_.id) cancelPending();
        if (isRunning(id)) stopGame(id);
    }
    if (id == game_.id) {
        QSignalBlocker b(armedSwitch_);
        armedSwitch_->setChecked(on);
    }
    rebuildBindings();
    updateGameItems();
    if (announce) {
        const GameDef g = Games::get(id);
        const HotkeySpec key = id == game_.id ? hotkeySpec_ : Games::startKey(id);
        notify(on ? T("«%1» используется — %2 запускает её, даже когда открыта другая игра.").arg(g.name, key.display())
                  : T("«%1» не используется — запустить её нельзя, пока не включишь кружок.").arg(g.name));
    }
    updateTrayState();
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == hzBox_ && e->type() == QEvent::MouseButtonPress) fillHzBox();
    if (o == gameList_->viewport()) {
        if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick) {
            auto* me = static_cast<QMouseEvent*>(e);
            const QPoint pos = me->position().toPoint();
            // правая кнопка — только меню игры, открытая игра не меняется
            if (me->button() == Qt::RightButton) return true;
            if (QListWidgetItem* it = gameList_->itemAt(pos)) {
                const QRect r = gameList_->visualItemRect(it);
                if (me->button() == Qt::LeftButton && GameListDelegate::circleRect(r, gameDelegate_->compact).contains(pos)) {
                    const QString id = it->data(GameListDelegate::IdRole).toString();
                    if (e->type() == QEvent::MouseButtonPress) setArmed(id, !Games::armed(id), true);
                    return true;                 // выбор игры не меняется
                }
            }
        } else if (e->type() == QEvent::MouseMove) {
            gameList_->viewport()->update();     // подсветка кружка под курсором
        }
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::showGameMenu(const QPoint& pos) {
    QListWidgetItem* it = gameList_->itemAt(pos);
    if (!it) return;
    const QString id = it->data(GameListDelegate::IdRole).toString();
    bool ok = false;
    const GameDef g = Games::get(id, &ok);
    if (!ok) return;
    const auto& t = Theme::t();
    const bool run = isRunning(id) || (id == game_.id && pendingStart_);
    const HotkeySpec key = id == game_.id ? hotkeySpec_ : Games::startKey(id);
    QMenu m(gameList_);
    QAction* title = m.addAction(g.name);
    title->setEnabled(false);
    m.addSeparator();
    QAction* toggle = m.addAction(AqIcons::ui(run ? "stop" : "play", t.redHi, 14),
                                  QString("%1\t%2").arg(run ? T("Остановить") : T("Запустить"), key.display()));
    toggle->setEnabled(run || Games::armed(id));
    QAction* armed = m.addAction(T("Используется — можно запускать"));
    armed->setCheckable(true);
    armed->setChecked(Games::armed(id));
    QAction* open = m.addAction(AqIcons::ui("chevron-right", t.muted, 14), T("Открыть"));
    open->setEnabled(id != game_.id);
    m.addSeparator();
    QAction* startKey = m.addAction(AqIcons::ui("keyboard", t.muted, 14), T("Клавиша старт/стоп… (%1)").arg(key.display()));
    const HotkeySpec ak = Games::armKey(id);
    QAction* armKey = m.addAction(AqIcons::ui("keyboard", t.muted, 14),
                                  ak.isValid() ? T("Клавиша «используется / нет»… (%1)").arg(ak.display())
                                               : T("Клавиша «используется / нет»…"));
    QAction* edit = m.addAction(AqIcons::ui("pencil", t.muted, 14), T("Изменить…"));
    m.addSeparator();
    QAction* del = m.addAction(AqIcons::ui("trash", t.muted, 14), T("Убрать из списка…"));
    QAction* chosen = m.exec(gameList_->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == toggle) toggleGame(id, false);
    else if (chosen == armed) setArmed(id, armed->isChecked(), true);
    else if (chosen == open) selectGame(id);
    else if (chosen == startKey) {
        HotkeySpec sp = key;
        if (!HotkeyDialog::ask(this, T("Клавиша старт/стоп"), T("Для «%1». Её же видно на значке игры в списке.").arg(g.name),
                               sp, false, [this](bool s) { suspendHotkeys(s); }))
            return;
        if (id == game_.id) setHotkey(sp, true);
        else { Games::setStartKey(id, sp); rebuildBindings(); updateGameItems(); }
    } else if (chosen == armKey) {
        HotkeySpec sp = ak;
        if (!HotkeyDialog::ask(this, T("Клавиша «используется / нет»"),
                               T("Нажатие включает «%1» (её можно запускать) или выключает (запустить нельзя никак).").arg(g.name),
                               sp, true, [this](bool s) { suspendHotkeys(s); }))
            return;
        Games::setArmKey(id, sp);
        rebuildBindings();
    } else if (chosen == edit) {
        selectGame(id);
        QTimer::singleShot(0, this, &MainWindow::editGame);
    } else if (chosen == del) {
        QTimer::singleShot(0, this, [this, id]() { removeGameById(id); });
    }
}

// ===========================================================================
// Действия и режим
// ===========================================================================
void MainWindow::setGameMode(GameMode m, bool announce) {
    if (!hasGame()) return;
    if (m == game_.mode) { syncMini(); return; }
    game_.mode = m;
    Games::save(game_);                 // режим хранится у игры
    if (QListWidgetItem* it = gameList_->currentItem()) it->setData(GameListDelegate::SubRole, modeSubtitle(m, !cur_.multi()));
    stateToUi();
    updateHeader();
    commit();
    if (announce) notify(T("Режим: %1").arg(gameModeName(m)));
}

void MainWindow::applyModeLayout() {
    const GameMode m = game_.mode;
    const bool steps = m == GameMode::Steps;
    const bool multi = cur_.multi();
    {
        QSignalBlocker b(modeSeg_);
        modeSeg_->setCurrent(static_cast<int>(m));
        QSignalBlocker b2(singleSeg_);
        singleSeg_->setCurrent(m == GameMode::Hold ? 1 : steps ? 2 : 0);
    }
    // «По шагам» — всегда полный выбор режимов: в нём шагов обычно несколько
    modeSeg_->setVisible(multi || steps);
    singleSeg_->setVisible(!multi && !steps);
    if (QListWidgetItem* it = gameList_->currentItem())
        if (hasGame()) it->setData(GameListDelegate::SubRole, modeSubtitle(m, !multi));
    const bool hold = (m == GameMode::Hold);
    updateActionsHint();
    // в режиме «Зажать» интервал не нужен
    for (QWidget* w : {static_cast<QWidget*>(slider_), static_cast<QWidget*>(speedBox_),
                       static_cast<QWidget*>(randomizeSwitch_), static_cast<QWidget*>(jitterBox_),
                       static_cast<QWidget*>(unitSeg_), static_cast<QWidget*>(frameSyncSwitch_)})
        w->setEnabled(!hold);
    if (!hold) jitterBox_->setEnabled(cur_.randomize);
    hzBox_->setEnabled(!hold && cur_.frameSync);
    {
        const bool wasLoading = loading_;
        loading_ = true;
        actionsEdit_->setState(steps ? cur_.runList(m) : cur_.actions, m, cur_.startDelayMs,
                               cur_.intervalMs, game_.minIntervalMs);
        loading_ = wasLoading;
    }
}

// Заголовок и подсказка под действиями (интервал в них — живой: меняют в «Скорости»)
void MainWindow::updateActionsHint() {
    const GameMode m = game_.mode;
    const bool multi = cur_.multi();
    const bool hold = (m == GameMode::Hold);
    const QString every = T("повтор каждые %1 мс").arg(num(cur_.intervalMs));
    if (m == GameMode::Steps) {
        const QList<ActionStep> list = cur_.runList(m);
        actionsTitle_->setText(T("По шагам ↓ ↑"));
        QString hint = T("Каждый круг: шаги один за другим, с паузами между ними · %1. Над клавишей — режим шага: нажать ↓, отпустить ↑ или клик ↕.").arg(every);
        if (!Steps::strayUps(list).isEmpty())
            hint += ' ' + T("Шаги с пунктиром пропускаются: «отпустить» без «нажать» раньше.");
        const QList<ActionStep> left = Steps::leftDown(list);
        if (!left.isEmpty())
            hint += ' ' + T("Не отпущено за круг: %1 — отпустится само в конце круга.").arg(actionsLabel(left, 0, int(left.size())));
        actionsHint_->setText(hint);
    } else if (!multi) {
        actionsTitle_->setText(hold ? T("Одно действие — зажать") : T("Одно действие — повторять"));
        actionsHint_->setText(hold
            ? T("Пока кликер включён — кнопка зажата. Ушёл из игры — отпускается, вернулся — снова зажата.")
            : T("Нажимает снова и снова (%1). Над клавишей — режим: клик или держать нажатым. Можно добавить ещё действия.").arg(every));
    } else switch (m) {
        case GameMode::Combo:
            actionsTitle_->setText(T("Вместе (комбинация)"));
            actionsHint_->setText(T("Каждый цикл все клавиши зажимаются вместе, как F3+A, и держатся самое долгое «держать» · %1.").arg(every));
            break;
        case GameMode::Hold:
            actionsTitle_->setText(T("Зажать и держать"));
            actionsHint_->setText(T("Пока кликер включён — клавиши зажаты. Ушёл из игры — отпускаются, вернулся — снова зажаты."));
            break;
        default:
            actionsTitle_->setText(T("Действия по очереди"));
            actionsHint_->setText(T("Каждый цикл: действия одно за другим, с паузами между ними · %1.").arg(every));
            break;
    }
}

void MainWindow::stateToUi() {
    const bool wasLoading = loading_;
    loading_ = true;
    cur_.intervalMs = qMax(cur_.intervalMs, repeatMin());
    randomizeSwitch_->setChecked(cur_.randomize);
    jitterBox_->setValue(cur_.jitterMs);
    jitterBox_->setEnabled(cur_.randomize);
    lockSwitch_->setChecked(cur_.lockWindow);
    frameSyncSwitch_->setChecked(cur_.frameSync);
    soundOnBox_->setSound(cur_.soundOn);
    soundOffBox_->setSound(cur_.soundOff);
    soundsOffHint_->setVisible(!Sounds::enabled());
    loading_ = wasLoading;
    fillHzBox();
    syncSlider();
    syncSpeedBox();
    applyModeLayout();
}

void MainWindow::syncSlider() {
    const double lo = game_.minIntervalMs, hi = 5000.0;
    const double iv = qBound(lo, double(cur_.intervalMs), hi);
    const int v = lo >= hi ? 1000 : 1000 - qRound(1000.0 * qLn(iv / lo) / qLn(hi / lo));
    const bool wasLoading = loading_;
    loading_ = true;
    slider_->setValue(v);
    loading_ = wasLoading;
}

void MainWindow::syncSpeedBox() {
    const bool wasLoading = loading_;
    loading_ = true;
    const int unit = qMax(0, unitSeg_->current());
    const int minMs = qMax(1, repeatMin());
    const bool multi = (cur_.multi() && game_.mode != GameMode::Hold) || game_.mode == GameMode::Steps;
    if (unit == 1) {
        speedBox_->setDecimals(cur_.intervalMs > 2000 ? 2 : 1);
        speedBox_->setRange(0.02, 1000.0 / minMs);
        speedBox_->setSingleStep(0.5);
        speedBox_->setSuffix(T(" /с"));
        speedBox_->setValue(1000.0 / qMax(1, cur_.intervalMs));
        speedCaption_->setText(multi ? T("Циклов в секунду") : T("Кликов в секунду"));
    } else if (unit == 2) {
        speedBox_->setDecimals(0);
        speedBox_->setRange(1, 60000.0 / minMs);
        speedBox_->setSingleStep(10);
        speedBox_->setSuffix(T(" /мин"));
        speedBox_->setValue(qRound(60000.0 / qMax(1, cur_.intervalMs)));
        speedCaption_->setText(multi ? T("Циклов в минуту") : T("Кликов в минуту"));
    } else {
        speedBox_->setDecimals(0);
        speedBox_->setRange(repeatMin(), 60000);
        speedBox_->setSingleStep(1);
        speedBox_->setSuffix(T(" мс"));
        speedBox_->setValue(cur_.intervalMs);
        speedCaption_->setText(T("Интервал"));
    }
    loading_ = wasLoading;
}

ClickerConfig MainWindow::buildConfig(const GameDef& g, const GameState& st, const WindowInfo& w) {
    ClickerConfig c;
    c.actions = st.runList(g.mode);
    c.steps = g.mode == GameMode::Steps && !c.actions.isEmpty();
    if (c.actions.isEmpty()) c.actions = {ActionStep{}};
    c.simultaneous = (g.mode == GameMode::Combo);
    c.hold = (g.mode == GameMode::Hold);
    if (g.mode != GameMode::Sequence && g.mode != GameMode::Steps)
        for (ActionStep& a : c.actions) a.afterMs = 0;
    c.startDelayMs = st.startDelayMs;
    c.intervalMs = qMax(st.intervalMs, st.repeatMin(g.mode, g.minIntervalMs));
    c.minCycleMs = g.minIntervalMs;
    c.randomize = st.randomize;
    c.jitterMs = st.jitterMs;
    c.lockToWindow = st.lockWindow;
    c.frameMs = (st.frameSync && g.mode != GameMode::Hold) ? FrameSync::frameMs(syncHz(st, w.handle)) : 0;
    c.rule = g.rule();
    c.boundHandle = w.handle;
    c.boundPid = w.pid;
    c.mouseOnlyOverGame = Prefs::mouseOnlyOverGame();
    return c;
}

double MainWindow::monitorHz(unsigned long long handle) {
    // спрашиваем систему не чаще раза в 3 с (Linux — новое подключение к X11)
    static unsigned long long cachedHandle = ~0ull;
    static qint64 cachedAt = 0;
    static double cachedHz = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (handle == cachedHandle && now - cachedAt < 3000 && cachedHz > 0) return cachedHz;
    double hz = FrameSync::monitorHz(handle);          // Windows: монитор окна игры
    if (hz <= 0) {
        QScreen* scr = nullptr;
        int x = 0, y = 0;
        if (FrameSync::windowCenter(handle, &x, &y))    // Linux: экран, где окно игры
            for (QScreen* sc : QGuiApplication::screens()) {
                const QRect g = sc->geometry();
                if (QRect(g.topLeft(), g.size() * sc->devicePixelRatio()).contains(x, y)) { scr = sc; break; }
            }
        if (!scr) scr = QGuiApplication::primaryScreen();
        hz = scr ? scr->refreshRate() : 60;
    }
    if (hz < 20 || hz > 1000) hz = 60;
    cachedHandle = handle;
    cachedAt = now;
    cachedHz = hz;
    return hz;
}

double MainWindow::syncHz(const GameState& st, unsigned long long handle) {
    return st.frameHz > 0 ? st.frameHz : monitorHz(handle);
}

unsigned long long MainWindow::boundHandle(const QString& gameId) const {
    auto it = runners_.find(gameId);
    return it == runners_.end() ? 0 : it->second->bound.handle;
}

void MainWindow::fillHzBox() {
    const bool wasLoading = loading_;
    loading_ = true;
    hzBox_->clear();
    const double hz = monitorHz(boundHandle(game_.id));
    hzBox_->addItem(T("Авто · %1 Гц").arg(qRound(hz)), 0);
    QList<int> list = FrameSync::hzChoices();
    if (cur_.frameHz > 0 && !list.contains(cur_.frameHz)) list << cur_.frameHz;
    std::sort(list.begin(), list.end());
    for (int v : list) hzBox_->addItem(T("%1 Гц").arg(v), v);
    hzBox_->setCurrentIndex(qMax(0, hzBox_->findData(cur_.frameHz)));
    hzBox_->setEnabled(cur_.frameSync && game_.mode != GameMode::Hold);
    loading_ = wasLoading;
}

void MainWindow::pushConfig() {
    if (!hasGame()) return;
    GameRunner& r = runner(game_.id);
    r.def = game_;
    r.st = cur_;
    r.clicker.setConfig(buildConfig(game_, cur_, r.bound));
}

void MainWindow::commit() {
    if (loading_ || !hasGame()) return;
    Games::saveState(game_.id, cur_);   // сразу на диск — переживёт выключение ПК
    pushConfig();
    if (game_.mode != GameMode::Hold) jitterBox_->setEnabled(cur_.randomize);
    actionsEdit_->setTimes(cur_.startDelayMs, cur_.intervalMs);   // интервал могли поменять в «Скорости»
    updateActionsHint();
    updateRate();
    updatePresetLabel();
    updateProfileState();
    syncMini();
    if (clk().isRunning() && !startProblem().isEmpty()) {
        const QString why = startProblem();
        stopGame(game_.id);
        notify(T("Кликер остановлен: ") + why);
    }
}

double MainWindow::perSecond(const GameDef& g, const GameState& st) const {
    if (g.mode == GameMode::Hold) return 0;
    const QList<ActionStep> list = st.runList(g.mode);
    // «По шагам»: нажатие — каждый шаг «нажать»
    int n = qMax(1, int(list.size()));
    if (g.mode == GameMode::Steps) {
        n = 0;
        for (const ActionStep& a : list) n += a.press != ActionStep::Up;
    }
    const bool combo = g.mode == GameMode::Combo && n > 1;
    // круг целиком — не быстрее минимума игры (паузы между клавишами бывают 0)
    double cycle = qMax<double>(g.minIntervalMs, FrameSync::cycleMs(list, qMax(st.intervalMs, st.repeatMin(g.mode, g.minIntervalMs)),
                                                                     g.mode == GameMode::Combo));
    if (st.frameSync) {                 // в такт: целое число кадров на цикл
        const double f = FrameSync::frameMs(syncHz(st, boundHandle(g.id)));
        cycle = FrameSync::frames(cycle, f) * f;
    }
    return combo ? 1000.0 / cycle : 1000.0 / cycle * n;
}

void MainWindow::updateRate() {
    if (game_.mode == GameMode::Hold) {
        rateLabel_->setText("—");
        rateCaption_->setText(T("в режиме «Зажать» клавиши просто держатся"));
        perMinuteLabel_->setText(T("Интервал не нужен: нажатие одно, пока кликер включён."));
        return;
    }
    const double ps = perSecond(game_, cur_);
    rateLabel_->setText(QString("≈ %1").arg(dec(ps)));
    rateCaption_->setText(game_.mode == GameMode::Combo && cur_.multi() ? T("комбинаций в секунду") : T("нажатий в секунду"));
    // кликер работает — показываем, сколько он жмёт на самом деле
    if (clk().isRunning() && rateHist_.size() >= 2) {
        const RateSample a = rateHist_.first(), b = rateHist_.last();
        const double dt = (b.t - a.t) / 1000.0;
        const double real = dt > 0.2 ? (b.n - a.n) / dt : 0;
        perMinuteLabel_->setText(T("Сейчас: %1 в секунду · %2 в минуту").arg(dec(real), num(qRound64(real * 60))));
    } else {
        QString t;
        if (cur_.frameSync) {
            const double hz = syncHz(cur_, boundHandle(game_.id)), f = FrameSync::frameMs(hz);
            const int fr = qMax(FrameSync::frames(FrameSync::cycleMs(cur_.runList(game_.mode), qMax(cur_.intervalMs, repeatMin()),
                                                                     game_.mode == GameMode::Combo), f),
                                FrameSync::frames(game_.minIntervalMs, f));
            QString every = everyFrames(fr);
            if (cur_.randomize && cur_.jitterMs > 0) every = T("в среднем ") + every;
            t = T("В такт %1 Гц: %2 (%3 мс) · %4 в минуту")
                    .arg(qRound(hz)).arg(every, dec(fr * f), num(qRound64(ps * 60)));
        } else {
            t = T("%1 в минуту · минимум %2 мс").arg(num(qRound64(ps * 60))).arg(game_.minIntervalMs);
            if (cur_.randomize && cur_.jitterMs > 0) t = T("в среднем ") + t;
        }
        perMinuteLabel_->setText(t);
    }
}

// ===========================================================================
// Пресеты
// ===========================================================================
void MainWindow::reloadPresets() {
    presetMenu_->clear();
    const QList<GamePreset> list = Games::presets(game_);
    for (int i = 0; i < list.size(); ++i) {
        QAction* a = presetMenu_->addAction(list[i].name);
        connect(a, &QAction::triggered, this, [this, i]() { applyPreset(i); });
    }
}

void MainWindow::applyPreset(int index) {
    if (!hasGame()) return;
    const QList<GamePreset> l = Games::presets(game_);
    if (index < 0 || index >= l.size()) return;
    // привязку к окну и такт монитора пресет не трогает — это про ПК, а не про игру
    const GameState keep = cur_;
    cur_ = l[index].state;
    cur_.lockWindow = keep.lockWindow;
    cur_.frameSync = keep.frameSync;
    cur_.frameHz = keep.frameHz;
    cur_.soundOn = keep.soundOn;
    cur_.soundOff = keep.soundOff;
    // шаги и «старт через» в пресетах не хранятся — свои не теряем
    cur_.steps = keep.steps;
    cur_.startDelayMs = keep.startDelayMs;
    if (l[index].mode >= 0 && gameModeFromInt(l[index].mode) != game_.mode) {
        game_.mode = gameModeFromInt(l[index].mode);
        Games::save(game_);
        updateHeader();
    }
    stateToUi();
    commit();
    notify(T("Пресет: %1").arg(l[index].name));
}

void MainWindow::updatePresetLabel() {
    QString name = T("свои настройки");
    for (const GamePreset& p : Games::presets(game_)) {
        GameState a = p.state, b = cur_;
        a.lockWindow = b.lockWindow;
        a.frameSync = b.frameSync;
        a.frameHz = b.frameHz;
        a.soundOn = b.soundOn;
        a.soundOff = b.soundOff;
        if (game_.mode != GameMode::Steps) a.steps = b.steps;   // не в «По шагам» шаги не жмутся
        a.startDelayMs = b.startDelayMs;
        a.intervalMs = qMax(a.intervalMs, a.repeatMin(game_.mode, game_.minIntervalMs));
        const bool modeOk = p.mode < 0 || gameModeFromInt(p.mode) == game_.mode;
        if (a == b && modeOk) { name = p.name; break; }
    }
    presetBtn_->setToolTip(T("Пресет: ") + name + T("\nГотовые настройки для этой игры"));
    presetBtn_->setText(presetBtn_->fontMetrics().elidedText(name, Qt::ElideRight, 150));
}

// ===========================================================================
// Профили: активный профиль, несохранённые изменения, новый / сохранить / сбросить
// ===========================================================================
bool MainWindow::profileDirty() const {
    if (!hasGame()) return false;
    const QString active = Games::activeProfile(game_.id);
    if (active.isEmpty()) return false;
    int mode = -1;
    const GameState saved = Games::loadProfile(game_.id, active, &mode);
    return !(saved == cur_) || (mode >= 0 && gameModeFromInt(mode) != game_.mode);
}

void MainWindow::updateProfileState() {
    const QString active = hasGame() ? Games::activeProfile(game_.id) : QString();
    const QStringList names = hasGame() ? Games::profiles(game_.id) : QStringList();
    const bool dirty = profileDirty();
    {
        QSignalBlocker b(profileBox_);
        profileBox_->clear();
        profileBox_->addItem(T("Без профиля"), QString());
        for (const QString& n : names) {
            const HotkeySpec k = Games::profileKey(game_.id, n);
            profileBox_->addItem(k.isValid() ? QString("%1   [%2]").arg(n, k.display()) : n, n);
        }
        const int i = profileBox_->findData(active);
        profileBox_->setCurrentIndex(i >= 0 ? i : 0);
    }
    QString state, text;
    if (active.isEmpty()) {
        state = "none";
        text = names.isEmpty() ? T("Профилей нет. «Новый» — свой набор настроек.")
                               : T("Без профиля — настройки сохраняются у игры.");
    } else if (dirty) {
        state = "dirty";
        text = T("● Есть несохранённые изменения — «Сохранить» или «Сбросить».");
    } else {
        state = "saved";
        text = T("✓ Профиль «%1» — всё сохранено.").arg(elide(active, 28));
    }
    if (profileState_->property("state").toString() != state) {
        profileState_->setProperty("state", state);
        Theme::repolish(profileState_);
    }
    profileState_->setText(text);
    saveProfileBtn_->setProperty("dirty", dirty);
    Theme::repolish(saveProfileBtn_);
    saveProfileBtn_->setIcon(AqIcons::ui("save", dirty ? Theme::t().onRed : Theme::t().redHi, 14));
    saveProfileBtn_->setToolTip(active.isEmpty() ? T("Сохранить текущие настройки как новый профиль")
                                                 : T("Сохранить изменения в «%1»").arg(active));
    delProfileBtn_->setEnabled(!active.isEmpty());
    renameProfileBtn_->setEnabled(!active.isEmpty());
    profileKeyBtn_->setEnabled(!active.isEmpty());
    profileMoreBtn_->setEnabled(hasGame());
    if (exportProfileAction_) exportProfileAction_->setEnabled(!active.isEmpty());
    {
        const bool inOv = !hasGame() || Games::profileInOverlay(game_.id, active);
        const bool wasLoading = loading_;
        loading_ = true;
        profileOverlayBtn_->setChecked(inOv);
        loading_ = wasLoading;
        profileOverlayBtn_->setEnabled(hasGame());
        profileOverlayBtn_->setIcon(AqIcons::ui(inOv ? "gauge" : "gauge-off", inOv ? Theme::t().redHi : Theme::t().faint, 16, 1.9));
        const QString who = active.isEmpty() ? T("Без профиля") : T("Профиль «%1»").arg(active);
        profileOverlayBtn_->setToolTip(inOv ? T("%1 показывается в оверлее КПС — нажми, чтобы скрыть").arg(who)
                                            : T("%1 не показывается в оверлее КПС — нажми, чтобы показывать").arg(who));
    }
    const HotkeySpec pk = active.isEmpty() ? HotkeySpec{} : Games::profileKey(game_.id, active);
    profileKeyBtn_->setText(pk.isValid() ? pk.display() : QString());
    // без клавиши — только значок, ровно по центру рамки (с пустым текстом он уезжал влево)
    profileKeyBtn_->setToolButtonStyle(pk.isValid() ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    if (pk.isValid()) { profileKeyBtn_->setMinimumSize(30, 30); profileKeyBtn_->setMaximumSize(QWIDGETSIZE_MAX, 30); }
    else profileKeyBtn_->setFixedSize(30, 30);
    profileKeyBtn_->setToolTip(active.isEmpty() ? T("Клавиша профиля: сначала выбери или создай профиль")
                               : pk.isValid() ? T("Клавиша профиля «%1»: %2 — включает его прямо в игре").arg(active, pk.display())
                                              : T("Назначить клавишу профилю «%1» — включать его прямо в игре").arg(active));
    for (QWidget* w : {static_cast<QWidget*>(profileBox_), static_cast<QWidget*>(newProfileBtn_),
                       static_cast<QWidget*>(saveProfileBtn_), static_cast<QWidget*>(resetProfileBtn_)})
        w->setEnabled(hasGame());
    if (mini_) mini_->setProfiles(names, active, dirty);
    rebuildTrayProfiles();
}

bool MainWindow::confirmLeaveProfile() {
    if (!profileDirty()) return true;
    const QString active = Games::activeProfile(game_.id);
    QMessageBox box(QMessageBox::Question, kAppTitle,
                    T("В профиле «%1» есть несохранённые изменения.").arg(active),
                    QMessageBox::NoButton, dialogParent());
    box.setInformativeText(T("Сохранить их перед переключением?"));
    QPushButton* save = box.addButton(T("Сохранить"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(T("Не сохранять"), QMessageBox::DestructiveRole);
    box.addButton(T("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.exec();
    if (box.clickedButton() == save) {
        Games::saveProfile(game_.id, active, cur_, static_cast<int>(game_.mode));
        return true;
    }
    return box.clickedButton() == discard;
}

void MainWindow::switchProfile(const QString& name) {
    if (!hasGame()) return;
    const QString active = Games::activeProfile(game_.id);
    if (name == active) { updateProfileState(); return; }
    if (!confirmLeaveProfile()) { updateProfileState(); return; }
    if (name.isEmpty()) {
        Games::setActiveProfile(game_.id, QString());
        updateProfileState();
        notify(T("Без профиля: настройки остались как есть."));
        return;
    }
    loadProfile(name);
}

void MainWindow::loadProfile(const QString& name) {
    if (name.isEmpty() || !Games::profileExists(game_.id, name)) return;
    int mode = -1;
    cur_ = Games::loadProfile(game_.id, name, &mode);
    Games::setActiveProfile(game_.id, name);
    if (mode >= 0 && gameModeFromInt(mode) != game_.mode) {
        game_.mode = gameModeFromInt(mode);
        Games::save(game_);
        updateHeader();
    }
    stateToUi();
    commit();
    notify(T("Профиль: %1").arg(name));
}

// Клавиша профиля нажата в игре: для открытой игры — как выбор в списке,
// для другой — её настройки на диске (и в её кликере, если он работает)
void MainWindow::applyProfileFor(const QString& gameId, const QString& profile) {
    bool ok = false;
    GameDef g = Games::get(gameId, &ok);
    if (!ok || !Games::profileExists(gameId, profile)) return;
    if (gameId == game_.id) {
        const QString active = Games::activeProfile(gameId);
        if (active.compare(profile, Qt::CaseInsensitive) == 0) {
            notify(T("Профиль «%1» уже выбран.").arg(profile));
            return;
        }
        if (profileDirty()) {
            notify(T("Профиль не переключён: в «%1» есть несохранённые изменения — сохрани их.").arg(active));
            return;
        }
        loadProfile(profile);
        return;
    }
    int mode = -1;
    const GameState st = Games::loadProfile(gameId, profile, &mode);
    Games::setActiveProfile(gameId, profile);
    Games::saveState(gameId, st);
    if (mode >= 0 && gameModeFromInt(mode) != g.mode) {
        g.mode = gameModeFromInt(mode);
        Games::save(g);
    }
    if (GameRunner* r = findRunner(gameId)) {
        r->def = g;
        r->st = Games::loadState(g);
        if (r->clicker.isRunning()) r->clicker.setConfig(buildConfig(r->def, r->st, r->bound));
    }
    for (int i = 0; i < gameList_->count(); ++i)
        if (gameList_->item(i)->data(GameListDelegate::IdRole).toString() == gameId)
            gameList_->item(i)->setData(GameListDelegate::SubRole, modeSubtitle(g.mode, !st.multi()));
    notify(T("«%1»: профиль %2").arg(g.name, profile));
}

void MainWindow::editProfileKey() {
    if (!hasGame()) return;
    const QString active = Games::activeProfile(game_.id);
    if (active.isEmpty()) return;
    HotkeySpec sp = Games::profileKey(game_.id, active);
    if (!HotkeyDialog::ask(this, T("Клавиша профиля"),
                           T("Нажатие включает профиль «%1» прямо в игре — даже когда AutoQ свёрнут.").arg(active),
                           sp, true, [this](bool s) { suspendHotkeys(s); }))
        return;
    Games::setProfileKey(game_.id, active, sp);
    rebuildBindings();
    updateProfileState();
    if (sp.isValid()) notify(T("Профиль «%1» — клавиша %2.").arg(active, sp.display()));
}

GameState MainWindow::standardState() const {
    GameState s = Games::defaultState(game_);
    s.lockWindow = true;
    return s;
}

GameMode MainWindow::standardMode() const {
    bool ok = false;
    const GameDef d = Games::builtinDefault(game_.id, &ok);
    if (ok) return d.mode;
    const QList<GamePreset> ps = Games::presets(game_);
    return ps.isEmpty() || ps[0].mode < 0 ? GameMode::Sequence : gameModeFromInt(ps[0].mode);
}

void MainWindow::newProfile(int base) {
    if (!hasGame()) return;
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(T("Новый профиль — AutoQ"));
    dlg.setMinimumWidth(440);
    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(20, 18, 20, 16);
    lay->setSpacing(10);
    lay->addWidget(mkLabel(T("Новый профиль для «%1»").arg(game_.name), "cardTitle"));
    lay->addWidget(mkLabel(T("Название"), "fieldLabel"));
    auto* nameEdit = new QLineEdit();
    nameEdit->setPlaceholderText(T("Например: Быстрый скип"));
    nameEdit->setMaxLength(60);
    lay->addWidget(nameEdit);
    lay->addWidget(mkLabel(T("С чего начать"), "fieldLabel"));
    auto* baseBox = new QComboBox();
    baseBox->addItem(T("С нуля — стандартные настройки игры"));
    baseBox->addItem(T("Копия текущих настроек"));
    const QList<GamePreset> presets = Games::presets(game_);
    for (const GamePreset& p : presets) baseBox->addItem(T("Пресет: ") + p.name);
    baseBox->setCurrentIndex(qBound(0, base == 0 ? 1 : base == 1 ? 0 : base, baseBox->count() - 1));
    lay->addWidget(baseBox);
    auto* note = mkLabel(T("Профиль хранит клавиши, режим, скорость и привязку к окну. Меняй настройки как хочешь и жми «Сохранить» — или «Сбросить», чтобы вернуть."), "small");
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* foot = new QHBoxLayout();
    foot->addStretch(1);
    auto* cancel = new QPushButton(T("Отмена"));
    auto* create = new QPushButton(T("Создать"));
    create->setObjectName("primary");
    create->setDefault(true);
    foot->addWidget(cancel);
    foot->addWidget(create);
    lay->addLayout(foot);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(create, &QPushButton::clicked, &dlg, [&]() {
        const QString n = nameEdit->text().trimmed();
        QString why;
        if (!Games::validProfileName(n, &why)) {
            QMessageBox::warning(&dlg, T("Профиль"), why);
            return;
        }
        if (Games::profileExists(game_.id, n) &&
            !askYesNo(&dlg, T("Профиль"), T("Профиль «%1» уже есть. Перезаписать?").arg(n),
                      T("Перезаписать"), T("Отмена"), false))
            return;
        dlg.accept();
    });
    nameEdit->setFocus();
    if (dlg.exec() != QDialog::Accepted) return;

    const QString n = nameEdit->text().trimmed();
    GameState st = cur_;
    GameMode mode = game_.mode;
    const int b = baseBox->currentIndex();
    if (b == 0) {
        st = standardState();
        mode = standardMode();
    } else if (b >= 2 && b - 2 < presets.size()) {
        const GamePreset& p = presets[b - 2];
        st = p.state;
        st.lockWindow = cur_.lockWindow;
        st.frameSync = cur_.frameSync;
        st.frameHz = cur_.frameHz;
        st.soundOn = cur_.soundOn;
        st.soundOff = cur_.soundOff;
        st.steps = cur_.steps;
        st.startDelayMs = cur_.startDelayMs;
        if (p.mode >= 0) mode = gameModeFromInt(p.mode);
        st.intervalMs = qMax(st.intervalMs, st.repeatMin(mode, game_.minIntervalMs));
    }
    Games::saveProfile(game_.id, n, st, static_cast<int>(mode));
    Games::setActiveProfile(game_.id, n);
    cur_ = st;
    if (mode != game_.mode) {
        game_.mode = mode;
        Games::save(game_);
        updateHeader();
    }
    stateToUi();
    commit();
    rebuildBindings();
    notify(T("Профиль «%1» создан.").arg(n));
}

void MainWindow::saveProfile() {
    if (!hasGame()) return;
    const QString active = Games::activeProfile(game_.id);
    if (active.isEmpty()) { newProfile(0); return; }
    Games::saveProfile(game_.id, active, cur_, static_cast<int>(game_.mode));
    updateProfileState();
    notify(T("Профиль «%1» сохранён.").arg(active));
}

void MainWindow::showResetMenu(QWidget* anchor) {
    if (!hasGame()) return;
    const QString active = Games::activeProfile(game_.id);
    const auto& t = Theme::t();
    QMenu m(anchor);
    QAction* revert = m.addAction(AqIcons::ui("undo", t.muted, 16),
                                  active.isEmpty() ? T("Вернуть сохранённое (профиль не выбран)")
                                                   : T("Вернуть сохранённое в «%1»").arg(elide(active, 30)));
    revert->setEnabled(!active.isEmpty() && profileDirty());
    QAction* standard = m.addAction(AqIcons::ui("reset", t.muted, 16), T("Стандартные настройки игры"));
    QAction* chosen = m.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 2)));
    if (chosen == revert) {
        loadProfile(active);
    } else if (chosen == standard) {
        if (active.isEmpty() &&
            !askYesNo(dialogParent(), T("Сбросить"),
                T("Заменить текущие настройки «%1» стандартными?").arg(game_.name), T("Сбросить"), T("Отмена")))
            return;
        cur_ = standardState();
        const GameMode sm = standardMode();
        if (sm != game_.mode) {
            game_.mode = sm;
            Games::save(game_);
            updateHeader();
        }
        stateToUi();
        commit();
        notify(active.isEmpty() ? T("Стандартные настройки")
                                : T("Стандартные настройки — в профиль «%1» ещё не сохранены").arg(active));
    }
}

void MainWindow::deleteProfile() {
    const QString active = Games::activeProfile(game_.id);
    if (active.isEmpty()) return;
    if (!askYesNo(dialogParent(), T("Удалить профиль"),
            T("Удалить профиль «%1»?\nТекущие настройки игры останутся.").arg(active),
            T("Удалить"), T("Отмена"), false))
        return;
    Games::removeProfile(game_.id, active);
    updateProfileState();
    rebuildBindings();
    notify(T("Профиль «%1» удалён.").arg(active));
}

void MainWindow::renameProfile() {
    const QString active = Games::activeProfile(game_.id);
    if (active.isEmpty()) return;
    bool ok = false;
    const QString n = askText(dialogParent(), T("Переименовать профиль"), T("Новое имя:"), active, &ok).trimmed();
    if (!ok || n == active) return;
    QString why;
    if (!Games::validProfileName(n, &why)) { QMessageBox::warning(dialogParent(), T("Профиль"), why); return; }
    if (Games::profileExists(game_.id, n) && n.compare(active, Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(dialogParent(), T("Профиль"), T("Профиль «%1» уже есть.").arg(n));
        return;
    }
    Games::renameProfile(game_.id, active, n);
    updateProfileState();
    rebuildBindings();
}

void MainWindow::exportProfile() {
    const QString active = Games::activeProfile(game_.id);
    if (active.isEmpty()) return;
    if (profileDirty() && askYesNo(dialogParent(), T("Экспорт профиля"),
                                   T("В профиле «%1» есть несохранённые изменения. Сохранить их перед экспортом?").arg(active),
                                   T("Сохранить"), T("Экспортировать как есть")))
        saveProfile();
    QString base = active;
    for (const QChar c : QStringLiteral("<>:\"/\\|?*")) base.replace(c, '_');
    const QString start = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                              .filePath(QString("%1 - %2.ini").arg(game_.name, base).replace(QRegularExpression("[<>:\"/\\\\|?*]"), "_"));
    const QString file = QFileDialog::getSaveFileName(dialogParent(), T("Экспорт профиля"), start,
                                                      T("Профиль AutoQ (*.ini)"));
    if (file.isEmpty()) return;
    QString err;
    if (!Games::exportProfile(game_.id, active, file, &err)) { QMessageBox::warning(dialogParent(), T("Экспорт профиля"), err); return; }
    notify(T("Профиль «%1» сохранён в файл: %2").arg(active, QFileInfo(file).fileName()));
}

void MainWindow::importProfile() {
    if (!hasGame()) return;
    const QString file = QFileDialog::getOpenFileName(dialogParent(), T("Импорт профиля"),
                                                      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                      T("Профиль AutoQ (*.ini);;Все файлы (*)"));
    if (file.isEmpty()) return;
    QString err, from;
    const QString name = Games::importProfile(game_.id, file, &err, &from);
    if (name.isEmpty()) {
        QMessageBox::warning(dialogParent(), T("Импорт профиля"), T("Не удалось импортировать «%1»: %2").arg(QFileInfo(file).fileName(), err));
        return;
    }
    updateProfileState();
    rebuildTrayProfiles();
    // сразу включить новый профиль (если текущие изменения не жалко)
    if (confirmLeaveProfile()) loadProfile(name);
    updateProfileState();
    notify(from.isEmpty() ? T("Импортирован профиль «%1».").arg(name)
                          : T("Импортирован профиль «%1» (сделан для «%2»).").arg(name, from));
}

// ===========================================================================
// Окно игры: правила (процесс / заголовок) и выбранное окно
// ===========================================================================
void MainWindow::updateRules() {
    procEdit_->setItems(game_.processes);
    titleEdit_->setItems(game_.titles);
}

void MainWindow::setRules(const QStringList& procs, const QStringList& titles) {
    if (!hasGame()) return;
    game_.processes = procs;
    game_.titles = titles;
    Games::save(game_);
    pushConfig();
    resultUntil_ = 0;
    refreshLive();
}

void MainWindow::setStatusBox(const QString& state, const QString& text) {
    if (statusBox_->property("state").toString() != state) {
        statusBox_->setProperty("state", state);
        Theme::repolish(statusBox_);
    }
    if (statusBox_->text() != text) statusBox_->setText(text);
}

void MainWindow::setCaptureSeconds(int sec) {
    Prefs::setCaptureSeconds(sec);
    timeSeg_->setSeconds(Prefs::captureSeconds());
    countdown_->setIdleText(T("Выбрать окно (%1 сек)").arg(Prefs::captureSeconds()));
    if (mini_) mini_->setCaptureSeconds(Prefs::captureSeconds());
}

bool MainWindow::bound() const {
    GameRunner* r = findRunner(game_.id);
    return r && r->bound.handle != 0;
}

void MainWindow::bindWindow(const WindowInfo& w) {
    bnd() = w;
    pushConfig();
}

void MainWindow::unbindWindow() {
    bnd() = WindowInfo{};
    pushConfig();
}

void MainWindow::rememberWindow(const WindowInfo& w) {
    if (!w.valid) return;
    if (ownWindow(w)) return;
    auto push = [](QStringList& list, const QString& v) {
        if (v.trimmed().isEmpty()) return;
        list.removeAll(v);
        list.prepend(v);
        while (list.size() > 10) list.removeLast();
    };
    if (w.processKnown) push(recentProcs_, w.process);
    push(recentTitles_, w.title);
    procEdit_->setSuggestions(recentProcs_);
    titleEdit_->setSuggestions(recentTitles_);
}

void MainWindow::startWindowCheck() {
    if (!hasGame()) return;
    if (countdown_->active() || (mini_ && mini_->capturing())) return;
    beginCapture();
}

// Обратный отсчёт «переключись в игру»: в мини-окне, если оно открыто, иначе в главном
void MainWindow::beginCapture() {
    resultUntil_ = 0;
    const int sec = Prefs::captureSeconds();
    if (mini_ && mini_->isVisible()) {
        countdown_->cancel();
        mini_->startCapture();
    } else {
        if (mini_) mini_->cancelCapture();
        countdown_->start(sec);
        setStatusBox("wait", T("Переключись в окно игры «%1»…").arg(game_.name));
    }
    if (!isVisible() && !(mini_ && mini_->isVisible()))
        notify(T("Переключись в игру — через %1 сек AutoQ выберет её окно%2.")
                   .arg(sec).arg(pendingStart_ ? T(" и начнёт") : ""));
    updateTrayState();
    updateMiniLive();
}

void MainWindow::checkWindowDone() {
    const bool wantStart = pendingStart_;
    pendingStart_ = false;
    const WindowInfo w = probe_->query();
    rememberWindow(w);
    auto show = [this](const char* state, const QString& text, int miniState, const QString& miniText) {
        setStatusBox(state, text);
        resultUntil_ = nowMs() + 10000;   // результат держится 10 с
        miniResultState_ = miniState;
        miniResultText_ = miniText;
        updateMiniLive();
        if (!isVisible() && !(mini_ && mini_->isVisible())) notify(text);  // выбирали из трея / клавишей
    };
    auto finish = [this, wantStart](bool ok) {
        if (ok && wantStart) startClicker();
        else if (!ok && wantStart) notify(T("Кликер не запущен: окно игры не выбрано."));
        updateTrayState();
        updateGameItems();
    };
    if (!w.valid) {
        show("bad", T("✗ Не удалось определить активное окно."), 2, T("Не удалось определить окно"));
        finish(false);
        return;
    }
    if (ownWindow(w)) {
        show("warn", T("Выбралось окно AutoQ — нажми ещё раз и переключись в игру за %1 сек.")
                         .arg(Prefs::captureSeconds()), 2, T("Выбралось окно AutoQ — попробуй ещё раз"));
        finish(false);
        return;
    }
    const QString who = QString("«%1»%2").arg(elide(w.title.isEmpty() ? T("без заголовка") : w.title),
                                              w.processKnown ? QString(" (%1)").arg(w.process) : QString());
    if (matchesRule(game_.rule(), w)) {
        bindWindow(w);
        show("ok", T("✓ Выбрано окно игры %1 — кликаю только в нём.").arg(who), 1, T("Окно игры выбрано"));
        finish(true);
        return;
    }
    // Окно не подходит под правила — может, это и есть игра (своя сборка,
    // другой лаунчер, или правил ещё нет): дописываем процесс и заголовок в правила.
    const bool hasProcs = !game_.processes.isEmpty(), hasTitles = !game_.titles.isEmpty();
    QStringList addProcs, addTitles;
    if (w.processKnown && !w.process.isEmpty() && !processMatches(game_.processes, w) && (hasProcs || !hasTitles))
        addProcs << w.process;
    if (!w.title.isEmpty() && ((hasTitles && !titleMatches(game_.titles, w.title)) || (!hasProcs && !hasTitles)))
        addTitles << w.title;
    if (!w.processKnown && hasProcs && !hasTitles && !w.title.isEmpty()) addTitles << w.title;
    // у игры ещё нет правил — первое выбранное окно и становится правилом (без вопросов)
    const bool noRules = !hasProcs && !hasTitles;
    QStringList what;
    if (!addProcs.isEmpty()) what << T("процесс %1").arg(addProcs.join(", "));
    if (!addTitles.isEmpty() && !(noRules && !addProcs.isEmpty())) what << T("заголовок «%1»").arg(elide(addTitles.join(", "), 60));
    if (noRules && !addProcs.isEmpty()) addTitles.clear();    // процесса достаточно — заголовок меняется
    QWidget* parent = dialogParent();
    const bool ask = !what.isEmpty() && (noRules ||
        askYesNo(parent, T("Окно игры"),
            T("Окно %1 не подходит под правила «%2».\n\nЭто окно игры? AutoQ допишет в правила: %3 — и будет кликать только в этом окне.").arg(who, game_.name, what.join(T(" и "))),
            T("Да, это игра"), T("Нет"), false));
    if (ask) {
        QStringList p = game_.processes, t = game_.titles;
        for (const QString& s : addProcs) if (!p.contains(s, Qt::CaseInsensitive)) p << s;
        for (const QString& s : addTitles) if (!t.contains(s, Qt::CaseInsensitive)) t << s;
        game_.processes = p;
        game_.titles = t;
        Games::save(game_);
        updateRules();
        bindWindow(w);
        show("ok", noRules ? T("✓ Выбрано окно %1 — AutoQ запомнил его для «%2».").arg(who, game_.name)
                           : T("✓ Правила дополнены — выбрано окно %1.").arg(who), 1, T("Окно игры выбрано"));
        finish(true);
        return;
    }
    show("bad", T("✗ Это не окно игры: %1 — окно не выбрано. Переключись именно в «%2».")
                    .arg(who, game_.name), 2, T("Это не окно игры — окно не выбрано"));
    finish(false);
}

// ===========================================================================
// Кликеры игр
// ===========================================================================
GameRunner& MainWindow::runner(const QString& id) {
    auto it = runners_.find(id);
    if (it == runners_.end()) it = runners_.emplace(id, std::make_unique<GameRunner>()).first;
    return *it->second;
}

GameRunner* MainWindow::findRunner(const QString& id) const {
    auto it = runners_.find(id);
    return it == runners_.end() ? nullptr : it->second.get();
}

bool MainWindow::isRunning(const QString& id) const {
    GameRunner* r = findRunner(id);
    return r && r->clicker.isRunning();
}

QStringList MainWindow::runningIds() const {
    QStringList out;
    for (const auto& r : runners_)
        if (!r.first.isEmpty() && r.second->clicker.isRunning()) out << r.first;
    return out;
}

QString MainWindow::focusRunnerId() const {
    if (hasGame() && (isRunning(game_.id) || pendingStart_)) return game_.id;
    const QStringList run = runningIds();
    return run.isEmpty() ? game_.id : run.first();
}

QString MainWindow::actionsLabel(const QList<ActionStep>& list, int from, int to) const {
    QStringList l;
    for (int i = from; i < to && i < list.size(); ++i) {
        if (l.size() == 3) { l << QString("+%1").arg(to - i); break; }
        l << list[i].label();
    }
    return l.join(" · ");
}

OverlayData MainWindow::overlayData() {
    OverlayData d;
    d.hasGame = hasGame();
    auto entryFor = [this](const QString& id) {
        OverlayEntry e;
        GameRunner* r = findRunner(id);
        const bool isCur = id == game_.id;
        const GameDef g = isCur || !r ? game_ : r->def;
        const GameState st = isCur || !r ? cur_ : r->st;
        e.key = id;
        e.running = r && r->clicker.isRunning();
        e.pending = isCur && pendingStart_;
        e.starting = r && r->clicker.waitingStart();
        e.allowed = r && r->clicker.allowedNow();
        e.pauseReason = r ? r->clicker.pauseReason() : 0;
        e.hold = g.mode == GameMode::Hold;
        if (r) {
            e.c1 = r->clicker.actionCount(0);
            for (int i = 1; i < Clicker::kCounters; ++i) e.c2 += r->clicker.actionCount(i);
            e.total = r->clicker.clickCount();
        }
        // в «По шагам» — каждая клавиша, которую шаги нажимают (так их считает кликер)
        const QList<ActionStep> pl = st.pressList(g.mode);
        e.a1 = actionsLabel(pl, 0, 1);
        e.a2 = pl.size() > 1 ? actionsLabel(pl, 1, pl.size()) : QString();
        e.game = g.name;
        e.profile = Games::activeProfile(id);
        return e;
    };
    // открытая игра первой, за ней остальные работающие — по порядку в списке игр;
    // у каждой — только если её профилю разрешено «Показывать в оверлее»
    const QString first = focusRunnerId();
    QStringList ids;
    if (!first.isEmpty()) ids << first;
    const QStringList run = runningIds();
    for (int i = 0; i < gameList_->count(); ++i) {
        const QString id = gameList_->item(i)->data(GameListDelegate::IdRole).toString();
        if (run.contains(id) && !ids.contains(id)) ids << id;
    }
    for (const QString& id : run)
        if (!ids.contains(id)) ids << id;
    for (const QString& id : ids) {
        if (!Games::profileInOverlay(id, Games::activeProfile(id))) continue;
        d.entries << entryFor(id);
        if (d.entries.size() >= 6) break;     // больше шести плашек закроют полэкрана
    }
    return d;
}

// ===========================================================================
// Запуск, клавиши
// ===========================================================================
QString MainWindow::startProblemFor(const GameDef& g, const GameState& st) const {
    if (g.id.isEmpty())
        return T("сначала добавь игру — кнопка «Добавить игру».");
    if (!const_cast<MainWindow*>(this)->clk().inputAvailable())   // доступ к вводу один на всех
#ifdef __APPLE__
        return T("разреши AutoQ «Универсальный доступ»: Системные настройки → Конфиденциальность и безопасность → Универсальный доступ.");
#else
        return T("нет доступа к X11 — нажатия отправлять некуда.");
#endif
    // действие = клавиша, которая что-то включает: кликер нажимал бы её сам
    for (const HkBinding& b : bindings_) {
        if (!(b.spec.mods == 0 || b.spec.mods == ModShift)) continue;
        // сочетание из двух клавиш ловится только с клавиатуры человека — нажатия кликера его не включат
        if (b.spec.isChord()) continue;
        for (const ActionStep& a : st.runList(g.mode)) {
            const bool same = b.spec.isMouse() ? (a.mouse && a.button == b.spec.mouseButton())
                                               : (!a.mouse && a.key.compare(b.spec.key, Qt::CaseInsensitive) == 0);
            if (!same) continue;
            for (const HkTarget& t : b.targets)
                if (t.kind == 0 && t.game == g.id)
                    return T("клавиша действия совпадает с клавишей старт/стоп (%1) — кликер выключал бы сам себя.").arg(b.spec.display());
            return T("клавиша действия %1 — это клавиша другой игры или профиля в AutoQ: кликер нажимал бы её сам.").arg(b.spec.display());
        }
    }
    return QString();
}

QString MainWindow::startProblem() const { return startProblemFor(game_, cur_); }

void MainWindow::toggleRunning() { toggleFrom(false); }

// Кнопка Старт (главное и мини-окно): категория не используется — кнопка её включает
void MainWindow::startPressed() {
    if (hasGame() && !Games::armed(game_.id) && !clk().isRunning() && !pendingStart_) setArmed(game_.id, true, true);
    else toggleRunning();
}

bool MainWindow::capturingAnyKey() const {
    return captureBtn_->isCapturing() || (mini_ && mini_->capturingKey()) || (hotkey_ && hotkey_->suspended());
}

void MainWindow::suspendHotkeys(bool on) {
    if (hotkey_) hotkey_->setSuspended(on);
}

void MainWindow::onHotkeyEvent(const QString& specStr, bool down) {
    if (qEnvironmentVariableIsSet("AQ_DEBUG_HK")) qWarning("hotkey %s %s", qPrintable(specStr), down ? "down" : "up");
    const HotkeySpec sp = HotkeySpec::parse(specStr);
    const HkBinding* bind = nullptr;
    for (const HkBinding& b : bindings_) if (b.spec == sp) { bind = &b; break; }
    if (!bind) return;
    const QList<HkTarget> targets = bind->targets;   // список может перестроиться по ходу
    if (!down) {
        // «пока держу»: отпустил — стоп
        for (const HkTarget& t : targets) {
            if (t.kind != 0 || Games::keyMode(t.game) != 1) continue;
            if (t.game == game_.id && pendingStart_) cancelPending();
            GameRunner* r = findRunner(t.game);
            if (r && r->holdStarted) {
                r->holdStarted = false;
                if (r->clicker.isRunning()) stopGame(t.game);
            }
        }
        return;
    }
    if (capturingAnyKey()) return;
    // сквозная клавиша (буква, цифра) — пока печатают в окне AutoQ, это не команда
    // (две клавиши «Tab+F» так не печатают — это всегда команда)
    if (sp.passthrough() && !sp.isMouse() && !sp.isChord() && QApplication::activeWindow()) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || qobject_cast<QAbstractSpinBox*>(fw) ||
                   fw->inherits("QAbstractItemView")))
            return;
    }
    QStringList toggles, holds;
    for (const HkTarget& t : targets) {
        if (t.kind == 1) setArmed(t.game, !Games::armed(t.game), true);
        else if (t.kind == 2) applyProfileFor(t.game, t.profile);
        else if (Games::keyMode(t.game) == 1) holds << t.game;
        else toggles << t.game;
    }
    // одна клавиша на нескольких играх: работает хоть одна — выключить все, иначе включить все
    if (!toggles.isEmpty()) {
        bool any = false;
        for (const QString& id : toggles) any = any || isRunning(id) || (id == game_.id && pendingStart_);
        for (const QString& id : toggles) {
            if (any) {
                if (id == game_.id && pendingStart_) cancelPending();
                else if (isRunning(id)) stopGame(id);
            } else if (id == game_.id) {
                toggleFrom(true);
            } else {
                startBackground(id, true);
            }
        }
    }
    for (const QString& id : holds) {
        if (isRunning(id)) continue;
        if (id == game_.id) toggleFrom(true);
        else startBackground(id, true);
        runner(id).holdStarted = true;
    }
}

void MainWindow::toggleGame(const QString& id, bool fromHotkey) {
    if (id == game_.id) { toggleFrom(fromHotkey); return; }
    if (isRunning(id)) stopGame(id);
    else startBackground(id, fromHotkey);
}

bool MainWindow::refuseUnused(const QString& id) {
    if (id.isEmpty() || Games::armed(id)) return false;
    notify(T("«%1» не используется — включи кружок у категории, чтобы её можно было запустить.").arg(Games::get(id).name));
    return true;
}

void MainWindow::toggleFrom(bool fromHotkey) {
    if (!hasGame()) {
        notify(T("Нельзя запустить: ") + startProblem());
        return;
    }
    if (clk().isRunning()) {
        stopGame(game_.id);
        return;
    }
    if (pendingStart_) {                 // второе нажатие во время отсчёта — отмена
        cancelPending();
        return;
    }
    if (refuseUnused(game_.id)) return;
    const QString problem = startProblem();
    if (!problem.isEmpty()) {
        if (QWidget* p = dialogParent(); p && !fromHotkey) QMessageBox::warning(p, kAppTitle, T("Нельзя запустить: ") + problem);
        else notify(T("Нельзя запустить: ") + problem);
        return;
    }
    if (!cur_.lockWindow) { startClicker(); return; }
    // Привязка к окну: кликаем только в выбранном окне игры
    if (bound() && probe_->exists(bnd().handle)) {
        if (fromHotkey) {               // нажали клавишу в другом окне той же игры — берём его
            const WindowInfo w = probe_->query();
            if (w.handle != bnd().handle && matchesRule(game_.rule(), w)) bindWindow(w);
        }
        startClicker();
        return;
    }
    if (fromHotkey) {                   // клавиша нажата прямо в игре — её окно и выбираем
        const WindowInfo w = probe_->query();
        if (matchesRule(game_.rule(), w)) {
            // шёл отсчёт «Выбрать окно» — он больше не нужен (иначе в конце выбрал бы заново)
            countdown_->cancel();
            if (mini_) mini_->cancelCapture();
            bindWindow(w);
            setStatusBox("ok", T("✓ Выбрано окно игры «%1».").arg(elide(w.title.isEmpty() ? w.process : w.title)));
            resultUntil_ = nowMs() + 4000;
            miniResultState_ = 1;
            miniResultText_ = T("Окно игры выбрано");
            startClicker();
            return;
        }
    }
    // Окно ещё не выбрано: сначала выбрать (отсчёт), потом старт
    unbindWindow();
    pendingStart_ = true;
    beginCapture();
    updateGameItems();
}

void MainWindow::startClicker() {
    pushConfig();
    const bool was = clk().isRunning();
    clk().start();
    if (!was) Sounds::play(Sounds::On, Games::keyMode(game_.id) == 1, cur_.soundOn);
    rateHist_.clear();
    updateTrayState();
    updateGameItems();
    refreshLive();
}

void MainWindow::stopGame(const QString& id) {
    GameRunner* r = findRunner(id);
    if (!r) return;
    if (r->clicker.isRunning()) Sounds::play(Sounds::Off, Games::keyMode(id) == 1, r->st.soundOff);
    r->clicker.stop();
    r->holdStarted = false;
    updateTrayState();
    updateGameItems();
    refreshLive();
}

bool MainWindow::startBackground(const QString& id, bool fromHotkey) {
    bool ok = false;
    const GameDef g = Games::get(id, &ok);
    if (!ok || refuseUnused(id)) return false;
    const GameState st = Games::loadState(g);
    GameRunner& r = runner(id);
    const QString problem = startProblemFor(g, st);
    if (!problem.isEmpty()) {
        notify(T("«%1» не запущена: ").arg(g.name) + problem);
        return false;
    }
    if (st.lockWindow) {
        if (!(r.bound.handle && probe_->exists(r.bound.handle))) {
            r.bound = WindowInfo{};
            // клавиша нажата в игре — её окно; иначе ищем открытое окно этой игры
            const WindowInfo w = probe_->query();
            if (fromHotkey && !ownWindow(w) && matchesRule(g.rule(), w)) {
                r.bound = w;
            } else {
                for (const WindowInfo& x : probe_->listWindows())
                    if (!ownWindow(x) && matchesRule(g.rule(), x)) { r.bound = x; break; }
            }
        }
        if (!r.bound.handle) {
            notify(g.rule().isEmpty()
                       ? T("«%1» не запущена: у игры нет правил окна — открой её в AutoQ и нажми «Выбрать окно».").arg(g.name)
                       : T("«%1» не запущена: окно игры не найдено — запусти игру или выбери окно в AutoQ.").arg(g.name));
            return false;
        }
    }
    r.def = g;
    r.st = st;
    r.clicker.setConfig(buildConfig(g, st, r.bound));
    r.clicker.start();
    Sounds::play(Sounds::On, Games::keyMode(id) == 1, st.soundOn);
    updateTrayState();
    updateGameItems();
    return true;
}

void MainWindow::cancelPending() {
    if (!pendingStart_) return;
    pendingStart_ = false;
    countdown_->cancel();
    if (mini_) mini_->cancelCapture();
    updateTrayState();
    updateGameItems();
}

void MainWindow::rebuildBindings() {
    QList<HkBinding> list;
    auto add = [&list](const HotkeySpec& sp, const HkTarget& t) {
        if (!sp.isValid()) return;
        for (HkBinding& b : list)
            if (b.spec == sp) {
                b.spec.block = b.spec.block || sp.block;    // одна клавиша — забрали у игры для одного, значит, для всех
                b.targets << t;
                return;
            }
        list << HkBinding{sp, {t}};
    };
    for (const GameDef& g : Games::list()) {
        // клавиша «используется / нет» работает всегда — ею категорию и включают
        add(Games::armKey(g.id), HkTarget{1, g.id, QString()});
        // не используется — ни старт/стоп, ни профили (открыта она или нет)
        if (!Games::armed(g.id)) continue;
        add(g.id == game_.id ? hotkeySpec_ : Games::startKey(g.id), HkTarget{0, g.id, QString()});
        for (const QString& p : Games::profiles(g.id)) add(Games::profileKey(g.id, p), HkTarget{2, g.id, p});
    }
    bindings_ = list;
    if (hotkey_) {
        QList<HotkeySpec> specs;
        for (const HkBinding& b : bindings_) specs << b.spec;
        const QList<HotkeySpec> now = hotkey_->bindings();
        bool same = specs == now;           // == не смотрит на block — сверяем и его
        for (int i = 0; same && i < specs.size(); ++i) same = specs[i].block == now[i].block;
        if (!same) hotkey_->setBindings(specs);
    }
}

void MainWindow::setHotkey(const HotkeySpec& sp, bool save) {
    if (!sp.isValid()) return;
    hotkeySpec_ = sp;
    hotkeyPick_->setCurrent(sp.isChord() ? QString() : sp.key, sp.display());   // в списке сочетаний нет
    if (save && hasGame()) {
        Games::setStartKey(game_.id, sp);
        hotkeySpec_.block = Games::startKeyBlock(game_.id);     // та же клавиша — как было, другая — пропускается
    }
    hotkeyStatus_->hide();
    updateHotkeyInfo();
    if (mini_) mini_->setHotkey(sp.isChord() ? QString() : sp.key, sp.display());
    rebuildBindings();
    updateTrayState();
    updateGameItems();
}

// Под клавишей: как она работает (сквозная, сочетание, не доходит до игры) и переключатель
void MainWindow::updateHotkeyInfo() {
    const HotkeySpec& sp = hotkeySpec_;
    blockRow_->setVisible(sp.canBlock());
    {
        QSignalBlocker b(blockSwitch_);
        blockSwitch_->setChecked(sp.block && sp.canBlock());
    }
    if (sp.isChord()) {
        hotkeyInfo_->setText(T("«%1» — две клавиши: зажми %2 и нажми %3. Обе при этом нажимаются в игре как обычно.")
                                 .arg(sp.display(), Keys::label(sp.hold), Keys::label(sp.key)));
        hotkeyInfo_->show();
    } else if (sp.canBlock() && sp.block) {
        hotkeyInfo_->setText(T("«%1» только запускает и останавливает кликер — в игру и другие программы нажатие не попадает.")
                                 .arg(sp.display()));
        hotkeyInfo_->show();
    } else if (sp.isMouse()) {
        hotkeyInfo_->setText(T("«%1» — кнопка мыши: включает кликер и при этом нажимается в игре как обычно.")
                                 .arg(sp.display()));
        hotkeyInfo_->show();
    } else if (sp.passthrough()) {
        hotkeyInfo_->setText(T("«%1» — сквозная: включает кликер и при этом нажимается в игре как обычно.")
                                 .arg(sp.display()));
        hotkeyInfo_->show();
    } else {
        hotkeyInfo_->hide();
    }
}

void MainWindow::onCaptureStarted() {
    // снимаем глобальные клавиши, иначе они сами перехватят нажатие
    suspendHotkeys(true);
    hotkeyStatus_->setText(T("Нажми свою клавишу, сочетание, две клавиши (зажми Tab и нажми F) или кнопку мыши (Esc — отмена)…"));
    hotkeyStatus_->show();
}

void MainWindow::onCaptureFinished(bool accepted, const HotkeySpec& spec) {
    suspendHotkeys(false);
    if (accepted) setHotkey(spec, true);
    else hotkeyStatus_->hide();
}

void MainWindow::updateHotkeyStatus() {
    if (!hotkey_ || captureBtn_->isCapturing() || !hasGame() || !hotkey_->applied()) return;
    QString text;
    // клавиша не назначена нарочно (игра «не используется») — спрашивать систему не о чем:
    // она ответила бы «недоступна», и вышло бы ложное «нет подключения к X11»
    bool bound = false;
    for (const HkBinding& b : bindings_) bound = bound || b.spec == hotkeySpec_;
    switch (bound ? hotkey_->statusOf(hotkeySpec_) : GlobalHotkey::Status::Ok) {
        case GlobalHotkey::Status::Busy:
            text = T("⚠ Клавиша %1 занята другой программой или системой — выбери другую.")
                       .arg(hotkeySpec_.display());
#ifdef _WIN32
            if (hotkeySpec_.key == "f12" && hotkeySpec_.mods == 0 && !hotkeySpec_.isChord())
                text = T("⚠ F12 Windows резервирует под отладчик — выбери другую клавишу.");
#endif
            break;
        case GlobalHotkey::Status::Invalid:
            text = T("⚠ %1 нельзя назначить клавишей старт/стоп.").arg(hotkeySpec_.display());
            break;
        case GlobalHotkey::Status::Unavailable:
#ifdef __APPLE__
            text = T("⚠ Горячая клавиша ждёт разрешения: Системные настройки → Конфиденциальность и безопасность → Универсальный доступ → AutoQ.");
            break;
#endif
            text = hotkeySpec_.passthrough() || hotkeySpec_.isMouse()
                       ? T("⚠ Для «%1» нужен XInput2 — выбери F-клавишу или сочетание с Ctrl/Alt.")
                             .arg(hotkeySpec_.display())
                       : T("⚠ Глобальные клавиши недоступны (нет подключения к X11).");
            break;
        default:
            break;
    }
    // «Tab» на одном и «Tab+F» на другом: Tab срабатывает сразу, ещё до F
    if (text.isEmpty())
        for (const HkBinding& b : bindings_) {
            if (b.spec.mods != hotkeySpec_.mods || b.spec.isChord() == hotkeySpec_.isChord()) continue;
            const HotkeySpec& chord = b.spec.isChord() ? b.spec : hotkeySpec_;
            const HotkeySpec& single = b.spec.isChord() ? hotkeySpec_ : b.spec;
            if (chord.hold != single.key) continue;
            text = T("⚠ «%1» и «%2» мешают друг другу: %1 срабатывает сразу, ещё до второй клавиши. Лучше выбрать другую.")
                       .arg(single.display(), chord.display());
            break;
        }
    if (text.isEmpty()) {
        if (hotkeyStatus_->text().startsWith("⚠")) hotkeyStatus_->hide();
    } else {
        hotkeyStatus_->setText(text);
        hotkeyStatus_->show();
    }
}

// ===========================================================================
// Монитор запуска: время, круги, нажатия, текущий шаг
// ===========================================================================
void MainWindow::updateMonitor(bool full) {
    if (!hasGame() || !board_->blockVisible("monitor")) return;
    GameRunner* r = findRunner(game_.id);
    const bool run = r && r->clicker.isRunning();
    const bool hold = game_.mode == GameMode::Hold;
    if (full) {
        const long long ms = r ? r->clicker.runMs() : 0;
        const long long sec = ms / 1000;
        const QString time = sec >= 3600 ? QString("%1:%2:%3").arg(sec / 3600).arg(sec / 60 % 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'))
                                         : QString("%1:%2").arg(sec / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'));
        monTime_->setText(time);
        monCycles_->setText(hold ? QStringLiteral("—") : num(r ? static_cast<qint64>(r->clicker.cycleCount()) : 0));
        monPresses_->setText(num(r ? static_cast<qint64>(r->clicker.clickCount()) : 0));
        QString rate = QStringLiteral("—");
        if (run && !hold && rateHist_.size() >= 2) {
            const double dt = (rateHist_.last().t - rateHist_.first().t) / 1000.0;
            if (dt > 0.2) rate = dec((rateHist_.last().n - rateHist_.first().n) / dt);
        }
        monRate_->setText(rate);
        QString st;
        QString kind = "none";
        if (!run) st = ms > 0 ? T("Остановлен") : T("Ещё не запускали");
        else if (r->clicker.waitingStart()) { st = T("Ждёт старта"); kind = "wait"; }
        else if (!r->clicker.allowedNow()) {
            st = r->clicker.pauseReason() == 2 ? T("Пауза: курсор вне игры") : T("Пауза: не окно игры");
            kind = "warn";
        } else { st = T("Работает"); kind = "run"; }
        monState_->setText(st);
        if (monState_->property("state").toString() != kind) {
            monState_->setProperty("state", kind);
            Theme::repolish(monState_);
        }
    }
    // что жмётся сейчас
    const GameMode mode = run ? r->def.mode : game_.mode;
    const QList<ActionStep> list = run ? r->st.runList(mode) : cur_.runList(mode);
    auto stepName = [mode](const ActionStep& a) {
        if (mode != GameMode::Steps) return a.label();
        return a.label() + ' ' + a.pressMark();
    };
    QString text;
    if (list.isEmpty()) {
        text = QString();
    } else if (run && r->clicker.waitingStart()) {
        text = T("Сейчас: пауза «старт через»");
    } else if (run && !r->clicker.allowedNow()) {
        text = T("Сейчас: пауза — ничего не нажимается");
    } else if (mode == GameMode::Hold) {
        QStringList l;
        for (const ActionStep& a : list) l << a.label();
        text = (run ? T("Зажато: %1") : T("Зажмёт: %1")).arg(l.join(" + "));
    } else if (mode == GameMode::Combo && list.size() > 1) {
        QStringList l;
        for (const ActionStep& a : list) l << a.label();
        text = T("Все сразу: %1").arg(l.join(" + "));
    } else if (!run) {
        text = list.size() == 1 ? T("Действие: %1").arg(stepName(list[0]))
                                : T("В цепочке шагов: %1").arg(list.size());
    } else {
        const int step = r->clicker.currentStep();
        if (step >= 0 && step < list.size())
            text = list.size() == 1 ? T("Сейчас: %1").arg(stepName(list[step]))
                                    : T("Сейчас: шаг %1 из %2 — %3").arg(step + 1).arg(list.size()).arg(stepName(list[step]));
        else text = T("Сейчас: пауза перед повтором");
    }
    if (monStep_->text() != text) monStep_->setText(text);
}

// ===========================================================================
// Живые показатели (раз в 0.5 с)
// ===========================================================================
void MainWindow::refreshLive() {
    const bool run = hasGame() && clk().isRunning();
    const unsigned long long count = hasGame() ? clk().clickCount() : 0;
    counterLabel_->setText(num(static_cast<qint64>(count)));
    if (counterBox_->property("running").toBool() != run) {
        counterBox_->setProperty("running", run);
        Theme::repolish(counterBox_);
        for (QLabel* l : counterBox_->findChildren<QLabel*>()) Theme::repolish(l);
    }
    // сколько жмёт на самом деле — по счётчику за последние ~2 секунды
    if (run) {
        const qint64 now = nowMs();
        if (!rateHist_.isEmpty() && count < rateHist_.last().n) rateHist_.clear();
        rateHist_ << RateSample{now, count};
        while (rateHist_.size() > 2 && now - rateHist_.first().t > 2100) rateHist_.removeFirst();
        if (rateHist_.size() >= 2 && game_.mode != GameMode::Hold) {
            const double dt = (rateHist_.last().t - rateHist_.first().t) / 1000.0;
            const double real = dt > 0.2 ? (rateHist_.last().n - rateHist_.first().n) / dt : 0;
            counterRate_->setText(T("сейчас %1 /с · %2 /мин").arg(dec(real), num(qRound64(real * 60))));
            counterRate_->show();
        }
        updateRate();
    } else if (counterRate_->isVisible()) {
        counterRate_->hide();
        rateHist_.clear();
        updateRate();
    }
    updateHotkeyStatus();
    updateMonitor(true);
    if (run && stepTimer_ && !stepTimer_->isActive()) stepTimer_->start();

    const bool canProbe = probe_->available();
    // окна игр закрыли — выбор сбрасываем (у каждой игры свой)
    if (canProbe) {
        for (auto& it : runners_) {
            GameRunner& r = *it.second;
            if (it.first.isEmpty() || !r.bound.handle || probe_->exists(r.bound.handle)) continue;
            r.bound = WindowInfo{};
            const bool wasRunning = r.clicker.isRunning();
            if (wasRunning && Prefs::stopOnGameClose()) {
                Sounds::play(Sounds::Off, false, r.st.soundOff);
                r.clicker.stop();
                r.holdStarted = false;
                notify(T("Кликер «%1» выключен: окно игры закрылось.").arg(r.def.name.isEmpty() ? it.first : r.def.name));
            } else {
                r.clicker.setConfig(buildConfig(r.def, r.st, r.bound));
            }
            if (it.first == game_.id) {
                setStatusBox("warn", T("Окно игры закрылось — выбери его снова, когда игра запустится."));
                resultUntil_ = nowMs() + 8000;
                miniResultState_ = 2;
                miniResultText_ = T("Окно игры закрылось — выбери снова");
            }
            updateTrayState();
        }
    }
    // «работает» в списке игр
    for (int i = 0; i < gameList_->count(); ++i) {
        QListWidgetItem* it = gameList_->item(i);
        const QString id = it->data(GameListDelegate::IdRole).toString();
        const bool r = isRunning(id) || (id == game_.id && pendingStart_);
        if (it->data(GameListDelegate::RunningRole).toBool() != r) {
            it->setData(GameListDelegate::RunningRole, r);
            updateTrayState();
        }
    }
    if (!hasGame()) { updateMiniLive(); return; }

    WindowInfo w;
    if (canProbe) {
        w = probe_->query();
        rememberWindow(w);
    }
    const bool waiting = countdown_->active() || (mini_ && mini_->capturing());
    if (waiting || nowMs() < resultUntil_) { updateMiniLive(); return; }

    const QString hk = hotkeySpec_.display();
    if (!cur_.lockWindow) {
        setStatusBox("warn", T("Привязка выключена — нажимает в любом активном окне."));
    } else if (!canProbe) {
        setStatusBox("bad", T("⚠ Нет доступа к X11 — окно игры не определить."));
    } else if (bound()) {
        const WindowInfo b = bnd();
        ClickerConfig c;
        c.lockToWindow = true;
        c.rule = game_.rule();
        c.boundHandle = b.handle;
        c.boundPid = b.pid;
        const QString name = elide(b.title.isEmpty() ? b.process : b.title, 36);
        const bool mouseUsed = cur_.usesMouse(game_.mode) && Prefs::mouseOnlyOverGame();
        if (clickAllowed(c, w) && mouseUsed && !probe_->pointerOver(b.handle))
            setStatusBox("warn", T("Окно «%1» активно, но курсор не над ним — мышью кликаю только над окном игры%2").arg(name, run ? T(" (пауза)") : ""));
        else if (clickAllowed(c, w))
            setStatusBox("ok", run && clk().waitingStart() ? T("✓ Окно «%1» активно — старт через %2 мс").arg(name, num(cur_.startDelayMs))
                             : run ? T("✓ Окно «%1» активно — нажимает").arg(name)
                                   : T("✓ Выбрано окно «%1» — оно активно").arg(name));
        else if (ownWindow(w))
            setStatusBox("none", T("Выбрано окно «%1». Сейчас активно окно AutoQ%2")
                                     .arg(name, run ? T(" — пауза") : ""));
        else
            setStatusBox("none", T("Выбрано окно «%1». Сейчас активно другое окно%2")
                                     .arg(name, run ? T(" — пауза") : ""));
    } else if (game_.rule().isEmpty()) {
        setStatusBox("warn", T("У игры пока нет правил окна — нажми «Выбрать окно» и переключись в игру: AutoQ запомнит её сам."));
    } else {
        setStatusBox("none", T("Окно игры не выбрано — кликер никуда не нажимает. Нажми «Выбрать окно» или %1 прямо в игре.").arg(hk));
    }
    updateMiniLive();
}

void MainWindow::syncMini() {
    if (!mini_) return;
    const auto& t = Theme::t();
    QList<MiniWindow::GameItem> items;
    for (const GameDef& g : Games::list())
        items << MiniWindow::GameItem{g.id, g.name,
                                      AqIcons::gameTile(g.icon, QColor(g.color), g.image, 22, 2.0, false, t.light)};
    mini_->setGames(items, game_.id);
    mini_->setState(cur_, game_.mode, repeatMin());
    mini_->setHotkey(hotkeySpec_.isChord() ? QString() : hotkeySpec_.key, hotkeySpec_.display());
    updateMiniLive();
}

void MainWindow::updateMiniLive() {
    if (!mini_) return;
    QString caption;
    if (game_.mode == GameMode::Hold) {
        caption = T("зажатий за сессию · режим «Зажать»");
    } else {
        caption = T("нажатий за сессию · ≈ %1 в секунду").arg(dec(perSecond(game_, cur_)));
    }
    const bool run = hasGame() && clk().isRunning();
    mini_->setCount(num(static_cast<qint64>(hasGame() ? clk().clickCount() : 0)), caption);
    const bool waiting = countdown_->active() || mini_->capturing();
    const QString proc = bound() ? bnd().process : game_.processes.value(0);
    const QString st = statusBox_->property("state").toString();
    if (!hasGame()) mini_->setWindowStatus(2, T("Игра не выбрана"), QString());
    else if (waiting) mini_->setWindowStatus(3, T("Переключись в окно игры…"), proc);
    else if (nowMs() < resultUntil_) mini_->setWindowStatus(miniResultState_, miniResultText_, proc);
    else if (!cur_.lockWindow) mini_->setWindowStatus(2, T("Привязка к окну выключена"), QString());
    else if (!bound()) mini_->setWindowStatus(2, T("Окно не выбрано — кликер не нажимает"), proc);
    else if (st == "ok") mini_->setWindowStatus(1, run ? T("Окно игры активно — жмёт") : T("Окно выбрано и активно"), proc);
    else if (st == "warn") mini_->setWindowStatus(2, T("Курсор не над окном игры — пауза"), proc);
    else mini_->setWindowStatus(0, run ? T("Пауза — сейчас не окно игры") : T("Окно выбрано · сейчас не активно"), proc);
}

// ===========================================================================
// Трей, настройки
// ===========================================================================
void MainWindow::buildTray() {
    tray_ = new QSystemTrayIcon(this);
    trayMenu_ = new QMenu(this);
    trayStatusAction_ = trayMenu_->addAction(AqIcons::appIcon(), AQ_NAME);
    trayStatusAction_->setEnabled(false);
    trayMenu_->addSeparator();
    trayToggleAction_ = trayMenu_->addAction(T("Старт"));
    connect(trayToggleAction_, &QAction::triggered, this, &MainWindow::toggleRunning);
    QAction* stopAll = trayMenu_->addAction(T("Остановить все игры"));
    stopAll->setObjectName("stopAll");
    stopAll->setVisible(false);
    connect(stopAll, &QAction::triggered, this, [this]() {
        cancelPending();
        for (const QString& id : runningIds()) stopGame(id);
    });
    trayGameMenu_ = trayMenu_->addMenu(T("Игра"));
    trayProfileMenu_ = trayMenu_->addMenu(T("Профиль"));
    trayCheckAction_ = trayMenu_->addAction(T("Выбрать окно игры"));
    connect(trayCheckAction_, &QAction::triggered, this, &MainWindow::startWindowCheck);
    trayMenu_->addSeparator();
    QAction* showAction = trayMenu_->addAction(T("Показать окно"));
    connect(showAction, &QAction::triggered, this, &MainWindow::showMain);
    trayMiniAction_ = trayMenu_->addAction(T("Мини-окно"));
    connect(trayMiniAction_, &QAction::triggered, this, &MainWindow::showMini);
    trayOverlayMenu_ = trayMenu_->addMenu(T("Оверлей КПС"));
    trayOverlayAction_ = trayOverlayMenu_->menuAction();
    connect(trayOverlayMenu_, &QMenu::aboutToShow, this, [this]() { fillOverlayMenu(trayOverlayMenu_); });
    fillOverlayMenu(trayOverlayMenu_);
    QAction* overlaySettings = trayMenu_->addAction(T("Настроить оверлей…"));
    connect(overlaySettings, &QAction::triggered, this, &MainWindow::openOverlaySettings);
    QAction* settingsAction = trayMenu_->addAction(T("Настройки…"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
#ifdef _WIN32
    if (!Autostart::elevated()) {
        QAction* admin = trayMenu_->addAction(T("Перезапустить от имени администратора"));
        connect(admin, &QAction::triggered, this, &MainWindow::restartAsAdmin);
    }
#endif
    trayMenu_->addSeparator();
    QAction* quitAction = trayMenu_->addAction(T("Выход"));
    connect(quitAction, &QAction::triggered, this, &MainWindow::quitApp);
    tray_->setContextMenu(trayMenu_);
    tray_->setIcon(AqIcons::appIcon(true));
    tray_->setToolTip(kAppTitle);
    tray_->show();
    connect(tray_, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
}

void MainWindow::rebuildTrayGames() {
    if (!trayGameMenu_) return;
    const auto& t = Theme::t();
    for (QActionGroup* g : trayGameMenu_->findChildren<QActionGroup*>()) g->deleteLater();
    trayGameMenu_->clear();
    const QList<GameDef> games = Games::list();
    if (games.isEmpty()) {
        QAction* none = trayGameMenu_->addAction(T("игр пока нет"));
        none->setEnabled(false);
    }
    auto* group = new QActionGroup(trayGameMenu_);
    for (const GameDef& g : games) {
        QIcon icon(AqIcons::gameTile(g.icon, QColor(g.color), g.image, 20, 2.0, false, t.light));
        QAction* a = trayGameMenu_->addAction(icon, isRunning(g.id) ? QString("%1  ▶").arg(g.name) : g.name);
        a->setCheckable(true);
        a->setChecked(g.id == game_.id);
        a->setActionGroup(group);
        // смену игры откладываем: она пересоздаёт это меню, а удалять пункт
        // прямо во время его срабатывания нельзя
        const QString id = g.id;
        connect(a, &QAction::triggered, this, [this, id]() {
            QTimer::singleShot(0, this, [this, id]() { selectGame(id); });
        });
    }
    trayGameMenu_->addSeparator();
    QAction* add = trayGameMenu_->addAction(AqIcons::ui("plus", t.redHi, 16), T("Добавить игру…"));
    connect(add, &QAction::triggered, this, [this]() {
        QTimer::singleShot(0, this, [this]() { showMain(); addGame(); });
    });
}

void MainWindow::rebuildTrayProfiles() {
    if (!trayProfileMenu_) return;
    trayProfileMenu_->clear();
    if (!hasGame()) {
        trayProfileMenu_->addAction(T("сначала добавь игру"))->setEnabled(false);
        return;
    }
    const QStringList names = Games::profiles(game_.id);
    const QString active = Games::activeProfile(game_.id);
    auto* group = new QActionGroup(trayProfileMenu_);
    auto addItem = [&](const QString& label, const QString& n) {
        QAction* a = trayProfileMenu_->addAction(label);
        a->setCheckable(true);
        a->setChecked(n == active);
        a->setActionGroup(group);
        connect(a, &QAction::triggered, this, [this, n]() {
            QTimer::singleShot(0, this, [this, n]() { switchProfile(n); });
        });
    };
    addItem(T("Без профиля"), QString());
    for (const QString& n : names) addItem(n, n);
    trayProfileMenu_->addSeparator();
    QAction* save = trayProfileMenu_->addAction(active.isEmpty() ? T("Сохранить как новый профиль…")
                                                                 : T("Сохранить «%1»").arg(active));
    connect(save, &QAction::triggered, this, [this]() { QTimer::singleShot(0, this, &MainWindow::saveProfile); });
}

void MainWindow::updateTrayState() {
    const bool run = hasGame() && clk().isRunning();
    const QString hk = hotkeySpec_.display();
    const bool hold = hasGame() && Games::keyMode(game_.id) == 1;
    const bool unused = hasGame() && !Games::armed(game_.id) && !run && !pendingStart_;
    startBtn_->setState(run, hk, pendingStart_, unused);
    QStringList names;
    for (const QString& id : runningIds()) {
        GameRunner* r = findRunner(id);
        names << (r && !r->def.name.isEmpty() ? r->def.name : Games::get(id).name);
    }
    statusDot_->setActive(!names.isEmpty());
    statusText_->setText(pendingStart_ ? T("Выбери окно игры — переключись в «%1»…").arg(game_.name)
                         : !names.isEmpty() ? T("Работает · %1").arg(names.join(", "))
                         : !hasGame() ? T("Добавь игру, чтобы начать")
                         : unused ? T("«%1» не используется — включи кружок у категории").arg(game_.name)
                         : hold ? T("Остановлен — держи %1 в игре").arg(hk)
                                : T("Остановлен — нажми %1 в игре").arg(hk));
    const QString gameName = hasGame() ? game_.name : T("нет игры");
    if (trayStatusAction_)
        trayStatusAction_->setText(names.isEmpty() ? QString(AQ_NAME " — %1 · %2").arg(T("остановлен"), gameName)
                                                   : QString(AQ_NAME " — %1 · %2").arg(T("работает"), names.join(", ")));
    if (trayToggleAction_) {
        trayToggleAction_->setText(QString("%1\t%2").arg(run ? T("Стоп") : pendingStart_ ? T("Отмена") : T("Старт"), hk));
        trayToggleAction_->setEnabled(!unused);
    }
    if (trayMenu_)
        if (QAction* stopAll = trayMenu_->findChild<QAction*>("stopAll")) stopAll->setVisible(names.size() > 1);
    if (tray_) {
        tray_->setIcon(AqIcons::appIcon(names.isEmpty()));
        tray_->setToolTip(names.isEmpty() ? QString("%1 — %2 (%3)").arg(kAppTitle, gameName, T("остановлен"))
                                          : QString("%1 — %2 (%3)").arg(kAppTitle, names.join(", "), T("работает")));
    }
    if (mini_) mini_->setRunning(run, hk, pendingStart_, unused);
}

QWidget* MainWindow::dialogParent() {
    if (isVisible()) return this;
    if (mini_ && mini_->isVisible()) return mini_;
    return nullptr;
}

// Окно AutoQ открыто — сообщение внутри окна (новое сразу заменяет старое);
// свёрнут в трей — уведомление у значка
void MainWindow::notify(const QString& text) {
    if (isVisible() && !isMinimized()) { Toast::show(rootStack_, text); return; }
    if (mini_ && mini_->isVisible() && !mini_->isMinimized()) { mini_->toast(text); return; }
    if (!Prefs::notifications()) return;
    if (tray_ && tray_->isVisible())
        tray_->showMessage(kAppTitle, text, QSystemTrayIcon::Information, 2500);
}

void MainWindow::showMain() {
    if (mini_) mini_->hide();
    miniMode_ = false;
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::showMini() {
    if (!hasGame() || !Prefs::onboardingDone()) {
        showMain();
        if (Prefs::onboardingDone()) notify(T("Сначала добавь игру."));
        return;
    }
    miniMode_ = true;
    syncMini();
    updateProfileState();
    mini_->applyOnTop();
    mini_->adjustSize();
    if (QScreen* scr = screen()) {
        const QRect a = scr->availableGeometry();
        if (!mini_->isVisible()) mini_->move(a.right() - mini_->width() - 24, a.top() + 60);
    }
    mini_->showNormal();
    mini_->raise();
    hide();
}

// Боковая панель с играми: слева, справа, узкая (только значки) или скрыта.
// Общая для всех игр; меняется в редакторе интерфейса (Ещё → Боковая панель, шаблоны).
void MainWindow::applySidebarMode(int mode) {
    mode = qBound(0, mode, 3);
    sideMode_ = mode;
    const bool narrow = mode == 2;
    // место: слева или справа от карточек
    const int at = bodyLay_->indexOf(sidebar_);
    const int want = mode == 1 ? bodyLay_->count() - 1 : 0;
    if (at != want) {
        bodyLay_->removeWidget(sidebar_);
        bodyLay_->insertWidget(mode == 1 ? bodyLay_->count() : 0, sidebar_);
    }
    sidebar_->setProperty("side", mode == 1 ? "right" : "left");
    Theme::repolish(sidebar_);
    // узкая: только значки игр, логотип и кнопки — в столбик (поля — с учётом плотности)
    sidebar_->setFixedWidth(narrow ? 84 : 236);
    sidebar_->layout()->setContentsMargins(Theme::sp(narrow ? 8 : 12), Theme::sp(14), Theme::sp(narrow ? 8 : 12), Theme::sp(14));
    sidebar_->layout()->setSpacing(Theme::sp(4));
    brandLay_->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    brandLay_->setContentsMargins(narrow ? 0 : Theme::sp(8), 0, narrow ? 0 : Theme::sp(2), Theme::sp(12));
    brandLay_->setSpacing(Theme::sp(narrow ? 6 : 9));
    // распорка между названием и кнопками: в ряду — тянется, в столбике — нет
    // (иначе без игр кнопки уезжают на середину панели); setDirection её переворачивает — ставим после
    if (QSpacerItem* sp = brandLay_->itemAt(2) ? brandLay_->itemAt(2)->spacerItem() : nullptr) {
        if (narrow) sp->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
        else sp->changeSize(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
        brandLay_->setStretch(2, narrow ? 0 : 1);
        brandLay_->invalidate();
    }
    for (QWidget* w : {static_cast<QWidget*>(brandLogo_), brandNames_, static_cast<QWidget*>(layoutBtn_), static_cast<QWidget*>(settingsBtn_)})
        brandLay_->setAlignment(w, narrow ? Qt::AlignHCenter : Qt::Alignment());
    for (QLabel* l : brandNames_->findChildren<QLabel*>()) l->setAlignment(narrow ? Qt::AlignHCenter : Qt::AlignLeft);
    sideSection_->setVisible(!narrow);
    gameDelegate_->compact = narrow;
    gameList_->doItemsLayout();
    gameList_->viewport()->update();
    emptyHint_->setVisible(gameList_->count() == 0 && !narrow);
    addGameBtn_->setText(narrow ? QString() : T("Добавить игру"));
    addGameBtn_->setToolTip(narrow ? T("Добавить игру: готовую (Genshin, Minecraft…) или свою") : T("Готовые игры (Genshin, Minecraft…) или своя"));
    updateSidebarVisible();
    // применить сразу: при смене через шаблон это внутри перехода, и снимок «после» — уже с новой панелью
    sidebar_->layout()->activate();
}

// Скрытая панель: игры, «добавить» и настройки — кнопкой у названия игры. Нет игр или блок
// «Игра» спрятан (свёрнут) — панель всё равно видна, иначе к ним не добраться
void MainWindow::updateSidebarVisible() {
    const bool gameBlock = hasGame() && board_->blockVisible("game");
    sidebar_->setVisible(sideMode_ != 3 || gameList_->count() == 0 || !gameBlock);
    switchGameBtn_->setVisible(sideMode_ == 3);
}

QWidget* MainWindow::pageArea() const {
    return sideMode_ == 3 && root_ ? root_ : static_cast<QWidget*>(pages_);
}

void MainWindow::showSwitchMenu() {
    QMenu m(this);
    const auto& t = Theme::t();
    const qreal dpr = devicePixelRatioF();
    for (const GameDef& g : Games::list()) {
        QAction* a = m.addAction(QIcon(AqIcons::gameTile(g.icon, QColor(g.color), g.image, 20, dpr, false, t.light)),
                                 isRunning(g.id) ? T("%1 — работает").arg(g.name) : g.name);
        if (g.id == game_.id) {                   // открытая — жирным
            QFont f = a->font();
            f.setBold(true);
            a->setFont(f);
        }
        const QString id = g.id;
        connect(a, &QAction::triggered, this, [this, id]() {
            if (id == game_.id) return;
            QTimer::singleShot(0, this, [this, id]() {
                for (int i = 0; i < gameList_->count(); ++i)
                    if (gameList_->item(i)->data(GameListDelegate::IdRole).toString() == id) { gameList_->setCurrentRow(i); break; }
            });
        });
    }
    m.addSeparator();
    m.addAction(AqIcons::ui("plus", t.text, 14, 2.2), T("Добавить игру…"), this, [this]() {
        QTimer::singleShot(0, this, &MainWindow::addGame);
    });
    m.addAction(AqIcons::ui("settings", t.text, 14, 1.8), T("Настройки…"), this, [this]() {
        QTimer::singleShot(0, this, &MainWindow::openSettings);
    });
    m.addAction(AqIcons::ui("layout", t.text, 14), T("Редактор интерфейса"), this, [this]() {
        QTimer::singleShot(0, this, [this]() { setLayoutEditing(true); });
    });
    m.addAction(T("Показать боковую панель"), this, [this]() {
        QTimer::singleShot(0, this, [this]() { board_->setSidebarMode(0, true); });
    });
    switchGameBtn_->setDown(true);
    m.exec(switchGameBtn_->mapToGlobal(QPoint(0, switchGameBtn_->height() + 4)));
    switchGameBtn_->setDown(false);
}

void MainWindow::setLayoutEditing(bool on) {
    if (on && !hasGame()) {
        notify(T("Сначала добавь игру: раскладка карточек у каждой игры своя."));
        return;
    }
    if (on && mini_ && mini_->isVisible()) showMain();
    board_->setEditMode(on);
}

void MainWindow::applyLayoutToAll() {
    if (!hasGame()) return;
    const QList<GameDef> games = Games::list();
    if (games.size() < 2) {
        notify(T("Других игр пока нет."));
        return;
    }
    if (!askYesNo(dialogParent(), T("Раскладка для всех игр"),
                  T("Сделать такую раскладку карточек у всех игр (%1)? Их нынешние раскладки заменятся.").arg(games.size()),
                  T("Для всех игр"), T("Отмена")))
        return;
    for (const GameDef& g : games) CardLayout::save(g.id, board_->cardLayout());
    notify(T("Теперь у всех игр раскладка как у «%1».").arg(game_.name));
}

void MainWindow::openSettings() { openSettingsPage(SettingsDialog::General); }
void MainWindow::openOverlaySettings() { openSettingsPage(SettingsDialog::Overlay); }

void MainWindow::setOverlayMode(int mode) {
    if (!overlay_) return;
    OverlaySettings s = overlay_->settings();
    s.enabled = mode != 0;
    if (mode != 0) s.when = mode == 2 ? 1 : 0;
    s.save();
    overlay_->setSettings(s);
    syncOverlayUi();
    notify(mode == 0 ? T("Оверлей КПС выключен.")
                     : mode == 2 ? T("Оверлей КПС: показывается всегда.")
                                 : T("Оверлей КПС: появится, когда кликер заработает."));
}

void MainWindow::fillOverlayMenu(QMenu* m) {
    for (QActionGroup* g : m->findChildren<QActionGroup*>(QString(), Qt::FindDirectChildrenOnly)) g->deleteLater();
    m->clear();
    const OverlaySettings s = overlay_ ? overlay_->settings() : OverlaySettings::load();
    const int cur = !s.enabled ? 0 : s.when == 1 ? 2 : 1;
    auto* group = new QActionGroup(m);
    const QString names[] = {T("Выключен"), T("Только пока кликер работает"), T("Всегда (пока выбрана игра)")};
    for (int i = 0; i < 3; ++i) {
        QAction* a = m->addAction(names[i]);
        a->setCheckable(true);
        a->setChecked(i == cur);
        a->setActionGroup(group);
        connect(a, &QAction::triggered, this, [this, i]() {
            QTimer::singleShot(0, this, [this, i]() { setOverlayMode(i); });
        });
    }
    m->addSeparator();
    QAction* cfg = m->addAction(AqIcons::ui("settings", Theme::t().muted, 16), T("Настроить оверлей…"));
    connect(cfg, &QAction::triggered, this, [this]() { QTimer::singleShot(0, this, &MainWindow::openOverlaySettings); });
}

void MainWindow::showOverlayMenu(QWidget* anchor) {
    QMenu m(anchor);
    fillOverlayMenu(&m);
    m.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 2)));
}

void MainWindow::syncOverlayUi() {
    const bool on = overlay_ && overlay_->settings().enabled;
    {
        QSignalBlocker b(overlayBtn_);
        overlayBtn_->setChecked(on);
    }
    overlayBtn_->setIcon(AqIcons::ui("gauge", on ? Theme::t().redHi : Theme::t().muted, 18, 1.9));
    if (trayOverlayAction_) trayOverlayAction_->setText(on ? T("Оверлей КПС: вкл") : T("Оверлей КПС: выкл"));
    if (mini_) mini_->setOverlayOn(on);
}

void MainWindow::openSettingsPage(int page) {
    SettingsDialog dlg(overlay_, static_cast<SettingsDialog::Page>(page), dialogParent());
    connect(&dlg, &SettingsDialog::overlayChanged, this, &MainWindow::syncOverlayUi);
    connect(&dlg, &SettingsDialog::restartRequested, this, [this, &dlg]() {
        dlg.accept();
        QTimer::singleShot(0, this, &MainWindow::restartApp);
    });
    connect(&dlg, &SettingsDialog::adminRestartRequested, this, [this, &dlg]() {
        dlg.accept();
        QTimer::singleShot(0, this, &MainWindow::restartAsAdmin);
    });
    connect(&dlg, &SettingsDialog::generalChanged, this, [this]() {
        for (auto& r : runners_)
            if (!r.first.isEmpty() && r.second->clicker.isRunning())
                r.second->clicker.setConfig(buildConfig(r.second->def, r.second->st, r.second->bound));
        pushConfig();
        if (mini_) mini_->applyOnTop();
        refreshLive();
    });
    connect(&dlg, &SettingsDialog::appearanceChanged, this, &MainWindow::appearanceChanged);
    connect(&dlg, &SettingsDialog::editLayoutRequested, this, [this, &dlg]() {
        dlg.accept();
        QTimer::singleShot(0, this, [this]() { setLayoutEditing(true); });
    });
    connect(&dlg, &SettingsDialog::onboardingRequested, this, [this, &dlg]() {
        dlg.accept();
        QTimer::singleShot(0, this, &MainWindow::startOnboarding);
    });
    dlg.exec();
    // общий звук мог смениться — «Как в настройках · …» у игры тоже
    soundOnBox_->refill();
    soundOffBox_->refill();
    soundsOffHint_->setVisible(!Sounds::enabled());
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    // клик по значку — показать окно (в мини-режиме — мини-окно)
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
        if (miniMode_ && hasGame()) { mini_->showNormal(); mini_->raise(); mini_->activateWindow(); }
        else showMain();
    }
}

void MainWindow::restartApp() {
    restart_ = true;
    quitApp();
}

void MainWindow::restartAsAdmin() {
    if (Autostart::elevated()) return;
    // окно UAC: «Да» — запустится копия с правами (дождётся, пока эта закроется)
    if (Autostart::restartElevated()) quitApp();
    else notify(T("Права администратора не получены — в окне UAC выбрали «Нет»."));
}

void MainWindow::quitApp() {
    quitting_ = true;
    for (auto& r : runners_) r.second->clicker.stop();
    qApp->quit();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!quitting_ && !Prefs::closeToTray()) {   // крестик = выход (Настройки → Основные)
        quitApp();
        e->accept();
        return;
    }
    if (quitting_ || !tray_ || !tray_->isVisible()) {
        QMainWindow::closeEvent(e);
        return;
    }
    hide();
    e->ignore();
    if (!hintShown_) {
        notify(T("AutoQ свёрнут в трей. Выход — через меню значка."));
        hintShown_ = true;
    }
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
}

