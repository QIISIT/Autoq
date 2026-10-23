#pragma once
// Оверлей КПС: прозрачная надпись поверх игры — сколько кликов в секунду
// делает кликер, по каждому действию отдельно, всего за сессию, статус, график.
// Окно не принимает мышь и фокус (клики проходят сквозь него в игру).
// Всё оформление настраивается: место, монитор, цвета, прозрачность, размер,
// шрифт, скругление, в строку / столбиком, обводка текста.
#include <QWidget>
#include "i18n.h"
#include <QColor>
#include <QVector>
#include <functional>
#include <deque>
#include <map>
#include <QList>

struct OverlaySettings {
    bool enabled = false;
    int when = 0;                 // 0 — пока кликер работает, 1 — всегда
    bool showCps = true;          // большое число — клики в секунду
    bool showLabel = true;        // подпись к числу («КПС»)
    bool showActions = true;      // по действиям: «ЛКМ 10 · F 10»
    bool showTotal = false;       // всего за сессию
    bool showStatus = true;       // работает / пауза
    bool showGame = false;        // название игры
    bool showGraph = false;       // график за 10 секунд
    QString label = T("КПС");
    int anchor = 2;               // 0..8: ↖ ↑ ↗ / ← · → / ↙ ↓ ↘
    int offsetX = 24;             // отступ от края экрана (для середины — сдвиг)
    int offsetY = 24;
    int screen = 0;               // монитор
    QColor textColor = QColor("#FFFFFF");
    QColor numberColor;           // не задан — цвет акцента
    QColor bgColor = QColor("#0B0B0D");
    int bgOpacity = 65;           // прозрачность подложки, %
    int opacity = 100;            // общая видимость, %
    int scale = 100;              // размер, %
    int font = 0;                 // 0 — Unbounded, 1 — Onest, 2 — JetBrains Mono
    int radius = 12;
    bool vertical = false;        // столбиком
    bool outline = true;          // обводка текста — читается на любом фоне
    int windowMs = 1000;          // за какое время считать клики в секунду

    static OverlaySettings load();
    void save() const;
    static OverlaySettings defaults();
};

// Что показывать сейчас (считается из счётчиков кликера)
struct OverlayView {
    double cps = 0;               // всего кликов в секунду
    double cps1 = 0, cps2 = 0;    // по действиям
    QString a1, a2;               // подписи действий: «ЛКМ», «F» (a2 пусто — нет второго)
    long long total = 0;
    int status = 0;               // 0 — остановлен, 1 — работает, 2 — пауза (не окно игры), 3 — ждёт окно,
                                  // 4 — пауза (курсор не над игрой), 5 — ждёт «старт через»
    bool hold = false;            // режим «Зажать»
    QString game;                 // «Genshin Impact»
    QString profile;              // имя профиля (пусто — без профиля)
    bool named = false;           // несколько игр сразу — название первым, даже если «Игра» выключена
    QVector<double> graph;        // последние 10 секунд
};

// Одна игра (с её профилем) в оверлее — заполняет главное окно
struct OverlayEntry {
    QString key;                  // id игры: у каждой своя история кликов и график
    bool running = false, pending = false, allowed = false, hold = false;
    bool starting = false;        // идёт «старт через N мс»
    int pauseReason = 0;          // 1 — не окно игры, 2 — курсор не над игрой
    unsigned long long c1 = 0, c2 = 0, total = 0;
    QString a1, a2, game, profile;
};

// Данные для оверлея: по строке на каждую работающую игру, если у её профиля
// включено «Показывать в оверлее». Первая — игра, открытая в AutoQ.
struct OverlayData {
    bool hasGame = false;
    QList<OverlayEntry> entries;
    bool anyActive() const;       // хоть одна работает или ждёт окно
};

namespace OverlayPaint {
    QSize size(const OverlaySettings& s, const OverlayView& v);
    void paint(QPainter& p, const QPointF& tl, const OverlaySettings& s, const OverlayView& v,
               bool moveHint = false);
    // несколько игр в одной плашке: по строке на каждую, колонки выровнены
    QSize size(const OverlaySettings& s, const QList<OverlayView>& views);
    void paint(QPainter& p, const QPointF& tl, const OverlaySettings& s, const QList<OverlayView>& views,
               bool moveHint = false);
    OverlayView sample();         // пример для образца в настройках
}

class OverlayWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverlayWindow(std::function<OverlayData()> source);
    void setSettings(const OverlaySettings& s);
    const OverlaySettings& settings() const { return s_; }
    void setMoveMode(bool on);
    bool moveMode() const { return move_; }
    void refresh();               // пересчитать видимость сейчас

signals:
    void moved(int screen, int offsetX, int offsetY);   // перетащили мышью

protected:
    void paintEvent(QPaintEvent*) override;
    void timerEvent(QTimerEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void applyFlags();
    void refresh(const OverlayData& d);
    void tick();
    void relayout();
    QRect screenRect() const;
    std::function<OverlayData()> source_;
    OverlaySettings s_;
    QList<OverlayView> views_;    // что рисуем сейчас — по строке на игру
    struct Sample { qint64 t; unsigned long long c1, c2; };
    struct Track {                // своя история у каждой игры
        std::deque<Sample> hist;
        QVector<double> graph;
        double cps = 0, cps1 = 0, cps2 = 0;
        qint64 seen = 0;
    };
    std::map<QString, Track> tracks_;
    qint64 lastGraph_ = 0;
    bool move_ = false;
    bool dragging_ = false;
    QPoint dragOffset_;
    int timer_ = 0;
    int raiseTick_ = 0;
};

// Образец оверлея поверх «игры» — в окне настроек
class OverlayPreview : public QWidget {
public:
    explicit OverlayPreview(QWidget* parent = nullptr);
    void setSettings(const OverlaySettings& s) { s_ = s; update(); }
    QSize sizeHint() const override { return QSize(440, 248); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    OverlaySettings s_;
};
