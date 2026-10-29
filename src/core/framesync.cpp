#include "core/framesync.h"
#include <cmath>
#include <algorithm>

QList<int> FrameSync::hzChoices() { return {30, 60, 75, 90, 100, 120, 144, 165, 180, 240, 280, 360}; }

double FrameSync::frameMs(double hz) { return hz >= 1 ? 1000.0 / hz : 0; }

double FrameSync::cycleMs(const QList<ActionStep>& actions, int intervalMs, bool simultaneous) {
    double c = std::max(1, intervalMs);
    const int n = actions.size();
    if (simultaneous && n > 1) {
        int hold = 0;
        for (const ActionStep& a : actions) hold = std::max(hold, a.holdMs);
        return c + hold;
    }
    for (int i = 0; i < n; ++i) {
        c += actions[i].holdMs;
        if (i + 1 < n) c += actions[i].afterMs;
    }
    return c;
}

double FrameSync::meanCycleMs(const QList<ActionStep>& actions, int intervalMs, bool simultaneous, int jitterMs,
                              int minCycleMs) {
    const int wait = std::max(1, intervalMs);
    const double busy = cycleMs(actions, intervalMs, simultaneous) - wait;   // «держать» и паузы
    if (jitterMs <= 0) return std::max<double>(minCycleMs, busy + wait);
    double sum = 0;
    for (int u = -jitterMs; u <= jitterMs; ++u) sum += std::max<double>(minCycleMs, busy + std::max(1, intervalMs + u));
    return sum / (2 * jitterMs + 1);
}

double FrameSync::meanCycleFrames(const QList<ActionStep>& actions, int interval, bool simultaneous, int jitter,
                                  int minCycle) {
    const double busy = cycleMs(actions, interval, simultaneous) - std::max(1, interval);   // «держать» и паузы
    const double least = std::max(kFrameUnit, minCycle);
    if (jitter <= 0) return std::max(least, busy + std::max(0, interval)) / kFrameUnit;
    double sum = 0;
    for (int u = -jitter; u <= jitter; ++u)
        sum += std::max(least, busy + std::max(interval > 0 ? 1 : 0, interval + u));
    return sum / (2 * jitter + 1) / kFrameUnit;
}

int FrameSync::frames(double cycle, double frame) {
    if (frame <= 0) return 1;
    return std::max(1, static_cast<int>(std::lround(cycle / frame)));
}

using Clock = std::chrono::steady_clock;
static Clock::duration msDur(double ms) {
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(ms));
}
static double msBetween(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

Clock::time_point FrameSync::Grid::at(long long k) const { return t0_ + msDur(frame_ * double(k)); }

Clock::time_point FrameSync::Grid::next(double frameMs, Clock::time_point now, bool useVblank) {
    if (frameMs != cfgFrame_) {          // новый такт — новая сетка от «сейчас»
        cfgFrame_ = frame_ = frameMs;
        t0_ = now;
        next_ = 0;
        synced_ = {};
    }
    // раз в 2 с сверяемся с монитором: сетка — посередине кадров, дальше всего от их границ
    if (useVblank && (synced_ == Clock::time_point{} || now - synced_ > std::chrono::seconds(2))) {
        synced_ = now;
        double period = 0;
        Clock::time_point vb;
        if (vblank(&period, &vb) && std::abs(period - cfgFrame_) < cfgFrame_ * 0.03) {
            const Clock::time_point want = at(next_);
            frame_ = period;
            t0_ = vb + msDur(period / 2);
            next_ = std::llround(msBetween(t0_, want) / frame_);
        }
    }
    // цикл затянулся (долгое «держать») или кликер ждал игру — ближайший кадр
    if (at(next_) < now) next_ = static_cast<long long>(std::ceil(msBetween(t0_, now) / frame_));
    return at(next_);
}
