#pragma once
// Профили стиля: оформление (стиль интерфейса, тема, цвета, значки, анимации, шрифты,
// скругление, рамки, вид мелких элементов) под своим именем и, по желанию, раскладка
// карточек. Каждый стиль — файл <папка данных>/styles/<имя>.aqstyle (JSON): его можно
// отправить другу и загрузить «Из файла…». Имя стиля записано в файле, имя файла —
// только для людей. Регистр в именах не различается.
#include "ui/style/theme.h"
#include <QString>
#include <QStringList>

namespace Styles {
    struct Profile {
        QString name;
        Theme::Appearance look;
        QString layout;            // раскладка карточек (CardLayout::exportJson), пусто — без неё
        bool hasLayout() const { return !layout.isEmpty(); }
    };

    QString dir();                                  // <папка данных>/styles
    QString extension();                            // ".aqstyle"
    QStringList names();                            // все стили, по алфавиту
    bool exists(const QString& name);
    bool validName(const QString& name, QString* why = nullptr);
    QString uniqueName(const QString& name);        // «Неон», занято — «Неон (2)»

    QString toJson(const Profile& p);
    bool fromJson(const QString& text, Profile* p); // false — это не стиль AutoQ

    bool load(const QString& name, Profile* p);
    bool save(const Profile& in, QString* error = nullptr);   // такое имя уже есть — заменяет
    bool remove(const QString& name);
    bool rename(const QString& from, const QString& to, QString* error = nullptr);
    QString filePath(const QString& name);          // файл стиля (пусто — такого нет)

    // Оформление совпадает (стиль сейчас стоит) — без размера интерфейса
    bool sameLook(const Theme::Appearance& a, const Theme::Appearance& b);
}
