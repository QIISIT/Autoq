#pragma once
// Карточки главного окна (Д.И.1, редактор интерфейса 1.14): раскладка по CardLayout —
// у каждой игры своя.
//  - Каждую карточку можно свернуть кнопкой справа вверху: она становится
//    маленьким прямоугольником над карточками, щелчок — развернуть.
//  - Режим редактирования: карточки перетаскиваются за заголовок, блоки внутри —
//    за любое место (из карточки в карточку). Ширину и высоту карточки меняют,
//    потянув за правый и нижний край; вид, цвет, заголовок, номер и «блоки в ряд» —
//    в меню карточки (шестерёнка). Блок прячут глазом — он уходит в «Скрытые».
//    Шаблоны, раскладка в файл и обратно, боковая панель — в полосе редактирования.
//    Ctrl+Z / Ctrl+Y — отменить и вернуть.
// Блоки — живые виджеты: при смене раскладки они только переезжают в другую карточку.
#include <QFrame>
#include <QMap>
#include <QPointer>
#include <functional>
#include "cardlayout.h"

class QLabel;
class QToolButton;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QMenu;
class QShortcut;
class FlowLayout;
class RowFlow;
class EdgeGrip;

// Блок карточки: содержимое (body) и, по желанию, кнопки для заголовка (head) —
// они встают в заголовок карточки, если блок в ней первый, иначе — над блоком
class BoardBlock : public QWidget {
    Q_OBJECT
public:
    BoardBlock(const QString& id, const QString& name, QWidget* parent = nullptr);
    QString id() const { return id_; }
    QString name() const { return name_; }
    QVBoxLayout* body() const { return body_; }
    void setHead(QWidget* head);
    QWidget* head() const { return head_; }
    bool wide = false;            // карточка с этим блоком — на всю ширину (действия, шапка)
    bool bottom = false;          // в конце карточки — прижат к низу
    bool stretch = false;         // в ряду (шапка) — занимает всё свободное место
    int rowMin = 0;               // в ряду — не уже этого (иначе ряд переносится)
    void setDivider(bool on);     // тонкая линия над блоком, если он в карточке не первый
    void showDivider(bool on) { if (divider_) divider_->setVisible(on); }
    void placeHead(QHBoxLayout* cardHead, int index);   // nullptr — над самим блоком
    void setEditing(bool on);
    QSize minimumSizeHint() const override;   // в редактировании — не уже 150 (ярлык и глаз)
signals:
    void dragStarted(BoardBlock* b);
    void hideRequested(BoardBlock* b);
protected:
    void resizeEvent(QResizeEvent* e) override;
private:
    QString id_, name_;
    QWidget* top_;
    QHBoxLayout* topLay_;
    QLabel* caption_;
    QVBoxLayout* body_;
    QWidget* head_ = nullptr;
    QWidget* divider_ = nullptr;
    QWidget* shield_ = nullptr;   // в режиме редактирования — поверх блока, за него тянут
    bool editing_ = false;
};

class BoardCard : public QFrame {
    Q_OBJECT
public:
    BoardCard(const QString& id, QWidget* parent = nullptr);
    QString id() const { return id_; }
    QLabel* title() const { return title_; }
    // вид карточки по раскладке; number — номер на карточке (0 — без номера)
    void applySpec(const CardLayout::Card& spec, bool editing, int number);
    const CardLayout::Card& spec() const { return spec_; }
    void setBlocks(const QList<BoardBlock*>& blocks, bool editing);
    const QList<BoardBlock*>& blocks() const { return blocks_; }
    bool wide() const;
    bool rowMode() const { return spec_.row; }
    QWidget* emptyHint() const;
    void refreshIcons();
signals:
    void collapseClicked();
    void settingsClicked(QToolButton* from);
    void renameClicked();
    void deleteClicked();
    void dragStarted(BoardCard* c);
    // край: 0 — правый (ширина), 1 — нижний (высота)
    void resizeStarted(BoardCard* c, int edge);
    void resizeMoved(const QPoint& global);
    void resizeFinished();
    void resizeReset(BoardCard* c, int edge);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
private:
    void placeGrips();
    QString id_;
    CardLayout::Card spec_;
    QVBoxLayout* mainLay_;
    QWidget* headW_;
    QHBoxLayout* headLay_;
    QToolButton* grip_;
    QLabel* num_;
    QLabel* title_;
    QToolButton* settings_;
    QToolButton* rename_;
    QToolButton* delete_;
    QToolButton* collapse_;
    QVBoxLayout* blocksLay_;
    RowFlow* rowLay_;
    QLabel* empty_;
    EdgeGrip* gripR_;
    EdgeGrip* gripB_;
    QList<BoardBlock*> blocks_;
    QPoint pressPos_;
    bool pressed_ = false;
    bool editing_ = false;
};

class Board : public QWidget {
    Q_OBJECT
public:
    explicit Board(QWidget* parent = nullptr);
    BoardBlock* addBlock(const QString& id, const QString& name);
    QLabel* titleLabel(const QString& cardId) const;
    // раскладка игры (история отмен — заново: у другой игры она своя)
    void setCardLayout(const CardLayout::Layout& l);
    const CardLayout::Layout& cardLayout() const { return layout_; }
    bool blockVisible(const QString& block) const;   // не в «Скрытых» и карточка не свёрнута
    void setEditMode(bool on, bool animate = true);
    bool editMode() const { return edit_; }
    QWidget* editBar() const { return bar_; }      // полоса режима редактирования — над прокруткой
    QList<QWidget*> visibleCards() const;          // для каскада при смене игры
    void setAnimArea(QWidget* w) { animArea_ = w; }
    void setSideAnimArea(QWidget* w) { sideArea_ = w; }   // переход, когда меняется боковая панель
    // боковая панель с играми: 0 слева, 1 справа, 2 узкая (только значки), 3 скрыта (общая для всех игр)
    int sidebarMode() const { return sidebar_; }
    // record — с историей отмен (кнопка в шапке); без — загрузка настроек
    void setSidebarMode(int mode, bool record = false);
    void refreshIcons();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool undo();
    bool redo();
signals:
    void edited();                                 // раскладку поменяли — сохранить для игры
    void editModeChanged(bool on);
    void applyToAllRequested();
    void sidebarModeChanged(int mode);
    void message(const QString& text);             // показать уведомление
protected:
    bool event(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
private:
    struct State { CardLayout::Layout layout; int sidebar = 0; };
    BoardCard* card(const QString& id);
    void rebuild();
    void doLayout();
    int columnsFor(int w) const;
    QList<BoardCard*> gridCards() const;
    CardLayout::Card specOf(const QString& id) const;     // с учётом тянущегося края
    // поменять раскладку (с отменой); animate — плавно, через снимок
    void change(const std::function<void(CardLayout::Layout&)>& f, bool animate = true);
    void applyState(const State& s, bool animate);
    void remember();                               // нынешнее — в историю отмен (перед правкой)
    void startDrag(QWidget* src, const QString& mime, const QString& id);
    bool findTarget(const QPoint& p);
    void buildBar();
    void updateBar();
    void askNewCard();
    void askRename(const QString& cardId);
    void showCardMenu(const QString& cardId, QToolButton* from, const QPoint& global);
    void fillHiddenMenu();
    void fillTemplatesMenu();
    void exportLayout();
    void importLayout();
    void beginResize(BoardCard* c, int edge);
    void moveResize(const QPoint& global);
    void endResize();
    void cancelResize();                           // раскладку сменили, пока тянули край

    QMap<QString, BoardBlock*> blocks_;
    QMap<QString, BoardCard*> cards_;
    CardLayout::Layout layout_;
    int sidebar_ = 0;
    QList<State> undo_, redo_;
    QWidget* strip_;
    FlowLayout* stripLay_;
    QWidget* marker_;
    QLabel* sizeTag_;                              // «½ ширины» у края, пока тянут
    QFrame* bar_ = nullptr;
    QPushButton* barReset_ = nullptr;
    QToolButton* barUndo_ = nullptr;
    QToolButton* barRedo_ = nullptr;
    QPushButton* barHidden_ = nullptr;
    QMenu* hiddenMenu_ = nullptr;
    QMenu* templatesMenu_ = nullptr;
    QList<QShortcut*> editKeys_;                   // Ctrl+Z / Ctrl+Y — включены только в редакторе
    QPointer<QWidget> animArea_;
    QPointer<QWidget> sideArea_;
    bool edit_ = false;
    bool laying_ = false;
    int cols_ = 1;
    // перетаскивание: что тянем и куда встанет
    QString dragKind_, dragId_;
    QString targetCard_;
    int targetIndex_ = -1;
    // край карточки тянут: какую, какой край, что получается
    QString resizeId_;
    int resizeEdge_ = -1;
    int previewSpan_ = -1, previewHeight_ = -1;
    QPoint resizeOrigin_;                          // левый верхний угол карточки, когда начали тянуть
    QPoint resizePress_;                           // где нажали (до порога перетаскивания край не двигается)
    bool resizeDragging_ = false;
};
