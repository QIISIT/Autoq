#pragma once
// Все значки AutoQ рисуются кодом (вектор), без внешних файлов:
//  - логотип — красная клавиша Q;
//  - значки игр — свой набор (компас, поезд, город…), без логотипов самих игр;
//  - значки интерфейса (плюс, карандаш, мишень…).
#include <QPixmap>
#include <QIcon>
#include <QColor>
#include <QStringList>
#include <QByteArray>

class QPainter;
class QRectF;

namespace AqIcons {
    // --- значки игр ---
    QStringList gameGlyphs();                    // compass, train, city, …
    QString glyphLabel(const QString& name);     // "компас"
    bool isGlyph(const QString& name);
    QStringList palette();                       // 8 цветов
    QString paletteLabel(const QString& hex);    // "золотой"

    // Стиль значков игр: gloss (Глянец, по умолчанию), duo (Иллюстрация),
    // neon (Неон), classic (Классика — простые линии, как до версии 1.9)
    QStringList iconStyles();
    QString iconStyleLabel(const QString& id);   // "Глянец"
    QString iconStyleNote(const QString& id);    // короткое описание
    QString normalizeIconStyle(const QString& id);
    void setIconStyle(const QString& id);
    QString iconStyle();

    // Плитка игры: цветной значок на подложке или своя картинка (PNG-байты).
    // active — рамка цвета акцента (выбранная игра). Рисуется текущим стилем.
    QPixmap gameTile(const QString& glyph, const QColor& color, const QByteArray& image,
                     int px, qreal dpr, bool active, bool lightTheme);
    // То же, но в заданном стиле (образцы в настройках)
    QPixmap gameTileStyled(const QString& style, const QString& glyph, const QColor& color,
                           const QByteArray& image, int px, qreal dpr, bool active, bool lightTheme);

    // Картинку пользователя приводим к квадрату 128×128 PNG (для хранения в настройках)
    QByteArray normalizeImage(const QImage& in);

    // Цвет акцента (из настроек оформления): логотип и рамка выбранной игры
    void setAccent(const QColor& c);
    QColor accent();

    // --- логотип ---
    QPixmap logo(int px, qreal dpr = 1.0, bool idle = false);
    QPixmap logoWithAccent(int px, qreal dpr, const QColor& accent);   // для образцов цвета
    QIcon appIcon(bool idle = false);

    // --- значки интерфейса ---
    QStringList uiNames();
    QPixmap uiPixmap(const QString& name, const QColor& color, int px, qreal dpr = 1.0,
                     qreal stroke = 2.0);
    QIcon ui(const QString& name, const QColor& color, int px = 16, qreal stroke = 2.0);

    // Нарисовать значок (игры или интерфейса) в квадрат rect
    bool paint(QPainter& p, const QString& name, const QRectF& rect, const QColor& color,
               qreal stroke = 1.8);
}
