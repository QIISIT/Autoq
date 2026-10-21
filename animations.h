#pragma once
// Анимации интерфейса (Настройки → Оформление → Анимации, выбор при первом запуске):
//  - transition — смена экрана/игры: снимок «до» и «после», переход поверх
//    (плавно, сдвигом или каскадом карточек). Живые виджеты не трогаются —
//    поэтому ничего не «едет» и не ломается на любой ОС;
//  - Toast — короткое сообщение внутри окна (вместо уведомлений у трея, пока
//    окно открыто): новое сразу заменяет старое.
#include <QFrame>
#include <QList>
#include <QPointer>
#include <functional>

class QLabel;
class QTimer;

namespace Anim {
    QString style();                      // none / fade / slide / cascade
    bool enabled();
    // change() меняет содержимое area; cards — что выезжает по очереди (каскад)
    void transition(QWidget* area, const std::function<void()>& change,
                    const QList<QWidget*>& cards = {}, const QString& styleOverride = QString());
    // показать только что появившийся виджет (попап, тост) по стилю анимаций
    void appear(QWidget* w, const QString& styleOverride = QString());
}

class Toast : public QFrame {
    Q_OBJECT
public:
    // host — окно (или его центральный виджет), внизу которого появится сообщение
    static void show(QWidget* host, const QString& text, int ms = 2600);
    static void hideAll(QWidget* host);
protected:
    void mousePressEvent(QMouseEvent*) override { hide(); deleteLater(); }
private:
    explicit Toast(QWidget* host);
    void place();
    bool eventFilter(QObject* o, QEvent* e) override;
    QWidget* host_;
    QLabel* text_;
    QTimer* timer_;
};

// Маленькая «живая» демонстрация анимации — для выбора при первом запуске
class AnimDemo : public QWidget {
public:
    explicit AnimDemo(const QString& style, QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(220, 120); }
protected:
    void paintEvent(QPaintEvent*) override;
    void timerEvent(QTimerEvent*) override;
private:
    QString style_;
    qint64 start_ = 0;
};
