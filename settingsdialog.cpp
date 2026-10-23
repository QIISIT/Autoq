#include "settingsdialog.h"
#include "i18n.h"
#include <functional>
#include <QMessageBox>
#include "autostart.h"
#include "widgets.h"
#include "icons.h"
#include "appinfo.h"
#include "colorpicker.h"
#include "games.h"
#include "animations.h"
#include "sounds.h"
#include <QApplication>
#include <QGuiApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QFrame>
#include <QStackedWidget>
#include <QScrollArea>
#include <QLineEdit>
#include <QSpinBox>
#include <QSlider>
#include <QDir>
#include <QFileDialog>
#include <QComboBox>
#include <QScreen>
#include <QSignalBlocker>
#include <QPainter>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>

static QLabel* lbl(const QString& text, const char* obj) {
    auto* l = new QLabel(text);
    l->setObjectName(obj);
    return l;
}

static QString autoText_() {
#ifdef _WIN32
    return T("Автозапуск");
#elif defined(__APPLE__)
    return T("Запускать при входе");
#else
    return T("Автозапуск в трей");
#endif
}

// значок кнопки цвета темы: запоминается на кнопке и перекрашивается в refresh() при смене темы
static void themedIcon(QAbstractButton* b, const char* name, bool muted, int px) {
    b->setProperty("aqIcon", QString::fromLatin1(name));
    b->setProperty("aqIconMuted", muted);
    b->setProperty("aqIconPx", px);
    b->setIcon(AqIcons::ui(name, muted ? Theme::t().muted : Theme::t().text, px));
}

// маленькая кнопка «вернуть как было» рядом с ползунком
static QToolButton* resetMini(const QString& tip) {
    auto* b = new QToolButton();
    b->setObjectName("resetMini");
    b->setFixedSize(24, 24);
    themedIcon(b, "reset", true, 13);
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

SettingsDialog::SettingsDialog(OverlayWindow* overlay, Page page, QWidget* parent)
    : QDialog(parent), overlay_(overlay) {
    setWindowTitle(T("Настройки — AutoQ"));
    setWindowModality(Qt::WindowModal);   // оверлей можно таскать мышью, пока окно открыто
    oNumAuto_ = nullptr;
    os_ = overlay_ ? overlay_->settings() : OverlaySettings::load();
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* top = new QHBoxLayout();
    top->setContentsMargins(22, 16, 22, 6);
    headIcon_ = new QLabel();
    top->addWidget(headIcon_);
    top->addWidget(lbl(T("Настройки"), "cardTitle"));
    top->addStretch(1);
    tabs_ = new Segmented({T("Основные"), T("Оформление"), T("Оверлей КПС")}, false);
    top->addWidget(tabs_);
    outer->addLayout(top);

    pages_ = new QStackedWidget();
    auto scrolled = [](QWidget* page) {
        auto* scroll = new QScrollArea();
        scroll->setObjectName("mainScroll");
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        return scroll;
    };
    pages_->addWidget(scrolled(buildGeneralPage()));
    pages_->addWidget(scrolled(buildAppearancePage()));
    pages_->addWidget(scrolled(buildOverlayPage()));
    outer->addWidget(pages_, 1);

    auto* foot = new QHBoxLayout();
    foot->setContentsMargins(22, 8, 22, 16);
    foot->addStretch(1);
    auto* done = new QPushButton(T("Готово"));
    done->setObjectName("primary");
    done->setDefault(true);
    foot->addWidget(done);
    outer->addLayout(foot);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);

    connect(tabs_, &Segmented::changed, this, [this](int i) {
        Anim::transition(pages_, [this, i]() { pages_->setCurrentIndex(i); }, {}, Anim::style() == "cascade" ? "slide" : QString());
    });
    tabs_->setCurrent(page);
    pages_->setCurrentIndex(page);

    rebuildBgRow();
    refresh();
    overlayToUi();

    int maxH = 900;
    if (QScreen* scr = parent ? parent->screen() : QGuiApplication::primaryScreen())
        maxH = scr->availableGeometry().height() - 60;
    resize(700, qMin(860, maxH));
}

SettingsDialog::~SettingsDialog() {
    if (overlay_ && overlay_->moveMode()) overlay_->setMoveMode(false);
}

// ===========================================================================
// Основные
// ===========================================================================
QWidget* SettingsDialog::buildGeneralPage() {
    auto* page = new QWidget();
    page->setObjectName("mainInner");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 10, 22, 10);
    lay->setSpacing(12);
    auto row = [&](const QString& text, const QString& tip, bool on, std::function<void(bool)> apply) {
        auto* h = new QHBoxLayout();
        h->setSpacing(10);
        auto* sw = new ToggleSwitch();
        sw->setChecked(on);
        sw->setAccessibleName(text);
        sw->setToolTip(tip);
        h->addWidget(sw);
        auto* l = lbl(text, "muted");
        l->setToolTip(tip);
        l->setWordWrap(true);
        h->addWidget(l, 1);
        lay->addLayout(h);
        connect(sw, &ToggleSwitch::toggled, this, [this, apply](bool v) { apply(v); emit generalChanged(); });
        return sw;
    };
    // язык — сменится после перезапуска
    {
        auto* lr = new QHBoxLayout();
        lr->setSpacing(10);
        lr->addWidget(lbl(QString::fromUtf8("Язык / Language"), "fieldLabel"), 1);
        auto* langSeg = new Segmented({T("Как в системе"), QString::fromUtf8("Русский"), QStringLiteral("English")}, false);
        auto s = AppSettings::open();
        const QString cur = s->value("language", "auto").toString();
        langSeg->setCurrent(cur == "ru" ? 1 : cur == "en" ? 2 : 0);
        lr->addWidget(langSeg);
        lay->addLayout(lr);
        auto* note = lbl(QString(), "small");
        note->setWordWrap(true);
        auto* restart = new QPushButton(T("Перезапустить AutoQ"));
        restart->setObjectName("accent");
        restart->hide();
        lay->addWidget(note);
        lay->addWidget(restart, 0, Qt::AlignLeft);
        connect(langSeg, &Segmented::changed, this, [note, restart](int i) {
            auto st = AppSettings::open();
            const QString v = i == 1 ? "ru" : i == 2 ? "en" : "auto";
            st->setValue("language", v);
            st->sync();
            const bool same = I18n::fromSetting(v) == I18n::language();
            note->setText(same ? QString() : (I18n::fromSetting(v) == I18n::En
                ? QStringLiteral("The language will change after AutoQ restarts.")
                : QString::fromUtf8("Язык сменится после перезапуска AutoQ.")));
            restart->setText(I18n::fromSetting(v) == I18n::En ? QStringLiteral("Restart AutoQ")
                                                              : QString::fromUtf8("Перезапустить AutoQ"));
            restart->setVisible(!same);
        });
        connect(restart, &QPushButton::clicked, this, &SettingsDialog::restartRequested);
    }

    lay->addWidget(lbl(T("Запуск"), "fieldLabel"));
#ifdef _WIN32
    const QString autoText = T("Автозапуск");
    const QString autoTip = T("AutoQ запускается при входе в Windows и ждёт в трее — нажми клавишу старт/стоп в игре.\nВыключить можно здесь или в Диспетчере задач → Автозагрузка.");
#elif defined(__APPLE__)
    const QString autoText = T("Запускать при входе");
    const QString autoTip = T("AutoQ запускается при входе в macOS и ждёт в строке меню.");
#else
    const QString autoText = T("Автозапуск в трей");
    const QString autoTip = T("AutoQ запускается вместе с системой и ждёт в трее — нажми клавишу старт/стоп в игре");
#endif
    ToggleSwitch* autoSw = row(autoText, autoTip, Autostart::isEnabled(), [](bool) {});
    // переключатель показывает, как есть на самом деле (могли выключить в Диспетчере задач)
    disconnect(autoSw, &ToggleSwitch::toggled, this, nullptr);
    connect(autoSw, &ToggleSwitch::toggled, this, [this, autoSw](bool v) {
        if (!Autostart::setEnabled(v))
            QMessageBox::warning(this, autoText_(), T("Не удалось изменить автозапуск."));
        QSignalBlocker b(autoSw);
        autoSw->setChecked(Autostart::isEnabled());
        emit generalChanged();
    });
#ifdef _WIN32
    // права администратора: без них игры, запущенные «от администратора», не примут клики
    {
        row(T("Просить права администратора при запуске"),
            T("Игры, запущенные от имени администратора, принимают клики только от программы с такими же правами.\n"
              "Выключи — и окна UAC при запуске не будет; права можно получить кнопкой ниже."),
            Prefs::elevateAtStart(), [](bool v) { Prefs::setElevateAtStart(v); });
        const bool admin = Autostart::elevated();
        auto* h = new QHBoxLayout();
        h->setContentsMargins(50, 0, 0, 0);
        h->setSpacing(8);
        auto* ic = new QLabel();
        ic->setPixmap(AqIcons::ui("shield", admin ? Theme::t().ok : Theme::t().warn, 16).pixmap(16, 16));
        h->addWidget(ic, 0, Qt::AlignTop);
        auto* st = lbl(admin ? T("Сейчас AutoQ работает с правами администратора — клики доходят до любых игр.")
                             : T("Сейчас AutoQ работает без прав администратора: игры, запущенные от имени администратора, не примут клики."),
                       "small");
        st->setWordWrap(true);
        h->addWidget(st, 1);
        lay->addLayout(h);
        if (!admin) {
            auto* btn = new QPushButton(AqIcons::ui("shield", Theme::t().redHi, 16), T("Перезапустить от имени администратора"));
            btn->setObjectName("accent");
            btn->setCursor(Qt::PointingHandCursor);
            btn->setToolTip(T("Windows спросит разрешение (окно UAC). Игры, кликер и настройки останутся как были."));
            auto* bh = new QHBoxLayout();
            bh->setContentsMargins(50, 0, 0, 0);
            bh->addWidget(btn);
            bh->addStretch(1);
            lay->addLayout(bh);
            connect(btn, &QPushButton::clicked, this, &SettingsDialog::adminRestartRequested);
        }
    }
#endif
    row(T("Открывать мини-окно вместо большого"), T("При обычном запуске сразу показывать компактное окно поверх игры"),
        Prefs::startMini(), [](bool v) { Prefs::setStartMini(v); });
    row(T("Мини-окно поверх всех окон"), T("Выключи — и мини-окно будет обычным окном: игры и программы смогут его перекрывать"),
        Prefs::miniOnTop(), [](bool v) { Prefs::setMiniOnTop(v); });
    row(T("Крестик сворачивает в трей"), T("Выключи — и крестик будет закрывать AutoQ совсем"),
        Prefs::closeToTray(), [](bool v) { Prefs::setCloseToTray(v); });
    row(T("Уведомления у значка в трее"), T("Когда окно AutoQ закрыто: «Профиль сохранён», «Окно игры выбрано» и т. п. Пока окно открыто — сообщения внутри окна."),
        Prefs::notifications(), [](bool v) { Prefs::setNotifications(v); });

    lay->addWidget(lbl(T("Кликер"), "fieldLabel"));
    row(T("Кнопкой мыши — только когда курсор над окном игры"),
        T("Иначе клик ушёл бы в соседнее окно или на другой монитор.\nВыключи, если кликер ставит паузу, хотя курсор над игрой\n(бывает под Wayland и с некоторыми лаунчерами)."),
        Prefs::mouseOnlyOverGame(), [](bool v) { Prefs::setMouseOnlyOverGame(v); });
    row(T("Выключать кликер, когда окно игры закрылось"), T("Игру закрыли или она вылетела — кликер останавливается сам"),
        Prefs::stopOnGameClose(), [](bool v) { Prefs::setStopOnGameClose(v); });

    // --- звук при включении и выключении кликера: свой для «вкл» и для «выкл»
    lay->addWidget(lbl(T("Звук"), "fieldLabel"));
    {
        auto* soundSw = row(T("Звук, когда кликер включается и выключается"),
                            T("Слышно прямо в игре, не глядя на оверлей. Звук для «вкл» и для «выкл» — свой."),
                            Sounds::enabled(), [](bool v) { Sounds::setEnabled(v); });
        auto* box = new QWidget();
        auto* g = new QGridLayout(box);
        g->setContentsMargins(50, 0, 0, 0);
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        auto pickRow = [this, g](int r, Sounds::Kind kind, const QString& caption) {
            g->addWidget(lbl(caption, "muted"), r, 0);
            auto* box = new SoundBox(kind, false);
            box->setAccessibleName(caption);
            box->setMinimumWidth(200);
            g->addWidget(box, r, 1);
            auto* play = new QToolButton();
            play->setObjectName("icon");
            play->setFixedSize(30, 30);
            themedIcon(play, "play", true, 12);
            play->setToolTip(T("Послушать"));
            play->setAccessibleName(T("Послушать"));
            g->addWidget(play, r, 2);
            connect(play, &QToolButton::clicked, this, [kind]() { Sounds::preview(Sounds::sound(kind)); });
            connect(box, &SoundBox::soundChosen, this, [kind](const QString& id) { Sounds::setSound(kind, id); });
        };
        pickRow(0, Sounds::On, T("Включился"));
        pickRow(1, Sounds::Off, T("Выключился"));
        g->addWidget(lbl(T("Громкость"), "muted"), 2, 0);
        auto* volRow = new QHBoxLayout();
        volRow->setSpacing(8);
        auto* vol = new QSlider(Qt::Horizontal);
        vol->setRange(0, 100);
        vol->setValue(Sounds::volume());
        vol->setAccessibleName(T("Громкость звука"));
        vol->setToolTip(T("Тише ← → громче"));
        volRow->addWidget(vol, 1);
        auto* volVal = lbl(QString("%1%").arg(Sounds::volume()), "small");
        volVal->setMinimumWidth(40);
        volRow->addWidget(volVal);
        g->addLayout(volRow, 2, 1, 1, 2);
        auto* holdSw = new ToggleSwitch();
        holdSw->setChecked(Sounds::inHoldMode());
        holdSw->setAccessibleName(T("Звук и в режиме «Пока держу»"));
        auto* hr = new QHBoxLayout();
        hr->setSpacing(10);
        hr->addWidget(holdSw);
        auto* hl = lbl(T("И в режиме «Пока держу» (там кликер включается на каждое нажатие)"), "small");
        hl->setWordWrap(true);
        hr->addWidget(hl, 1);
        g->addLayout(hr, 3, 0, 1, 3);
        // «Мои звуки»: свои WAV — в папке данных, видны во всех списках звуков
        auto* mr = new QHBoxLayout();
        mr->setSpacing(8);
        auto* mineLbl = lbl(QString(), "small");
        mineLbl->setWordWrap(true);
        auto updMine = [mineLbl]() {
            const int n = Sounds::userSounds().size();
            mineLbl->setText(n ? T("Мои звуки: %1. Свой звук можно выбрать и у игры или профиля — карточка «Профиль и запуск».").arg(n)
                               : T("Свои звуки (WAV) — пункт «Добавить свой звук…» в списке. Свой звук можно выбрать и у игры или профиля."));
        };
        updMine();
        auto* mineDir = new QPushButton(T("Папка звуков"));
        themedIcon(mineDir, "folder", false, 14);
        mineDir->setToolTip(T("Положи сюда файлы WAV — они появятся в списках звуков"));
        connect(mineDir, &QPushButton::clicked, this, []() {
            QDir().mkpath(Sounds::userDir());
            QDesktopServices::openUrl(QUrl::fromLocalFile(Sounds::userDir()));
        });
        for (SoundBox* sb : box->findChildren<SoundBox*>()) connect(sb, &SoundBox::soundChosen, this, updMine);
        mr->addWidget(mineLbl, 1);
        mr->addWidget(mineDir, 0, Qt::AlignTop);
        g->addLayout(mr, 4, 0, 1, 3);
        g->setColumnStretch(3, 1);
        lay->addWidget(box);
        box->setEnabled(Sounds::enabled());
        connect(soundSw, &ToggleSwitch::toggled, box, &QWidget::setEnabled);
        connect(vol, &QSlider::valueChanged, this, [volVal](int v) { Sounds::setVolume(v); volVal->setText(QString("%1%").arg(v)); });
        connect(vol, &QSlider::sliderReleased, this, []() { Sounds::preview(Sounds::sound(Sounds::On)); });
        connect(holdSw, &ToggleSwitch::toggled, this, [](bool v) { Sounds::setInHoldMode(v); });
    }

    // --- папка данных: где лежат настройки и профили (обычные файлы, без реестра)
    lay->addWidget(lbl(T("Данные"), "fieldLabel"));
    {
        auto* pathRow = new QHBoxLayout();
        pathRow->setSpacing(8);
        auto* ic = new QLabel();
        ic->setPixmap(AqIcons::ui("folder", Theme::t().muted, 16).pixmap(16, 16));
        pathRow->addWidget(ic);
        auto* path = new QLineEdit(QDir::toNativeSeparators(AppSettings::dataDir()));
        path->setReadOnly(true);
        path->setAccessibleName(T("Папка данных AutoQ"));
        path->setToolTip(T("Здесь лежат настройки (settings.ini) и профили (папка profiles)"));
        pathRow->addWidget(path, 1);
        lay->addLayout(pathRow);
        auto* note = lbl(QString(), "small");
        note->setWordWrap(true);
        lay->addWidget(note);
        auto* btns = new QHBoxLayout();
        btns->setSpacing(8);
        auto* openBtn = new QPushButton(T("Открыть папку"));
        themedIcon(openBtn, "folder", true, 14);
        auto* moveBtn = new QPushButton(T("Сменить папку…"));
        auto* defBtn = new QPushButton(T("Вернуть по умолчанию"));
        for (QPushButton* b : {openBtn, moveBtn, defBtn}) {
            b->setObjectName("outline");
            b->setCursor(Qt::PointingHandCursor);
            btns->addWidget(b);
        }
        btns->addStretch(1);
        lay->addLayout(btns);
        auto refreshData = [path, note, moveBtn, defBtn]() {
            path->setText(QDir::toNativeSeparators(AppSettings::dataDir()));
            path->setCursorPosition(0);
            const bool port = AppSettings::portable();
            note->setText(port ? T("Портативный режим: всё хранится в папке AutoQ-data рядом с программой. "
                                   "Чтобы хранить как обычно — удали или переименуй эту папку.")
                               : T("settings.ini — настройки и игры, profiles — профили, по файлу на каждый. "
                                   "Чтобы перенести AutoQ на другой ПК, скопируй папку целиком."));
            moveBtn->setEnabled(!port);
            defBtn->setVisible(AppSettings::customDataDir());
        };
        refreshData();
        connect(openBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(AppSettings::dataDir()));
        });
        // перенести в папку: текущие данные — туда; если там уже есть данные AutoQ — спросить
        auto moveTo = [this, refreshData](QString dir) {
            bool copy = true;
            if (AppSettings::hasData(dir)) {
                QMessageBox box(QMessageBox::Question, T("Папка данных"),
                                T("В этой папке уже есть настройки AutoQ."), QMessageBox::NoButton, this);
                box.setInformativeText(T("Взять их (AutoQ перезапустится) или заменить текущими?"));
                QPushButton* take = box.addButton(T("Взять оттуда"), QMessageBox::AcceptRole);
                QPushButton* replace = box.addButton(T("Заменить текущими"), QMessageBox::DestructiveRole);
                box.addButton(T("Отмена"), QMessageBox::RejectRole);
                box.setDefaultButton(take);
                box.exec();
                if (box.clickedButton() == take) copy = false;
                else if (box.clickedButton() != replace) return;
            }
            QString err;
            if (!AppSettings::setDataDir(dir, copy, &err)) {
                QMessageBox::warning(this, T("Папка данных"),
                                     T("Не получилось: нет доступа к «%1». Выбери другую папку.").arg(QDir::toNativeSeparators(err)));
                return;
            }
            refreshData();
            if (!copy) emit restartRequested();       // настройки сменились целиком — перечитать с нуля
        };
        connect(moveBtn, &QPushButton::clicked, this, [this, moveTo]() {
            QString dir = QFileDialog::getExistingDirectory(this, T("Папка данных AutoQ"), AppSettings::dataDir());
            if (dir.isEmpty()) return;
            // выбрали непустую чужую папку (например D:\Games) — свою подпапку AutoQ внутри
            const QDir d(dir);
            if (!AppSettings::hasData(dir) && d.dirName().compare("AutoQ", Qt::CaseInsensitive) != 0 &&
                !d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
                dir = d.filePath("AutoQ");
            moveTo(QDir::cleanPath(dir));
        });
        connect(defBtn, &QPushButton::clicked, this, [moveTo]() { moveTo(AppSettings::defaultDataDir()); });
    }

    lay->addWidget(lbl(T("Первый запуск"), "fieldLabel"));
    auto* again = new QPushButton(T("Пройти выбор оформления и анимаций заново"));
    again->setObjectName("outline");
    again->setCursor(Qt::PointingHandCursor);
    again->setToolTip(T("Те же экраны, что при первом запуске: стиль интерфейса и анимации"));
    connect(again, &QPushButton::clicked, this, &SettingsDialog::onboardingRequested);
    lay->addWidget(again, 0, Qt::AlignLeft);
    lay->addStretch(1);
    return page;
}

// ===========================================================================
// Оформление
// ===========================================================================
QWidget* SettingsDialog::buildAppearancePage() {
    auto* page = new QWidget();
    page->setObjectName("mainInner");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 10, 22, 10);
    lay->setSpacing(12);
    lay->addWidget(lbl(T("Всё меняется сразу. «Свой…» — любой цвет, с образцом окна."), "muted"));

    // --- стиль интерфейса
    lay->addWidget(lbl(T("Стиль интерфейса"), "fieldLabel"));
    auto* vrow = new QHBoxLayout();
    vrow->setSpacing(10);
    for (const QString& id : Theme::variants()) {
        auto* b = new CardButton();
        b->setObjectName("choiceCard");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("variant", id);
        b->setToolTip(Theme::variantLabel(id) + " — " + Theme::variantNote(id));
        b->setAccessibleName(T("Стиль: ") + Theme::variantLabel(id));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        auto* v = new QVBoxLayout(b);
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(4);
        auto* pv = new ThemePreview();
        pv->setMinimumSize(150, 92);
        pv->setMaximumSize(QWIDGETSIZE_MAX, 110);
        pv->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        pv->setAttribute(Qt::WA_TransparentForMouseEvents);
        v->addWidget(pv);
        auto* n = lbl(Theme::variantLabel(id), "gameName");
        n->setAttribute(Qt::WA_TransparentForMouseEvents);
        v->addWidget(n);
        b->setMinimumHeight(146);
        vrow->addWidget(b);
        variantCards_ << b;
        variantPreviews_ << pv;
        connect(b, &QPushButton::clicked, this, [this, id]() {
            Theme::Appearance a = Theme::current();
            a.variant = id;
            // свои скругление и шрифт заголовков — сбросить, чтобы стиль было видно
            a.radius = -1;
            a.displayFont = -1;
            apply(a);
        });
    }
    lay->addLayout(vrow);

    auto* themeRow = new QHBoxLayout();
    themeRow->addWidget(lbl(T("Тема"), "fieldLabel"), 1);
    themeSeg_ = new Segmented({T("Тёмная"), T("Светлая")}, false);
    themeRow->addWidget(themeSeg_);
    lay->addLayout(themeRow);

    lay->addWidget(lbl(T("Цвет акцента — кнопки, выделение, логотип"), "fieldLabel"));
    auto* accRow = new QHBoxLayout();
    accRow->setSpacing(8);
    accentBtns_ = makeSwatches(Theme::accentPresets(), accentGroup_, accRow);
    auto* accCustom = new QPushButton(T("Свой…"));
    accCustom->setToolTip(T("Любой цвет акцента — с живым образцом интерфейса"));
    accRow->addSpacing(4);
    accRow->addWidget(accCustom);
    accRow->addStretch(1);
    lay->addLayout(accRow);
    accentInfo_ = lbl(QString(), "small");
    lay->addWidget(accentInfo_);

    bgTitle_ = lbl(QString(), "fieldLabel");
    lay->addWidget(bgTitle_);
    bgRow_ = new QHBoxLayout();
    bgRow_->setSpacing(8);
    lay->addLayout(bgRow_);
    bgInfo_ = lbl(QString(), "small");
    lay->addWidget(bgInfo_);

    lay->addWidget(lbl(T("Значки игр"), "fieldLabel"));
    auto* styleRow = new QHBoxLayout();
    styleRow->setSpacing(8);
    styleGroup_.setExclusive(true);
    const QStringList styles = AqIcons::iconStyles();
    for (int i = 0; i < styles.size(); ++i) {
        auto* b = new QToolButton();
        b->setObjectName("styleCard");
        b->setCheckable(true);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setText(AqIcons::iconStyleLabel(styles[i]));
        b->setToolTip(AqIcons::iconStyleLabel(styles[i]) + " — " + AqIcons::iconStyleNote(styles[i]));
        b->setAccessibleName(T("Стиль значков: ") + AqIcons::iconStyleLabel(styles[i]));
        b->setIconSize(QSize(102, 30));
        b->setMinimumSize(132, 72);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("styleId", styles[i]);
        styleGroup_.addButton(b, i);
        styleRow->addWidget(b);
        styleBtns_ << b;
    }
    lay->addLayout(styleRow);
    styleInfo_ = lbl(QString(), "small");
    lay->addWidget(styleInfo_);

    // --- анимации
    auto* animRow = new QHBoxLayout();
    animRow->addWidget(lbl(T("Анимации"), "fieldLabel"), 1);
    QStringList animNames;
    for (const QString& id : Theme::animations()) animNames << Theme::animLabel(id);
    animSeg_ = new Segmented(animNames, false);
    animRow->addWidget(animSeg_);
    lay->addLayout(animRow);
    animInfo_ = lbl(QString(), "small");
    lay->addWidget(animInfo_);
    // скорость анимаций: ползунок + маленький образец (играет при наведении и при смене)
    {
        auto* sp = new QHBoxLayout();
        sp->setSpacing(10);
        sp->addWidget(lbl(T("Скорость анимаций"), "muted"));
        animSpeed_ = new QSlider(Qt::Horizontal);
        animSpeed_->setRange(0, Theme::animSpeedChoices().size() - 1);
        animSpeed_->setPageStep(1);
        animSpeed_->setToolTip(T("Медленнее ← → быстрее: экраны, карточки, сообщения, добавление действий"));
        animSpeed_->setAccessibleName(T("Скорость анимаций"));
        sp->addWidget(animSpeed_, 1);
        animSpeedVal_ = lbl(QString(), "small");
        animSpeedVal_->setMinimumWidth(130);
        sp->addWidget(animSpeedVal_);
        animDemo_ = new AnimDemo(Theme::current().anim);
        animDemo_->setFixedSize(150, 84);
        animDemo_->setToolTip(T("Наведи — покажется анимация с выбранной скоростью"));
        sp->addWidget(animDemo_);
        lay->addLayout(sp);
    }

    // --- редактор интерфейса
    lay->addWidget(lbl(T("Редактор интерфейса"), "fieldLabel"));
    auto* ed = new QFrame();
    ed->setObjectName("editorBox");
    auto* eg = new QGridLayout(ed);
    eg->setContentsMargins(14, 12, 14, 12);
    eg->setHorizontalSpacing(10);
    eg->setVerticalSpacing(10);
    eg->setColumnStretch(1, 1);
    int r = 0;
    eg->addWidget(lbl(T("Скругление"), "muted"), r, 0);
    auto* rr = new QHBoxLayout();
    rr->setSpacing(8);
    radiusSlider_ = new QSlider(Qt::Horizontal);
    radiusSlider_->setRange(0, 24);
    radiusSlider_->setAccessibleName(T("Скругление"));
    radiusSlider_->setToolTip(T("Скругление карточек и кнопок, 0 — прямые углы"));
    rr->addWidget(radiusSlider_, 1);
    radiusVal_ = lbl(QString(), "connectorText");
    radiusVal_->setFixedWidth(44);
    radiusVal_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rr->addWidget(radiusVal_);
    auto* radiusReset = resetMini(T("Как у стиля"));
    rr->addWidget(radiusReset);
    eg->addLayout(rr, r++, 1);
    eg->addWidget(lbl(T("Плотность"), "muted"), r, 0);
    densitySeg_ = new Segmented({T("Компактно"), T("Обычно"), T("Просторно")}, false);
    densitySeg_->setToolTip(T("Отступы между карточками и элементами"));
    eg->addWidget(densitySeg_, r++, 1);
    eg->addWidget(lbl(T("Заголовки"), "muted"), r, 0);
    displayFontSeg_ = new Segmented({T("Как в стиле"), "Unbounded", "Onest", "Mono"}, false);
    displayFontSeg_->setToolTip(T("Шрифт названий, логотипа и кнопки Старт"));
    eg->addWidget(displayFontSeg_, r++, 1);
    eg->addWidget(lbl(T("Текст"), "muted"), r, 0);
    textFontSeg_ = new Segmented({"Onest", "Mono", T("Системный")}, false);
    textFontSeg_->setToolTip(T("Шрифт обычного текста. Системный — как в остальных программах"));
    eg->addWidget(textFontSeg_, r++, 1);
    eg->addWidget(lbl(T("Рамки"), "muted"), r, 0);
    bordersSeg_ = new Segmented({T("Мягкие"), T("Обычные"), T("Чёткие")}, false);
    bordersSeg_->setToolTip(T("Насколько заметны рамки карточек и полей"));
    eg->addWidget(bordersSeg_, r++, 1);
    eg->addWidget(lbl(T("Интерфейс"), "muted"), r, 0, Qt::AlignTop);
    {
        // редактор интерфейса — прямо в главном окне: тянуть, менять размер и вид, прятать, шаблоны
        auto* cw = new QWidget();
        auto* cl = new QVBoxLayout(cw);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(6);
        auto* editBtn = new QPushButton(T("Редактор интерфейса"));
        editBtn->setObjectName("outline");
        themedIcon(editBtn, "layout", false, 15);
        editBtn->setCursor(Qt::PointingHandCursor);
        editBtn->setToolTip(T("Закрыть настройки и менять интерфейс прямо в окне"));
        connect(editBtn, &QPushButton::clicked, this, &SettingsDialog::editLayoutRequested);
        cl->addWidget(editBtn, 0, Qt::AlignLeft);
        auto* cn = lbl(T("Перетаскивай карточки и блоки, тяни края карточек, меняй их вид и цвет, прячь лишнее, выбирай шаблон и место панели с играми. У каждой игры своя раскладка."), "small");
        cn->setWordWrap(true);
        cl->addWidget(cn);
        eg->addWidget(cw, r++, 1);
    }
    auto* edReset = new QPushButton(T("Сбросить редактор"));
    edReset->setObjectName("ghost");
    edReset->setToolTip(T("Скругление, плотность, шрифты и рамки — как у стиля"));
    eg->addWidget(edReset, r++, 0, 1, 2, Qt::AlignLeft);
    lay->addWidget(ed);
    auto* edNote = lbl(T("Мелкие элементы не исчезнут: размеры кнопок и полей подстраиваются, а при нехватке места карточки встают в две или одну колонку."), "small");
    edNote->setWordWrap(true);
    lay->addWidget(edNote);

    // --- размер интерфейса
    auto* scRow = new QHBoxLayout();
    scRow->addWidget(lbl(T("Размер интерфейса"), "fieldLabel"), 1);
    QStringList scNames;
    for (int v : Theme::guiScaleChoices()) scNames << (v == 100 ? T("100% (как в системе)") : QString("%1%").arg(v));
    scaleSeg_ = new Segmented(scNames, false);
    scaleSeg_->setToolTip(T("Всё окно крупнее или мельче — текст, кнопки, значки. Меняется после перезапуска."));
    scRow->addWidget(scaleSeg_);
    lay->addLayout(scRow);
    scaleNote_ = lbl(QString(), "small");
    scaleNote_->setWordWrap(true);
    lay->addWidget(scaleNote_);
    // мельче 100 % — только на экранах с масштабом больше 100 % (иначе Qt рисует кусками)
    const int minScale = Theme::minUsableGuiScale();
    for (int i = 0; i < Theme::guiScaleChoices().size(); ++i)
        if (Theme::guiScaleChoices()[i] < minScale) {
            scaleSeg_->button(i)->setEnabled(false);
            scaleSeg_->button(i)->setToolTip(T("Мельче 100 % — только когда масштаб экрана в системе больше 100 % (например 125 %): мельче пикселя экрана окно не рисуется."));
        }
    auto* scaleLimit = lbl(T("Мельче 100 % на этом экране нельзя: масштаб в системе 100 %. Сделай его больше (например 125 %) — и 90 % станет доступно."), "faint");
    scaleLimit->setWordWrap(true);
    scaleLimit->setVisible(minScale > 90);
    lay->addWidget(scaleLimit);
    scaleRestart_ = new QPushButton(T("Перезапустить AutoQ"));
    scaleRestart_->setObjectName("accent");
    scaleRestart_->hide();
    lay->addWidget(scaleRestart_, 0, Qt::AlignLeft);

    auto* reset = new QPushButton(T("Сбросить оформление"));
    reset->setObjectName("ghost");
    reset->setToolTip(T("Вернуть «Студию», красно-чёрную тему и значки «Глянец»"));
    auto* rrow = new QHBoxLayout();
    rrow->addWidget(reset);
    rrow->addStretch(1);
    lay->addLayout(rrow);
    lay->addStretch(1);

    connect(themeSeg_, &Segmented::changed, this, [this](int i) {
        Theme::Appearance a = Theme::current();
        a.dark = (i == 0);
        apply(a);
    });
    connect(&accentGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        Theme::Appearance a = Theme::current();
        a.accent = QColor(Theme::accentPresets().value(id));
        apply(a);
    });
    connect(accCustom, &QPushButton::clicked, this, [this]() {
        QColor c = Theme::current().accent;
        if (!ColorPickerDialog::getColor(this, ColorPickerDialog::Accent, c)) return;
        Theme::Appearance a = Theme::current();
        a.accent = c;
        apply(a);
    });
    connect(&bgGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        Theme::Appearance a = Theme::current();
        const QColor c(Theme::bgPresets(a.dark).value(id));
        if (a.dark) a.bgDark = c; else a.bgLight = c;
        apply(a);
    });
    connect(&styleGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        Theme::Appearance a = Theme::current();
        a.iconStyle = AqIcons::iconStyles().value(id, "gloss");
        apply(a);
    });
    connect(animSeg_, &Segmented::changed, this, [this](int i) {
        Theme::Appearance a = Theme::current();
        a.anim = Theme::animations().value(i, "slide");
        apply(a);
        rebuildAnimDemo();
    });
    connect(animSpeed_, &QSlider::valueChanged, this, [this](int v) {
        Theme::Appearance a = Theme::current();
        a.animSpeed = Theme::animSpeedChoices().value(v, 100);
        Theme::saveAppearance(a);
        Theme::apply(*qApp, a);          // только скорость — перерисовывать окно не нужно
        animSpeedVal_->setText(Theme::animSpeedLabel(a.animSpeed));
        if (animDemo_) animDemo_->replay();
    });
    // пока тянут ползунок — только число; применяем, когда отпустили (стиль всего окна — не мгновенно)
    connect(radiusSlider_, &QSlider::valueChanged, this, [this](int v) {
        if (loadingEditor_) return;
        radiusVal_->setText(QString("%1 px").arg(v));
        if (radiusSlider_->isSliderDown()) return;
        Theme::Appearance a = Theme::current();
        a.radius = v;
        apply(a);
    });
    connect(radiusSlider_, &QSlider::sliderReleased, this, [this]() {
        Theme::Appearance a = Theme::current();
        a.radius = radiusSlider_->value();
        apply(a);
    });
    connect(radiusReset, &QToolButton::clicked, this, [this]() {
        Theme::Appearance a = Theme::current();
        a.radius = -1;
        apply(a);
    });
    connect(densitySeg_, &Segmented::changed, this, [this](int i) {
        Theme::Appearance a = Theme::current();
        a.density = i;
        apply(a);
    });
    connect(displayFontSeg_, &Segmented::changed, this, [this](int i) {
        Theme::Appearance a = Theme::current();
        a.displayFont = i - 1;
        apply(a);
    });
    connect(textFontSeg_, &Segmented::changed, this, [this](int i) {
        Theme::Appearance a = Theme::current();
        a.textFont = i;
        apply(a);
    });
    connect(bordersSeg_, &Segmented::changed, this, [this](int i) {
        Theme::Appearance a = Theme::current();
        a.borders = i;
        apply(a);
    });
    connect(edReset, &QPushButton::clicked, this, [this]() {
        Theme::Appearance a = Theme::current();
        a.radius = -1;
        a.density = 1;
        a.displayFont = -1;
        a.textFont = -1;
        a.borders = 1;
        apply(a);
    });
    connect(scaleSeg_, &Segmented::changed, this, [this](int i) {
        const int v = Theme::guiScaleChoices().value(i, 100);
        Theme::setGuiScale(v);
        const bool same = v == Theme::startupGuiScale();
        scaleNote_->setText(same ? QString() : T("Размер изменится после перезапуска AutoQ."));
        scaleRestart_->setVisible(!same);
    });
    connect(scaleRestart_, &QPushButton::clicked, this, &SettingsDialog::restartRequested);
    connect(reset, &QPushButton::clicked, this, [this]() {
        Theme::Appearance a;
        a.dark = true;
        a.accent = Theme::defaultAccent();
        a.bgDark = Theme::defaultBg(true);
        a.bgLight = Theme::defaultBg(false);
        a.anim = Theme::current().anim;
        a.animSpeed = Theme::current().animSpeed;
        apply(a);
    });
    return page;
}

void SettingsDialog::rebuildAnimDemo() {
    if (!animDemo_) return;
    // у образца стиль задаётся при создании — меняем образец целиком
    auto* fresh = new AnimDemo(Theme::current().anim);
    fresh->setFixedSize(animDemo_->size());
    fresh->setToolTip(animDemo_->toolTip());
    if (QLayout* l = animDemo_->parentWidget() ? animDemo_->parentWidget()->layout() : nullptr) {
        if (QLayoutItem* old = l->replaceWidget(animDemo_, fresh, Qt::FindChildrenRecursively)) delete old;
    }
    animDemo_->deleteLater();
    animDemo_ = fresh;
    animDemo_->replay();
}

void SettingsDialog::refreshEditor() {
    if (!radiusSlider_) return;
    const Theme::Appearance& a = Theme::current();
    loadingEditor_ = true;
    if (animSpeed_) {
        const QList<int> ch = Theme::animSpeedChoices();
        int best = 0;
        for (int i = 0; i < ch.size(); ++i) if (qAbs(ch[i] - a.animSpeed) < qAbs(ch[best] - a.animSpeed)) best = i;
        QSignalBlocker bs(animSpeed_);
        animSpeed_->setValue(best);
        animSpeedVal_->setText(Theme::animSpeedLabel(a.animSpeed));
        animSpeed_->setEnabled(a.anim != "none");
    }
    {
        QSignalBlocker b(radiusSlider_);
        radiusSlider_->setValue(Theme::cardRadiusFor(a));
    }
    radiusVal_->setText(a.radius < 0 ? T("стиль") : QString("%1 px").arg(a.radius));
    QSignalBlocker b1(densitySeg_), b2(displayFontSeg_), b3(textFontSeg_), b4(bordersSeg_), b5(animSeg_), b6(scaleSeg_);
    densitySeg_->setCurrent(a.density);
    displayFontSeg_->setCurrent(a.displayFont + 1);
    textFontSeg_->setCurrent(qMax(0, a.textFont));
    bordersSeg_->setCurrent(a.borders);
    animSeg_->setCurrent(Theme::animations().indexOf(Theme::normalizeAnim(a.anim)));
    animInfo_->setText(T("Сейчас: ") + Theme::animLabel(a.anim) + " — " + Theme::animNote(a.anim));
    scaleSeg_->setCurrent(Theme::guiScaleChoices().indexOf(Theme::guiScale()));
    for (int i = 0; i < variantCards_.size(); ++i) {
        const QString id = variantCards_[i]->property("variant").toString();
        variantCards_[i]->setChecked(id == Theme::normalizeVariant(a.variant));
        Theme::Appearance pa = a;          // образец — в своих цветах, но формой стиля
        pa.variant = id;
        pa.radius = -1;
        pa.displayFont = -1;
        variantPreviews_[i]->setAppearance(pa);
    }
    loadingEditor_ = false;
}

QList<QToolButton*> SettingsDialog::makeSwatches(const QStringList& colors, QButtonGroup& group, QHBoxLayout* row) {
    QList<QToolButton*> out;
    group.setExclusive(true);
    for (int i = 0; i < colors.size(); ++i) {
        auto* b = new QToolButton();
        b->setObjectName("swatch");
        b->setCheckable(true);
        b->setFixedSize(30, 30);
        b->setProperty("hex", colors[i]);
        b->setToolTip(Theme::colorName(colors[i]));
        b->setAccessibleName(T("Цвет: ") + Theme::colorName(colors[i]));
        b->setCursor(Qt::PointingHandCursor);
        group.addButton(b, i);
        row->addWidget(b);
        out << b;
    }
    return out;
}

void SettingsDialog::rebuildBgRow() {
    for (QToolButton* b : bgBtns_) { bgGroup_.removeButton(b); b->deleteLater(); }
    bgBtns_.clear();
    while (QLayoutItem* it = bgRow_->takeAt(0)) {
        if (it->widget()) { it->widget()->hide(); it->widget()->deleteLater(); }
        delete it;
    }
    const bool dark = Theme::current().dark;
    bgBtns_ = makeSwatches(Theme::bgPresets(dark), bgGroup_, bgRow_);
    auto* custom = new QPushButton(T("Свой…"));
    custom->setToolTip(dark ? T("Любой тёмный цвет фона — с образцом") : T("Любой светлый цвет фона — с образцом"));
    connect(custom, &QPushButton::clicked, this, [this]() {
        Theme::Appearance a = Theme::current();
        QColor c = a.dark ? a.bgDark : a.bgLight;
        if (!ColorPickerDialog::getColor(this, ColorPickerDialog::Background, c)) return;
        if (a.dark) a.bgDark = c; else a.bgLight = c;
        apply(a);
    });
    bgRow_->addSpacing(4);
    bgRow_->addWidget(custom);
    bgRow_->addStretch(1);
}

void SettingsDialog::apply(const Theme::Appearance& a) {
    const bool themeChanged = a.dark != Theme::current().dark;
    Theme::apply(*qApp, a);
    Theme::saveAppearance(a);
    if (themeChanged) rebuildBgRow();
    refresh();
    emit appearanceChanged();
}

void SettingsDialog::refresh() {
    const Theme::Appearance& a = Theme::current();
    const auto& t = Theme::t();
    themeSeg_->blockSignals(true);
    themeSeg_->setCurrent(a.dark ? 0 : 1);
    themeSeg_->blockSignals(false);
    auto paint = [&t](QList<QToolButton*>& btns, QButtonGroup& group, const QColor& cur) {
        group.setExclusive(false);
        for (QToolButton* b : btns) {
            const QString hex = b->property("hex").toString();
            b->setStyleSheet(QString("QToolButton#swatch { background: %1; border: 2px solid %2; border-radius: 15px; }"
                                     "QToolButton#swatch:checked { border: 3px solid %3; }")
                                 .arg(hex, t.fieldBorder2.name(), t.text.name()));
            b->setChecked(hex.compare(cur.name(), Qt::CaseInsensitive) == 0);
        }
        group.setExclusive(true);
    };
    paint(accentBtns_, accentGroup_, a.accent);
    const QColor bg = a.dark ? a.bgDark : a.bgLight;
    paint(bgBtns_, bgGroup_, bg);
    accentInfo_->setText(T("Сейчас: ") + Theme::colorLabel(a.accent));
    bgTitle_->setText(a.dark ? T("Фон тёмной темы") : T("Фон светлой темы"));
    bgInfo_->setText(T("Сейчас: ") + Theme::colorLabel(bg));
    {
        const qreal dpr = devicePixelRatioF();
        const struct { const char* glyph; const char* color; } sample[] = {
            {"compass", "#F2C66D"}, {"cube", "#6EDC8C"}, {"flame", "#FF9F43"}};
        for (QToolButton* b : styleBtns_) {
            const QString id = b->property("styleId").toString();
            QPixmap pm(qRound(102 * dpr), qRound(30 * dpr));
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            for (int i = 0; i < 3; ++i)
                p.drawPixmap(i * 36, 0, AqIcons::gameTileStyled(id, sample[i].glyph, QColor(sample[i].color),
                                                                {}, 30, dpr, false, t.light));
            p.end();
            b->setIcon(QIcon(pm));
            QSignalBlocker block(b);
            b->setChecked(id == a.iconStyle);
        }
        styleInfo_->setText(T("Сейчас: ") + AqIcons::iconStyleLabel(a.iconStyle) + " — " +
                            AqIcons::iconStyleNote(a.iconStyle));
    }
    headIcon_->setPixmap(AqIcons::uiPixmap("settings", t.redHi, 20, devicePixelRatioF(), 1.8));
    for (QAbstractButton* b : findChildren<QAbstractButton*>()) {
        const QString name = b->property("aqIcon").toString();
        if (!name.isEmpty())
            b->setIcon(AqIcons::ui(name, b->property("aqIconMuted").toBool() ? t.muted : t.text, b->property("aqIconPx").toInt()));
    }
    refreshEditor();
    if (oNumAuto_) {
        paintColorButtons();
        oPreview_->update();
    }
}

// ===========================================================================
// Оверлей КПС
// ===========================================================================
QWidget* SettingsDialog::buildOverlayPage() {
    auto* page = new QWidget();
    page->setObjectName("mainInner");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 10, 22, 10);
    lay->setSpacing(10);

    auto toggleRow = [](ToggleSwitch*& sw, const QString& text, const QString& tip = QString()) {
        auto* h = new QHBoxLayout();
        h->setSpacing(8);
        sw = new ToggleSwitch();
        sw->setAccessibleName(text);
        if (!tip.isEmpty()) sw->setToolTip(tip);
        h->addWidget(sw);
        auto* l = lbl(text, "muted");
        if (!tip.isEmpty()) l->setToolTip(tip);
        h->addWidget(l, 1);
        return h;
    };
    auto section = [&](const QString& title) {
        lay->addSpacing(4);
        lay->addWidget(lbl(title.toUpper(), "section"));
    };
    // у каждого ползунка — своя кнопка «как по умолчанию»
    auto sliderRow = [](const QString& text, QSlider*& sl, QLabel*& val, int lo, int hi, int def) {
        auto* h = new QHBoxLayout();
        h->setSpacing(10);
        auto* l = lbl(text, "muted");
        l->setFixedWidth(110);
        h->addWidget(l);
        sl = new QSlider(Qt::Horizontal);
        sl->setRange(lo, hi);
        sl->setAccessibleName(text);
        h->addWidget(sl, 1);
        val = lbl(QString(), "connectorText");
        val->setFixedWidth(52);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(val);
        QToolButton* rb = resetMini(T("По умолчанию: %1").arg(def));
        QSlider* target = sl;
        QObject::connect(rb, &QToolButton::clicked, target, [target, def]() { target->setValue(def); });
        h->addWidget(rb);
        return h;
    };

    // --- включить и когда
    auto* head = toggleRow(oEnabled_, T("Показывать оверлей поверх игры"),
                           T("Прозрачная надпись поверх всех окон: сколько кликов в секунду делает AutoQ"));
    oWhen_ = new Segmented({T("Пока работает"), T("Всегда")}, false);
    oWhen_->setToolTip(T("Пока работает — только когда кликер включён.\nВсегда — пока выбрана игра."));
    head->addWidget(oWhen_);
    lay->addLayout(head);
    auto* note = lbl(T("Клики проходят сквозь оверлей в игру. Меняй настройки — оверлей и образец ниже меняются сразу.") + "\n" +
                     T("Работают несколько игр — все в одной плашке, по строке с названием и профилем. Скрыть игру или профиль из оверлея — "
                       "кнопка со спидометром у профиля (карточка «Профиль и запуск»)."), "small");
    note->setWordWrap(true);
    lay->addWidget(note);
    oPreview_ = new OverlayPreview();
    lay->addWidget(oPreview_, 0, Qt::AlignHCenter);

    // --- что показывать
    section(T("Что показывать"));
    auto* what = new QGridLayout();
    what->setHorizontalSpacing(18);
    what->setVerticalSpacing(8);
    what->addLayout(toggleRow(oShowCps_, T("Клики в секунду (число)")), 0, 0);
    auto* lab = toggleRow(oShowLabel_, T("Подпись"));
    oLabel_ = new QLineEdit();
    oLabel_->setMaxLength(16);
    oLabel_->setFixedWidth(110);
    oLabel_->setPlaceholderText(T("КПС"));
    oLabel_->setToolTip(T("Текст рядом с числом: КПС, CPS, клики/с…"));
    lab->addWidget(oLabel_);
    what->addLayout(lab, 0, 1);
    what->addLayout(toggleRow(oShowActions_, T("По действиям (ЛКМ · F)"),
                              T("Сколько в секунду жмёт каждое действие отдельно")), 1, 0);
    what->addLayout(toggleRow(oShowTotal_, T("Всего за сессию")), 1, 1);
    what->addLayout(toggleRow(oShowStatus_, T("Статус (работает / пауза)")), 2, 0);
    what->addLayout(toggleRow(oShowGame_, T("Название игры")), 2, 1);
    what->addLayout(toggleRow(oShowGraph_, T("График за 10 секунд")), 3, 0);
    auto* win = new QHBoxLayout();
    win->addWidget(lbl(T("Считать за"), "muted"));
    oWindow_ = new Segmented({T("0,5 с"), T("1 с"), T("2 с")}, false);
    oWindow_->setToolTip(T("За какое время усреднять клики в секунду: короче — живее, длиннее — ровнее"));
    win->addWidget(oWindow_);
    win->addStretch(1);
    what->addLayout(win, 3, 1);
    lay->addLayout(what);

    // --- где
    section(T("Где на экране"));
    auto* where = new QHBoxLayout();
    where->setSpacing(18);
    auto* grid = new QGridLayout();
    grid->setSpacing(4);
    oAnchorGroup_.setExclusive(true);
    const QString tips[9] = {T("слева сверху"), T("сверху по центру"), T("справа сверху"), T("слева по центру"),
                                  T("в центре"), T("справа по центру"), T("слева снизу"), T("снизу по центру"), T("справа снизу")};
    for (int i = 0; i < 9; ++i) {
        auto* b = new QToolButton();
        b->setObjectName("anchor");
        b->setCheckable(true);
        b->setFixedSize(34, 26);
        b->setToolTip(T("Место: %1").arg(tips[i]));
        b->setAccessibleName(b->toolTip());
        b->setCursor(Qt::PointingHandCursor);
        oAnchorGroup_.addButton(b, i);
        oAnchorBtns_ << b;
        grid->addWidget(b, i / 3, i % 3);
    }
    auto* gridBox = new QFrame();
    gridBox->setObjectName("slot");
    auto* gb = new QVBoxLayout(gridBox);
    gb->setContentsMargins(8, 8, 8, 8);
    gb->addLayout(grid);
    where->addWidget(gridBox, 0, Qt::AlignTop);
    auto* wcol = new QGridLayout();
    wcol->setHorizontalSpacing(8);
    wcol->setVerticalSpacing(8);
    auto mkOff = [](QSpinBox*& sp, const QString& tip) {
        sp = new QSpinBox();
        sp->setRange(-4000, 8000);
        sp->setSuffix(" px");
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        sp->setAlignment(Qt::AlignCenter);
        sp->setAccelerated(true);
        sp->setFixedWidth(90);
        sp->setToolTip(tip);
        sp->setAccessibleName(tip);
        return sp;
    };
    wcol->addWidget(lbl(T("Отступ ↔"), "muted"), 0, 0);
    wcol->addWidget(mkOff(oOffX_, T("Отступ от края экрана по горизонтали (колёсико — быстро)")), 0, 1);
    wcol->addWidget(lbl(T("Отступ ↕"), "muted"), 1, 0);
    wcol->addWidget(mkOff(oOffY_, T("Отступ от края экрана по вертикали (колёсико — быстро)")), 1, 1);
    wcol->addWidget(lbl(T("Монитор"), "muted"), 2, 0);
    oScreen_ = new QComboBox();
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i)
        oScreen_->addItem(QString("%1 — %2×%3").arg(i + 1).arg(screens[i]->geometry().width())
                              .arg(screens[i]->geometry().height()));
    wcol->addWidget(oScreen_, 2, 1, 1, 2);
    oMoveBtn_ = new QPushButton(T("Перетащить мышью"));
    oMoveBtn_->setObjectName("outline");
    oMoveBtn_->setCheckable(true);
    oMoveBtn_->setToolTip(T("Оверлей появится на экране — тащи его мышью куда удобно, потом нажми ещё раз"));
    wcol->addWidget(oMoveBtn_, 3, 0, 1, 3);
    wcol->setColumnStretch(2, 1);
    where->addLayout(wcol, 1);
    lay->addLayout(where);

    // --- вид
    section(T("Вид"));
    auto* colors = new QHBoxLayout();
    colors->setSpacing(8);
    auto mkSw = [](QToolButton*& b, const QString& tip) {
        b = new QToolButton();
        b->setObjectName("swatch");
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        return b;
    };
    colors->addWidget(lbl(T("Текст"), "muted"));
    colors->addWidget(mkSw(oTextColor_, T("Цвет текста")));
    colors->addSpacing(10);
    colors->addWidget(lbl(T("Число"), "muted"));
    colors->addWidget(mkSw(oNumColor_, T("Цвет числа")));
    oNumAuto_ = new QToolButton();
    oNumAuto_->setObjectName("templateChip");
    oNumAuto_->setText(T("как акцент"));
    oNumAuto_->setCheckable(true);
    oNumAuto_->setToolTip(T("Цвет числа — как цвет акцента AutoQ"));
    oNumAuto_->setCursor(Qt::PointingHandCursor);
    colors->addWidget(oNumAuto_);
    colors->addSpacing(10);
    colors->addWidget(lbl(T("Подложка"), "muted"));
    colors->addWidget(mkSw(oBgColor_, T("Цвет подложки")));
    colors->addStretch(1);
    lay->addLayout(colors);
    const OverlaySettings def = OverlaySettings::defaults();
    lay->addLayout(sliderRow(T("Подложка"), oBgOpacity_, oBgOpacityVal_, 0, 100, def.bgOpacity));
    lay->addLayout(sliderRow(T("Видимость"), oOpacity_, oOpacityVal_, 15, 100, def.opacity));
    lay->addLayout(sliderRow(T("Размер"), oScale_, oScaleVal_, 50, 300, def.scale));
    lay->addLayout(sliderRow(T("Скругление"), oRadius_, oRadiusVal_, 0, 30, def.radius));
    auto* look = new QGridLayout();
    look->setHorizontalSpacing(10);
    look->setVerticalSpacing(8);
    look->addWidget(lbl(T("Шрифт"), "muted"), 0, 0);
    oFont_ = new Segmented({"Unbounded", "Onest", "Mono"}, false);
    look->addWidget(oFont_, 0, 1);
    look->addWidget(lbl(T("Расположение"), "muted"), 1, 0);
    oLayout_ = new Segmented({T("В строку"), T("Столбиком")}, false);
    look->addWidget(oLayout_, 1, 1);
    look->addLayout(toggleRow(oOutline_, T("Обводка текста — видно на любом фоне")), 2, 0, 1, 2);
    look->setColumnStretch(2, 1);
    lay->addLayout(look);

#if !defined(_WIN32) && !defined(__APPLE__)
    // --- Linux: Wayland
    section("Linux · Wayland");
    lay->addLayout(toggleRow(oXWayland_, T("Окна AutoQ через XWayland (нужно под Wayland)"),
                             T("Под Wayland окно-оверлей нельзя поставить в нужное место поверх игры.\nЧерез XWayland — можно (и мини-окно держится поверх игры).\nМеняется после перезапуска AutoQ.")));
    oPlatformNote_ = lbl(QString(), "small");
    oPlatformNote_->setWordWrap(true);
    lay->addWidget(oPlatformNote_);
    {
        auto s = AppSettings::open();
        oXWayland_->setChecked(s->value("useXWayland", true).toBool());
    }
    const bool waylandSession = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    const QString plat = QGuiApplication::platformName();
    if (!waylandSession) {
        oPlatformNote_->setText(T("Сейчас X11 — менять ничего не нужно."));
        oXWayland_->setEnabled(false);
    } else if (plat == "wayland") {
        oPlatformNote_->setText(T("⚠ Сейчас AutoQ — Wayland-окно: оверлей не встанет в нужное место поверх игры. Включи переключатель и перезапусти AutoQ."));
    } else {
        oPlatformNote_->setText(T("Работает через XWayland — оверлей и мини-окно держатся поверх игры."));
    }
    connect(oXWayland_, &ToggleSwitch::toggled, this, [this](bool on) {
        auto s = AppSettings::open();
        s->setValue("useXWayland", on);
        oPlatformNote_->setText(T("Изменится после перезапуска AutoQ (значок в трее → Выход, затем запусти снова)."));
    });
#endif

    auto* reset = new QPushButton(T("Сбросить оверлей"));
    reset->setObjectName("ghost");
    auto* rr = new QHBoxLayout();
    rr->addWidget(reset);
    rr->addStretch(1);
    lay->addLayout(rr);
    lay->addStretch(1);

    // --- сигналы: любое изменение -> сразу на оверлей и на диск
    for (ToggleSwitch* sw : {oEnabled_, oShowCps_, oShowLabel_, oShowActions_, oShowTotal_, oShowStatus_,
                             oShowGame_, oShowGraph_, oOutline_})
        connect(sw, &ToggleSwitch::toggled, this, [this]() { overlayFromUi(); });
    for (Segmented* sg : {oWhen_, oWindow_, oFont_, oLayout_})
        connect(sg, &Segmented::changed, this, [this]() { overlayFromUi(); });
    connect(oLabel_, &QLineEdit::textChanged, this, [this]() { overlayFromUi(); });
    connect(&oAnchorGroup_, &QButtonGroup::idClicked, this, [this]() {
        // новый угол — отступ по умолчанию, чтобы оверлей не улетел за край
        loadingOverlay_ = true;
        oOffX_->setValue(oAnchorGroup_.checkedId() % 3 == 1 ? 0 : 24);
        oOffY_->setValue(oAnchorGroup_.checkedId() / 3 == 1 ? 0 : 24);
        loadingOverlay_ = false;
        overlayFromUi();
        paintColorButtons();
    });
    for (QSpinBox* sp : {oOffX_, oOffY_})
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { overlayFromUi(); });
    connect(oScreen_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { overlayFromUi(); });
    for (QSlider* sl : {oBgOpacity_, oOpacity_, oScale_, oRadius_})
        connect(sl, &QSlider::valueChanged, this, [this]() { overlayFromUi(); });
    connect(oTextColor_, &QToolButton::clicked, this, [this]() { pickOverlayColor(0); });
    connect(oNumColor_, &QToolButton::clicked, this, [this]() { pickOverlayColor(1); });
    connect(oBgColor_, &QToolButton::clicked, this, [this]() { pickOverlayColor(2); });
    connect(oNumAuto_, &QToolButton::clicked, this, [this](bool on) {
        os_.numberColor = on ? QColor() : Theme::t().redHi;
        applyOverlay();
        paintColorButtons();
    });
    connect(oMoveBtn_, &QPushButton::toggled, this, [this](bool on) {
        oMoveBtn_->setText(on ? T("Готово — закрепить здесь") : T("Перетащить мышью"));
        if (overlay_) overlay_->setMoveMode(on);
    });
    if (overlay_)
        connect(overlay_, &OverlayWindow::moved, this, [this](int screen, int x, int y) {
            os_.screen = screen;
            os_.offsetX = x;
            os_.offsetY = y;
            os_.save();
            loadingOverlay_ = true;          // поля обновить, не трогая сам оверлей
            oOffX_->setValue(x);
            oOffY_->setValue(y);
            oScreen_->setCurrentIndex(qBound(0, screen, oScreen_->count() - 1));
            loadingOverlay_ = false;
            oPreview_->setSettings(os_);
        });
    connect(reset, &QPushButton::clicked, this, [this]() {
        const bool en = os_.enabled;
        os_ = OverlaySettings::defaults();
        os_.enabled = en;
        overlayToUi();
        applyOverlay();
    });
    return page;
}

void SettingsDialog::overlayToUi() {
    if (!oNumAuto_) return;
    loadingOverlay_ = true;
    oEnabled_->setChecked(os_.enabled);
    oWhen_->setCurrent(os_.when);
    oShowCps_->setChecked(os_.showCps);
    oShowLabel_->setChecked(os_.showLabel);
    if (oLabel_->text() != os_.label) oLabel_->setText(os_.label);
    oShowActions_->setChecked(os_.showActions);
    oShowTotal_->setChecked(os_.showTotal);
    oShowStatus_->setChecked(os_.showStatus);
    oShowGame_->setChecked(os_.showGame);
    oShowGraph_->setChecked(os_.showGraph);
    oWindow_->setCurrent(os_.windowMs == 500 ? 0 : os_.windowMs == 2000 ? 2 : 1);
    if (QAbstractButton* b = oAnchorGroup_.button(os_.anchor)) b->setChecked(true);
    oOffX_->setValue(os_.offsetX);
    oOffY_->setValue(os_.offsetY);
    oScreen_->setCurrentIndex(qBound(0, os_.screen, oScreen_->count() - 1));
    oBgOpacity_->setValue(os_.bgOpacity);
    oOpacity_->setValue(os_.opacity);
    oScale_->setValue(os_.scale);
    oRadius_->setValue(os_.radius);
    oFont_->setCurrent(os_.font);
    oLayout_->setCurrent(os_.vertical ? 1 : 0);
    oOutline_->setChecked(os_.outline);
    loadingOverlay_ = false;
    oBgOpacityVal_->setText(QString("%1%").arg(os_.bgOpacity));
    oOpacityVal_->setText(QString("%1%").arg(os_.opacity));
    oScaleVal_->setText(QString("%1%").arg(os_.scale));
    oRadiusVal_->setText(QString::number(os_.radius));
    oWhen_->setEnabled(os_.enabled);
    oLabel_->setEnabled(os_.showLabel);
    paintColorButtons();
    oPreview_->setSettings(os_);
}

void SettingsDialog::overlayFromUi() {
    if (loadingOverlay_) return;
    os_.enabled = oEnabled_->isChecked();
    os_.when = qMax(0, oWhen_->current());
    os_.showCps = oShowCps_->isChecked();
    os_.showLabel = oShowLabel_->isChecked();
    os_.label = oLabel_->text().trimmed();
    os_.showActions = oShowActions_->isChecked();
    os_.showTotal = oShowTotal_->isChecked();
    os_.showStatus = oShowStatus_->isChecked();
    os_.showGame = oShowGame_->isChecked();
    os_.showGraph = oShowGraph_->isChecked();
    const int w = oWindow_->current();
    os_.windowMs = w == 0 ? 500 : w == 2 ? 2000 : 1000;
    os_.anchor = qMax(0, oAnchorGroup_.checkedId());
    os_.offsetX = oOffX_->value();
    os_.offsetY = oOffY_->value();
    os_.screen = qMax(0, oScreen_->currentIndex());
    os_.bgOpacity = oBgOpacity_->value();
    os_.opacity = oOpacity_->value();
    os_.scale = oScale_->value();
    os_.radius = oRadius_->value();
    os_.font = qMax(0, oFont_->current());
    os_.vertical = oLayout_->current() == 1;
    os_.outline = oOutline_->isChecked();
    oBgOpacityVal_->setText(QString("%1%").arg(os_.bgOpacity));
    oOpacityVal_->setText(QString("%1%").arg(os_.opacity));
    oScaleVal_->setText(QString("%1%").arg(os_.scale));
    oRadiusVal_->setText(QString::number(os_.radius));
    oWhen_->setEnabled(os_.enabled);
    oLabel_->setEnabled(os_.showLabel);
    applyOverlay();
}

void SettingsDialog::applyOverlay() {
    os_.save();
    oPreview_->setSettings(os_);
    if (overlay_) overlay_->setSettings(os_);
    emit overlayChanged();
}

void SettingsDialog::paintColorButtons() {
    const auto& t = Theme::t();
    auto style = [&t](QToolButton* b, const QColor& c) {
        b->setStyleSheet(QString("QToolButton#swatch { background: %1; border: 2px solid %2; border-radius: 14px; }")
                             .arg(c.name(), t.fieldBorder2.name()));
    };
    style(oTextColor_, os_.textColor);
    style(oNumColor_, os_.numberColor.isValid() ? os_.numberColor : t.redHi);
    style(oBgColor_, os_.bgColor);
    oNumAuto_->setChecked(!os_.numberColor.isValid());
    const qreal dpr = devicePixelRatioF();
    for (int i = 0; i < oAnchorBtns_.size(); ++i) {
        // точка показывает, в какой части экрана будет оверлей
        QPixmap pm(qRound(26 * dpr), qRound(18 * dpr));
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(t.fieldBorder2, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(0.5, 0.5, 25, 17), 3, 3);
        const int col = i % 3, row = i / 3;
        p.setPen(Qt::NoPen);
        p.setBrush(oAnchorBtns_[i]->isChecked() ? t.redHi : t.muted);
        p.drawRoundedRect(QRectF(3 + col * 7.5, 3 + row * 4.5, 5, 3), 1, 1);
        p.end();
        oAnchorBtns_[i]->setIcon(QIcon(pm));
        oAnchorBtns_[i]->setIconSize(QSize(26, 18));
    }
}

void SettingsDialog::pickOverlayColor(int which) {
    const OverlaySettings before = os_;
    QColor c = which == 0 ? os_.textColor : which == 2 ? os_.bgColor
             : (os_.numberColor.isValid() ? os_.numberColor : Theme::t().redHi);
    ColorPickerDialog dlg(ColorPickerDialog::Plain, c, this);
    dlg.setWindowTitle(which == 0 ? T("Цвет текста оверлея — AutoQ") : which == 1 ? T("Цвет числа оверлея — AutoQ")
                                                                     : T("Цвет подложки оверлея — AutoQ"));
    // образец и сам оверлей меняются сразу, пока двигаешь цвет
    connect(&dlg, &ColorPickerDialog::colorChanged, this, [this, which](const QColor& nc) {
        if (which == 0) os_.textColor = nc;
        else if (which == 1) os_.numberColor = nc;
        else os_.bgColor = nc;
        oPreview_->setSettings(os_);
        if (overlay_) overlay_->setSettings(os_);
    });
    if (dlg.exec() == QDialog::Accepted) {
        applyOverlay();
    } else {
        os_ = before;
        oPreview_->setSettings(os_);
        if (overlay_) overlay_->setSettings(os_);
    }
    paintColorButtons();
}
