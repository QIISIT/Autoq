#pragma once
// Окно группы действий: имя и что в ней (тот же ряд карточек, что в «Действиях»).
// Правка меняет группу везде, где она стоит, — и в других профилях этой игры.
#include "games.h"

class QWidget;

namespace GroupDialog {
enum Result { Cancelled, Saved, Deleted };
// uses — сколько раз группа стоит в текущем ряду
Result edit(QWidget* parent, ActionGroup& g, int uses);
}
