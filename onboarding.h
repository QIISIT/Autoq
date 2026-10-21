#pragma once
// Первый запуск: выбор оформления (Студия / Стекло / Терминал) и анимаций —
// обязательно, один раз, до выбора игры. Пройти заново — Настройки → Оформление.
#include <QWidget>
#include <QList>

class QPushButton;
class QLabel;
class QStackedWidget;
class ThemePreview;

class OnboardingPage : public QWidget {
    Q_OBJECT
public:
    explicit OnboardingPage(QWidget* parent = nullptr);
    void restart();               // снова с первого шага (из настроек)
signals:
    void appearanceChanged();     // применили стиль — перерисовать окно
    void finished();
private:
    void goStep(int i);
    void chooseVariant(const QString& id);
    void chooseAnim(const QString& id);
    void refresh();
    QStackedWidget* steps_;
    QList<QLabel*> dots_;
    QList<QPushButton*> variantCards_;
    QList<ThemePreview*> previews_;
    QList<QPushButton*> animCards_;
    QPushButton* next_;
    QPushButton* back_;
    QLabel* stepTitle_;
    QLabel* stepNote_;
    QLabel* logo_;
    QString variant_, anim_;
    int step_ = 0;
};
