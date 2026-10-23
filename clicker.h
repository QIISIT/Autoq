#pragma once
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <memory>
#include <vector>
#include <array>
#include <QString>
#include <QList>
#include "winmatch.h"
#include "games.h"

struct ClickerConfig {
    QList<ActionStep> actions{ActionStep{}};   // действия цикла (у каждого — «держать» и «пауза после»)
    bool simultaneous = false;   // true — зажать все сразу (комбинация), false — по очереди
    bool hold = false;           // зажать и держать, пока кликер работает
    bool steps = false;          // «По шагам»: actions — шаги «нажать» / «отпустить»
    int startDelayMs = 0;        // первый цикл — через N мс после старта
    int intervalMs = 100;        // пауза между циклами
    int minCycleMs = 0;          // круг целиком (от начала до начала следующего) — не быстрее
    bool randomize = false;
    int jitterMs = 30;
    bool lockToWindow = false;   // кликать только в выбранном окне игры
    WindowRule rule;
    // выбранное окно игры: без него при привязке кликер не нажимает ничего
    unsigned long long boundHandle = 0;
    long long boundPid = 0;
    bool mouseOnlyOverGame = true;    // мышью — только когда курсор над окном игры
    // «В такт с монитором»: длительность кадра, мс (0 — выключено). Циклы начинаются
    // ровно на кадрах, через целое число кадров; разброс — тоже целыми кадрами
    double frameMs = 0;
};

// Можно ли нажимать в этом окне (общая проверка для кликера и интерфейса)
bool clickAllowed(const ClickerConfig& cfg, const WindowInfo& w);

class Clicker {
public:
    Clicker();
    ~Clicker();
    void start();
    void stop();
    bool isRunning() const { return running_; }
    void setConfig(const ClickerConfig& cfg);
    unsigned long long clickCount() const { return clickCount_; }
    // нажатия по действиям (для оверлея и подсказок): 0 — действие 1, 1 — действие 2…
    unsigned long long actionCount(int i) const {
        return (i >= 0 && i < kCounters) ? counts_[static_cast<size_t>(i)].load() : 0;
    }
    static constexpr int kCounters = 16;     // дальше 16-го считаются вместе с последним
    bool allowedNow() const { return allowedNow_; }     // окно подходит — кликер жмёт
    bool waitingStart() const { return waitingStart_; } // идёт «старт через N мс»
    int currentStep() const { return currentStep_; }    // шаг/действие, что идёт сейчас (-1 — пауза между циклами)
    unsigned long long cycleCount() const { return cycles_; }   // сколько кругов прошло целиком
    long long runMs() const;              // сколько идёт запуск, мс (после стопа — сколько шёл последний)
    int pauseReason() const { return pauseReason_; }     // 0 — нет, 1 — не окно игры, 2 — курсор вне игры
    bool inputAvailable() const;          // есть ли куда слать ввод (X11 на Linux, разрешение на macOS)

    // Отправить одно действие прямо сейчас (для тестов). Возвращает false,
    // если клавиша неизвестна или нет доступа к вводу.
    static bool sendOnce(const ActionStep& a);

private:
    void loop();
    void doCycle(const ClickerConfig& cfg);
    bool doSteps(const ClickerConfig& cfg);      // true — круг прошёл целиком
    bool allowedFor(const ClickerConfig& cfg);   // можно ли жать прямо сейчас (окно, курсор)
    void releaseHeld();
    void sleepWhileRunning(int ms);
    bool waitHeld(int ms, const ClickerConfig& cfg);   // пауза с зажатым: false — стоп или ушли из игры
    void sleepUntilRunning(std::chrono::steady_clock::time_point until);   // точно, до долей мс
    void countAction(int i);
    std::vector<ActionStep> held_;        // что сейчас зажато (режим "Зажать")
    std::atomic<bool> running_{false};
    std::atomic<unsigned long long> clickCount_{0};
    std::array<std::atomic<unsigned long long>, kCounters> counts_{};
    std::atomic<bool> allowedNow_{false};
    std::atomic<bool> waitingStart_{false};
    std::atomic<int> currentStep_{-1};
    std::atomic<unsigned long long> cycles_{0};
    std::atomic<long long> startMs_{0}, stopMs_{0};
    std::atomic<int> pauseReason_{0};
    std::thread worker_;
    ClickerConfig config_;
    std::mutex configMutex_;
    void* display_ = nullptr;             // Display* на Linux, CGEventSourceRef на macOS
    void* timer_ = nullptr;               // Windows: таймер высокой точности потока кликера
    std::unique_ptr<ActiveWindowProbe> probe_;
    std::chrono::steady_clock::time_point pointerAwaySince_{};   // только поток кликера
};
