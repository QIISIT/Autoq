#pragma once
// Действия игры: сколько угодно (до kMaxActions). У каждого — кнопка мыши или
// клавиша и «держать, мс» (0 — обычный щелчок); между действиями — пауза
// (режим «По очереди») или «+» (вместе / зажать). Длинный ряд переносится.
#include <QWidget>
#include <QList>
#include "games.h"

class QComboBox;
class QSpinBox;
class QToolButton;
class QPushButton;
class QLabel;
class KeyPicker;
class FlowLayout;

class ActionsEditor : public QWidget {
    Q_OBJECT
public:
    explicit ActionsEditor(QWidget* parent = nullptr);
    void setActions(const QList<ActionStep>& actions, GameMode mode);
    QList<ActionStep> actions() const { return actions_; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void refreshIcons();
signals:
    void changed();
private:
    struct SlotUi {
        QComboBox* type = nullptr;
        KeyPicker* value = nullptr;
        QSpinBox* hold = nullptr;
        QLabel* holdLabel = nullptr;
        QSpinBox* after = nullptr;
        QToolButton* remove = nullptr;
    };
    // Плавно: ряды доезжают на новые места, новое действие проявляется, убранное —
    // тает на своём месте, высота меняется постепенно (раньше всё перескакивало)
    struct Snapshot { QList<QRect> units; QRect add; int height = 0; };
    Snapshot snapshot() const;
    void animateFrom(const Snapshot& before, int removed, int added, const QPixmap& removedPm);
    void rebuild();
    void updateValues();
    void edited();
    QList<ActionStep> actions_;
    GameMode mode_ = GameMode::Sequence;
    FlowLayout* flow_;
    QList<SlotUi> slots_;
    QList<QWidget*> units_;                 // действие + пауза/«+» после него
    class QVariantAnimation* heightAnim_ = nullptr;
    QPushButton* add_ = nullptr;
    int builtCount_ = -1;
    GameMode builtMode_ = GameMode::Sequence;
    bool loading_ = false;
};
