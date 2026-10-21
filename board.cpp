#include "board.h"
#include "i18n.h"
#include "theme.h"
#include "icons.h"
#include "widgets.h"
#include "animations.h"
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

namespace {
const char* kCardMime = "application/x-autoq-card";
const char* kBlockMime = "application/x-autoq-block";

int gap() { return Theme::sp(14); }

QString builtinName(const QString& id) {
    if (id == "actions") return T("Действия");
    if (id == "speed") return T("Скорость");
    if (id == "window") return T("Окно игры");
    if (id == "profile") return T("Профиль и запуск");
    return QString();
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

// Поверх блока в режиме редактирования: блок виден, но не нажимается — за него тянут
class BlockShield : public QWidget {
public:
    explicit BlockShield(BoardBlock* b) : QWidget(b), block_(b) {
        setCursor(Qt::OpenHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(T("Перетащи блок на другое место — в эту или другую карточку"));
    }
protected:
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
        const QString name = fm.elidedText(block_->name(), Qt::ElideRight, qMax(40.0, width() - 60.0));
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
    QPoint press_;
    bool pressed_ = false;
};
}

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
        topLay_->addWidget(head_);
        top_->show();
    }
    head_->show();
}

void BoardBlock::setEditing(bool on) {
    // в редактировании — поля вокруг блока и место под ярлык с названием сверху
    // (если сверху строка с кнопками блока — ярлык встаёт на место подписи)
    const bool topRow = head_ && !top_->isHidden();
    caption_->setVisible(!on);
    layout()->setContentsMargins(on ? 8 : 0, on ? (topRow ? 6 : 34) : 0, on ? 8 : 0, on ? 8 : 0);
    if (on && !shield_) shield_ = new BlockShield(this);
    if (!shield_) return;
    shield_->setGeometry(rect());
    shield_->setVisible(on);
    if (on) shield_->raise();
}

void BoardBlock::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (shield_) shield_->setGeometry(rect());
}

// ===========================================================================
BoardCard::BoardCard(const QString& id, QWidget* parent) : QFrame(parent), id_(id) {
    setObjectName("card");
    setContextMenuPolicy(Qt::CustomContextMenu);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 16);
    lay->setSpacing(10);
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
    rename_ = miniButton(T("Переименовать карточку"));
    rename_->hide();
    delete_ = miniButton(T("Удалить карточку (блоки вернутся на свои места)"));
    delete_->hide();
    collapse_ = miniButton(T("Свернуть карточку"));
    headLay_->addWidget(grip_);
    headLay_->addWidget(num_);
    headLay_->addWidget(title_, 0);
    headLay_->addStretch(1);
    headLay_->addWidget(rename_);
    headLay_->addWidget(delete_);
    headLay_->addWidget(collapse_);
    lay->addWidget(headW_);
    blocksLay_ = new QVBoxLayout();
    blocksLay_->setContentsMargins(0, 0, 0, 0);
    blocksLay_->setSpacing(10);
    lay->addLayout(blocksLay_, 1);
    empty_ = new QLabel(T("Перетащи сюда блоки из других карточек"));
    empty_->setObjectName("cardEmpty");
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setMinimumHeight(84);
    empty_->hide();
    connect(collapse_, &QToolButton::clicked, this, &BoardCard::collapseClicked);
    connect(rename_, &QToolButton::clicked, this, &BoardCard::renameClicked);
    connect(delete_, &QToolButton::clicked, this, &BoardCard::deleteClicked);
    refreshIcons();
}

void BoardCard::refreshIcons() {
    const auto& t = Theme::t();
    grip_->setIcon(AqIcons::ui("grip", t.muted, 14));
    rename_->setIcon(AqIcons::ui("pencil", t.muted, 13, 2.0));
    delete_->setIcon(AqIcons::ui("trash", t.muted, 13, 2.0));
    collapse_->setIcon(AqIcons::ui("chevron-up", t.muted, 13, 2.2));
}

void BoardCard::setNumber(int n) { num_->setText(QString("%1").arg(n, 2, 10, QChar('0'))); }

bool BoardCard::wide() const {
    for (BoardBlock* b : blocks_)
        if (b->wide) return true;
    return false;
}

QWidget* BoardCard::emptyHint() const { return empty_; }

void BoardCard::setBlocks(const QList<BoardBlock*>& blocks, bool editing) {
    // прежние блоки вынимаем, не удаляя: они могут переехать в другую карточку
    while (QLayoutItem* it = blocksLay_->takeAt(0)) delete it;
    blocks_ = blocks;
    editing_ = editing;
    // кнопки первого блока — в заголовок карточки (не в режиме редактирования:
    // там они остаются при блоке, чтобы было видно, что едут вместе с ним)
    const int headAt = headLay_->indexOf(rename_);
    for (int i = 0; i < blocks.size(); ++i)
        blocks[i]->placeHead(i == 0 && !editing ? headLay_ : nullptr, headAt);
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
    if (blocks.isEmpty()) {
        blocksLay_->addWidget(empty_);
        empty_->setVisible(editing);
    } else {
        empty_->hide();
        empty_->setParent(this);
        empty_->hide();
    }
    if (firstBottom == blocks.size()) blocksLay_->addStretch(1);
}

void BoardCard::setEditing(bool on) {
    editing_ = on;
    const bool custom = CardLayout::isCustom(id_);
    grip_->setVisible(on);
    rename_->setVisible(on && custom);
    delete_->setVisible(on && custom);
    headW_->setCursor(on ? Qt::OpenHandCursor : Qt::ArrowCursor);
    headW_->setToolTip(on ? T("Перетащи карточку за заголовок на другое место") : QString());
    setProperty("editing", on);
    Theme::repolish(this);
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
    for (const QString& id : CardLayout::builtinCards()) card(id);
    buildBar();
    layout_ = CardLayout::defaults();
}

BoardBlock* Board::addBlock(const QString& id, const QString& name) {
    auto* b = new BoardBlock(id, name, this);
    blocks_[id] = b;
    connect(b, &BoardBlock::dragStarted, this, [this](BoardBlock* blk) { startDrag(blk, kBlockMime, blk->id()); });
    return b;
}

BoardCard* Board::card(const QString& id) {
    if (BoardCard* c = cards_.value(id)) return c;
    auto* c = new BoardCard(id, this);
    cards_[id] = c;
    connect(c, &BoardCard::collapseClicked, this, [this, id]() {
        change([id](CardLayout::Layout& l) { CardLayout::setCollapsed(l, id, true); });
    });
    connect(c, &BoardCard::renameClicked, this, [this, id]() { askRename(id); });
    connect(c, &BoardCard::deleteClicked, this, [this, id]() {
        change([id](CardLayout::Layout& l) { CardLayout::removeCustom(l, id); });
    });
    connect(c, &BoardCard::dragStarted, this, [this](BoardCard* cd) { startDrag(cd, kCardMime, cd->id()); });
    connect(c, &QWidget::customContextMenuRequested, this, [this, c, id](const QPoint& pos) {
        QMenu m(c);
        m.addAction(T("Свернуть"), this, [this, id]() {
            change([id](CardLayout::Layout& l) { CardLayout::setCollapsed(l, id, true); });
        });
        m.addAction(edit_ ? T("Готово — выйти из режима редактирования") : T("Настроить карточки…"), this,
                    [this]() { setEditMode(!edit_); });
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
    layout_ = CardLayout::normalize(l);
    rebuild();
}

void Board::change(const std::function<void(CardLayout::Layout&)>& f) {
    CardLayout::Layout l = layout_;
    f(l);
    l = CardLayout::normalize(l);
    if (l == layout_) return;
    auto apply = [this, l]() {
        layout_ = l;
        rebuild();
    };
    if (animArea_ && Anim::style() != "none") Anim::transition(animArea_, apply, {}, "fade");
    else apply();
    emit edited();
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
        BoardCard* c = card(spec.id);
        if (CardLayout::isCustom(spec.id)) c->title()->setText(spec.title);
        QList<BoardBlock*> bl;
        for (const QString& b : spec.blocks)
            if (BoardBlock* blk = blocks_.value(b)) bl << blk;
        c->setEditing(edit_);
        c->setBlocks(bl, edit_);
        const bool shown = edit_ || !bl.isEmpty();
        if (shown) c->setNumber(++n);
        if (!shown || spec.collapsed) {
            c->hide();
            if (shown) {
                auto* tile = new QToolButton();
                tile->setObjectName("cardTile");
                tile->setCursor(Qt::PointingHandCursor);
                tile->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                tile->setIcon(AqIcons::ui("chevron", Theme::t().muted, 13, 2.2));
                tile->setText(QString("%1  %2").arg(n, 2, 10, QChar('0')).arg(c->title()->text()));
                tile->setToolTip(T("Развернуть карточку"));
                tile->setAccessibleName(T("Развернуть: %1").arg(c->title()->text()));
                const QString id = spec.id;
                connect(tile, &QToolButton::clicked, this, [this, id]() {
                    QTimer::singleShot(0, this, [this, id]() {
                        change([id](CardLayout::Layout& l) { CardLayout::setCollapsed(l, id, false); });
                    });
                });
                stripLay_->addWidget(tile);
            }
        } else {
            c->show();
        }
    }
    strip_->setVisible(stripLay_->count() > 0);
    if (barReset_) barReset_->setEnabled(!CardLayout::isDefault(layout_));
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

int Board::columnsFor(int w) const {
    int need = 250, narrow = 0;
    for (BoardCard* c : gridCards()) {
        if (c->wide()) continue;
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
    int y = 0;
    if (!strip_->isHidden()) {
        const int h = stripLay_->heightForWidth(W);
        strip_->setGeometry(0, 0, W, h);
        y = h + g;
    }
    cols_ = columnsFor(W);
    // ряды: широкая карточка — одна в ряду; неполный ряд делят поровну
    QList<QList<BoardCard*>> rows;
    QList<BoardCard*> cur;
    for (BoardCard* c : gridCards()) {
        if (c->wide() || cols_ == 1) {
            if (!cur.isEmpty()) { rows << cur; cur.clear(); }
            rows << QList<BoardCard*>{c};
            continue;
        }
        cur << c;
        if (cur.size() == cols_) { rows << cur; cur.clear(); }
    }
    if (!cur.isEmpty()) rows << cur;
    for (const QList<BoardCard*>& row : rows) {
        const int k = row.size();
        const int cw = (W - g * (k - 1)) / k;
        int rowH = 0;
        for (BoardCard* c : row) {
            int h = c->hasHeightForWidth() ? c->heightForWidth(cw) : c->sizeHint().height();
            rowH = qMax(rowH, qMax(h, c->minimumSizeHint().height()));
        }
        int x = 0;
        for (int i = 0; i < k; ++i) {
            const int w = i == k - 1 ? W - x : cw;
            row[i]->setGeometry(x, y, w, rowH);
            x += w + g;
        }
        y += rowH + g;
    }
    const int total = qMax(0, y - g);
    if (minimumHeight() != total) {
        setMinimumHeight(total);
        updateGeometry();
    }
    marker_->raise();
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

void Board::setEditMode(bool on) {
    if (on == edit_) return;
    auto apply = [this, on]() {
        edit_ = on;
        bar_->setVisible(on);
        rebuild();
    };
    if (animArea_ && Anim::style() != "none") Anim::transition(animArea_, apply, {}, "fade");
    else apply();
    emit editModeChanged(on);
}

void Board::refreshIcons() {
    for (BoardCard* c : cards_) c->refreshIcons();
    if (bar_) {
        if (auto* ic = bar_->findChild<QLabel*>("editBarIcon"))
            ic->setPixmap(AqIcons::uiPixmap("layout", Theme::t().redHi, 18, devicePixelRatioF()));
    }
    rebuild();
}

// ---------------------------------------------------------------------------
// Полоса режима редактирования
// ---------------------------------------------------------------------------
void Board::buildBar() {
    bar_ = new QFrame();
    bar_->setObjectName("editBar");
    auto* h = new QHBoxLayout(bar_);
    h->setContentsMargins(14, 10, 10, 10);
    h->setSpacing(10);
    auto* icon = new QLabel();
    icon->setObjectName("editBarIcon");
    icon->setPixmap(AqIcons::uiPixmap("layout", Theme::t().redHi, 18, devicePixelRatioF()));
    h->addWidget(icon, 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout();
    texts->setSpacing(2);
    auto* title = new QLabel(T("Режим редактирования"));
    title->setObjectName("cardTitle");
    auto* hint = new QLabel(T("Карточки тащи за заголовок, блоки — за любое место. У каждой игры своя раскладка."));
    hint->setObjectName("small");
    hint->setWordWrap(true);
    texts->addWidget(title);
    texts->addWidget(hint);
    h->addLayout(texts, 1);
    auto* add = new QPushButton(T("Новая карточка"));
    add->setIcon(AqIcons::ui("plus", Theme::t().text, 14));
    add->setToolTip(T("Пустая карточка со своим названием — перетащи в неё нужные блоки"));
    barReset_ = new QPushButton(T("Сбросить"));
    barReset_->setToolTip(T("Вернуть стандартную раскладку этой игры"));
    auto* all = new QPushButton(T("Для всех игр"));
    all->setToolTip(T("Сделать такую же раскладку у всех игр"));
    auto* done = new QPushButton(T("Готово"));
    done->setObjectName("accent");
    for (QPushButton* b : {add, barReset_, all, done}) {
        b->setCursor(Qt::PointingHandCursor);
        h->addWidget(b, 0, Qt::AlignVCenter);
    }
    connect(add, &QPushButton::clicked, this, &Board::askNewCard);
    connect(barReset_, &QPushButton::clicked, this, [this]() {
        change([](CardLayout::Layout& l) { l = CardLayout::defaults(); });
    });
    connect(all, &QPushButton::clicked, this, &Board::applyToAllRequested);
    connect(done, &QPushButton::clicked, this, [this]() { setEditMode(false); });
    bar_->hide();
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
        const bool across = cols_ > 1 && !best->wide();     // карточки рядом — вставка слева/справа
        const bool before = across ? p.x() < r.center().x() : p.y() < r.center().y();
        targetCard_ = best->id();
        targetIndex_ = CardLayout::indexOf(layout_, best->id()) + (before ? 0 : 1);
        if (across) marker_->setGeometry(before ? r.left() - g / 2 - 2 : r.right() + g / 2 - 1, r.top(), 4, r.height());
        else marker_->setGeometry(r.left(), before ? r.top() - g / 2 - 2 : r.bottom() + g / 2 - 1, r.width(), 4);
    } else {
        targetCard_ = best->id();
        const QList<BoardBlock*>& bl = best->blocks();
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
