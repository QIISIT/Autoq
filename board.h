#pragma once
// Карточки главного окна (Д.И.1): раскладка по CardLayout — у каждой игры своя.
//  - Каждую карточку можно свернуть кнопкой справа вверху: она становится
//    маленьким прямоугольником над карточками, щелчок — развернуть.
//  - Режим редактирования: карточки перетаскиваются за заголовок, блоки внутри —
//    за любое место (из карточки в карточку). Свои карточки создаются пустыми,
//    с названием, — блоки в них переносят из других.
// Блоки — живые виджеты: при смене раскладки они только переезжают в другую карточку.
#include <QFrame>
#include <QMap>
#include <QPointer>
#include "cardlayout.h"

class QLabel;
class QToolButton;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class FlowLayout;

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
    bool wide = false;            // карточка с этим блоком — на всю ширину (действия)
    bool bottom = false;          // в конце карточки — прижат к низу
    void setDivider(bool on);     // тонкая линия над блоком, если он в карточке не первый
    void showDivider(bool on) { if (divider_) divider_->setVisible(on); }
    void placeHead(QHBoxLayout* cardHead, int index);   // nullptr — над самим блоком
    void setEditing(bool on);
signals:
    void dragStarted(BoardBlock* b);
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
};

class BoardCard : public QFrame {
    Q_OBJECT
public:
    BoardCard(const QString& id, QWidget* parent = nullptr);
    QString id() const { return id_; }
    QLabel* title() const { return title_; }
    void setNumber(int n);
    void setBlocks(const QList<BoardBlock*>& blocks, bool editing);
    const QList<BoardBlock*>& blocks() const { return blocks_; }
    bool wide() const;
    void setEditing(bool on);
    QWidget* emptyHint() const;
    void refreshIcons();
signals:
    void collapseClicked();
    void renameClicked();
    void deleteClicked();
    void dragStarted(BoardCard* c);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    QString id_;
    QWidget* headW_;
    QHBoxLayout* headLay_;
    QToolButton* grip_;
    QLabel* num_;
    QLabel* title_;
    QToolButton* rename_;
    QToolButton* delete_;
    QToolButton* collapse_;
    QVBoxLayout* blocksLay_;
    QLabel* empty_;
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
    void setCardLayout(const CardLayout::Layout& l);
    const CardLayout::Layout& cardLayout() const { return layout_; }
    void setEditMode(bool on);
    bool editMode() const { return edit_; }
    QWidget* editBar() const { return bar_; }      // полоса режима редактирования — над прокруткой
    QList<QWidget*> visibleCards() const;          // для каскада при смене игры
    void setAnimArea(QWidget* w) { animArea_ = w; }
    void refreshIcons();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
signals:
    void edited();                                 // раскладку поменяли — сохранить для игры
    void editModeChanged(bool on);
    void applyToAllRequested();
protected:
    bool event(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
private:
    BoardCard* card(const QString& id);
    void rebuild();
    void doLayout();
    int columnsFor(int w) const;
    QList<BoardCard*> gridCards() const;
    void change(const std::function<void(CardLayout::Layout&)>& f);
    void startDrag(QWidget* src, const QString& mime, const QString& id);
    bool findTarget(const QPoint& p);
    void buildBar();
    void askNewCard();
    void askRename(const QString& cardId);

    QMap<QString, BoardBlock*> blocks_;
    QMap<QString, BoardCard*> cards_;
    CardLayout::Layout layout_;
    QWidget* strip_;
    FlowLayout* stripLay_;
    QWidget* marker_;
    QFrame* bar_ = nullptr;
    QPushButton* barReset_ = nullptr;
    QPointer<QWidget> animArea_;
    bool edit_ = false;
    bool laying_ = false;
    int cols_ = 1;
    // перетаскивание: что тянем и куда встанет
    QString dragKind_, dragId_;
    QString targetCard_;
    int targetIndex_ = -1;
};
