#pragma once
// Что кликеру даёт ОС. Реализация — platform/*/clicker_*.cpp
#include <chrono>

struct ActionStep;

// Нажатия в систему: SendInput (Windows), XTest (Linux), CGEvent (macOS)
class InputSender {
public:
    InputSender();
    ~InputSender();
    InputSender(const InputSender&) = delete;
    InputSender& operator=(const InputSender&) = delete;

    bool available() const;       // есть ли куда слать ввод (X11 на Linux, разрешение на macOS)
    bool send(const ActionStep& a, bool down);
    // Нажать, только если клавиатура у окна window. focused = false — она уже у другого окна.
    // Linux: менеджер окон переводит фокус X раньше _NET_ACTIVE_WINDOW, поэтому проверка и
    // нажатие — под захватом сервера. Windows и macOS: окно переднего плана меняется сразу
    bool pressIn(const ActionStep& a, unsigned long long window, bool& focused);

private:
    void* h_ = nullptr;           // Display* (Linux), CGEventSourceRef (macOS)
};

// Точные паузы потока кликера («В такт с монитором»: при 240 Гц кадр — 4 мс).
// Живёт, пока кликер работает
class PreciseTimer {
public:
    PreciseTimer();
    ~PreciseTimer();
    PreciseTimer(const PreciseTimer&) = delete;
    PreciseTimer& operator=(const PreciseTimer&) = delete;
    void sleepFor(std::chrono::steady_clock::duration d);

private:
    void* h_ = nullptr;           // Windows: таймер высокой точности
};
