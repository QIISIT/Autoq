#pragma once
// «В такт с монитором» (карточка «Скорость»): клики встают на сетку кадров.
// Монитор (и многие игры) обновляются N раз в секунду; клики чаще, чем раз
// в кадр, игра склеивает в один, а клики «мимо такта» ложатся неровно — в один
// кадр два, в соседний ни одного. В такте каждый цикл начинается ровно через
// целое число кадров, поэтому каждый клик попадает в свой кадр.
// Windows: сетка ставится в середину кадра по данным DWM (момент vblank).
#include <QList>
#include <chrono>
#include "games.h"

namespace FrameSync {
    QList<int> hzChoices();                      // 30, 60, 75 … 360 — ручной выбор частоты
    double frameMs(double hz);                   // 144 Гц -> 6,94 мс; 0 — нет такта
    // длительность цикла без такта: интервал + «держать» и паузы действий
    double cycleMs(const QList<ActionStep>& actions, int intervalMs, bool simultaneous);
    // средняя длина цикла, как её отмеряет кликер: пауза = интервал ± разброс (не меньше 1 мс),
    // цикл — не короче minCycleMs. Разброс больше интервала — пауза чаще упирается в 1 мс, и
    // в среднем цикл длиннее интервала
    double meanCycleMs(const QList<ActionStep>& actions, int intervalMs, bool simultaneous, int jitterMs,
                       int minCycleMs);
    int frames(double cycleMs, double frameMs);  // целое число кадров на цикл (не меньше 1)
    // «По кадрам»: средняя длина круга в кадрах игры. В actions «держать» и паузы — кадры
    // (FrameUnits), interval — «повтор через» в кадрах; ожидание = интервал ± разброс (не
    // меньше кадра, если интервал не 0), круг — не короче minCycle кадров (и не короче кадра)
    double meanCycleFrames(const QList<ActionStep>& actions, int interval, bool simultaneous, int jitter,
                           int minCycle);
    // Windows: период и момент последнего обновления экрана (DWM). Иначе false.
    bool vblank(double* periodMs, std::chrono::steady_clock::time_point* at);
    // Windows: частота монитора, на котором окно (HWND); 0 — не знаем
    double monitorHz(unsigned long long windowHandle);
    // Linux (X11): центр окна в пикселях экрана; false — не знаем
    bool windowCenter(unsigned long long windowHandle, int* x, int* y);

    // Сетка кадров кликера: номер кадра -> момент времени. Без накопления ошибки:
    // момент считается от начала сетки, а не от прошлого клика.
    class Grid {
    public:
        using clock = std::chrono::steady_clock;
        void reset() { cfgFrame_ = 0; }
        // когда начать следующий цикл: на кадре, не раньше now. Смена кадра в
        // настройках — новая сетка; useVblank — сверка с монитором (Windows)
        clock::time_point next(double frameMs, clock::time_point now, bool useVblank = true);
        void advance(int frames) { next_ += frames < 1 ? 1 : frames; }   // после цикла
        double frame() const { return frame_; }
        clock::time_point at(long long k) const;
    private:
        double cfgFrame_ = 0;      // кадр из настроек
        double frame_ = 0;         // кадр сетки: из настроек или точный от DWM
        clock::time_point t0_{};
        long long next_ = 0;       // номер кадра следующего цикла
        clock::time_point synced_{};
    };
}
