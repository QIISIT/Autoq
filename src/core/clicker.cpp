#include "core/clicker.h"
#include "core/keys.h"
#include "core/framesync.h"
#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>

// монотонное время, мс (+1 — чтобы 0 значило «ещё не запускали»)
static long long steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}

// Нажать (отпускают через release). С привязкой — только если клавиатура у окна игры;
// нет — ещё и allowedNow_ = false
bool Clicker::press(const ActionStep& a, const ClickerConfig& cfg) {
    if (!cfg.lockToWindow) return input_.send(a, true);
    bool focused = true;
    const bool sent = input_.pressIn(a, activeWin_, focused);
    if (!focused) { pauseReason_ = 1; allowedNow_ = false; }
    return sent;
}

void Clicker::release(const ActionStep& a) { input_.send(a, false); }

Clicker::Clicker() : probe_(std::make_unique<ActiveWindowProbe>()) {}

Clicker::~Clicker() { stop(); }

bool Clicker::inputAvailable() const { return input_.available(); }

bool Clicker::sendOnce(const ActionStep& a) {
    InputSender s;
    return s.send(a, true) && s.send(a, false);
}

void Clicker::sleepWhileRunning(int ms) {
    if (ms <= 0) return;
    // кусочками: «Стоп» срабатывает сразу, даже при длинной паузе
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= until) break;
        std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(until - now, std::chrono::milliseconds(20)));
    }
}

// «В такт с монитором» и доли кадра: обычный sleep в Windows опаздывает на 1–2 мс
void Clicker::sleepUntilRunning(std::chrono::steady_clock::time_point until) {
    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= until) break;
        const auto d = std::min<std::chrono::steady_clock::duration>(until - now, std::chrono::milliseconds(20));
        if (timer_) timer_->sleepFor(d);
        else std::this_thread::sleep_for(d);
    }
}

// «Зажать»: клавиши уже зажаты, ждать нечего — раз в ms проверяем окно игры (ушли —
// отпустить, вернулись — зажать снова). Стоп и смена настроек будят сразу, а не через ms
void Clicker::napHold(std::chrono::milliseconds ms) {
    std::unique_lock<std::mutex> lk(wakeMutex_);
    wake_.wait_for(lk, ms, [this] { return !running_ || configDirty_; });
    configDirty_ = false;
}

// Пауза, пока что-то зажато: раз в 40 мс проверяем, что всё ещё в игре — ушли
// (другое окно, курсор с окна) — сразу false, и зажатое отпускают, а не держат
// до конца паузы. false — и при стопе.
bool Clicker::waitHeld(int ms, const ClickerConfig& cfg) {
    using clock = std::chrono::steady_clock;
    const auto until = clock::now() + std::chrono::milliseconds(std::max(0, ms));
    auto check = clock::now() + std::chrono::milliseconds(40);
    while (running_) {
        const auto now = clock::now();
        if (now >= until) return true;
        if (cfg.lockToWindow && now >= check) {
            if (!allowedFor(cfg)) { allowedNow_ = false; return false; }
            check = now + std::chrono::milliseconds(40);
        }
        std::this_thread::sleep_for(std::min<clock::duration>(until - now, std::chrono::milliseconds(20)));
    }
    return false;
}

// «По кадрам»: ждём, пока кадров игры станет target, — кусочками по 20 мс, чтобы «Стоп» срабатывал сразу
bool Clicker::waitFrameCount(uint64_t target, const ClickerConfig& cfg, bool held) {
    using clock = std::chrono::steady_clock;
    FrameWatch::Feed* f = cfg.frames.get();
    const auto since = clock::now();
    auto check = since + std::chrono::milliseconds(40);
    while (running_) {
        if (f && f->waitUntil(target, std::chrono::milliseconds(20))) { noFrames_ = false; return true; }
        if (!f) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto now = clock::now();
        // кадров нет дольше 0,3 с — игра свёрнута, на паузе, грузится или нет прав: ждём, не жмём;
        // зажатое — отпустить (фриз, загрузка — не держать клавишу, пока кадров нет)
        const auto last = f ? std::max(f->lastFrame(), since) : since;
        if (now - last > std::chrono::milliseconds(300)) {
            noFrames_ = true;
            allowedNow_ = false;
            if (held || framesCfgChanged_.exchange(false)) return false;   // другое окно или режим — взять новые настройки
        }
        if (held && cfg.lockToWindow && now >= check) {
            if (!allowedFor(cfg)) { allowedNow_ = false; return false; }
            check = now + std::chrono::milliseconds(40);
        }
    }
    return false;
}

bool Clicker::alignFrame(const ClickerConfig& cfg) {
    FrameWatch::Feed* f = cfg.frames.get();
    framePhase_ = 0;
    if (!waitFrameCount(f ? f->count() + 1 : 1, cfg, false)) return false;
    frameBase_ = f ? f->count() : 0;
    return true;
}

bool Clicker::waitFrames(int n, const ClickerConfig& cfg, bool held) {
    using clock = std::chrono::steady_clock;
    if (n <= 0) return running_;
    FrameWatch::Feed* f = cfg.frames.get();
    const int total = framePhase_ + n;
    int part = n;                                // в том же кадре — ещё n десятых
    if (total >= kFrameUnit) {
        // идём с середины кадра — его конец и есть первый из целых кадров
        const uint64_t from = f ? (framePhase_ > 0 ? frameBase_ : f->count()) : 0;
        framePhase_ = 0;
        if (!waitFrameCount(from + static_cast<uint64_t>(total / kFrameUnit), cfg, held)) return false;
        frameBase_ = f ? f->count() : 0;
        part = total % kFrameUnit;
    }
    if (part == 0) return true;
    // дробь — доля длительности кадра сейчас (по последним кадрам, не по среднему: просадка
    // FPS — и доля длиннее). Следующий кадр пришёл раньше (кадр вышел короче) — момент уже
    // прошёл: жмём сразу, а счёт кадров дальше — от того же места (полкадра не теряются)
    framePhase_ += part;
    clock::duration frame = f ? f->frameTime() : clock::duration{};
    if (frame <= clock::duration{}) frame = std::chrono::microseconds(16667);   // кадров не видно — как 60 к/с
    const auto until = clock::now() + frame * part / kFrameUnit;
    if (!f) { sleepUntilRunning(until); return running_; }
    while (running_) {
        const auto now = clock::now();
        if (now >= until) return true;
        if (f->waitUntil(frameBase_ + 1, std::min<clock::duration>(until - now, std::chrono::milliseconds(20)))) return true;
    }
    return false;
}

void Clicker::pause(int n, const ClickerConfig& cfg) {
    if (cfg.byFrames) waitFrames(n, cfg, false);
    else sleepWhileRunning(n);
}

bool Clicker::holdFor(int n, const ClickerConfig& cfg) {
    return cfg.byFrames ? waitFrames(n, cfg, true) : waitHeld(n, cfg);
}

void Clicker::countAction(int i) {
    counts_[static_cast<size_t>(std::min(i, kCounters - 1))] += 1;
}

// «По шагам»: каждый шаг — только нажать или только отпустить. Что шаги нажали и не
// отпустили к концу круга (или круг прервался: стоп, ушли из игры) — отпускаем сами,
// чтобы ничего не осталось зажатым. «Отпустить» без «нажать» — пропускается: иначе
// отпустилась бы кнопка, которую держит сам игрок.
bool Clicker::doSteps(const ClickerConfig& cfg) {
    const int n = cfg.actions.size();
    bool whole = true;
    const QList<ActionStep> inputs = Steps::pressedInputs(cfg.actions);
    std::vector<ActionStep> down;
    auto heldAt = [&down](const ActionStep& a) {
        for (size_t j = 0; j < down.size(); ++j) if (down[j].sameInput(a)) return static_cast<int>(j);
        return -1;
    };
    for (int i = 0; i < n; ++i) {
        // за паузу могли нажать «Стоп» или переключиться из игры
        if (!running_ || (i > 0 && !allowedFor(cfg))) { if (running_) allowedNow_ = false; whole = false; break; }
        currentStep_ = i;
        const ActionStep& a = cfg.actions[i];
        const int k = heldAt(a);
        if (a.press == ActionStep::Up) {
            if (k >= 0) {
                release(a);
                down.erase(down.begin() + k);
            }
        } else if (a.press == ActionStep::Click) {
            // нажать и сразу отпустить (зажатую раньше — тоже: после шага она отпущена).
            // Не нажалось — зажатая остаётся в down: её отпустят в конце круга
            if (press(a, cfg)) {
                release(a);
                if (k >= 0) down.erase(down.begin() + k);
                const int idx = Steps::inputIndex(inputs, a);
                if (idx >= 0) countAction(idx);
                clickCount_ += 1;
            }
        } else if (k < 0 && press(a, cfg)) {
            down.push_back(a);
            const int idx = Steps::inputIndex(inputs, a);
            if (idx >= 0) countAction(idx);
            clickCount_ += 1;
        }
        if (!allowedNow_) { whole = false; break; }      // клавиатура уже не у игры
        if (i + 1 < n) {
            // что-то зажато — пауза с проверкой окна: ушли из игры — отпускаем сразу
            if (down.empty()) pause(a.afterMs, cfg);
            else if (!holdFor(a.afterMs, cfg)) { whole = false; break; }
        }
    }
    for (auto it = down.rbegin(); it != down.rend(); ++it) release(*it);
    currentStep_ = -1;
    return whole && running_;
}

void Clicker::doCycle(const ClickerConfig& cfg) {
    const int n = cfg.actions.size();
    if (n == 0) return;
    // круг засчитываем, когда он прошёл целиком (стоп или уход из игры посреди круга — не в счёт)
    if (cfg.steps) {
        if (doSteps(cfg)) cycles_ += 1;
        return;
    }
    if (cfg.simultaneous && n > 1) {
        // комбинация: зажать все, подержать, отпустить в обратном порядке (как F3+A)
        int holdMs = 0, pressed = 0;
        for (const ActionStep& a : cfg.actions) {
            if (!press(a, cfg) && !allowedNow_) break;    // клавиатура уже не у игры
            ++pressed;
            holdMs = std::max(holdMs, a.holdMs);
        }
        const bool stayed = pressed == n && holdFor(holdMs, cfg);    // ушли из игры — отпускаем, не дожидаясь конца
        for (int i = pressed - 1; i >= 0; --i) release(cfg.actions[i]);
        if (pressed < n) return;
        clickCount_ += 1;
        for (int i = 0; i < n; ++i) countAction(i);
        if (stayed) cycles_ += 1;
        return;
    }
    bool whole = true;
    for (int i = 0; i < n; ++i) {
        // за паузу могли переключиться из игры — следующее действие только в окно игры
        if (i > 0 && (!running_ || !allowedFor(cfg))) { allowedNow_ = false; whole = false; break; }
        currentStep_ = i;
        const ActionStep& a = cfg.actions[i];
        if (!press(a, cfg) && !allowedNow_) { whole = false; break; }   // клавиатура уже не у игры
        // «держать» — при стопе или уходе из игры отпускаем сразу
        const bool stayed = a.holdMs <= 0 || holdFor(a.holdMs, cfg);
        release(a);
        countAction(i);
        clickCount_ += 1;
        if (!stayed) { whole = false; break; }
        if (i + 1 < n) pause(a.afterMs, cfg);
    }
    currentStep_ = -1;
    if (whole && running_) cycles_ += 1;
}

bool Clicker::allowedFor(const ClickerConfig& cfg) {
    pauseReason_ = 0;
    if (!cfg.lockToWindow) return true;
    const WindowInfo w = probe_->query();
    if (!clickAllowed(cfg, w)) { pauseReason_ = 1; return false; }
    activeWin_ = w.handle;
    // кнопкой мыши — только когда курсор над окном игры (иначе клик ушёл бы
    // в соседнее окно или на другой монитор и забрал бы фокус у игры)
    bool anyMouse = false;
    for (const ActionStep& a : cfg.actions) anyMouse = anyMouse || a.mouse;
    const bool mouseUsed = cfg.mouseOnlyOverGame && anyMouse;
    if (!(cfg.boundHandle && mouseUsed)) return true;
    // Курсор «ушёл» с игры — пауза не сразу, а если так ~0,3 с подряд: под
    // XWayland и в играх, ловящих мышь, проверка иногда ошибается на миг
    const auto now = std::chrono::steady_clock::now();
    if (probe_->pointerOver(cfg.boundHandle)) { pointerAwaySince_ = {}; return true; }
    if (pointerAwaySince_ == std::chrono::steady_clock::time_point{}) pointerAwaySince_ = now;
    if (now - pointerAwaySince_ < std::chrono::milliseconds(250)) return true;
    pauseReason_ = 2;
    return false;
}

bool clickAllowed(const ClickerConfig& cfg, const WindowInfo& w) {
    if (!cfg.lockToWindow) return true;
    if (!cfg.boundHandle || !w.valid) return false;      // окно не выбрано — не жмём нигде
    if (w.handle == cfg.boundHandle) return true;
    // то же приложение игры, но другое его окно (лаунчер открыл новое) — по правилам
    return cfg.boundPid > 0 && w.pid == cfg.boundPid && matchesRule(cfg.rule, w);
}

static bool sameAction(const ActionStep& a, const ActionStep& b) {
    return a.mouse == b.mouse && (a.mouse ? a.button == b.button
                                          : a.key.compare(b.key, Qt::CaseInsensitive) == 0);
}

void Clicker::releaseHeld() {
    for (auto it = held_.rbegin(); it != held_.rend(); ++it) release(*it);
    held_.clear();
}

void Clicker::loop() {
    std::mt19937 rng(std::random_device{}());
    FrameSync::Grid grid;
    timer_ = std::make_unique<PreciseTimer>();
    // «старт через N мс» (или N кадров): первый цикл — не сразу после нажатия
    ClickerConfig first;
    { std::lock_guard<std::mutex> lk(configMutex_); first = config_; }
    bool aligned = false;                 // «По кадрам»: круг начнётся сразу после кадра игры
    if (first.startDelayMs > 0) {
        waitingStart_ = true;
        // «По кадрам»: от следующего кадра — N кадров (и дробь): с этого места первый круг
        if (first.byFrames && !first.hold) aligned = alignFrame(first) && waitFrames(first.startDelayMs, first, false);
        else sleepWhileRunning(first.startDelayMs);
        waitingStart_ = false;
    }
    while (running_) {
        ClickerConfig cfg;
        { std::lock_guard<std::mutex> lk(configMutex_); cfg = config_; }

        // «По кадрам»: круг — сразу после кадра игры, «держать» и паузы — кадрами (и долями кадра)
        if (!cfg.hold && cfg.byFrames) {
            if (!held_.empty()) releaseHeld();
            grid.reset();
            if (!aligned) { aligned = alignFrame(cfg); continue; }
            FrameWatch::Feed* f = cfg.frames.get();
            const uint64_t c0 = framePhase_ > 0 ? frameBase_ : f ? f->count() : 0;
            const bool allowed = allowedFor(cfg);
            allowedNow_ = allowed;
            if (allowed) doCycle(cfg);
            int wait = cfg.intervalMs;
            if (cfg.randomize && cfg.jitterMs > 0) {
                std::uniform_int_distribution<int> dist(-cfg.jitterMs, cfg.jitterMs);
                wait = std::max(cfg.intervalMs > 0 ? 1 : 0, wait + dist(rng));
            }
            bool ok = allowedNow_ && waitFrames(wait, cfg, false);
            // круг целиком — не короче minCycleMs (хотя бы кадр: паузы между клавишами могут быть 0)
            const uint64_t need = c0 + static_cast<uint64_t>(std::max(1, (cfg.minCycleMs + kFrameUnit - 1) / kFrameUnit));
            if (ok && f && f->count() < need) {
                framePhase_ = 0;
                ok = waitFrameCount(need, cfg, false);
                frameBase_ = f->count();
            }
            if (!ok) aligned = false;     // вышли посреди круга — следующий снова с кадра
            continue;
        }
        aligned = false;
        noFrames_ = false;                // «По кадрам» выключили, пока кадров не было

        // «В такт с монитором»: цикл начинается ровно на своём кадре
        if (!cfg.hold && cfg.frameMs > 0) {
            if (!held_.empty()) releaseHeld();
            sleepUntilRunning(grid.next(cfg.frameMs, std::chrono::steady_clock::now()));
            if (!running_) break;
            const bool allowed = allowedFor(cfg);
            allowedNow_ = allowed;
            if (allowed) doCycle(cfg);
            int n = FrameSync::frames(FrameSync::cycleMs(cfg.actions, cfg.intervalMs, cfg.simultaneous), cfg.frameMs);
            if (cfg.randomize && cfg.jitterMs > 0) {
                const int j = static_cast<int>(std::lround(cfg.jitterMs / cfg.frameMs));   // разброс — целыми кадрами
                if (j > 0) n += std::uniform_int_distribution<int>(-j, j)(rng);
            }
            grid.advance(std::max(n, FrameSync::frames(cfg.minCycleMs, cfg.frameMs)));
            continue;
        }
        grid.reset();

        const bool allowed = allowedFor(cfg);
        allowedNow_ = allowed;

        if (cfg.hold) {
            // Зажать: держим, пока активна игра; ушли в другое окно — отпускаем
            std::vector<ActionStep> want;
            if (allowed)
                for (const ActionStep& a : cfg.actions) {
                    bool dup = false;
                    for (const ActionStep& w : want) dup = dup || sameAction(w, a);
                    if (!dup) want.push_back(a);
                }
            bool changed = want.size() != held_.size();
            for (size_t i = 0; !changed && i < want.size(); ++i) changed = !sameAction(want[i], held_[i]);
            if (changed) {
                releaseHeld();
                for (const ActionStep& a : want)
                    if (press(a, cfg)) held_.push_back(a);
                if (!held_.empty()) {
                    clickCount_ += 1;
                    for (int i = 0; i < static_cast<int>(held_.size()); ++i) countAction(i);
                }
            }
            napHold(std::chrono::milliseconds(40));
            continue;
        }
        if (!held_.empty()) releaseHeld();   // режим сменили на ходу

        const auto cycleStart = std::chrono::steady_clock::now();
        if (allowed) doCycle(cfg);

        int wait = cfg.intervalMs;
        if (cfg.randomize && cfg.jitterMs > 0) {
            std::uniform_int_distribution<int> dist(-cfg.jitterMs, cfg.jitterMs);
            wait = std::max(cfg.intervalMs > 0 ? 1 : 0, wait + dist(rng));
        }
        // ждём кусочками, чтобы "Стоп" срабатывал сразу даже при большом интервале;
        // круг целиком — не быстрее minCycleMs (паузы между клавишами могут быть 0)
        auto until = std::max(std::chrono::steady_clock::now() + std::chrono::milliseconds(wait),
                              cycleStart + std::chrono::milliseconds(cfg.minCycleMs));
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            if (now >= until) break;
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - now);
            std::this_thread::sleep_for(std::min(left, std::chrono::milliseconds(20)));
        }
    }
    releaseHeld();   // стоп — ничего не остаётся зажатым
    timer_.reset();
}

void Clicker::start() {
    if (running_) return;
    if (worker_.joinable()) worker_.join();
    clickCount_ = 0;
    cycles_ = 0;
    for (auto& c : counts_) c = 0;
    allowedNow_ = false;
    pauseReason_ = 0;
    noFrames_ = false;
    framesCfgChanged_ = false;
    waitingStart_ = false;
    currentStep_ = -1;
    startMs_ = steadyMs();
    stopMs_ = 0;
    running_ = true;
    worker_ = std::thread(&Clicker::loop, this);
}

void Clicker::stop() {
    if (running_.exchange(false)) stopMs_ = steadyMs();
    { std::lock_guard<std::mutex> lk(wakeMutex_); }   // поток ждёт в napHold — разбудить
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

long long Clicker::runMs() const {
    const long long from = startMs_;
    if (!from) return 0;
    const long long to = running_ ? steadyMs() : stopMs_.load();
    return to > from ? to - from : 0;
}

void Clicker::setConfig(const ClickerConfig& cfg) {
    {
        std::lock_guard<std::mutex> lk(configMutex_);
        config_ = cfg;
    }
    framesCfgChanged_ = true;
    {
        std::lock_guard<std::mutex> lk(wakeMutex_);
        configDirty_ = true;
    }
    wake_.notify_all();
}
