#include "board.h"
#include "i18n.h"
#include "theme.h"
#include "icons.h"
#include "widgets.h"
#include "animations.h"
#include "colorpicker.h"
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QMouseEvent>
#include <QInputDialog>
#include <QScrollArea>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QBoxLayout>
#include <QMenu>
#include <QTimer>
#include <QShortcut>
#include <QCursor>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QActionGroup>

namespace {
const char* kCardMime = "application/x-autoq-card";
const char* kBlockMime = "application/x-autoq-block";
const int kUndoMax = 60;

int gap() { return Theme::sp(14); }

QString builtinName(const QString& id) {
    if (id == "head") return T("Шапка");
    if (id == "actions") return T("Действия");
    if (id == "speed") return T("Скорость");
    if (id == "window") return T("Окно игры");
    if (id == "profile") return T("Профиль и запуск");
    return QString();
}

QString spanName(int span) {
    switch (span) {
        case 1: return T("¼ ширины");
        case 2: return T("½ ширины");
        case 3: return T("¾ ширины");
        case 4: return T("Вся ширина");
        default: return T("Сама");
    }
}

QToolButton* miniButton(const QString& tip) {
    auto* b = new QToolButton();
    b->setObjectName("resetMini");
    b->setFixedSize(24, 24);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    return b;
}

// Поверх блока в режиме редактирования: блок виден, но не нажимается — за него тянут.
// Глаз справа — спрятать блок (вернуть — «Скрытые» в полосе редактирования).
class BlockShield : public QWidget {
public:
    explicit BlockShield(BoardBlock* b) : QWidget(b), block_(b) {
        setCursor(Qt::OpenHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(T("Перетащи блок на другое место — в эту или другую карточку"));
        eye_ = new QToolButton(this);
        eye_->setObjectName("shieldEye");
        eye_->setFixedSize(24, 24);
        eye_->setCursor(Qt::PointingHandCursor);
        eye_->setToolTip(T("Скрыть блок (вернуть — «Скрытые» вверху)"));
        eye_->setAccessibleName(T("Скрыть блок «%1»").arg(b->name()));
        eye_->setIcon(AqIcons::ui("eye-off", Theme::t().onRed, 14, 2.0));
        QObject::connect(eye_, &QToolButton::clicked, b, [b]() { emit b->hideRequested(b); });
    }
    void refreshIcon() { eye_->setIcon(AqIcons::ui("eye-off", Theme::t().onRed, 14, 2.0)); }
protected:
    void resizeEvent(QResizeEvent*) override { eye_->move(width() - eye_->width() - 6, 5); }
    void paintEvent(QPaintEvent*) override {
        const auto& t = Theme::t();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor fill = t.card;
        fill.setAlphaF(underMouse() ? 0.35 : 0.55);
        p.setBrush(fill);
        p.setPen(QPen(underMouse() ? t.red : t.fieldBorder2, 1.2, Qt::DashLine));
        p.drawRoundedRect(r, 10, 10);
        // ярлык с названием блока
        QFont f = font();
        f.setPixelSize(11);
        f.setWeight(QFont::DemiBold);
        const QFontMetricsF fm(f);
        const QString name = fm.elidedText(block_->name(), Qt::ElideRight, qMax(20.0, width() - 80.0));
        const QRectF tag(8, 6, fm.horizontalAdvance(name) + 34, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(t.red);
        p.drawRoundedRect(tag, 11, 11);
        AqIcons::paint(p, "grip", QRectF(tag.left() + 7, tag.top() + 4, 14, 14), t.onRed, 2.0);
        p.setFont(f);
        p.setPen(t.onRed);
        p.drawText(tag.adjusted(25, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, name);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        press_ = e->position().toPoint();
        pressed_ = true;
        setCursor(Qt::ClosedHandCursor);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!pressed_ || (e->position().toPoint() - press_).manhattanLength() < QApplication::startDragDistance()) return;
        pressed_ = false;
        setCursor(Qt::OpenHandCursor);
        emit block_->dragStarted(block_);
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        pressed_ = false;
        setCursor(Qt::OpenHandCursor);
    }
private:
    BoardBlock* block_;
    QToolButton* eye_;
    QPoint press_;
    bool pressed_ = false;
};
}

// Край карточки в режиме редактирования: потянуть — ширина (правый) или высота
// (нижний), двойной щелчок — «как было по умолчанию»
class EdgeGrip : public QWidget {
public:
    using Cb = std::function<void(int phase, const QPoint& global)>;   // 0 нажали, 1 тянут, 2 отпустили, 3 двойной щелчок
    EdgeGrip(bool horizontal, QWidget* parent) : QWidget(parent), horiz_(horizontal) {
        setCursor(horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(horizontal ? T("Потяни — ширина карточки (двойной щелчок — сама)")
                              : T("Потяни — высота карточки (двойной щелчок — по содержимому)"));
        hide();
    }
    Cb cb;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& t = Theme::t();
        p.setPen(Qt::NoPen);
        p.setBrush(underMouse() || down_ ? t.red : t.fieldBorder2);
        const QRectF r = horiz_ ? QRectF(width() / 2.0 - 2, height() / 2.0 - 18, 4, 36)
                                : QRectF(width() / 2.0 - 18, height() / 2.0 - 2, 36, 4);
        p.drawRoundedRect(r, 2, 2);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        down_ = true;
        update();
        if (cb) cb(0, e->globalPosition().toPoint());
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (down_ && cb) cb(1, e->globalPosition().toPoint());
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!down_) return;
        down_ = false;
        update();
        if (cb) cb(2, e->globalPosition().toPoint());
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (cb) cb(3, e->globalPosition().toPoint());
    }
private:
    bool horiz_;
    bool down_ = false;
};

// Блоки в ряд (шапка): слева направо; растягивающиеся забирают свободное место.
// Не влезают — переносятся; перенесённая строка без растягивающихся — по правому краю.
class RowFlow : public QLayout {
public:
    explicit RowFlow(int spacing) : sp_(spacing) { setContentsMargins(0, 0, 0, 0); }
    ~RowFlow() override { while (QLayoutItem* it = takeAt(0)) delete it; }
    void addItem(QLayoutItem* it) override { items_ << it; }
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int i) const override { return items_.value(i); }
    QLayoutItem* takeAt(int i) override { return (i >= 0 && i < items_.size()) ? items_.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return run(QRect(0, 0, w, 0), true); }
    QSize sizeHint() const override {
        int w = 0, h = 0, n = 0;
        for (QLayoutItem* it : items_) {
            if (it->isEmpty()) continue;
            w += it->sizeHint().width() + (n++ ? sp_ : 0);
            h = qMax(h, it->sizeHint().height());
        }
        return QSize(w, h);
    }
    QSize minimumSize() const override {
        int w = 0, h = 0;
        for (QLayoutItem* it : items_) {
            if (it->isEmpty()) continue;
            w = qMax(w, needOf(it, false));
            h = qMax(h, it->minimumSize().height());
        }
        return QSize(w, h);
    }
    void setGeometry(const QRect& r) override {
        QLayout::setGeometry(r);
        run(r, false);
    }
private:
    static BoardBlock* blockOf(QLayoutItem* it) { return qobject_cast<BoardBlock*>(it->widget()); }
    // сколько места нужно: растягивающемуся — его минимум (для переноса — не меньше rowMin)
    static int needOf(QLayoutItem* it, bool forWrap) {
        BoardBlock* b = blockOf(it);
        if (b && b->stretch) return qMax(it->minimumSize().width(), forWrap ? b->rowMin : 0);
        return qMax(it->minimumSize().width(), it->sizeHint().width());
    }
    static int heightOf(QLayoutItem* it, int w) {
        const int h = it->hasHeightForWidth() ? it->heightForWidth(w) : it->sizeHint().height();
        return qMax(h, it->minimumSize().height());
    }
    int run(const QRect& r, bool test) const {
        const int W = qMax(1, r.width());
        QList<QList<QLayoutItem*>> lines;
        QList<QLayoutItem*> cur;
        int x = 0;
        for (QLayoutItem* it : items_) {
            if (it->isEmpty()) continue;
            const int need = qMin(W, needOf(it, true));
            if (!cur.isEmpty() && x + sp_ + need > W) { lines << cur; cur.clear(); x = 0; }
            x += (cur.isEmpty() ? 0 : sp_) + need;
            cur << it;
        }
        if (!cur.isEmpty()) lines << cur;
        int y = r.y();
        for (int li = 0; li < lines.size(); ++li) {
            const QList<QLayoutItem*>& line = lines[li];
            QList<int> ws;
            int used = 0, stretchers = 0;
            for (QLayoutItem* it : line) {
                const int w = qMin(W, needOf(it, false));
                ws << w;
                used += w;
                BoardBlock* b = blockOf(it);
                if (b && b->stretch) ++stretchers;
            }
            used += sp_ * (int(line.size()) - 1);
            const int extra = qMax(0, W - used);
            int left = r.x();
            if (stretchers) {
                int given = 0, k = 0;
                for (int i = 0; i < line.size(); ++i) {
                    BoardBlock* b = blockOf(line[i]);
                    if (!b || !b->stretch) continue;
                    const int add = ++k == stretchers ? extra - given : extra / stretchers;
                    ws[i] += add;
                    given += add;
                }
            } else if (li > 0) {
                left += extra;                      // перенесённое — по правому краю
            }
            int lh = 0;
            for (int i = 0; i < line.size(); ++i) lh = qMax(lh, heightOf(line[i], ws[i]));
            if (!test) {
                int xx = left;
                for (int i = 0; i < line.size(); ++i) {
                    const int h = qMin(lh, heightOf(line[i], ws[i]));
                    line[i]->setGeometry(QRect(xx, y + (lh - h) / 2, ws[i], h));
                    xx += ws[i] + sp_;
                }
            }
            y += lh + (li + 1 < lines.size() ? sp_ : 0);
        }
        return y - r.y();
    }
    QList<QLayoutItem*> items_;
    int sp_;
};

// ===========================================================================
BoardBlock::BoardBlock(const QString& id, const QString& name, QWidget* parent)
    : QWidget(parent), id_(id), name_(name) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);
    top_ = new QWidget();
    topLay_ = new QHBoxLayout(top_);
    topLay_->setContentsMargins(0, 0, 0, 0);
    topLay_->setSpacing(10);
    caption_ = new QLabel(name);
    caption_->setObjectName("small");
    topLay_->addWidget(caption_);
    topLay_->addStretch(1);
    top_->hide();
    lay->addWidget(top_);
    body_ = new QVBoxLayout();
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(10);
    lay->addLayout(body_);
}

void BoardBlock::setDivider(bool on) {
    if (on && !divider_) {
        divider_ = new QFrame();
        divider_->setObjectName("divider");
        static_cast<QVBoxLayout*>(layout())->insertWidget(0, divider_);
    }
    if (divider_) divider_->setVisible(on);
}

void BoardBlock::setHead(QWidget* head) {
    head_ = head;
    placeHead(nullptr, 0);
}

void BoardBlock::placeHead(QHBoxLayout* cardHead, int index) {
    if (!head_) { top_->hide(); return; }
    if (cardHead) {
        cardHead->insertWidget(index, head_);
        top_->hide();
    } else {
        if (head_->parentWidget() != top_) topLay_->addWidget(head_);
        top_->show();
    }
    head_->show();
}

void BoardBlock::setEditing(bool on) {
    // в редактировании — поля вокруг блока и место под ярлык с названием и глаз сверху
    caption_->setVisible(!on);
    layout()->setContentsMargins(on ? 8 : 0, on ? 34 : 0, on ? 8 : 0, on ? 8 : 0);
    if (editing_ != on) {
        editing_ = on;
        updateGeometry();                      // минимальная ширина в редактировании другая
    }
    if (on && !shield_) shield_ = new BlockShield(this);
    if (!shield_) return;
    static_cast<BlockShield*>(shield_)->refreshIcon();
    shield_->setGeometry(rect());
    shield_->setVisible(on);
    if (on) shield_->raise();
}

QSize BoardBlock::minimumSizeHint() const {
    // узкий блок (кнопки в шапке) в редактировании шире, чтобы ярлык с названием и глаз не налезали;
    // широкому — его собственный минимум (иначе в редакторе колонок больше, чем будет на деле)
    QSize s = QWidget::minimumSizeHint();
    if (editing_) s.setWidth(qMax(s.width(), 150));
    return s;
}

void BoardBlock::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (shield_) shield_->setGeometry(rect());
}

// ===========================================================================
BoardCard::BoardCard(const QString& id, QWidget* parent) : QFrame(parent), id_(id) {
    setObjectName("card");
    setContextMenuPolicy(Qt::CustomContextMenu);
    spec_ = CardLayout::isBuiltin(id) ? CardLayout::defaultCard(id) : CardLayout::Card{};
    spec_.id = id;
    mainLay_ = new QVBoxLayout(this);
    mainLay_->setProperty("aqManual", true);       // поля ставит applySpec (с учётом плотности)
    mainLay_->setContentsMargins(16, 14, 16, 16);
    mainLay_->setSpacing(10);
    headW_ = new QWidget();
    headW_->installEventFilter(this);
    headLay_ = new QHBoxLayout(headW_);
    headLay_->setContentsMargins(0, 0, 0, 0);
    headLay_->setSpacing(10);
    grip_ = new QToolButton();
    grip_->setObjectName("resetMini");
    grip_->setFixedSize(18, 24);
    grip_->setAttribute(Qt::WA_TransparentForMouseEvents);
    grip_->hide();
    num_ = new QLabel();
    num_->setObjectName("num");
    title_ = new QLabel(CardLayout::isBuiltin(id) ? builtinName(id) : QString());
    title_->setObjectName("cardTitle");
    title_->setTextFormat(Qt::PlainText);       // название своей карточки — как написано (и из файла раскладки)
    settings_ = miniButton(T("Вид карточки: ширина, высота, рамка, цвет, заголовок"));
    settings_->hide();
    rename_ = miniButton(T("Переименовать карточку"));
    rename_->hide();
    delete_ = miniButton(T("Удалить карточку (блоки вернутся на свои места)"));
    delete_->hide();
    collapse_ = miniButton(T("Свернуть карточку"));
    headLay_->addWidget(grip_);
    headLay_->addWidget(num_);
    headLay_->addWidget(title_, 0);
    headLay_->addStretch(1);
    headLay_->addWidget(settings_);
    headLay_->addWidget(rename_);
    headLay_->addWidget(delete_);
    headLay_->addWidget(collapse_);
    mainLay_->addWidget(headW_);
    blocksLay_ = new QVBoxLayout();
    blocksLay_->setContentsMargins(0, 0, 0, 0);
    blocksLay_->setSpacing(10);
    mainLay_->addLayout(blocksLay_, 1);
    rowLay_ = new RowFlow(14);
    mainLay_->addLayout(rowLay_);
    empty_ = new QLabel(T("Перетащи сюда блоки из других карточек"));
    empty_->setObjectName("cardEmpty");
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setMinimumHeight(84);
    empty_->hide();
    gripR_ = new EdgeGrip(true, this);
    gripB_ = new EdgeGrip(false, this);
    for (int edge : {0, 1}) {
        EdgeGrip* g = edge == 0 ? gripR_ : gripB_;
        g->cb = [this, edge](int phase, const QPoint& global) {
            switch (phase) {
                case 0: emit resizeStarted(this, edge); break;
                case 1: emit resizeMoved(global); break;
                case 2: emit resizeFinished(); break;
                default: emit resizeReset(this, edge); break;
            }
        };
    }
    connect(collapse_, &QToolButton::clicked, this, &BoardCard::collapseClicked);
    connect(settings_, &QToolButton::clicked, this, [this]() { emit settingsClicked(settings_); });
    connect(rename_, &QToolButton::clicked, this, &BoardCard::renameClicked);
    connect(delete_, &QToolButton::clicked, this, &BoardCard::deleteClicked);
    refreshIcons();
}

void BoardCard::refreshIcons() {
    const auto& t = Theme::t();
    grip_->setIcon(AqIcons::ui("grip", t.muted, 14));
    settings_->setIcon(AqIcons::ui("settings", t.muted, 13, 2.0));
    rename_->setIcon(AqIcons::ui("pencil", t.muted, 13, 2.0));
    delete_->setIcon(AqIcons::ui("trash", t.muted, 13, 2.0));
    collapse_->setIcon(AqIcons::ui("chevron-up", t.muted, 13, 2.2));
}

bool BoardCard::wide() const {
    for (BoardBlock* b : blocks_)
        if (b->wide) return true;
    return false;
}

QWidget* BoardCard::emptyHint() const { return empty_; }

void BoardCard::applySpec(const CardLayout::Card& spec, bool editing, int number) {
    spec_ = spec;
    editing_ = editing;
    const bool custom = CardLayout::isCustom(id_);
    if (custom) title_->setText(spec.title);
    num_->setText(number > 0 ? QString("%1").arg(number, 2, 10, QChar('0')) : QString());
    num_->setVisible(number > 0);
    // без заголовка — только вне редактирования: за заголовок карточку и тянут
    headW_->setVisible(editing || !spec.hideTitle);
    grip_->setVisible(editing);
    settings_->setVisible(editing);
    rename_->setVisible(editing && custom);
    delete_->setVisible(editing && custom);
    headW_->setCursor(editing ? Qt::OpenHandCursor : Qt::ArrowCursor);
    headW_->setToolTip(editing ? T("Перетащи карточку за заголовок на другое место") : QString());
    // прозрачная — без полей (как шапка), но в редактировании поля нужны под рамку
    const bool bare = spec.style == CardLayout::Clear && !editing;
    const int mh = bare ? 0 : Theme::sp(16), mt = bare ? 0 : Theme::sp(14);
    mainLay_->setContentsMargins(mh, mt, mh, mh);
    setProperty("cstyle", spec.style);
    setProperty("editing", editing);
    // свой цвет — рамка (и лёгкий фон у акцентной и «без рамки»)
    QString sheet;
    if (!spec.color.isEmpty() && spec.style != CardLayout::Clear) {
        QColor c(spec.color);
        QColor tint = c;
        tint.setAlphaF(spec.style == CardLayout::Accent ? 0.14 : 0.08);
        const QString tintS = QString("rgba(%1,%2,%3,%4)").arg(tint.red()).arg(tint.green()).arg(tint.blue()).arg(tint.alpha());
        if (spec.style == CardLayout::Normal) sheet = QString("QFrame#card { border-color: %1; }").arg(c.name());
        else if (spec.style == CardLayout::Borderless) sheet = QString("QFrame#card { background: %1; }").arg(tintS);
        else sheet = QString("QFrame#card { border-color: %1; background: %2; }").arg(c.name(), tintS);
    }
    if (styleSheet() != sheet) setStyleSheet(sheet);
    Theme::repolish(this);
    gripR_->setVisible(editing);
    gripB_->setVisible(editing);
    placeGrips();
}

void BoardCard::setBlocks(const QList<BoardBlock*>& blocks, bool editing) {
    // прежние блоки вынимаем, не удаляя: они могут переехать в другую карточку
    while (QLayoutItem* it = blocksLay_->takeAt(0)) delete it;
    while (QLayoutItem* it = rowLay_->takeAt(0)) delete it;
    blocks_ = blocks;
    editing_ = editing;
    const bool row = spec_.row;
    // кнопки первого блока — в заголовок карточки (не в редактировании: там они
    // остаются при блоке, чтобы было видно, что едут вместе с ним; не в ряду и не
    // без заголовка — там они над своим блоком)
    const bool headFree = !editing && !row && !spec_.hideTitle;
    const int headAt = headLay_->indexOf(settings_);
    for (int i = 0; i < blocks.size(); ++i)
        blocks[i]->placeHead(i == 0 && headFree ? headLay_ : nullptr, headAt);
    if (row) {
        for (int i = 0; i < blocks.size(); ++i) {
            rowLay_->addWidget(blocks[i]);
            blocks[i]->showDivider(false);
            blocks[i]->show();
            blocks[i]->setEditing(editing);
        }
    } else {
        // блоки «внизу», что идут последними, прижимаем к низу карточки
        int firstBottom = blocks.size();
        while (firstBottom > 0 && blocks[firstBottom - 1]->bottom) --firstBottom;
        for (int i = 0; i < blocks.size(); ++i) {
            if (i == firstBottom) blocksLay_->addStretch(1);
            blocksLay_->addWidget(blocks[i]);
            blocks[i]->showDivider(i > 0 && !editing);
            blocks[i]->show();
            blocks[i]->setEditing(editing);
        }
        if (firstBottom == blocks.size()) blocksLay_->addStretch(1);
    }
    if (blocks.isEmpty()) {
        blocksLay_->insertWidget(0, empty_);
        empty_->setVisible(editing);
    } else {
        empty_->setParent(this);
        empty_->hide();
    }
    gripR_->raise();
    gripB_->raise();
}

void BoardCard::placeGrips() {
    gripR_->setGeometry(width() - 9, 16, 9, qMax(10, height() - 32));
    gripB_->setGeometry(16, height() - 9, qMax(10, width() - 32), 9);
    gripR_->raise();
    gripB_->raise();
}

void BoardCard::resizeEvent(QResizeEvent* e) {
    QFrame::resizeEvent(e);
    placeGrips();
}

bool BoardCard::eventFilter(QObject* o, QEvent* e) {
    if (o != headW_ || !editing_) return QFrame::eventFilter(o, e);
    if (e->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::LeftButton) {
            pressPos_ = me->position().toPoint();
            pressed_ = true;
            headW_->setCursor(Qt::ClosedHandCursor);
            return true;
        }
    } else if (e->type() == QEvent::MouseMove && pressed_) {
        auto* me = static_cast<QMouseEvent*>(e);
        if ((me->position().toPoint() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
            pressed_ = false;
            headW_->setCursor(Qt::OpenHandCursor);
            emit dragStarted(this);
        }
        return true;
    } else if (e->type() == QEvent::MouseButtonRelease) {
        pressed_ = false;
        headW_->setCursor(Qt::OpenHandCursor);
    }
    return QFrame::eventFilter(o, e);
}

// ===========================================================================
Board::Board(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    strip_ = new QWidget(this);
    strip_->setObjectName("cardStrip");
    stripLay_ = new FlowLayout(strip_, 8, 8);
    stripLay_->setContentsMargins(0, 0, 0, 0);
    strip_->hide();
    marker_ = new QWidget(this);
    marker_->setObjectName("dropMarker");
    marker_->setAttribute(Qt::WA_TransparentForMouseEvents);
    marker_->setAttribute(Qt::WA_StyledBackground);
    marker_->hide();
    sizeTag_ = new QLabel(this);
    sizeTag_->setObjectName("sizeTag");
    sizeTag_->setAttribute(Qt::WA_TransparentForMouseEvents);
    sizeTag_->hide();
    for (const QString& id : CardLayout::builtinCards()) card(id);
    buildBar();
    layout_ = CardLayout::defaults();
    // отменить / вернуть — только в режиме редактирования и не пока тянут край.
    // Каждое сочетание — один раз: два QShortcut на одно сочетание «неоднозначны» и не срабатывают
    QList<QKeySequence> undoKeys = QKeySequence::keyBindings(QKeySequence::Undo);
    QList<QKeySequence> redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    redoKeys << QKeySequence(Qt::CTRL | Qt::Key_Y) << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z);
    QList<QKeySequence> used;
    for (int isUndo = 1; isUndo >= 0; --isUndo) {
        for (const QKeySequence& k : isUndo ? undoKeys : redoKeys) {
            if (k.isEmpty() || used.contains(k)) continue;
            used << k;
            auto* sc = new QShortcut(k, this);
            sc->setContext(Qt::WindowShortcut);
            connect(sc, &QShortcut::activated, this, [this, isUndo]() {
                if (!edit_ || !isVisible() || resizeEdge_ >= 0) return;
                isUndo ? undo() : redo();
            });
        }
    }
}

BoardBlock* Board::addBlock(const QString& id, const QString& name) {
    auto* b = new BoardBlock(id, name, this);
    blocks_[id] = b;
    b->hide();
    connect(b, &BoardBlock::dragStarted, this, [this](BoardBlock* blk) { startDrag(blk, kBlockMime, blk->id()); });
    connect(b, &BoardBlock::hideRequested, this, [this](BoardBlock* blk) {
        const QString id = blk->id();
        // кнопка — внутри блока: раскладку меняем после обработчика
        QTimer::singleShot(0, this, [this, id]() {
            change([id](CardLayout::Layout& l) { CardLayout::hideBlock(l, id); });
        });
    });
    return b;
}

BoardCard* Board::card(const QString& id) {
    if (BoardCard* c = cards_.value(id)) return c;
    auto* c = new BoardCard(id, this);
    cards_[id] = c;
    connect(c, &BoardCard::collapseClicked, this, [this, id]() {
        change([id](CardLayout::Layout& l) { CardLayout::setCollapsed(l, id, true); });
    });
    connect(c, &BoardCard::settingsClicked, this, [this, id](QToolButton* from) {
        showCardMenu(id, from, from->mapToGlobal(QPoint(0, from->height())));
    });
    connect(c, &BoardCard::renameClicked, this, [this, id]() { askRename(id); });
    connect(c, &BoardCard::deleteClicked, this, [this, id]() {
        QTimer::singleShot(0, this, [this, id]() {
            change([id](CardLayout::Layout& l) { CardLayout::removeCustom(l, id); });
        });
    });
    connect(c, &BoardCard::dragStarted, this, [this](BoardCard* cd) { startDrag(cd, kCardMime, cd->id()); });
    connect(c, &BoardCard::resizeStarted, this, &Board::beginResize);
    connect(c, &BoardCard::resizeMoved, this, &Board::moveResize);
    connect(c, &BoardCard::resizeFinished, this, &Board::endResize);
    connect(c, &BoardCard::resizeReset, this, [this](BoardCard* cd, int edge) {
        const QString cid = cd->id();
        QTimer::singleShot(0, this, [this, cid, edge]() {
            change([cid, edge](CardLayout::Layout& l) {
                const int i = CardLayout::indexOf(l, cid);
                if (i < 0) return;
                if (edge == 0) l[i].span = 0;
                else l[i].height = 0;
            }, false);
        });
    });
    connect(c, &QWidget::customContextMenuRequested, this, [this, c, id](const QPoint& pos) {
        if (edit_) { showCardMenu(id, nullptr, c->mapToGlobal(pos)); return; }
        QMenu m(c);
        m.addAction(T("Свернуть"), this, [this, id]() {
            change([id](CardLayout::Layout& l) { CardLayout::setCollapsed(l, id, true); });
        });
        m.addAction(T("Настроить интерфейс…"), this, [this]() { setEditMode(true); });
        m.exec(c->mapToGlobal(pos));
    });
    Theme::applyDensity(c);
    return c;
}

QLabel* Board::titleLabel(const QString& cardId) const {
    BoardCard* c = cards_.value(cardId);
    return c ? c->title() : nullptr;
}

void Board::setCardLayout(const CardLayout::Layout& l) {
    if (resizeEdge_ >= 0) cancelResize();
    layout_ = CardLayout::normalize(l);
    undo_.clear();
    redo_.clear();
    rebuild();
}

bool Board::blockVisible(const QString& block) const {
    const QString c = CardLayout::cardOf(layout_, block);
    if (c.isEmpty() || c == CardLayout::kHidden) return false;
    const int i = CardLayout::indexOf(layout_, c);
    return i >= 0 && !layout_[i].collapsed;
}

void Board::setSidebarMode(int mode, bool record) {
    mode = qBound(0, mode, 3);
    if (mode == sidebar_) return;
    if (record) {                                  // как правка в редакторе: Ctrl+Z вернёт
        remember();
        applyState(State{layout_, mode}, true);
        return;
    }
    sidebar_ = mode;
    emit sidebarModeChanged(mode);
}

void Board::remember() {
    undo_ << State{layout_, sidebar_};
    while (undo_.size() > kUndoMax) undo_.removeFirst();
    redo_.clear();
}

void Board::applyState(const State& s, bool animate) {
    if (resizeEdge_ >= 0) cancelResize();          // край тянули — раскладка уже другая
    const bool sideChanged = s.sidebar != sidebar_;
    // панель меняется в том же снимке, что и карточки (переход — по всему окну)
    auto apply = [this, s, sideChanged]() {
        layout_ = s.layout;
        sidebar_ = s.sidebar;
        if (sideChanged) emit sidebarModeChanged(sidebar_);
        rebuild();
    };
    QWidget* area = sideChanged && sideArea_ ? sideArea_.data() : animArea_.data();
    if (animate && area && Anim::style() != "none") Anim::transition(area, apply, {}, "fade");
    else apply();
    updateBar();
    emit edited();
}

void Board::change(const std::function<void(CardLayout::Layout&)>& f, bool animate) {
    CardLayout::Layout l = layout_;
    f(l);
    l = CardLayout::normalize(l);
    if (l == layout_) return;
    remember();
    applyState(State{l, sidebar_}, animate);
}

bool Board::undo() {
    if (undo_.isEmpty()) return false;
    redo_ << State{layout_, sidebar_};
    while (redo_.size() > kUndoMax) redo_.removeFirst();
    applyState(undo_.takeLast(), true);
    return true;
}

bool Board::redo() {
    if (redo_.isEmpty()) return false;
    undo_ << State{layout_, sidebar_};
    while (undo_.size() > kUndoMax) undo_.removeFirst();
    applyState(redo_.takeLast(), true);
    return true;
}

void Board::rebuild() {
    // свои карточки, которых больше нет, — удалить
    for (auto it = cards_.begin(); it != cards_.end();) {
        if (CardLayout::isCustom(it.key()) && CardLayout::indexOf(layout_, it.key()) < 0) {
            BoardCard* c = it.value();
            c->setBlocks({}, false);
            c->hide();
            c->deleteLater();
            it = cards_.erase(it);
        } else {
            ++it;
        }
    }
    // свёрнутые — маленькие прямоугольники сверху
    stripLay_->clear();
    int n = 0;
    for (const CardLayout::Card& spec : layout_) {
        if (spec.id == CardLayout::kHidden) continue;
        BoardCard* c = card(spec.id);
        QList<BoardBlock*> bl;
        for (const QString& b : spec.blocks)
            if (BoardBlock* blk = blocks_.value(b)) bl << blk;
        const bool shown = edit_ || !bl.isEmpty();
        const int number = shown && !spec.hideNumber ? ++n : 0;
        c->applySpec(spec, edit_, number);
        c->setBlocks(bl, edit_);
        if (!shown || spec.collapsed) {
            c->hide();
            if (shown) {
                auto* tile = new QToolButton();
                tile->setObjectName("cardTile");
                tile->setCursor(Qt::PointingHandCursor);
                tile->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                tile->setIcon(AqIcons::ui("chevron", Theme::t().muted, 13, 2.2));
                tile->setText(number > 0 ? QString("%1  %2").arg(number, 2, 10, QChar('0')).arg(c->title()->text())
                                         : c->title()->text());
                tile->setToolTip(T("Развернуть карточку"));
                tile->setAccessibleName(T("Развернуть: %1").arg(c->title()->text()));
                const QString id = spec.id;
                connect(tile, &QToolButton::clicked, this, [this, id]() {
                    QTimer::singleShot(0, this, [this, id]() {
                        change([id](CardLayout::Layout& l) { CardLayout::setCollapsed(l, id, false); });
                    });
                });
                stripLay_->addWidget(tile);
                tile->show();                      // сразу (addWidget показал бы позже — переход снял бы пустую полосу)
            }
        } else {
            c->show();
        }
    }
    // скрытые блоки — ни в одной карточке (их кнопки из заголовка — обратно к ним)
    for (const QString& b : CardLayout::hiddenBlocks(layout_)) {
        BoardBlock* blk = blocks_.value(b);
        if (!blk) continue;
        blk->placeHead(nullptr, 0);
        blk->setEditing(false);
        if (blk->parentWidget() != this) blk->setParent(this);
        blk->hide();
    }
    strip_->setVisible(stripLay_->count() > 0);
    updateBar();
    doLayout();
}

QList<BoardCard*> Board::gridCards() const {
    QList<BoardCard*> out;
    for (const CardLayout::Card& spec : layout_)
        if (BoardCard* c = cards_.value(spec.id); c && !c->isHidden()) out << c;
    return out;
}

QList<QWidget*> Board::visibleCards() const {
    QList<QWidget*> out;
    for (BoardCard* c : gridCards()) out << c;
    return out;
}

CardLayout::Card Board::specOf(const QString& id) const {
    const int i = CardLayout::indexOf(layout_, id);
    CardLayout::Card c = i >= 0 ? layout_[i] : CardLayout::Card{};
    if (id == resizeId_) {
        if (previewSpan_ >= 0) c.span = previewSpan_;
        if (previewHeight_ >= 0) c.height = previewHeight_;
    }
    return c;
}

int Board::columnsFor(int w) const {
    int need = 250, narrow = 0;
    for (BoardCard* c : gridCards()) {
        if (c->wide() || specOf(c->id()).span > 0) continue;
        ++narrow;
        need = qMax(need, c->minimumSizeHint().width());
    }
    for (int cols = qMin(4, narrow); cols > 1; --cols)
        if (w >= cols * need + (cols - 1) * gap()) return cols;
    return 1;
}

QSize Board::minimumSizeHint() const {
    int w = 0;
    for (BoardCard* c : gridCards()) w = qMax(w, c->minimumSizeHint().width());
    return QSize(w, minimumHeight());
}

QSize Board::sizeHint() const { return QSize(900, minimumHeight()); }

void Board::doLayout() {
    if (laying_) return;
    laying_ = true;
    const int W = qMax(1, width()), g = gap();
    cols_ = columnsFor(W);
    // сетка в 12 долей: «сама» — как раньше (широкая — на всю строку, узкие делят
    // строку поровну), своя ширина — четвертями; не влезает содержимое — шире
    auto unitW = [W, g](int u) { return ((W + g) * u) / 12 - g; };
    struct Cell { BoardCard* c; int units; bool fixed; int height; };
    QList<QList<Cell>> rows;
    QList<Cell> cur;
    int sum = 0;
    for (BoardCard* c : gridCards()) {
        const CardLayout::Card sp = specOf(c->id());
        const bool fixed = sp.span > 0;
        int u = fixed ? sp.span * 3 : (c->wide() || cols_ == 1) ? 12 : 12 / cols_;
        const int minW = c->minimumSizeHint().width();
        while (u < 12 && unitW(u) < minW) u = qMin(12, u + 3);
        if (!cur.isEmpty() && sum + u > 12) { rows << cur; cur.clear(); sum = 0; }
        cur << Cell{c, u, fixed, sp.height};
        sum += u;
        if (sum >= 12) { rows << cur; cur.clear(); sum = 0; }
    }
    if (!cur.isEmpty()) rows << cur;

    int y = 0;
    bool stripPlaced = strip_->isHidden();
    auto placeStrip = [&]() {
        const int h = stripLay_->heightForWidth(W);
        strip_->setGeometry(0, y, W, h);
        y += h + g;
        stripPlaced = true;
    };
    for (QList<Cell>& row : rows) {
        // свёрнутые — под шапкой (ряды из карточек «в ряд» в начале), иначе — сверху
        if (!stripPlaced && !(row.size() == 1 && row[0].c->rowMode())) placeStrip();
        int total = 0, autos = 0;
        for (const Cell& cl : row) { total += cl.units; autos += !cl.fixed; }
        // неполная строка: остаток — карточкам «сама» поровну
        if (autos > 0 && total < 12) {
            int left = 12 - total, k = 0;
            for (Cell& cl : row) {
                if (cl.fixed) continue;
                const int add = ++k == autos ? left : (12 - total) / autos;
                cl.units += add;
                left -= add;
            }
            total = 12;
        }
        QList<int> ws;
        int x = 0, rowH = 0;
        for (int i = 0; i < row.size(); ++i) {
            const bool last = i == row.size() - 1;
            const int w = (last && total == 12) ? W - x : qMax(40, unitW(row[i].units));
            ws << w;
            x += w + g;
            BoardCard* c = row[i].c;
            const int h = c->hasHeightForWidth() ? c->heightForWidth(w) : c->sizeHint().height();
            rowH = qMax(rowH, qMax(qMax(h, c->minimumSizeHint().height()), row[i].height));
        }
        x = 0;
        for (int i = 0; i < row.size(); ++i) {
            row[i].c->setGeometry(x, y, ws[i], rowH);
            x += ws[i] + g;
        }
        y += rowH + g;
    }
    if (!stripPlaced) placeStrip();
    const int total = qMax(0, y - g);
    if (minimumHeight() != total) {
        setMinimumHeight(total);
        updateGeometry();
    }
    marker_->raise();
    sizeTag_->raise();
    laying_ = false;
}

bool Board::event(QEvent* e) {
    if (e->type() == QEvent::LayoutRequest) doLayout();
    return QWidget::event(e);
}

void Board::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    doLayout();
}

void Board::setEditMode(bool on, bool animate) {
    if (on == edit_) return;
    if (resizeEdge_ >= 0) cancelResize();
    // полоса редактирования (её обёртка — у главного окна) — в том же снимке, что и карточки
    auto apply = [this, on]() {
        edit_ = on;
        bar_->setVisible(on);
        emit editModeChanged(on);
        rebuild();
    };
    // поле ввода в карточке забрало бы Ctrl+Z себе — в редакторе фокус не в блоках
    if (on)
        if (QWidget* f = QApplication::focusWidget(); f && isAncestorOf(f)) f->clearFocus();
    // история отмен — та же, пока открыта эта игра (её сбрасывает только смена игры)
    if (animate && animArea_ && Anim::style() != "none") Anim::transition(animArea_, apply, {}, "fade");
    else apply();
}

void Board::refreshIcons() {
    for (BoardCard* c : cards_) c->refreshIcons();
    if (bar_) {
        const auto& t = Theme::t();
        if (auto* ic = bar_->findChild<QLabel*>("editBarIcon"))
            ic->setPixmap(AqIcons::uiPixmap("layout", t.redHi, 18, devicePixelRatioF()));
        if (barUndo_) barUndo_->setIcon(AqIcons::ui("undo", t.text, 15, 2.0));
        if (barRedo_) barRedo_->setIcon(AqIcons::ui("redo", t.text, 15, 2.0));
        if (barHidden_) barHidden_->setIcon(AqIcons::ui("eye", t.text, 15, 2.0));
    }
    rebuild();
}

// ---------------------------------------------------------------------------
// Полоса режима редактирования
// ---------------------------------------------------------------------------
void Board::buildBar() {
    bar_ = new QFrame();
    bar_->setObjectName("editBar");
    auto* v = new QVBoxLayout(bar_);
    v->setContentsMargins(14, 10, 10, 10);
    v->setSpacing(8);
    auto* h = new QHBoxLayout();
    h->setSpacing(10);
    auto* icon = new QLabel();
    icon->setObjectName("editBarIcon");
    icon->setPixmap(AqIcons::uiPixmap("layout", Theme::t().redHi, 18, devicePixelRatioF()));
    h->addWidget(icon, 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout();
    texts->setSpacing(2);
    auto* title = new QLabel(T("Редактор интерфейса"));
    title->setObjectName("cardTitle");
    auto* hint = new QLabel(T("Карточки тащи за заголовок, блоки — за любое место, края карточек — чтобы поменять размер. "
                              "Шестерёнка на карточке — её вид, глаз на блоке — спрятать. У каждой игры своя раскладка."));
    hint->setObjectName("small");
    hint->setWordWrap(true);
    texts->addWidget(title);
    texts->addWidget(hint);
    h->addLayout(texts, 1);
    auto* done = new QPushButton(T("Готово"));
    done->setObjectName("accent");
    done->setCursor(Qt::PointingHandCursor);
    h->addWidget(done, 0, Qt::AlignTop);
    v->addLayout(h);

    auto* toolsW = new QWidget();
    auto* tools = new FlowLayout(toolsW, 8, 8);
    tools->setContentsMargins(0, 0, 0, 0);
    auto iconBtn = [](const QString& tip) {
        auto* b = new QToolButton();
        b->setObjectName("icon");
        b->setFixedSize(34, 34);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        return b;
    };
    barUndo_ = iconBtn(T("Отменить (Ctrl+Z)"));
    barRedo_ = iconBtn(T("Вернуть (Ctrl+Y)"));
    auto* templates = new QPushButton(T("Шаблоны"));
    templates->setToolTip(T("Готовые раскладки: классика, компакт, монитор, одна колонка"));
    templatesMenu_ = new QMenu(templates);
    templates->setMenu(templatesMenu_);
    connect(templatesMenu_, &QMenu::aboutToShow, this, &Board::fillTemplatesMenu);
    barHidden_ = new QPushButton(T("Скрытые"));
    barHidden_->setToolTip(T("Спрятанные блоки — вернуть на место"));
    hiddenMenu_ = new QMenu(barHidden_);
    barHidden_->setMenu(hiddenMenu_);
    connect(hiddenMenu_, &QMenu::aboutToShow, this, &Board::fillHiddenMenu);
    auto* add = new QPushButton(T("Новая карточка"));
    add->setIcon(AqIcons::ui("plus", Theme::t().text, 14));
    add->setToolTip(T("Пустая карточка со своим названием — перетащи в неё нужные блоки"));
    auto* more = new QPushButton(T("Ещё"));
    more->setToolTip(T("Боковая панель, раскладка в файл, для всех игр, сброс"));
    auto* moreMenu = new QMenu(more);
    more->setMenu(moreMenu);
    {
        QMenu* side = moreMenu->addMenu(T("Боковая панель с играми"));
        auto* grp = new QActionGroup(side);
        const QStringList names{T("Слева"), T("Справа"), T("Узкая — только значки"), T("Скрыта")};
        for (int i = 0; i < names.size(); ++i) {
            QAction* a = side->addAction(names[i]);
            a->setCheckable(true);
            grp->addAction(a);
            connect(a, &QAction::triggered, this, [this, i]() {
                if (i == sidebar_) return;
                remember();
                applyState(State{layout_, i}, false);
            });
        }
        connect(side, &QMenu::aboutToShow, this, [side, this]() {
            const QList<QAction*> acts = side->actions();
            for (int i = 0; i < acts.size(); ++i) acts[i]->setChecked(i == sidebar_);
        });
        moreMenu->addSeparator();
        moreMenu->addAction(T("Сохранить раскладку в файл…"), this, &Board::exportLayout);
        moreMenu->addAction(T("Загрузить раскладку из файла…"), this, &Board::importLayout);
        moreMenu->addSeparator();
        moreMenu->addAction(T("Такая раскладка — у всех игр"), this, &Board::applyToAllRequested);
        barReset_ = nullptr;
        moreMenu->addAction(T("Сбросить — стандартная раскладка"), this, [this]() {
            change([](CardLayout::Layout& l) { l = CardLayout::defaults(); });
        });
    }
    for (QPushButton* b : {templates, barHidden_, more}) b->setProperty("menuBtn", true);
    for (QWidget* b : std::initializer_list<QWidget*>{barUndo_, barRedo_, templates, barHidden_, add, more}) {
        b->setCursor(Qt::PointingHandCursor);
        tools->addWidget(b);
    }
    v->addWidget(toolsW);
    connect(barUndo_, &QToolButton::clicked, this, [this]() { undo(); });
    connect(barRedo_, &QToolButton::clicked, this, [this]() { redo(); });
    connect(add, &QPushButton::clicked, this, &Board::askNewCard);
    connect(done, &QPushButton::clicked, this, [this]() { setEditMode(false); });
    bar_->hide();
    refreshIcons();
}

void Board::updateBar() {
    if (!bar_) return;
    if (barUndo_) barUndo_->setEnabled(!undo_.isEmpty());
    if (barRedo_) barRedo_->setEnabled(!redo_.isEmpty());
    if (barHidden_) {
        const int n = CardLayout::hiddenBlocks(layout_).size();
        barHidden_->setText(n ? T("Скрытые (%1)").arg(n) : T("Скрытые"));
    }
}

void Board::fillHiddenMenu() {
    hiddenMenu_->clear();
    const QStringList hidden = CardLayout::hiddenBlocks(layout_);
    if (hidden.isEmpty()) {
        hiddenMenu_->addAction(T("Скрытых блоков нет — спрятать: глаз на блоке"))->setEnabled(false);
        return;
    }
    for (const QString& b : hidden) {
        const QString name = blocks_.value(b) ? blocks_[b]->name() : b;
        hiddenMenu_->addAction(AqIcons::ui("eye", Theme::t().text, 14, 2.0), T("Вернуть «%1»").arg(name), this, [this, b]() {
            QTimer::singleShot(0, this, [this, b]() {
                change([b](CardLayout::Layout& l) { CardLayout::showBlock(l, b); });
            });
        });
    }
    if (hidden.size() > 1) {
        hiddenMenu_->addSeparator();
        hiddenMenu_->addAction(T("Вернуть все"), this, [this, hidden]() {
            QTimer::singleShot(0, this, [this, hidden]() {
                change([hidden](CardLayout::Layout& l) { for (const QString& b : hidden) CardLayout::showBlock(l, b); });
            });
        });
    }
}

void Board::fillTemplatesMenu() {
    templatesMenu_->clear();
    const QStringList tips{T("Как сразу после установки"),
                           T("Только главное: скорость и окно рядом, узкая панель игр"),
                           T("Большой монитор запуска под шапкой: время, круги, текущий шаг"),
                           T("Каждая карточка — на всю ширину, одна под другой")};
    for (int i = 0; i < CardLayout::kTemplates; ++i) {
        QAction* a = templatesMenu_->addAction(CardLayout::templateName(i) + " — " + tips.value(i));
        connect(a, &QAction::triggered, this, [this, i]() {
            QTimer::singleShot(0, this, [this, i]() {
                int side = 0;
                const CardLayout::Layout l = CardLayout::templateLayout(i, &side);
                if (l == layout_ && side == sidebar_) return;
                remember();
                applyState(State{l, side}, true);
                emit message(T("Шаблон «%1». Не то — Ctrl+Z.").arg(CardLayout::templateName(i)));
            });
        });
    }
}

void Board::exportLayout() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString file = QFileDialog::getSaveFileName(window(), T("Сохранить раскладку"), dir + "/AutoQ-layout.json",
                                                      T("Раскладка AutoQ (*.json)"));
    if (file.isEmpty()) return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        f.write(CardLayout::exportJson(layout_, sidebar_).toUtf8()) < 0) {
        emit message(T("Не получилось сохранить файл: %1").arg(f.errorString()));
        return;
    }
    emit message(T("Раскладка сохранена: %1").arg(QFileInfo(file).fileName()));
}

void Board::importLayout() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString file = QFileDialog::getOpenFileName(window(), T("Загрузить раскладку"), dir, T("Раскладка AutoQ (*.json)"));
    if (file.isEmpty()) return;
    QFile f(file);
    CardLayout::Layout l;
    int side = sidebar_;
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024 ||
        !CardLayout::importJson(QString::fromUtf8(f.readAll()), &l, &side)) {
        emit message(T("Это не файл раскладки AutoQ."));
        return;
    }
    if (l == layout_ && side == sidebar_) {
        emit message(T("Раскладка загружена — такая уже стоит."));
        return;
    }
    remember();
    applyState(State{l, side}, true);
    emit message(T("Раскладка загружена. Не то — Ctrl+Z."));
}

void Board::showCardMenu(const QString& cardId, QToolButton* from, const QPoint& global) {
    const int i = CardLayout::indexOf(layout_, cardId);
    if (i < 0 || cardId == CardLayout::kHidden) return;
    const CardLayout::Card spec = layout_[i];
    QMenu m(this);
    auto set = [this, cardId](const std::function<void(CardLayout::Card&)>& f, bool animate = false) {
        QTimer::singleShot(0, this, [this, cardId, f, animate]() {
            change([cardId, f](CardLayout::Layout& l) {
                const int k = CardLayout::indexOf(l, cardId);
                if (k >= 0) f(l[k]);
            }, animate);
        });
    };
    QMenu* width = m.addMenu(T("Ширина"));
    for (int s = 0; s <= 4; ++s) {
        QAction* a = width->addAction(spanName(s));
        a->setCheckable(true);
        a->setChecked(spec.span == s);
        connect(a, &QAction::triggered, this, [set, s]() { set([s](CardLayout::Card& c) { c.span = s; }); });
    }
    // своя высота — вернуть «по содержимому» (неактивные пункты меню в теме — красные, их не показываем)
    if (spec.height > 0) {
        QAction* autoH = m.addAction(T("Высота: по содержимому (сейчас %1)").arg(spec.height));
        connect(autoH, &QAction::triggered, this, [set]() { set([](CardLayout::Card& c) { c.height = 0; }); });
    }
    QMenu* look = m.addMenu(T("Вид"));
    const QStringList styles{T("Обычная"), T("Без рамки"), T("Акцентная"), T("Прозрачная")};
    for (int s = 0; s < styles.size(); ++s) {
        QAction* a = look->addAction(styles[s]);
        a->setCheckable(true);
        a->setChecked(spec.style == s);
        connect(a, &QAction::triggered, this, [set, s]() { set([s](CardLayout::Card& c) { c.style = s; }); });
    }
    QAction* color = m.addAction(AqIcons::ui("palette", Theme::t().text, 14, 2.0), T("Свой цвет…"));
    connect(color, &QAction::triggered, this, [this, set, spec]() {
        QTimer::singleShot(0, this, [this, set, spec]() {
            QColor c = spec.color.isEmpty() ? Theme::t().red : QColor(spec.color);
            if (!ColorPickerDialog::getColor(window(), ColorPickerDialog::Plain, c)) return;
            const QString name = c.name();
            set([name](CardLayout::Card& cd) {
                cd.color = name;
                if (cd.style == CardLayout::Clear) cd.style = CardLayout::Normal;   // у прозрачной цвета не видно
            });
        });
    });
    if (!spec.color.isEmpty()) {
        QAction* noColor = m.addAction(T("Цвет как в теме"));
        connect(noColor, &QAction::triggered, this, [set]() { set([](CardLayout::Card& c) { c.color.clear(); }); });
    }
    m.addSeparator();
    QAction* showTitle = m.addAction(T("Заголовок"));
    showTitle->setCheckable(true);
    showTitle->setChecked(!spec.hideTitle);
    connect(showTitle, &QAction::toggled, this, [set](bool on) { set([on](CardLayout::Card& c) { c.hideTitle = !on; }); });
    QAction* showNum = m.addAction(T("Номер"));
    showNum->setCheckable(true);
    showNum->setChecked(!spec.hideNumber);
    connect(showNum, &QAction::toggled, this, [set](bool on) { set([on](CardLayout::Card& c) { c.hideNumber = !on; }); });
    QAction* row = m.addAction(T("Блоки в ряд (как в шапке)"));
    row->setCheckable(true);
    row->setChecked(spec.row);
    connect(row, &QAction::toggled, this, [set](bool on) { set([on](CardLayout::Card& c) { c.row = on; }); });
    m.addSeparator();
    m.addAction(T("Свернуть"), this, [set]() { set([](CardLayout::Card& c) { c.collapsed = true; }, true); });
    if (CardLayout::isCustom(cardId)) {
        m.addAction(T("Переименовать…"), this, [this, cardId]() {
            QTimer::singleShot(0, this, [this, cardId]() { askRename(cardId); });
        });
        m.addAction(T("Удалить карточку"), this, [this, cardId]() {
            QTimer::singleShot(0, this, [this, cardId]() {
                change([cardId](CardLayout::Layout& l) { CardLayout::removeCustom(l, cardId); });
            });
        });
    }
    if (from) from->setDown(true);
    m.exec(global);
    if (from) from->setDown(false);
}

void Board::askNewCard() {
    bool ok = false;
    const QString name = QInputDialog::getText(window(), T("Новая карточка"), T("Название карточки:"), QLineEdit::Normal,
                                               T("Моя карточка"), &ok).simplified();
    if (!ok || name.isEmpty()) return;
    QString id;
    change([&id, name](CardLayout::Layout& l) { id = CardLayout::addCustom(l, name); });
    // показать новую карточку — она в конце
    QTimer::singleShot(0, this, [this, id]() {
        BoardCard* c = cards_.value(id);
        for (QWidget* p = parentWidget(); p && c; p = p->parentWidget())
            if (auto* sa = qobject_cast<QScrollArea*>(p)) { sa->ensureWidgetVisible(c, 0, 40); break; }
    });
}

void Board::askRename(const QString& cardId) {
    const int i = CardLayout::indexOf(layout_, cardId);
    if (i < 0) return;
    bool ok = false;
    const QString name = QInputDialog::getText(window(), T("Переименовать карточку"), T("Название карточки:"),
                                               QLineEdit::Normal, layout_[i].title, &ok).simplified();
    if (!ok || name.isEmpty()) return;
    change([cardId, name](CardLayout::Layout& l) { CardLayout::rename(l, cardId, name); });
}

// ---------------------------------------------------------------------------
// Края карточки: ширина четвертями, высота — с шагом 10 px
// ---------------------------------------------------------------------------
void Board::beginResize(BoardCard* c, int edge) {
    if (!edit_ || !c) return;
    resizeId_ = c->id();
    resizeEdge_ = edge;
    previewSpan_ = previewHeight_ = -1;
    // меряем от того места, где карточка была в начале: превью может перенести её в другой ряд
    resizeOrigin_ = c->mapTo(this, QPoint(0, 0));
    resizePress_ = QCursor::pos();
    resizeDragging_ = false;
}

void Board::cancelResize() {
    resizeId_.clear();
    resizeEdge_ = -1;
    previewSpan_ = previewHeight_ = -1;
    resizeDragging_ = false;
    sizeTag_->hide();
}

void Board::moveResize(const QPoint& global) {
    BoardCard* c = cards_.value(resizeId_);
    if (resizeEdge_ < 0 || !c) return;
    // щелчок с дрожанием руки — не перетаскивание (и двойной щелчок по краю не ломается)
    if (!resizeDragging_) {
        if ((global - resizePress_).manhattanLength() < QApplication::startDragDistance()) return;
        resizeDragging_ = true;
    }
    const QPoint p = mapFromGlobal(global);
    const QPoint o = resizeOrigin_;
    const int W = qMax(1, width()), g = gap();
    QString tag;
    if (resizeEdge_ == 0) {
        const int w = qMax(1, p.x() - o.x());
        const int span = qBound(1, qRound((w + g) * 4.0 / (W + g)), 4);
        if (span != previewSpan_) { previewSpan_ = span; doLayout(); }
        tag = spanName(span);
    } else {
        const int minH = c->minimumSizeHint().height();
        int h = qBound(0, (p.y() - o.y() + 5) / 10 * 10, CardLayout::kMaxHeight);
        if (h <= minH + 4) h = 0;                       // меньше содержимого — «по содержимому»
        if (h != previewHeight_) { previewHeight_ = h; doLayout(); }
        tag = h > 0 ? T("Высота %1").arg(h) : T("Высота по содержимому");
    }
    sizeTag_->setText(tag);
    sizeTag_->adjustSize();
    QPoint at = p + QPoint(14, 14);
    at.setX(qBound(0, at.x(), qMax(0, width() - sizeTag_->width())));
    at.setY(qBound(0, at.y(), qMax(0, height() - sizeTag_->height())));
    sizeTag_->move(at);
    sizeTag_->show();
    sizeTag_->raise();
}

void Board::endResize() {
    const QString id = resizeId_;
    const int edge = resizeEdge_, span = previewSpan_, h = previewHeight_;
    const bool dragged = resizeDragging_;
    cancelResize();
    // не тянули (просто щёлкнули) — ничего не меняем; то же значение change() и сам не запишет
    if (id.isEmpty() || !dragged || (edge == 0 ? span : h) < 0) { doLayout(); return; }
    // раскладку меняем после обработчика (рамка — внутри карточки)
    QTimer::singleShot(0, this, [this, id, edge, span, h]() {
        change([id, edge, span, h](CardLayout::Layout& l) {
            const int i = CardLayout::indexOf(l, id);
            if (i < 0) return;
            if (edge == 0) l[i].span = span;
            else l[i].height = h;
        }, false);
        doLayout();
    });
}

// ---------------------------------------------------------------------------
// Перетаскивание
// ---------------------------------------------------------------------------
void Board::startDrag(QWidget* src, const QString& mime, const QString& id) {
    if (!edit_) return;
    dragKind_ = mime;
    dragId_ = id;
    auto* data = new QMimeData();
    data->setData(mime, id.toUtf8());
    auto* drag = new QDrag(src);
    drag->setMimeData(data);
    // картинка под курсором — уменьшенный снимок
    const QPixmap shot = src->grab();
    const qreal k = qMin(1.0, 340.0 / qMax(1, src->width()));
    QPixmap pm(shot.size() * k);
    pm.setDevicePixelRatio(shot.devicePixelRatio());
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setOpacity(0.85);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(QRectF(QPointF(0, 0), QSizeF(src->size()) * k), shot, shot.rect());
    }
    drag->setPixmap(pm);
    drag->setHotSpot(QPoint(qRound(14 * k), qRound(14 * k)));
    drag->exec(Qt::MoveAction);
    dragKind_.clear();
    dragId_.clear();
    marker_->hide();
}

bool Board::findTarget(const QPoint& p) {
    targetCard_.clear();
    targetIndex_ = -1;
    const int g = gap();
    auto rectOf = [this](QWidget* w) { return QRect(w->mapTo(this, QPoint(0, 0)), w->size()); };
    auto dist = [](const QRect& r, const QPoint& pt) {
        const int dx = qMax(0, qMax(r.left() - pt.x(), pt.x() - r.right()));
        const int dy = qMax(0, qMax(r.top() - pt.y(), pt.y() - r.bottom()));
        return dx * dx + dy * dy;
    };
    BoardCard* best = nullptr;
    int bestD = INT_MAX;
    for (BoardCard* c : gridCards()) {
        if (dragKind_ == kCardMime && c->id() == dragId_) continue;
        const int d = dist(rectOf(c), p);
        if (d < bestD) { bestD = d; best = c; }
    }
    if (!best) return false;
    const QRect r = rectOf(best);
    if (dragKind_ == kCardMime) {
        // рядом с карточкой (в одной строке) — вставка слева/справа, иначе — сверху/снизу
        const bool across = r.width() < width() - g;
        const bool before = across ? p.x() < r.center().x() : p.y() < r.center().y();
        targetCard_ = best->id();
        targetIndex_ = CardLayout::indexOf(layout_, best->id()) + (before ? 0 : 1);
        if (across) marker_->setGeometry(before ? r.left() - g / 2 - 2 : r.right() + g / 2 - 1, r.top(), 4, r.height());
        else marker_->setGeometry(r.left(), before ? r.top() - g / 2 - 2 : r.bottom() + g / 2 - 1, r.width(), 4);
    } else {
        targetCard_ = best->id();
        const QList<BoardBlock*>& bl = best->blocks();
        if (best->rowMode() && !bl.isEmpty()) {
            // в ряду — ближайший блок, слева или справа от него
            int k = 0, kd = INT_MAX;
            for (int i = 0; i < bl.size(); ++i)
                if (const int d = dist(rectOf(bl[i]), p); d < kd) { kd = d; k = i; }
            const QRect br = rectOf(bl[k]);
            const bool before = p.x() < br.center().x();
            targetIndex_ = k + (before ? 0 : 1);
            marker_->setGeometry(before ? br.left() - 9 : br.right() + 5, br.top(), 4, br.height());
        } else {
            int idx = bl.size();
            for (int i = 0; i < bl.size(); ++i)
                if (p.y() < rectOf(bl[i]).center().y()) { idx = i; break; }
            targetIndex_ = idx;
            int y;
            if (bl.isEmpty()) y = rectOf(best->emptyHint()).top() - 6;
            else if (idx < bl.size()) y = rectOf(bl[idx]).top() - 7;
            else y = rectOf(bl.last()).bottom() + 4;
            marker_->setGeometry(r.left() + 12, y, r.width() - 24, 4);
        }
    }
    // у крайних карточек полоса — внутри доски, иначе её не видно
    QRect m = marker_->geometry();
    m.moveLeft(qBound(0, m.left(), qMax(0, width() - m.width())));
    m.moveTop(qBound(0, m.top(), qMax(0, height() - m.height())));
    marker_->setGeometry(m);
    marker_->show();
    marker_->raise();
    return true;
}

void Board::dragEnterEvent(QDragEnterEvent* e) {
    const QMimeData* m = e->mimeData();
    if (edit_ && (m->hasFormat(kCardMime) || m->hasFormat(kBlockMime))) e->acceptProposedAction();
    else e->ignore();
}

void Board::dragMoveEvent(QDragMoveEvent* e) {
    if (dragKind_.isEmpty() || !findTarget(e->position().toPoint())) {
        marker_->hide();
        e->ignore();
        return;
    }
    e->acceptProposedAction();
}

void Board::dragLeaveEvent(QDragLeaveEvent*) { marker_->hide(); }

void Board::dropEvent(QDropEvent* e) {
    marker_->hide();
    if (dragKind_.isEmpty() || !findTarget(e->position().toPoint())) { e->ignore(); return; }
    marker_->hide();
    e->acceptProposedAction();
    const QString kind = dragKind_, id = dragId_, to = targetCard_;
    const int idx = targetIndex_;
    // раскладку меняем после возврата из exec() — источник перетаскивания ещё жив
    QTimer::singleShot(0, this, [this, kind, id, to, idx]() {
        if (kind == kCardMime) change([id, idx](CardLayout::Layout& l) { CardLayout::moveCard(l, id, idx); });
        else change([id, to, idx](CardLayout::Layout& l) { CardLayout::moveBlock(l, id, to, idx); });
    });
}
