#pragma once
// Выбор цвета с живым образцом интерфейса:
//  - ThemePreview — маленький макет окна AutoQ в выбранных цветах;
//  - ColorPickerDialog — свой цвет акцента/фона (квадрат оттенков, полоса тона,
//    #HEX, готовые цвета) — образец меняется сразу, ещё до «Применить»;
//  - AppearancePopup — быстрый выбор темы и цвета (кнопка в мини-окне).
#include <QDialog>
#include <QFrame>
#include <QWidget>
#include <QColor>
#include "theme.h"

class QLineEdit;
class QLabel;
class QToolButton;
class Segmented;

class ThemePreview : public QWidget {
public:
    explicit ThemePreview(QWidget* parent = nullptr);
    void setAppearance(const Theme::Appearance& a) { a_ = a; update(); }
    QSize sizeHint() const override { return QSize(316, 196); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Theme::Appearance a_;
};

// Квадрат насыщенность/яркость для заданного тона
class SvPlane : public QWidget {
    Q_OBJECT
public:
    explicit SvPlane(QWidget* parent = nullptr);
    void setHsv(float h, float s, float v) { h_ = h; s_ = s; v_ = v; update(); }
    QSize sizeHint() const override { return QSize(236, 168); }
signals:
    void picked(float s, float v);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
private:
    void pickAt(const QPoint& p);
    float h_ = 0, s_ = 1, v_ = 1;
};

// Вертикальная полоса тона
class HueStrip : public QWidget {
    Q_OBJECT
public:
    explicit HueStrip(QWidget* parent = nullptr);
    void setHue(float h) { h_ = h; update(); }
    QSize sizeHint() const override { return QSize(20, 168); }
signals:
    void picked(float h);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
private:
    void pickAt(const QPoint& p);
    float h_ = 0;
};

class ColorPickerDialog : public QDialog {
    Q_OBJECT
public:
    enum Target { Accent, Background, Plain };   // Plain — любой цвет (оверлей), без образца окна
    ColorPickerDialog(Target target, const QColor& initial, QWidget* parent = nullptr);
    QColor color() const { return color_; }
    // true — пользователь нажал «Применить»; color — выбранный цвет
    static bool getColor(QWidget* parent, Target target, QColor& color);

signals:
    void colorChanged(const QColor& c);     // меняется на лету — для живого образца снаружи

private:
    void setColor(const QColor& c, bool fromHex = false);
    Target target_;
    QColor color_;
    float h_ = 0, s_ = 1, v_ = 1;
    SvPlane* plane_;
    HueStrip* hue_;
    QLineEdit* hex_;
    QLabel* swatchNow_;
    QLabel* note_;
    ThemePreview* preview_;
};

// Всплывающее окошко «Оформление»: тема + цвет акцента + «Все настройки…»
class AppearancePopup : public QFrame {
    Q_OBJECT
public:
    explicit AppearancePopup(QWidget* parent = nullptr);
    void popupBelow(QWidget* anchor);
signals:
    void changed();                 // оформление применено и сохранено
    void moreRequested();           // открыть окно «Настройки»
private:
    void apply(const Theme::Appearance& a);
    void refresh();
    Segmented* themeSeg_;
    QList<QToolButton*> swatches_;
};
