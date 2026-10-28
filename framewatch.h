#pragma once
// «По кадрам» (карточка «Скорость»): кадры окна игры — моменты, когда игра показала
// новый кадр. Кликер ждёт не миллисекунды, а N кадров игры: клик раз в кадр, «держать
// 2 кадра», «пауза 1 кадр» — и каждое нажатие попадает в свой кадр при любом FPS.
//  Linux (X11 / XWayland): расширение Present. X-сервер сообщает о каждом показанном
//    кадре окна всем, кто подписался на это окно; подписка — на окно игры и его дочерние
//    (Wine / Proton рисуют в дочернем). Прав не нужно.
//  Windows: события ETW о показе кадра процессом игры — DXGI (DirectX 10–12), D3D9 и
//    DxgKrnl (Vulkan, OpenGL). Читать события ETW можно только с правами администратора.
//  macOS: общего способа нет — «По кадрам» недоступно.
// Без Qt: только стандартная библиотека и API системы.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace FrameWatch {

enum class Status {
    Waiting,       // подписка есть, кадров пока нет (игра свёрнута, на паузе, грузится)
    Live,          // кадры идут
    NoWindow,      // окно игры не выбрано или закрылось
    NeedAdmin,     // Windows: без прав администратора события кадров не читаются
    Failed,        // не вышло подписаться (нет X11 или расширения Present, ошибка ETW)
    Unsupported    // на этой ОС кадры игры не видны (macOS)
};

using Clock = std::chrono::steady_clock;

// Кадры одного окна игры. Живёт, пока на него есть ссылка (кликер, интерфейс);
// один и тот же для всех, кто смотрит это окно.
class Feed {
public:
    uint64_t count() const { return count_.load(); }        // кадров с начала подписки
    // ждать, пока count() >= target, не дольше timeout. true — дождались
    bool waitUntil(uint64_t target, std::chrono::milliseconds timeout);
    double fps() const;                  // кадров в секунду за последнюю секунду (0 — кадров нет)
    Status status() const;               // Live — последний кадр был меньше 0,5 с назад
    Clock::time_point lastFrame() const; // когда пришёл последний кадр (пусто — ещё не было)

    // источник кадров (и тесты)
    void frame(Clock::time_point t = Clock::now());
    void setStatus(Status s);
private:
    std::atomic<uint64_t> count_{0};
    std::atomic<int> status_{static_cast<int>(Status::Waiting)};
    mutable std::mutex m_;
    std::condition_variable cv_;
    static constexpr int kRing = 1024;   // моменты последних кадров (fps до 1000 — за секунду)
    Clock::time_point ring_[kRing]{};
    uint64_t ringN_ = 0;
};

bool supported();                        // на этой ОС кадры игры видны
bool needsAdmin();                       // Windows без прав администратора — кадров не будет
// Подписаться на кадры окна игры: Linux — окно X11 (window), Windows — процесс (pid).
// То же окно / процесс — тот же Feed, пока на него есть ссылки.
std::shared_ptr<Feed> watch(unsigned long long window, long long pid);

}
