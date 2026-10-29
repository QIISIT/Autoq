#pragma once
// «По кадрам»: что даёт ОС. Один поток на все окна игр (Hub) — подписки на кадры
// окна (Linux) или сессия ETW (Windows). Реализация — platform/*/framewatch_*.cpp,
// без поддержки — platform/framewatch_none.cpp
#include "core/framewatch.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace FrameWatch {

struct Target {
    unsigned long long window = 0;
    long long pid = 0;
    std::weak_ptr<Feed> feed;
};

class Hub {
public:
    static Hub& get();
    ~Hub();
    std::shared_ptr<Feed> watch(unsigned long long window, long long pid);
    // Для потока ОС: подождать не дольше wait, пока поменяются цели, и забрать их
    // (changed — поменялись). false — поток выходит: AutoQ закрывается или 3 с смотреть некого
    bool next(std::vector<Target>& want, bool& changed, Clock::duration wait);

private:
    Hub() = default;
    void prune();                       // под m_: выкинуть цели, на которые никто не смотрит
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<Target> targets_;
    std::thread thread_;
    bool running_ = false;              // поток работает (под m_)
    bool quit_ = false;
    bool dirty_ = false;                // цели поменялись — подписаться сразу
    Clock::time_point idleSince_{};
};

namespace Os {
    bool supported();
    bool needsAdmin();
    bool byProcess();                   // Windows: кадры процесса (pid), иначе — окна
    void run(Hub& hub);                 // поток Hub: до hub.next() == false
}

}
