#pragma once
// Раскладка карточек главного окна (режим редактирования): какие карточки, в каком
// порядке, какие блоки в каждой, какого они размера и вида. У каждой игры своя
// раскладка (games/<id>/layout); без неё — стандартная.
//
// Блок — часть карточки, которую можно перетащить в другую карточку: «Скорость»,
// «Интервал», «Разброс», «Старт» и т. д. Свои карточки пользователя создаются
// пустыми, с названием — блоки в них перетаскивают из других карточек.
// Шапка (игра, счётчик, кнопки, «Старт») — тоже карточка: прозрачная, блоки в ряд.
// Скрытые блоки лежат в служебной карточке «hidden» — её не видно, блоки из неё
// возвращают кнопкой «Скрытые» в полосе редактирования.
#include <QList>
#include <QString>
#include <QStringList>

namespace CardLayout {
    enum Style { Normal = 0, Borderless = 1, Accent = 2, Clear = 3 };
    constexpr int kMaxHeight = 2000;
    inline const QString kHidden = QStringLiteral("hidden");

    struct Card {
        QString id;               // head / actions / speed / window / profile, своя: c1, c2… или hidden
        QString title;            // название своей карточки (у встроенных — пусто)
        QStringList blocks;       // id блоков сверху вниз (в ряд — слева направо)
        bool collapsed = false;   // свёрнута в маленький прямоугольник
        int span = 0;             // ширина: 0 — сама, 1…4 — четверти ширины
        int height = 0;           // своя высота, px (0 — по содержимому)
        int style = Normal;
        bool hideTitle = false;   // без заголовка (в редактировании он всё равно есть)
        bool hideNumber = false;
        QString color;            // свой цвет рамки и акцента «#rrggbb» (пусто — как в теме)
        bool row = false;         // блоки — в ряд, как в шапке
        bool operator==(const Card& o) const {
            return id == o.id && title == o.title && blocks == o.blocks && collapsed == o.collapsed &&
                   span == o.span && height == o.height && style == o.style && hideTitle == o.hideTitle &&
                   hideNumber == o.hideNumber && color == o.color && row == o.row;
        }
        bool operator!=(const Card& o) const { return !(*this == o); }
    };
    using Layout = QList<Card>;

    QStringList builtinCards();                  // head, actions, speed, window, profile
    QStringList blockIds();                      // все блоки, в порядке «по умолчанию»
    QStringList defaultHidden();                 // новые блоки, которых по умолчанию не видно
    QString homeOf(const QString& block);        // в какой встроенной карточке блок живёт по умолчанию
    bool isBuiltin(const QString& cardId);
    bool isCustom(const QString& cardId);
    Card defaultCard(const QString& id);         // встроенная карточка: блоки и вид по умолчанию

    Layout defaults();                           // стандартная (порядок — из старой настройки cardOrder)
    // привести в порядок: каждый блок ровно один раз, все встроенные карточки на месте,
    // «hidden» — последней, чужое и повторы — прочь, названия своих карточек — не
    // длиннее 40 символов, размеры и цвета — в допустимых пределах
    Layout normalize(Layout l);
    QString toJson(const Layout& l);
    Layout fromJson(const QString& json, bool* ok = nullptr);

    Layout load(const QString& gameId);
    void save(const QString& gameId, const Layout& l);   // стандартная — ключ удаляется
    bool isDefault(const Layout& l);

    int indexOf(const Layout& l, const QString& cardId);
    QString cardOf(const Layout& l, const QString& block);
    // переставить карточку: встанет перед той, что сейчас на месте toIndex (size() — в конец)
    void moveCard(Layout& l, const QString& cardId, int toIndex);
    // перенести блок в карточку на место index (-1 — в конец)
    void moveBlock(Layout& l, const QString& block, const QString& toCard, int index);
    QString addCustom(Layout& l, const QString& title, int at = -1);   // -> id новой карточки
    void removeCustom(Layout& l, const QString& cardId);               // её блоки — домой
    void setCollapsed(Layout& l, const QString& cardId, bool on);
    void rename(Layout& l, const QString& cardId, const QString& title);
    void hideBlock(Layout& l, const QString& block);                   // в «Скрытые»
    void showBlock(Layout& l, const QString& block);                   // из «Скрытых» — домой
    QStringList hiddenBlocks(const Layout& l);

    // Шаблоны: 0 — «Классика», 1 — «Компакт», 2 — «Монитор», 3 — «Одна колонка».
    // sidebar — где боковая панель с играми у шаблона (0 слева, 1 справа, 2 узкая, 3 скрыта)
    constexpr int kTemplates = 4;
    Layout templateLayout(int index, int* sidebar = nullptr);
    QString templateName(int index);

    // Раскладка в файл и из файла (вместе с боковой панелью)
    QString exportJson(const Layout& l, int sidebar);
    bool importJson(const QString& text, Layout* l, int* sidebar);
}
