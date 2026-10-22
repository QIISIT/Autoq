#include "clicker.h"
#include "keys.h"
#include "framesync.h"
#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX            // иначе макросы min/max ломают std::max
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <mmsystem.h>       // timeBeginPeriod — точный таймер для 5 мс
#elif defined(__APPLE__)
    #include <ApplicationServices/ApplicationServices.h>
    #include "macsupport.h"
#else
    #include <X11/Xlib.h>
    #include <X11/extensions/XTest.h>
#endif

// монотонное время, мс (+1 — чтобы 0 значило «ещё не запускали»)
static long long steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}

// ---------------------------------------------------------------------------
// Низкоуровневая отправка: нажать / отпустить одно действие
// ---------------------------------------------------------------------------
#ifdef _WIN32
// Скан-код клавиши. Для цифрового блока MapVirtualKey возвращает 0 (эти клавиши
// делят скан-коды со стрелками/Home/End), а без скан-кода игры с Raw Input
// нажатие не видят — поэтому для них берём коды из таблицы (набор 1).
static WORD scanCodeFor(unsigned short vk) {
    static const WORD numpad[10] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};
    if (vk >= 0x60 && vk <= 0x69) return numpad[vk - 0x60];
    if (vk == 0x6E) return 0x53;      // Num .
    if (vk == 0x2C) return 0x37;      // PrintScreen (E0 37)
    return static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
}

static bool sendAction(void*, const ActionStep& a, bool down) {
    INPUT in = {};
    if (a.mouse) {
        in.type = INPUT_MOUSE;
        switch (a.button) {
            case 3:  in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;  break;
            case 2:  in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
            case 8:  in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; in.mi.mouseData = XBUTTON1; break;
            case 9:  in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; in.mi.mouseData = XBUTTON2; break;
            default: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;   break;
        }
        return SendInput(1, &in, sizeof(INPUT)) == 1;
    }
    const KeyDef* k = Keys::byName(a.key);
    if (!k) return false;
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = k->winVK;
    // Скан-код обязателен: игры читают ввод через Raw Input/DirectInput
    in.ki.wScan = scanCodeFor(k->winVK);
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (k->winExtended ? KEYEVENTF_EXTENDEDKEY : 0);
    return SendInput(1, &in, sizeof(INPUT)) == 1;
}
#elif defined(__APPLE__)
// macOS: события Quartz (CGEvent) в системную очередь ввода. Без разрешения
// «Универсальный доступ» macOS их молча выбрасывает.
static bool sendAction(void* src, const ActionStep& a, bool down) {
    CGEventSourceRef source = static_cast<CGEventSourceRef>(src);
    CGEventRef e = nullptr;
    if (a.mouse) {
        CGEventRef where = CGEventCreate(nullptr);
        const CGPoint pt = where ? CGEventGetLocation(where) : CGPointZero;
        if (where) CFRelease(where);
        CGEventType type;
        CGMouseButton btn;
        switch (a.button) {
            case 3:  type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp; btn = kCGMouseButtonRight; break;
            case 2:  type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; btn = kCGMouseButtonCenter; break;
            // боковые: у macOS номера 3 и 4
            case 8:  type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; btn = static_cast<CGMouseButton>(3); break;
            case 9:  type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; btn = static_cast<CGMouseButton>(4); break;
            default: type = down ? kCGEventLeftMouseDown  : kCGEventLeftMouseUp;  btn = kCGMouseButtonLeft;  break;
        }
        e = CGEventCreateMouseEvent(source, type, pt, btn);
        if (!e) return false;
        if (a.button == 8 || a.button == 9)
            CGEventSetIntegerValueField(e, kCGMouseEventButtonNumber, a.button == 8 ? 3 : 4);
        // каждый клик — одиночный (иначе быстрые клики macOS считает двойными)
        CGEventSetIntegerValueField(e, kCGMouseEventClickState, 1);
    } else {
        const int code = Keys::macKeyCode(a.key);
        if (code < 0) return false;
        e = CGEventCreateKeyboardEvent(source, static_cast<CGKeyCode>(code), down);
        if (!e) return false;
        // какие модификаторы зажал сам кликер (шаги «Shift↓, F, Shift↑») — у каждого
        // потока кликера свои; остальные клавиши идут с ними, а не с «залипшими» чужими
        thread_local CGEventFlags held = 0;
        const QString k = a.key.toLower();
        if (k == "shift" || k == "ctrl" || k == "alt") {
            // модификатор — событие «изменились флаги», иначе игра его не увидит
            const CGEventFlags flag = k == "shift" ? kCGEventFlagMaskShift
                                    : k == "ctrl"  ? kCGEventFlagMaskControl
                                                   : kCGEventFlagMaskAlternate;
            held = down ? (held | flag) : (held & ~flag);
            CGEventSetType(e, kCGEventFlagsChanged);
            CGEventSetFlags(e, held);
        } else {
            CGEventSetFlags(e, held);
        }
    }
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
    return true;
}
#else
static bool sendAction(void* disp, const ActionStep& a, bool down) {
    Display* d = static_cast<Display*>(disp);
    if (!d) return false;
    if (a.mouse) {
        XTestFakeButtonEvent(d, static_cast<unsigned>(a.button), down ? True : False, CurrentTime);
    } else {
        const KeyDef* k = Keys::byName(a.key);
        if (!k) return false;
        KeyCode kc = XKeysymToKeycode(d, k->x11Sym);
        if (!kc) return false;
        XTestFakeKeyEvent(d, kc, down ? True : False, CurrentTime);
    }
    XFlush(d);
    return true;
}
#endif

// ---------------------------------------------------------------------------
Clicker::Clicker() {
#if defined(__APPLE__)
    display_ = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
#elif !defined(_WIN32)
    display_ = XOpenDisplay(nullptr);
#endif
    probe_ = std::make_unique<ActiveWindowProbe>();
}

Clicker::~Clicker() {
    stop();
#if defined(__APPLE__)
    if (display_) CFRelease(static_cast<CGEventSourceRef>(display_));
#elif !defined(_WIN32)
    if (display_) XCloseDisplay(static_cast<Display*>(display_));
#endif
}

bool Clicker::inputAvailable() const {
#ifdef _WIN32
    return true;
#elif defined(__APPLE__)
    return AqMac::accessibilityTrusted(false);
#else
    return display_ != nullptr;
#endif
}

bool Clicker::sendOnce(const ActionStep& a) {
#ifdef _WIN32
    return sendAction(nullptr, a, true) && sendAction(nullptr, a, false);
#elif defined(__APPLE__)
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    const bool ok = sendAction(src, a, true) && sendAction(src, a, false);
    if (src) CFRelease(src);
    return ok;
#else
    Display* d = XOpenDisplay(nullptr);
    if (!d) return false;
    bool ok = sendAction(d, a, true) && sendAction(d, a, false);
    XSync(d, False);
    XCloseDisplay(d);
    return ok;
#endif
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

#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

// Точное ожидание для «В такт с монитором». Windows: таймер высокой точности
// (Windows 10 1803+, ~0,5 мс) — обычный sleep опаздывает на 1–2 мс, а при 240 Гц
// кадр всего 4 мс. Таймер свой у потока кликера (timer_).
void Clicker::sleepUntilRunning(std::chrono::steady_clock::time_point until) {
    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= until) break;
        const auto d = std::min<std::chrono::steady_clock::duration>(until - now, std::chrono::milliseconds(20));
#ifdef _WIN32
        LARGE_INTEGER due;
        due.QuadPart = -std::max<long long>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(d).count() / 100);
        if (timer_ && SetWaitableTimer(static_cast<HANDLE>(timer_), &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(static_cast<HANDLE>(timer_), 100);
            continue;
        }
#endif
        std::this_thread::sleep_for(d);
    }
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

void Clicker::countAction(int i) {
    counts_[static_cast<size_t>(std::min(i, kCounters - 1))] += 1;
}

// «По шагам»: каждый шаг — только нажать или только отпустить. Что шаги нажали и не
// отпустили к концу круга (или круг прервался: стоп, ушли из игры) — отпускаем сами,
// чтобы ничего не осталось зажатым. «Отпустить» без «нажать» — пропускается: иначе
// отпустилась бы кнопка, которую держит сам игрок.
void Clicker::doSteps(const ClickerConfig& cfg) {
    const int n = cfg.actions.size();
    const QList<ActionStep> inputs = Steps::pressedInputs(cfg.actions);
    std::vector<ActionStep> down;
    auto heldAt = [&down](const ActionStep& a) {
        for (size_t j = 0; j < down.size(); ++j) if (down[j].sameInput(a)) return static_cast<int>(j);
        return -1;
    };
    for (int i = 0; i < n; ++i) {
        // за паузу могли нажать «Стоп» или переключиться из игры
        if (!running_ || (i > 0 && !allowedFor(cfg))) { if (running_) allowedNow_ = false; break; }
        currentStep_ = i;
        const ActionStep& a = cfg.actions[i];
        const int k = heldAt(a);
        if (a.press == ActionStep::Up) {
            if (k >= 0) {
                sendAction(display_, a, false);
                down.erase(down.begin() + k);
            }
        } else if (k < 0 && sendAction(display_, a, true)) {
            down.push_back(a);
            const int idx = Steps::inputIndex(inputs, a);
            if (idx >= 0) countAction(idx);
            clickCount_ += 1;
        }
        if (i + 1 < n) {
            // что-то зажато — пауза с проверкой окна: ушли из игры — отпускаем сразу
            if (down.empty()) sleepWhileRunning(a.afterMs);
            else if (!waitHeld(a.afterMs, cfg)) break;
        }
    }
    for (auto it = down.rbegin(); it != down.rend(); ++it) sendAction(display_, *it, false);
    currentStep_ = -1;
}

void Clicker::doCycle(const ClickerConfig& cfg) {
    const int n = cfg.actions.size();
    if (n == 0) return;
    cycles_ += 1;
    if (cfg.steps) { doSteps(cfg); return; }
    if (cfg.simultaneous && n > 1) {
        // комбинация: зажать все, подержать, отпустить в обратном порядке (как F3+A)
        int holdMs = 0;
        for (const ActionStep& a : cfg.actions) {
            sendAction(display_, a, true);
            holdMs = std::max(holdMs, a.holdMs);
        }
        waitHeld(holdMs, cfg);                 // ушли из игры — отпускаем, не дожидаясь конца
        for (int i = n - 1; i >= 0; --i) sendAction(display_, cfg.actions[i], false);
        clickCount_ += 1;
        for (int i = 0; i < n; ++i) countAction(i);
        return;
    }
    for (int i = 0; i < n; ++i) {
        // за паузу могли переключиться из игры — следующее действие только в окно игры
        if (i > 0 && (!running_ || !allowedFor(cfg))) { allowedNow_ = false; break; }
        currentStep_ = i;
        const ActionStep& a = cfg.actions[i];
        sendAction(display_, a, true);
        // «держать» — при стопе или уходе из игры отпускаем сразу
        const bool stayed = a.holdMs <= 0 || waitHeld(a.holdMs, cfg);
        sendAction(display_, a, false);
        countAction(i);
        clickCount_ += 1;
        if (!stayed) break;
        if (i + 1 < n) sleepWhileRunning(a.afterMs);
    }
    currentStep_ = -1;
}

bool Clicker::allowedFor(const ClickerConfig& cfg) {
    pauseReason_ = 0;
    if (!cfg.lockToWindow) return true;
    if (!clickAllowed(cfg, probe_->query())) { pauseReason_ = 1; return false; }
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
    for (auto it = held_.rbegin(); it != held_.rend(); ++it) sendAction(display_, *it, false);
    held_.clear();
}

void Clicker::loop() {
    std::mt19937 rng(std::random_device{}());
    FrameSync::Grid grid;
#ifdef _WIN32
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);   // Windows постарше
#endif
    // «старт через N мс»: первый цикл — не сразу после нажатия
    int startDelay = 0;
    { std::lock_guard<std::mutex> lk(configMutex_); startDelay = config_.startDelayMs; }
    if (startDelay > 0) {
        waitingStart_ = true;
        sleepWhileRunning(startDelay);
        waitingStart_ = false;
    }
    while (running_) {
        ClickerConfig cfg;
        { std::lock_guard<std::mutex> lk(configMutex_); cfg = config_; }

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
            grid.advance(n);
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
                    if (sendAction(display_, a, true)) held_.push_back(a);
                if (!held_.empty()) {
                    clickCount_ += 1;
                    for (int i = 0; i < static_cast<int>(held_.size()); ++i) countAction(i);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        if (!held_.empty()) releaseHeld();   // режим сменили на ходу

        if (allowed) doCycle(cfg);

        int wait = cfg.intervalMs;
        if (cfg.randomize && cfg.jitterMs > 0) {
            std::uniform_int_distribution<int> dist(-cfg.jitterMs, cfg.jitterMs);
            wait = std::max(1, wait + dist(rng));
        }
        // ждём кусочками, чтобы "Стоп" срабатывал сразу даже при большом интервале
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait);
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            if (now >= until) break;
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - now);
            std::this_thread::sleep_for(std::min(left, std::chrono::milliseconds(50)));
        }
    }
    releaseHeld();   // стоп — ничего не остаётся зажатым
#ifdef _WIN32
    if (timer_) { CloseHandle(static_cast<HANDLE>(timer_)); timer_ = nullptr; }
#endif
}

void Clicker::start() {
    if (running_) return;
    if (worker_.joinable()) worker_.join();
#ifdef _WIN32
    // По умолчанию таймер Windows тикает раз в ~15.6 мс — интервал 5 мс
    // превратился бы в 15. Просим точность 1 мс на время работы кликера.
    timeBeginPeriod(1);
#endif
    clickCount_ = 0;
    cycles_ = 0;
    for (auto& c : counts_) c = 0;
    allowedNow_ = false;
    pauseReason_ = 0;
    waitingStart_ = false;
    currentStep_ = -1;
    startMs_ = steadyMs();
    stopMs_ = 0;
    running_ = true;
    worker_ = std::thread(&Clicker::loop, this);
}

void Clicker::stop() {
    bool wasRunning = running_.exchange(false);
    if (wasRunning) stopMs_ = steadyMs();
    if (worker_.joinable()) worker_.join();
#ifdef _WIN32
    if (wasRunning) timeEndPeriod(1);
#else
    (void)wasRunning;
#endif
}

long long Clicker::runMs() const {
    const long long from = startMs_;
    if (!from) return 0;
    const long long to = running_ ? steadyMs() : stopMs_.load();
    return to > from ? to - from : 0;
}

void Clicker::setConfig(const ClickerConfig& cfg) {
    std::lock_guard<std::mutex> lk(configMutex_);
    config_ = cfg;
}
