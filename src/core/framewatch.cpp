#include "core/framewatch.h"
#include "core/framewatch_os.h"
#include <algorithm>
#include <vector>

using namespace FrameWatch;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Feed — счётчик кадров одного окна
// ---------------------------------------------------------------------------
bool Feed::waitUntil(uint64_t target, Clock::duration timeout) {
    if (count_.load() >= target) return true;
    std::unique_lock<std::mutex> lk(m_);
    return cv_.wait_for(lk, timeout, [&] { return count_.load() >= target; });
}

namespace {
constexpr auto kWindow = 2s;          // fps — по кадрам за 2 с до последнего
constexpr auto kStale = 1500ms;       // кадров нет дольше — «не идут» (пачки XWayland — раз в 1 с)
constexpr auto kSwitch = 250ms;       // источник молчит дольше — считаем другой
}

void Feed::frame(Clock::time_point t, uint64_t src) {
    {
        // счётчик — под тем же замком, что и ожидание: кадр не проскочит мимо ждущего
        std::lock_guard<std::mutex> lk(m_);
        if (ringN_ && src != src_) {
            // второе окно / swapchain игры: тот же кадр или свой ритм — не считаем, пока
            // первый показывает кадры
            if (t - srcLast_ < kSwitch) return;
            srcLast_ = t;
        } else {
            srcLast_ = std::max(srcLast_, t);
        }
        src_ = src;
        ring_[ringN_ % kRing] = t;
        ++ringN_;
        last_ = std::max(last_, t);
        count_.fetch_add(1);
    }
    cv_.notify_all();
}

Clock::time_point Feed::lastFrame() const {
    std::lock_guard<std::mutex> lk(m_);
    return last_;
}

double Feed::fps() const {
    std::vector<Clock::time_point> t;
    Clock::time_point now;
    {
        std::lock_guard<std::mutex> lk(m_);
        now = Clock::now();
        if (ringN_ < 2 || now - last_ > kStale) return 0;          // кадры не идут
        const uint64_t avail = std::min<uint64_t>(ringN_, kRing);
        t.reserve(avail);
        for (uint64_t k = 0; k < avail; ++k) {
            const Clock::time_point x = ring_[(ringN_ - 1 - k) % kRing];
            if (last_ - x <= kWindow) t.push_back(x);
        }
    }
    if (t.size() < 2) return 0;
    std::sort(t.begin(), t.end());
    // Кадры приходят и пачками: XWayland за скрытым окном отдаёт их раз в секунду разом.
    // Пачка в начале окна — точка отсчёта: её кадры показаны до неё, иначе (k-1)/(время)
    // на краю окна — тысячи кадров в секунду. Пачка — кадры с промежутками меньше 1/8
    // самого большого и сами короче 1/8 окна (ровный поток с рывком — не пачка)
    Clock::duration maxGap{};
    for (size_t i = 1; i < t.size(); ++i) maxGap = std::max(maxGap, t[i] - t[i - 1]);
    size_t b = 0;
    while (b + 1 < t.size() && t[b + 1] - t[b] < maxGap / 8) ++b;
    const Clock::duration span = t.back() - t.front();
    if (t[b] - t.front() > span / 8) b = 0;
    // все кадры разом, а потом тишина дольше, чем они шли, — сколько их в секунду, не видно
    if (b == 0 && now - t.back() > std::max<Clock::duration>(span, 50ms)) return 0;
    const double sec = std::chrono::duration<double>(t.back() - t[b]).count();
    const size_t n = t.size() - 1 - b;
    return n && sec > 0 ? double(n) / sec : 0;
}

Clock::duration Feed::frameTime() const {
    std::vector<Clock::time_point> t;
    {
        std::lock_guard<std::mutex> lk(m_);
        const uint64_t n = std::min<uint64_t>({ringN_, kRing, 9});
        for (uint64_t k = 0; k < n; ++k) t.push_back(ring_[(ringN_ - 1 - k) % kRing]);
    }
    std::sort(t.begin(), t.end());
    std::vector<Clock::duration> gaps;
    for (size_t i = 1; i < t.size(); ++i) gaps.push_back(t[i] - t[i - 1]);
    if (!gaps.empty()) {
        std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
        const Clock::duration mid = gaps[gaps.size() / 2];
        if (mid > 200us) return mid;
    }
    const double f = fps();                                       // кадры пришли пачкой
    return f > 0 ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / f))
                 : Clock::duration{};
}

Status Feed::status() const {
    if (count_.load() > 0 && Clock::now() - lastFrame() < kStale) return Status::Live;
    const auto s = static_cast<Status>(status_.load());
    return s == Status::Live ? Status::Waiting : s;
}

void Feed::setStatus(Status s) { status_ = static_cast<int>(s); }

// ---------------------------------------------------------------------------
// Presents — вызовы Present() игры (Windows)
// ---------------------------------------------------------------------------
void Presents::start(uint32_t thread, Api api, uint64_t swapchain, uint32_t flags, Clock::time_point t) {
    // прошлый вызов этого потока без конца (подписка оборвалась) и совсем старые — не копим
    calls_.erase(std::remove_if(calls_.begin(), calls_.end(),
                                [&](const Call& c) { return (c.thread == thread && c.api == api) || t - c.t > 2s; }),
                 calls_.end());
    calls_.push_back(Call{thread, api, swapchain, flags, t});
}

bool Presents::stop(uint32_t thread, Api api, uint32_t result, Shown& out) {
    for (auto it = calls_.begin(); it != calls_.end(); ++it) {
        if (it->thread != thread || it->api != api) continue;
        const bool ok = shown(api, it->flags, result);
        out.swapchain = it->swapchain;
        out.t = it->t;                      // кадр — когда игра его отдала
        calls_.erase(it);
        return ok;
    }
    return false;                           // начало не видели — подписались посреди вызова
}

bool Presents::shown(Api api, uint32_t flags, uint32_t result) {
    if (api == Api::Dxgi && (flags & 0x1)) return false;   // DXGI_PRESENT_TEST (у D3D9 бит 1 — DONOTWAIT)
    if (static_cast<int32_t>(result) < 0) return false;    // ошибка: WAS_STILL_DRAWING при DO_NOT_WAIT и др.
    if (api == Api::D3d9) return result != 0x08760878;     // S_PRESENT_OCCLUDED
    return result != 0x087A0001                            // DXGI_STATUS_OCCLUDED
        && result != 0x087A0005                            // DXGI_STATUS_NO_DESKTOP_ACCESS
        && result != 0x087A0008;                           // DXGI_STATUS_MODE_CHANGE_IN_PROGRESS
}

// ---------------------------------------------------------------------------
// Hub — один поток на все окна
// ---------------------------------------------------------------------------
Hub& Hub::get() { static Hub h; return h; }

Hub::~Hub() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Hub::prune() {
    targets_.erase(std::remove_if(targets_.begin(), targets_.end(),
                                  [](const Target& t) { return t.feed.expired(); }),
                   targets_.end());
}

std::shared_ptr<Feed> Hub::watch(unsigned long long window, long long pid) {
    const bool byPid = Os::byProcess();
    std::lock_guard<std::mutex> lk(m_);
    prune();
    for (const Target& t : targets_)
        if (byPid ? t.pid == pid : t.window == window)
            if (auto f = t.feed.lock()) return f;
    auto feed = std::make_shared<Feed>();
    if (!Os::supported()) { feed->setStatus(Status::Unsupported); return feed; }
    if (byPid ? pid <= 0 : window == 0) { feed->setStatus(Status::NoWindow); return feed; }
    targets_.push_back(Target{window, pid, feed});
    dirty_ = true;
    if (!running_) {
        if (thread_.joinable()) thread_.join();     // прошлый поток уже вышел сам
        running_ = true;
        idleSince_ = Clock::now();
        thread_ = std::thread([this] { Os::run(*this); });
    }
    cv_.notify_all();
    return feed;
}

bool Hub::next(std::vector<Target>& want, bool& changed, Clock::duration wait) {
    std::unique_lock<std::mutex> lk(m_);
    if (wait > Clock::duration::zero()) cv_.wait_for(lk, wait, [this] { return quit_ || dirty_; });
    if (quit_) return false;
    changed = dirty_;
    dirty_ = false;
    prune();
    const Clock::time_point now = Clock::now();
    if (!targets_.empty()) {
        idleSince_ = now;
    } else if (now - idleSince_ > 3s) {
        running_ = false;
        return false;
    }
    want = targets_;
    return true;
}

// ---------------------------------------------------------------------------
bool FrameWatch::supported() { return Os::supported(); }
bool FrameWatch::needsAdmin() { return Os::needsAdmin(); }

std::shared_ptr<Feed> FrameWatch::watch(unsigned long long window, long long pid) {
    return Hub::get().watch(window, pid);
}
