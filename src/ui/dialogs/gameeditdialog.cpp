#include "ui/dialogs/gameeditdialog.h"
#include "core/i18n.h"
#include "ui/dialogs/windowpicker.h"
#include <QFileInfo>
#include "ui/style/widgets.h"
#include "ui/style/icons.h"
#include "ui/style/theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QImageReader>
#include <QFrame>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>

static QLabel* fieldLabel(const QString& text, QWidget* buddy = nullptr) {
    auto* l = new QLabel(text);
    l->setObjectName("fieldLabel");
    if (buddy) l->setBuddy(buddy);
    return l;
}

GameEditDialog::GameEditDialog(const GameDef& game, bool isNew, QWidget* parent)
    : QDialog(parent), game_(game), isNew_(isNew) {
    setWindowTitle(isNew ? T("Добавить игру — AutoQ") : T("Изменить игру — AutoQ"));
    setMinimumWidth(560);
    const auto& t = Theme::t();
    const qreal dpr = devicePixelRatioF();

    // содержимое — в прокрутке (на невысоких экранах окно не сжимает поля)
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* body = new QWidget();
    body->setObjectName("mainInner");
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(20, 18, 20, 10);
    lay->setSpacing(12);
    customId_ = game.id;

    // --- шаблоны (только при добавлении): готовые игры из базы AutoQ ---
    if (isNew) {
        auto* tplHead = new QHBoxLayout();
        auto* tplTitle = new QLabel(T("Готовые игры"));
        tplTitle->setObjectName("cardTitle");
        tplHead->addWidget(tplTitle);
        auto* tplHint = new QLabel(T("— режим, правила окна и пресеты подставятся сами"));
        tplHint->setObjectName("small");
        tplHead->addWidget(tplHint);
        tplHead->addStretch(1);
        lay->addLayout(tplHead);
        auto* tplBox = new QWidget();
        auto* flow = new FlowLayout(tplBox, 6, 6);
        templateGroup_.setExclusive(true);
        auto addTpl = [&](const QString& id, const QString& name, const QString& glyph, const QString& color) {
            auto* b = new QToolButton();
            b->setObjectName("templateChip");
            b->setCheckable(true);
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setIconSize(QSize(20, 20));
            b->setIcon(QIcon(AqIcons::gameTile(glyph, QColor(color), {}, 20, dpr, false, t.light)));
            b->setText(name);
            b->setCursor(Qt::PointingHandCursor);
            b->setProperty("tpl", id);
            const bool taken = !id.isEmpty() && Games::contains(id);
            b->setEnabled(!taken);
            b->setToolTip(taken ? T("Уже в списке игр") : id.isEmpty() ? T("Своя игра — заполни поля ниже")
                                                                   : T("Взять готовые настройки «") + name + "»");
            templateGroup_.addButton(b);
            templateButtons_ << b;
            flow->addWidget(b);
            connect(b, &QToolButton::clicked, this, [this, id]() { chooseTemplate(id); });
        };
        addTpl(QString(), T("Своя игра"), "gamepad", "#FF5A66");
        for (const GameDef& d : Games::builtinDefaults()) addTpl(d.id, d.name, d.icon, d.color);
        lay->addWidget(tplBox);
        auto* div = new QFrame();
        div->setObjectName("divider");
        lay->addWidget(div);
    }

    // --- значок ---
    auto* iconRow = new QHBoxLayout();
    iconRow->setSpacing(16);
    preview_ = new QLabel();
    preview_->setFixedSize(76, 76);
    iconRow->addWidget(preview_, 0, Qt::AlignTop);
    auto* iconCol = new QVBoxLayout();
    iconCol->setSpacing(8);
    auto* iconHead = new QHBoxLayout();
    auto* iconTitle = new QLabel(T("Иконка"));
    iconTitle->setObjectName("cardTitle");
    iconHead->addWidget(iconTitle);
    iconHead->addStretch(1);
    auto* imgBtn = new QPushButton(AqIcons::ui("folder", t.muted, 14), T("Своя картинка…"));
    imgBtn->setToolTip(T("PNG, BMP или ICO — подойдёт любой квадратный рисунок"));
    auto* resetBtn = new QPushButton(T("Сбросить"));
    resetBtn->setObjectName("ghost");
    iconHead->addWidget(imgBtn);
    iconHead->addWidget(resetBtn);
    iconCol->addLayout(iconHead);

    auto* grid = new QGridLayout();
    grid->setSpacing(4);
    iconGroup_.setExclusive(true);
    const QStringList glyphs = AqIcons::gameGlyphs();
    for (int i = 0; i < glyphs.size(); ++i) {
        auto* b = new QToolButton();
        b->setObjectName("pick");
        b->setCheckable(true);
        b->setFixedSize(44, 44);
        b->setIconSize(QSize(32, 32));
        b->setToolTip(T("Значок: ") + AqIcons::glyphLabel(glyphs[i]));
        b->setAccessibleName(b->toolTip());
        b->setProperty("glyph", glyphs[i]);
        iconGroup_.addButton(b, i);
        iconButtons_ << b;
        grid->addWidget(b, i / 8, i % 8);
    }
    iconCol->addLayout(grid);

    auto* colorRow = new QHBoxLayout();
    colorRow->setSpacing(8);
    colorRow->addWidget(fieldLabel(T("Цвет")));
    colorGroup_.setExclusive(true);
    const QStringList pal = AqIcons::palette();
    for (int i = 0; i < pal.size(); ++i) {
        auto* b = new QToolButton();
        b->setObjectName("swatch");
        b->setCheckable(true);
        b->setFixedSize(26, 26);
        b->setStyleSheet(QString("QToolButton#swatch { background: %1; }").arg(pal[i]));
        b->setToolTip(T("Цвет: ") + AqIcons::paletteLabel(pal[i]));
        b->setAccessibleName(b->toolTip());
        colorGroup_.addButton(b, i);
        colorButtons_ << b;
        colorRow->addWidget(b);
    }
    colorRow->addStretch(1);
    iconCol->addLayout(colorRow);
    iconRow->addLayout(iconCol, 1);
    lay->addLayout(iconRow);

    // --- поля ---
    nameEdit_ = new QLineEdit();
    nameEdit_->setPlaceholderText(T("Например: Honkai Impact 3rd"));
    lay->addWidget(fieldLabel(T("Название"), nameEdit_));
    lay->addWidget(nameEdit_);

    modeSeg_ = new Segmented({T("По очереди"), T("Вместе"), T("Зажать"), T("По шагам")}, false);
    modeSeg_->setToolTip(T("По очереди: ЛКМ, потом F (Genshin).\nВместе: нажать обе сразу, как F3+A (Minecraft).\nЗажать: держать нажатыми, пока кликер включён.\nПо шагам: отдельно «нажать ↓» и «отпустить ↑», с паузами (кикфлипы, отмены)."));
    lay->addWidget(fieldLabel(T("Режим")));
    lay->addWidget(modeSeg_);

    procEdit_ = new QLineEdit();
    procEdit_->setObjectName("mono");
    procEdit_->setPlaceholderText(T("GameName.exe — через запятую"));
    procEdit_->setToolTip(T("Имя процесса игры. Надёжнее всего: окно другой программы\nс похожим заголовком (например вкладка браузера) не пройдёт."));
    lay->addWidget(fieldLabel(T("Процессы (через запятую) — можно оставить пустым"), procEdit_));
    {
        auto* pr = new QHBoxLayout();
        pr->setSpacing(6);
        pr->addWidget(procEdit_, 1);
        auto* pick = new QPushButton(T("Выбрать…"));
        pick->setToolTip(T("Выбрать процесс из открытых окон — вписывать не нужно"));
        pick->setCursor(Qt::PointingHandCursor);
        connect(pick, &QPushButton::clicked, this, [this, pick]() {
            WindowPickerPopup::open(pick, WindowPickerPopup::Process, Games::splitList(procEdit_->text()),
                                    [this](const QString& v) {
                QStringList l = Games::splitList(procEdit_->text());
                if (!l.contains(v, Qt::CaseInsensitive)) l << v;
                procEdit_->setText(Games::joinList(l));
                if (nameEdit_->text().trimmed().isEmpty()) nameEdit_->setText(QFileInfo(v).completeBaseName());
            });
        });
        pr->addWidget(pick);
        lay->addLayout(pr);
    }

    titleEdit_ = new QLineEdit();
    titleEdit_->setPlaceholderText(T("Часть заголовка окна — через запятую"));
    lay->addWidget(fieldLabel(T("Заголовок окна содержит"), titleEdit_));
    lay->addWidget(titleEdit_);

    auto* minRow = new QHBoxLayout();
    minIntervalBox_ = new QSpinBox();
    minIntervalBox_->setRange(5, 1000);
    minIntervalBox_->setSuffix(T(" мс"));
    minIntervalBox_->setFixedWidth(96);
    minIntervalBox_->setAlignment(Qt::AlignCenter);
    minIntervalBox_->setToolTip(T("Самый короткий интервал, который можно выставить для этой игры"));
    minRow->addWidget(fieldLabel(T("Минимальный интервал"), minIntervalBox_), 1);
    minRow->addWidget(minIntervalBox_);
    lay->addLayout(minRow);

    // --- захват из окна ---
    auto* cap = new QFrame();
    cap->setObjectName("captureBox");
    auto* capLay = new QVBoxLayout(cap);
    capLay->setContentsMargins(14, 12, 14, 12);
    capLay->setSpacing(10);
    auto* capHead = new QHBoxLayout();
    auto* capIcon = new QLabel();
    capIcon->setPixmap(AqIcons::uiPixmap("target", t.redHi, 16, dpr));
    auto* capTitle = new QLabel(T("Взять из окна игры"));
    capTitle->setObjectName("cardTitle");
    capHead->addWidget(capIcon);
    capHead->addWidget(capTitle);
    capHead->addStretch(1);
    capLay->addLayout(capHead);
    infoLabel_ = new QLabel(T("Нажми кнопку и переключись в игру — AutoQ запомнит процесс и заголовок окна."));
    infoLabel_->setObjectName("muted");
    infoLabel_->setWordWrap(true);
    capLay->addWidget(infoLabel_);
    auto* capRow = new QHBoxLayout();
    capRow->setSpacing(8);
    captureBtn_ = new QPushButton();
    captureBtn_->setObjectName("accent");
    captureBtn_->setMinimumHeight(40);
    countdown_ = new CaptureCountdown(captureBtn_, this);
    countdown_->setIdleText(T("Взять из окна игры"));
    capRow->addWidget(captureBtn_, 1);
    timeSeg_ = new TimeChoice();
    capRow->addWidget(timeSeg_);
    capLay->addLayout(capRow);
    lay->addWidget(cap);

    // --- кнопки ---
    auto* foot = new QHBoxLayout();
    foot->setSpacing(8);
    if (game_.builtin) {
        auto* def = new QPushButton(T("По умолчанию"));
        def->setObjectName("ghost");
        def->setToolTip(T("Вернуть встроенные правила окна, режим и значок"));
        connect(def, &QPushButton::clicked, this, &GameEditDialog::resetDefaults);
        foot->addWidget(def);
    }
    // убрать игру — здесь и по правому щелчку в списке (в шапке кнопки больше нет)
    if (!isNew) {
        auto* del = new QPushButton(T("Убрать игру…"));
        del->setObjectName("ghost");
        del->setToolTip(T("Убрать игру из списка (спросит ещё раз; профили останутся в папке данных)"));
        connect(del, &QPushButton::clicked, this, [this]() {
            removeRequested_ = true;
            reject();
        });
        foot->addWidget(del);
    }
    foot->addStretch(1);
    auto* cancel = new QPushButton(T("Отмена"));
    auto* save = new QPushButton(isNew ? T("Добавить") : T("Сохранить"));
    save->setObjectName("primary");
    save->setDefault(true);
    foot->addWidget(cancel);
    foot->addWidget(save);
    auto* scroll = new QScrollArea();
    scroll->setObjectName("mainScroll");
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll, 1);
    auto* footBox = new QWidget();
    footBox->setLayout(foot);
    foot->setContentsMargins(20, 10, 20, 16);
    outer->addWidget(footBox);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &GameEditDialog::accept);
    connect(imgBtn, &QPushButton::clicked, this, &GameEditDialog::chooseImage);
    connect(resetBtn, &QPushButton::clicked, this, &GameEditDialog::resetIcon);
    connect(&iconGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        icon_ = AqIcons::gameGlyphs().value(id, "gamepad");
        image_.clear();
        updatePreview();
    });
    connect(&colorGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        color_ = AqIcons::palette().value(id, "#FF5A66");
        updatePreview();
        syncPickers();
    });
    connect(timeSeg_, &TimeChoice::changed, this, [](int sec) { Prefs::setCaptureSeconds(sec); });
    connect(captureBtn_, &QPushButton::clicked, this, [this]() {
        countdown_->toggle(Prefs::captureSeconds());
    });
    connect(countdown_, &CaptureCountdown::finished, this, &GameEditDialog::captureNow);

    fill(game_);
    if (!templateButtons_.isEmpty()) templateButtons_.first()->setChecked(true);
    // высота по содержимому, но не выше экрана
    const int want = body->sizeHint().height() + footBox->sizeHint().height() + 4;
    int maxH = 900;
    if (QScreen* scr = parent ? parent->screen() : QGuiApplication::primaryScreen())
        maxH = scr->availableGeometry().height() - 60;
    resize(qMax(600, body->sizeHint().width() + 24), qMin(want, maxH));
}

void GameEditDialog::chooseTemplate(const QString& id) {
    for (QToolButton* b : templateButtons_) b->setChecked(b->property("tpl").toString() == id);
    if (id.isEmpty()) {
        GameDef c;
        c.id = customId_;
        c.mode = GameMode::Sequence;
        c.minIntervalMs = 5;
        c.icon = "gamepad";
        c.color = "#FF5A66";
        game_ = c;
        fill(c);
        infoLabel_->setText(T("Нажми кнопку и переключись в игру — AutoQ запомнит процесс и заголовок окна."));
        return;
    }
    bool ok = false;
    const GameDef d = Games::builtinDefault(id, &ok);
    if (!ok) return;
    game_ = d;
    fill(d);
    infoLabel_->setText(T("Правила окна «%1» уже заполнены. Если игра не распознаётся (своя сборка, другой лаунчер) — возьми их из окна игры.").arg(d.name));
}

void GameEditDialog::fill(const GameDef& g) {
    nameEdit_->setText(g.name);
    modeSeg_->setCurrent(static_cast<int>(g.mode));
    procEdit_->setText(Games::joinList(g.processes));
    titleEdit_->setText(Games::joinList(g.titles));
    minIntervalBox_->setValue(qBound(5, g.minIntervalMs, 1000));
    icon_ = AqIcons::isGlyph(g.icon) ? g.icon : QString("gamepad");
    color_ = g.color;
    image_ = g.image;
    updatePreview();
}

void GameEditDialog::syncPickers() {
    const bool light = Theme::t().light;
    const qreal dpr = devicePixelRatioF();
    // значки — в выбранном цвете, выбран текущий (если не своя картинка)
    iconGroup_.setExclusive(false);
    for (QToolButton* b : iconButtons_) {
        const QString g = b->property("glyph").toString();
        b->setIcon(QIcon(AqIcons::gameTile(g, QColor(color_), {}, 32, dpr, false, light)));
        b->setChecked(image_.isEmpty() && g == icon_);
    }
    iconGroup_.setExclusive(true);
    colorGroup_.setExclusive(false);
    const QStringList pal = AqIcons::palette();
    for (int i = 0; i < colorButtons_.size(); ++i)
        colorButtons_[i]->setChecked(pal[i].compare(color_, Qt::CaseInsensitive) == 0);
    colorGroup_.setExclusive(true);
}

void GameEditDialog::updatePreview() {
    preview_->setPixmap(AqIcons::gameTile(icon_, QColor(color_), image_, 76, devicePixelRatioF(),
                                          false, Theme::t().light));
    syncPickers();
}

void GameEditDialog::chooseImage() {
    QStringList pats;
    for (const QByteArray& f : QImageReader::supportedImageFormats()) {
        const QString s = QString::fromLatin1(f).toLower();
        if (s == "png" || s == "jpg" || s == "jpeg" || s == "bmp" || s == "ico" || s == "webp" || s == "gif")
            pats << "*." + s;
    }
    const QString file = QFileDialog::getOpenFileName(this, T("Своя картинка для игры"), QString(),
                                                      T("Картинки (%1)").arg(pats.join(' ')));
    if (file.isEmpty()) return;
    QImageReader reader(file);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::warning(this, windowTitle(), T("Не удалось открыть картинку: ") + reader.errorString());
        return;
    }
    image_ = AqIcons::normalizeImage(img);
    updatePreview();
}

void GameEditDialog::resetIcon() {
    bool ok = false;
    const GameDef d = Games::builtinDefault(game_.id, &ok);
    icon_ = ok ? d.icon : QString("gamepad");
    color_ = ok ? d.color : QString("#FF5A66");
    image_.clear();
    updatePreview();
}

void GameEditDialog::resetDefaults() {
    bool ok = false;
    const GameDef d = Games::builtinDefault(game_.id, &ok);
    if (ok) fill(d);
}

void GameEditDialog::captureNow() {
    ActiveWindowProbe probe;
    const WindowInfo w = probe.query();
    if (!w.valid) {
        infoLabel_->setText(T("✗ Не удалось определить активное окно."));
        return;
    }
    if (w.processKnown && processMatches({ownProcessName()}, w)) {
        infoLabel_->setText(T("✗ Захватилось окно AutoQ — переключись в игру за %1 сек.")
                                .arg(Prefs::captureSeconds()));
        return;
    }
    if (w.processKnown && !w.process.isEmpty()) procEdit_->setText(w.process);
    if (!w.title.isEmpty()) titleEdit_->setText(w.title);
    if (nameEdit_->text().trimmed().isEmpty())
        nameEdit_->setText(!w.title.isEmpty() ? w.title : QFileInfo(w.process).completeBaseName());
    infoLabel_->setText(T("✓ Взято: процесс «%1», заголовок «%2». Заголовок можно сократить до постоянной части.")
                            .arg(w.processKnown ? w.process : T("не определён"), w.title));
}

void GameEditDialog::accept() {
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), T("Укажи название игры."));
        return;
    }
    if (Games::nameTaken(name, game_.id)) {
        QMessageBox::warning(this, windowTitle(), T("Игра с таким названием уже есть."));
        return;
    }
    // процесс и заголовок можно не указывать: при первом «Выбрать окно» AutoQ
    // запомнит окно игры сам (или кликать можно без привязки к окну)
    const QStringList procs = Games::splitList(procEdit_->text());
    const QStringList titles = Games::splitList(titleEdit_->text());
    countdown_->cancel();
    game_.name = name;
    if (modeSeg_->current() >= 0) game_.mode = gameModeFromInt(modeSeg_->current());
    game_.processes = procs;
    game_.titles = titles;
    game_.minIntervalMs = minIntervalBox_->value();
    game_.icon = icon_;
    game_.color = color_;
    game_.image = image_;
    QDialog::accept();
}
