#pragma once
// Действия игры: сколько угодно (до kMaxActions). У каждого — кнопка мыши или
// клавиша и «держать, мс» (0 — обычный щелчок); между действиями — пауза
// (режим «По очереди») или «+» (вместе / зажать). Длинный ряд переносится.
// Режим «По шагам»: каждый шаг — только нажать (↓) или только отпустить (↑),
// стрелка на шаге меняет одно на другое (кикфлип: ЛКМ↓, ПКМ↓, ПКМ↑, ЛКМ↑).
// Цепочка начинается с «старт через N мс» и кончается «повтор через N мс»
// (пауза после последнего — тот же интервал, что в карточке «Скорость»).
// Шаги и действия можно таскать за ручку и менять местами, правый клик — меню.
#include <QWidget>
#include <QList>
#include <QPointer>
#include <functional>
#include "games.h"

class QComboBox;
class QSpinBox;
class QToolButton;
class QPushButton;
class QLabel;
class QFrame;
class KeyPicker;
class FlowLayout;

class ActionsEditor : public QWidget {
    Q_OBJECT
public:
    explicit ActionsEditor(QWidget* parent = nullptr);
    // list — действия или шаги («По шагам»); startMs — «старт через»,
    // intervalMs — «повтор через», minIntervalMs — меньше интервал нельзя
    void setState(const QList<ActionStep>& list, GameMode mode, int startMs, int intervalMs, int minIntervalMs);
    void setTimes(int startMs, int intervalMs);     // только числа (интервал меняли в «Скорости»)
    QList<ActionStep> actions() const { return actions_; }
    int startDelay() const { return startMs_; }
    int interval() const { return intervalMs_; }
    void setActiveStep(int i);                      // подсветить то, что кликер жмёт сейчас (-1 — ничего)
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void refreshIcons();
signals:
    void changed();
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
private:
    struct SlotUi {
        QFrame* frame = nullptr;
        QToolButton* grip = nullptr;
        QComboBox* type = nullptr;
        KeyPicker* value = nullptr;
        QToolButton* dir = nullptr;      // «По шагам»: ↓ нажать / ↑ отпустить
        QSpinBox* hold = nullptr;
        QLabel* holdLabel = nullptr;
        QSpinBox* after = nullptr;
        QToolButton* remove = nullptr;
    };
    // Плавно: ряды доезжают на новые места, новое проявляется, убранное —
    // тает на своём месте, высота меняется постепенно (раньше всё перескакивало)
    struct Snapshot { QList<QRect> units; QRect add; int height = 0; };
    Snapshot snapshot() const;
    // oldOf[j] — где была карточка j до изменения (-1 — новая); removedOld — убранная
    void animateFrom(const Snapshot& before, const QList<int>& oldOf, int removedOld, const QPixmap& removedPm);
    void stopAnimations();
    void later(const std::function<void()>& f);   // правка сразу после обработчика (см. .cpp)
    // изменить список с анимацией: f меняет actions_, oldOf — откуда кто приехал
    void applyEdit(const QList<ActionStep>& next, const QList<int>& oldOf, int removedOld);
    void insertAt(int at, const ActionStep& a);
    void removeAt(int i);
    void moveTo(int from, int to);
    ActionStep newItem() const;
    void showMenu(int i, const QPoint& global);
    void rebuild();
    void updateValues();
    void updateDir(SlotUi& ui, const ActionStep& a);
    void edited();
    int dropIndex(const QPoint& p) const;
    bool steps() const { return mode_ == GameMode::Steps; }
    int maxItems() const { return steps() ? kMaxSteps : kMaxActions; }

    QList<ActionStep> actions_;
    GameMode mode_ = GameMode::Sequence;
    int startMs_ = 0, intervalMs_ = 100, minIntervalMs_ = 1;
    FlowLayout* flow_;
    QList<SlotUi> slots_;
    QList<QWidget*> units_;                 // действие + пауза/«+» после него
    QWidget* startUnit_ = nullptr;          // «старт через»
    QSpinBox* startSpin_ = nullptr;
    QSpinBox* repeatSpin_ = nullptr;        // «повтор через» — после последнего
    QPointer<class QVariantAnimation> heightAnim_;
    QPushButton* add_ = nullptr;
    QWidget* dropMark_ = nullptr;
    int builtCount_ = -1;
    GameMode builtMode_ = GameMode::Sequence;
    int active_ = -1;
    int dragFrom_ = -1;
    int dropAt_ = -1;
    QPoint pressPos_;
    int pressSlot_ = -1;
    bool loading_ = false;
    bool busy_ = false;                     // идёт отложенная правка — вторую не начинаем
    int buildGen_ = 0;                      // номер сборки ряда (правка для старого ряда — устарела)
    int animGen_ = 0;                       // номер последней анимации
};
