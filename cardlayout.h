#pragma once
// Раскладка карточек главного окна (режим редактирования): какие карточки, в каком
// порядке, какие блоки в каждой и какие свёрнуты. У каждой игры своя раскладка
// (games/<id>/layout); без неё — стандартная.
//
// Блок — часть карточки, которую можно перетащить в другую карточку: «Скорость»
// (интервал, ползунок, разброс), «Такт монитора» и т. д. Свои карточки пользователя
// создаются пустыми, с названием — блоки в них перетаскивают из других карточек.
#include <QList>
#include <QString>
#include <QStringList>

namespace CardLayout {
    struct Card {
        QString id;               // actions / speed / window / profile или своя: c1, c2…
        QString title;            // название своей карточки (у встроенных — пусто)
        QStringList blocks;       // id блоков сверху вниз
        bool collapsed = false;   // свёрнута в маленький прямоугольник
        bool operator==(const Card& o) const {
            return id == o.id && title == o.title && blocks == o.blocks && collapsed == o.collapsed;
        }
    };
    using Layout = QList<Card>;

    QStringList builtinCards();                  // actions, speed, window, profile
    QStringList blockIds();                      // все блоки, в порядке «по умолчанию»
    QString homeOf(const QString& block);        // в какой встроенной карточке блок живёт по умолчанию
    bool isBuiltin(const QString& cardId);
    bool isCustom(const QString& cardId);

    Layout defaults();                           // стандартная (порядок — из старой настройки cardOrder)
    // привести в порядок: каждый блок ровно один раз, все встроенные карточки на месте,
    // чужое и повторы — прочь, названия своих карточек — не длиннее 40 символов
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
}
