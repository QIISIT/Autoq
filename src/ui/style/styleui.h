#pragma once
// Окна для профилей стиля (Настройки → Оформление → «Мои стили» и «Стили» в редакторе)
#include "ui/style/styles.h"

class QWidget;

namespace StyleUi {
    // «Сохранить текущий вид»: название и «вместе с раскладкой карточек».
    // layout — раскладка открытой игры (пусто — галочки нет). Имя занято — спросит, заменить ли
    bool saveCurrent(QWidget* parent, const QString& layout, QString* savedName = nullptr);
    // стиль из файла — в «Мои стили» (имя занято — «Имя (2)»)
    bool importFile(QWidget* parent, QString* name = nullptr);
    bool exportFile(QWidget* parent, const QString& name);
    bool askRename(QWidget* parent, const QString& name);
    bool askRemove(QWidget* parent, const QString& name);
    // заменить стиль текущим видом (раскладку — если она в стиле есть)
    bool update(QWidget* parent, const QString& name, const QString& layout);
    // что поставить: 0 — отмена, 1 — только оформление, 2 — и раскладку (allGames — у всех игр)
    int askApply(QWidget* parent, const Styles::Profile& p, bool* allGames);
}
