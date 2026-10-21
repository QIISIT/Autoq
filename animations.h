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
    qreal speed();                        // множитель скорости (Оформление → Скорость анимаций): 1 — обычно
    int ms(int base);                     // длительность с учётом скорости
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
// Играет, только пока над ней курсор (или над карточкой-родителем, setHoverHost);
// иначе стоит на последнем кадре — так видно одну анимацию, а не все сразу
class AnimDemo : public QWidget {
public:
    explicit AnimDemo(const QString& style, QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(220, 120); }
    void setHoverHost(QWidget* host);     // наведение на него тоже запускает показ
    void setSpeedOverride(int percent) { speedOverride_ = percent; }   // 0 — как в настройках
    void replay();                        // сыграть один раз с начала
protected:
    void paintEvent(QPaintEvent*) override;
    void timerEvent(QTimerEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    void setPlaying(bool on);
    qreal speedNow() const;
    QString style_;
    qreal t_ = 2200;                      // «виртуальное» время цикла, мс (2200 — стоп на готовом кадре)
    qint64 last_ = 0;
    bool hover_ = false;
    bool once_ = false;                   // replay(): один цикл без наведения
    int timer_ = 0;
    int speedOverride_ = 0;
    QPointer<QWidget> host_;
};
