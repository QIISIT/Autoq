#include "core/framewatch.h"
#include <algorithm>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <evntrace.h>
    #include <evntcons.h>
    #include <tdh.h>
    #include <climits>
    #include <cstddef>
    #include <string>
#elif !defined(__APPLE__) && defined(AQ_XCB_PRESENT)
    #include <xcb/xcb.h>
    #include <xcb/present.h>
    #include <poll.h>
    #include <cstdlib>
    #include <ctime>
    #define AQ_FRAMES_X11 1
#endif

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
// Hub — один поток на все окна: подписки (X11) или сессия ETW (Windows)
// ---------------------------------------------------------------------------
namespace {

struct Target {
    unsigned long long window = 0;
    long long pid = 0;
    std::weak_ptr<Feed> feed;
};

#ifdef _WIN32
// Поставщики событий показа кадра (GUID — из манифестов Windows)
const GUID kDxgi    = {0xCA11C036, 0x0102, 0x4A2D, {0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9}};
const GUID kD3d9    = {0x783ACA0A, 0x790E, 0x4D7F, {0x84, 0x51, 0xAA, 0x85, 0x05, 0x11, 0xC6, 0xB9}};
const GUID kDxgKrnl = {0x802EC45A, 0x1E99, 0x4B83, {0x99, 0x20, 0x87, 0xC9, 0x82, 0x77, 0xBA, 0x9D}};
const wchar_t kSession[] = L"AutoQ-Frames";

// Один кадр игры может прийти несколькими событиями: DXGI Present и следом DxgKrnl
// PresentHistory (D3D9 — и через DXGI). Считаем один источник — самый «высокий», что был
// у процесса за секунду
enum Level { kKrnlPresent = 0, kKrnlHistory, kKrnlDetailed, kApiD3d9, kApiDxgi, kLevels };

struct PidFeed {
    DWORD pid = 0;
    std::weak_ptr<Feed> feed;
    Clock::time_point seen[kLevels]{};
    Presents presents;
};
#endif

class Hub {
public:
    static Hub& get() { static Hub h; return h; }
    ~Hub();
    std::shared_ptr<Feed> watch(unsigned long long window, long long pid);
#ifdef _WIN32
    void etwEvent(PEVENT_RECORD r);
#endif
private:
    void run();
    bool sameTarget(const Target& t, unsigned long long window, long long pid) const;
    void prune();                       // под m_: выкинуть цели, на которые никто не смотрит
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<Target> targets_;
    std::thread thread_;
    bool running_ = false;              // поток работает (под m_)
    bool quit_ = false;
    bool dirty_ = false;                // цели поменялись — подписаться сразу
#ifdef _WIN32
    std::mutex pm_;                     // процессы для обработчика событий (поток ETW)
    std::vector<PidFeed> pids_;
    std::atomic<bool> readerDone_{false};
#endif
};

Hub::~Hub() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool Hub::sameTarget(const Target& t, unsigned long long window, long long pid) const {
#ifdef _WIN32
    (void)window;
    return t.pid == pid;
#else
    (void)pid;
    return t.window == window;
#endif
}

void Hub::prune() {
    targets_.erase(std::remove_if(targets_.begin(), targets_.end(),
                                  [](const Target& t) { return t.feed.expired(); }),
                   targets_.end());
}

std::shared_ptr<Feed> Hub::watch(unsigned long long window, long long pid) {
#ifdef _WIN32
    const bool none = pid <= 0;
#else
    const bool none = window == 0;
#endif
    std::lock_guard<std::mutex> lk(m_);
    prune();
    for (const Target& t : targets_)
        if (sameTarget(t, window, pid))
            if (auto f = t.feed.lock()) return f;
    auto feed = std::make_shared<Feed>();
    if (!supported()) { feed->setStatus(Status::Unsupported); return feed; }
    if (none) { feed->setStatus(Status::NoWindow); return feed; }
    targets_.push_back(Target{window, pid, feed});
    dirty_ = true;
    if (!running_) {
        if (thread_.joinable()) thread_.join();     // прошлый поток уже вышел сам
        running_ = true;
        thread_ = std::thread(&Hub::run, this);
    }
    cv_.notify_all();
    return feed;
}

#if defined(AQ_FRAMES_X11)
// ---------------------------------------------------------------------------
// Linux: расширение Present. Подписка на PresentCompleteNotify окна игры и его
// дочерних: X-сервер шлёт событие каждому подписчику, когда кадр показан.
// ---------------------------------------------------------------------------
struct Sub {
    xcb_window_t win = 0;
    uint32_t eid = 0;
    std::weak_ptr<Feed> feed;
    bool keep = false;
};

// окно и его дочерние (Wine / Proton рисуют в дочернем); false — окна больше нет
static bool collect(xcb_connection_t* c, xcb_window_t top, std::vector<xcb_window_t>& out) {
    std::vector<std::pair<xcb_window_t, int>> queue{{top, 0}};
    for (size_t i = 0; i < queue.size() && out.size() < 64; ++i) {
        const auto [w, depth] = queue[i];
        xcb_query_tree_reply_t* r = xcb_query_tree_reply(c, xcb_query_tree(c, w), nullptr);
        if (!r) {
            if (w == top) return false;
            continue;                                  // дочернее только что закрылось
        }
        out.push_back(w);
        if (depth < 3) {
            const xcb_window_t* ch = xcb_query_tree_children(r);
            const int n = xcb_query_tree_children_length(r);
            for (int k = 0; k < n; ++k) queue.push_back({ch[k], depth + 1});
        }
        free(r);
    }
    return true;
}

// когда кадр показан: ust события — мкс по CLOCK_MONOTONIC (так у X-сервера и XWayland).
// Странное (0, из будущего, давнее) — время прихода
static Clock::time_point shownAt(uint64_t ust, Clock::time_point now) {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t ago = int64_t(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000 - int64_t(ust);
    if (ust == 0 || ago < 0 || ago > 2000000) return now;
    return now - std::chrono::microseconds(ago);
}

static void sync(xcb_connection_t* c, const std::vector<Target>& want, std::vector<Sub>& subs) {
    for (Sub& s : subs) s.keep = false;
    for (const Target& t : want) {
        auto f = t.feed.lock();
        if (!f) continue;
        std::vector<xcb_window_t> wins;
        if (!collect(c, static_cast<xcb_window_t>(t.window), wins)) { f->setStatus(Status::NoWindow); continue; }
        f->setStatus(Status::Waiting);
        for (xcb_window_t w : wins) {
            auto it = std::find_if(subs.begin(), subs.end(), [w](const Sub& s) { return s.win == w; });
            if (it != subs.end()) { it->keep = true; it->feed = f; continue; }
            Sub s;
            s.win = w;
            s.eid = xcb_generate_id(c);
            s.feed = f;
            s.keep = true;
            xcb_present_select_input(c, s.eid, w, XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY);
            subs.push_back(s);
        }
    }
    for (auto it = subs.begin(); it != subs.end();) {
        if (it->keep) { ++it; continue; }
        xcb_present_select_input(c, it->eid, it->win, 0);   // окно закрыто — ошибка, её пропустим
        it = subs.erase(it);
    }
    xcb_flush(c);
}

void Hub::run() {
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    bool ok = c && !xcb_connection_has_error(c);
    uint8_t opcode = 0;
    if (ok) {
        const xcb_query_extension_reply_t* e = xcb_get_extension_data(c, &xcb_present_id);
        ok = e && e->present;
        if (ok) opcode = e->major_opcode;
    }
    std::vector<Sub> subs;
    Clock::time_point idleSince = Clock::now(), nextSync{};
    for (;;) {
        std::vector<Target> want;
        bool resync = false;
        {
            std::unique_lock<std::mutex> lk(m_);
            if (quit_) break;
            prune();
            const Clock::time_point now = Clock::now();
            if (targets_.empty()) {
                if (now - idleSince > 3s) { running_ = false; break; }   // смотреть некого — поток выходит
            } else {
                idleSince = now;
            }
            if (!ok) {
                for (const Target& t : targets_)
                    if (auto f = t.feed.lock()) f->setStatus(Status::Failed);
                cv_.wait_for(lk, 250ms);
                continue;
            }
            want = targets_;
            resync = dirty_ || now >= nextSync;
            dirty_ = false;
            if (resync) nextSync = now + 500ms;      // новые дочерние окна, закрытое окно игры
        }
        if (resync) sync(c, want, subs);
        pollfd p{xcb_get_file_descriptor(c), POLLIN, 0};
        poll(&p, 1, 50);
        while (xcb_generic_event_t* ev = xcb_poll_for_event(c)) {
            if ((ev->response_type & 0x7f) == XCB_GE_GENERIC) {
                const auto* ge = reinterpret_cast<const xcb_ge_generic_event_t*>(ev);
                if (ge->extension == opcode && ge->event_type == XCB_PRESENT_COMPLETE_NOTIFY) {
                    const auto* cn = reinterpret_cast<const xcb_present_complete_notify_event_t*>(ev);
                    // кадр (а не ответ на PresentNotifyMSC); пропущенный показ — тоже кадр игры
                    if (cn->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP) {
                        const Clock::time_point t = shownAt(cn->ust, Clock::now());
                        for (const Sub& s : subs)
                            if (s.eid == cn->event) {
                                if (auto f = s.feed.lock()) f->frame(t, s.win);   // окно — источник
                                break;
                            }
                    }
                }
            }
            free(ev);                                  // и ошибки (окно закрылось) — пропускаем
        }
        if (xcb_connection_has_error(c)) ok = false;   // X-сервер закрылся
    }
    if (c) xcb_disconnect(c);
}

#elif defined(_WIN32)
// ---------------------------------------------------------------------------
// Windows: сессия ETW в реальном времени. События показа кадра приходят от всех
// процессов; считаем только процессы игр, на которые смотрят.
// ---------------------------------------------------------------------------
#ifndef EVENT_TRACE_USE_MS_FLUSH_TIMER
#define EVENT_TRACE_USE_MS_FLUSH_TIMER 0x00000010     // FlushTimer — в мс (Windows 8+)
#endif
#ifndef EVENT_FILTER_TYPE_EVENT_ID
#define EVENT_FILTER_TYPE_EVENT_ID 0x80000200
#endif

static void WINAPI onEtwEvent(PEVENT_RECORD r) { static_cast<Hub*>(r->UserContext)->etwEvent(r); }

// когда событие случилось: время события — QueryPerformanceCounter (ClientContext = 1 и
// PROCESS_TRACE_MODE_RAW_TIMESTAMP). Странное — время прихода
static Clock::time_point eventTime(LONGLONG qpc, Clock::time_point now) {
    static const LONGLONG freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    const LONGLONG ago = q.QuadPart - qpc;
    if (freq <= 0 || ago < 0 || ago > 2 * freq) return now;
    return now - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(ago) / double(freq)));
}

// поле события по имени из манифеста поставщика (как у PresentMon); не вышло — 0
static uint64_t field(PEVENT_RECORD r, const wchar_t* name) {
    PROPERTY_DATA_DESCRIPTOR d{};
    d.PropertyName = reinterpret_cast<ULONGLONG>(name);
    d.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    uint64_t v = 0;
    if (TdhGetPropertySize(r, 0, nullptr, 1, &d, &size) == ERROR_SUCCESS && size <= sizeof(v))
        if (TdhGetProperty(r, 0, nullptr, 1, &d, size, reinterpret_cast<PBYTE>(&v)) != ERROR_SUCCESS) v = 0;
    return v;
}

static std::vector<char> sessionProps() {
    std::vector<char> buf(sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSession) + 2048, 0);
    auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
    p->Wnode.BufferSize = static_cast<ULONG>(buf.size());
    p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    p->Wnode.ClientContext = 1;                        // время — QueryPerformanceCounter
    p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_USE_MS_FLUSH_TIMER;
    p->FlushTimer = 1;                                 // события доходят за ~1 мс, а не за секунду
    p->BufferSize = 16;                                // КБ
    p->MinimumBuffers = 4;
    p->MaximumBuffers = 64;
    p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    return buf;
}

static void stopSession() {
    std::vector<char> buf = sessionProps();
    ControlTraceW(0, kSession, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data()), EVENT_TRACE_CONTROL_STOP);
}

// включить поставщика: только нужные события (фильтр — Windows 8.1+; не вышло — без
// фильтра, лишнее отсеет обработчик)
static void enableProvider(TRACEHANDLE s, const GUID& g, ULONGLONG keywords, std::initializer_list<USHORT> ids) {
    struct IdFilter { BOOLEAN filterIn; UCHAR reserved; USHORT count; USHORT events[4]; } f{};
    f.filterIn = TRUE;
    for (USHORT id : ids) if (f.count < 4) f.events[f.count++] = id;
    EVENT_FILTER_DESCRIPTOR d{};
    d.Ptr = reinterpret_cast<ULONGLONG>(&f);
    d.Size = static_cast<ULONG>(offsetof(IdFilter, events) + f.count * sizeof(USHORT));
    d.Type = EVENT_FILTER_TYPE_EVENT_ID;
    ENABLE_TRACE_PARAMETERS p{};
    p.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
    p.EnableFilterDesc = &d;
    p.FilterDescCount = 1;
    if (EnableTraceEx2(s, &g, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION, keywords, 0, 0, &p) != ERROR_SUCCESS)
        EnableTraceEx2(s, &g, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION, keywords, 0, 0, nullptr);
}

static bool processAlive(long long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;   // чужой защищённый процесс — живой
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

void Hub::etwEvent(PEVENT_RECORD r) {
    const EVENT_HEADER& h = r->EventHeader;
    const USHORT id = h.EventDescriptor.Id;
    int level;
    bool start = false, stop = false;          // начало / конец вызова Present()
    Presents::Api api = Presents::Api::Dxgi;
    if (IsEqualGUID(h.ProviderId, kDxgi)) {
        start = id == 42 || id == 55;          // Present_Start, PresentMultiplaneOverlay_Start
        stop = id == 43 || id == 56;           // Present_Stop, PresentMultiplaneOverlay_Stop
        level = kApiDxgi;
    } else if (IsEqualGUID(h.ProviderId, kD3d9)) {
        start = id == 1;                       // Present_Start
        stop = id == 2;                        // Present_Stop
        level = kApiD3d9;
        api = Presents::Api::D3d9;
    } else if (IsEqualGUID(h.ProviderId, kDxgKrnl)) {
        if (id == 215) level = kKrnlDetailed;  // PresentHistoryDetailed_Start
        else if (id == 171) level = kKrnlHistory;   // PresentHistory_Start
        else if (id == 184) level = kKrnlPresent;   // Present_Info (Vulkan, OpenGL)
        else return;
    } else {
        return;
    }
    if (level >= kApiD3d9 && !start && !stop) return;
    const Clock::time_point now = Clock::now();
    const Clock::time_point t = eventTime(h.TimeStamp.QuadPart, now);
    std::shared_ptr<Feed> f;
    Presents::Shown shown;
    {
        std::lock_guard<std::mutex> lk(pm_);
        auto p = std::find_if(pids_.begin(), pids_.end(), [&](const PidFeed& x) { return x.pid == h.ProcessId; });
        if (p == pids_.end()) return;
        if (!stop) {
            p->seen[level] = now;
            for (int l = level + 1; l < kLevels; ++l)
                if (p->seen[l] != Clock::time_point{} && now - p->seen[l] < 1s) return;   // кадр уже посчитан
        }
        if (start) {
            const uint64_t swapchain = field(r, api == Presents::Api::Dxgi ? L"pIDXGISwapChain" : L"pSwapchain");
            p->presents.start(h.ThreadId, api, swapchain, static_cast<uint32_t>(field(r, L"Flags")), t);
            return;
        }
        if (stop && !p->presents.stop(h.ThreadId, api, static_cast<uint32_t>(field(r, L"Result")), shown)) return;
        f = p->feed.lock();
    }
    if (!f) return;
    if (stop) f->frame(shown.t, shown.swapchain);
    else f->frame(t, 0);                       // события ядра — без swapchain
}

void Hub::run() {
    TRACEHANDLE session = 0, trace = INVALID_PROCESSTRACE_HANDLE;
    std::thread reader;
    bool up = false;
    Status fail = Status::Waiting;
    Clock::time_point idleSince = Clock::now(), retryAt{};
    auto shutdown = [&]() {
        if (!up) return;
        stopSession();
        CloseTrace(trace);
        if (reader.joinable()) reader.join();
        up = false;
    };
    for (;;) {
        std::vector<Target> want;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait_for(lk, 250ms, [this] { return quit_ || dirty_; });
            if (quit_) break;
            dirty_ = false;
            prune();
            const Clock::time_point now = Clock::now();
            if (!targets_.empty()) idleSince = now;
            else if (now - idleSince > 3s) {
                running_ = false;                      // смотреть некого — сессию закрываем, поток выходит
                break;
            }
            want = targets_;
        }
        const Clock::time_point now = Clock::now();
        if (up && readerDone_) shutdown();             // сессию остановили снаружи — поднимем заново
        if (!up && !want.empty() && now >= retryAt) {
            stopSession();                             // зависла от прошлого запуска (AutoQ закрыли аварийно)
            std::vector<char> props = sessionProps();
            ULONG rc = StartTraceW(&session, kSession, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()));
            if (rc == ERROR_SUCCESS) {
                enableProvider(session, kDxgi, 0x2, {42, 43, 55, 56});
                enableProvider(session, kD3d9, 0x2, {1, 2});
                // Base | Present: на Windows 10 у PresentHistory нет ключа Present
                enableProvider(session, kDxgKrnl, 0x8000001, {184, 171, 215});
                EVENT_TRACE_LOGFILEW log{};
                log.LoggerName = const_cast<LPWSTR>(kSession);
                log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD
                                     | PROCESS_TRACE_MODE_RAW_TIMESTAMP;   // время — QPC, как в ClientContext
                log.EventRecordCallback = onEtwEvent;
                log.Context = this;
                trace = OpenTraceW(&log);
                if (trace == INVALID_PROCESSTRACE_HANDLE) {
                    rc = GetLastError();
                    stopSession();
                } else {
                    readerDone_ = false;
                    reader = std::thread([this, trace]() {
                        TRACEHANDLE h = trace;
                        ProcessTrace(&h, 1, nullptr, nullptr);   // до остановки сессии
                        readerDone_ = true;
                    });
                    up = true;
                }
            }
            if (!up) {
                fail = rc == ERROR_ACCESS_DENIED ? Status::NeedAdmin : Status::Failed;
                retryAt = now + 5s;
            }
        }
        if (up && want.empty()) shutdown();
        {
            std::lock_guard<std::mutex> lk(pm_);
            std::vector<PidFeed> next;
            for (const Target& t : want) {
                PidFeed p;
                p.pid = static_cast<DWORD>(t.pid);
                for (const PidFeed& old : pids_)
                    if (old.pid == p.pid) p = old;     // что видели и начатые вызовы
                p.feed = t.feed;
                next.push_back(p);
            }
            pids_.swap(next);
        }
        for (const Target& t : want)
            if (auto f = t.feed.lock())
                f->setStatus(!up ? fail : processAlive(t.pid) ? Status::Waiting : Status::NoWindow);
    }
    shutdown();
    std::lock_guard<std::mutex> lk(pm_);
    pids_.clear();
}

#else
// macOS (и Linux без libxcb-present): кадров игры не видно
void Hub::run() {
    std::lock_guard<std::mutex> lk(m_);
    for (const Target& t : targets_)
        if (auto f = t.feed.lock()) f->setStatus(Status::Unsupported);
    targets_.clear();
    running_ = false;
}
#endif

} // namespace

// ---------------------------------------------------------------------------
bool FrameWatch::supported() {
#if defined(AQ_FRAMES_X11) || defined(_WIN32)
    return true;
#else
    return false;
#endif
}

bool FrameWatch::needsAdmin() {
#ifdef _WIN32
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return true;
    TOKEN_ELEVATION e{};
    DWORD n = 0;
    const bool elevated = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n) && e.TokenIsElevated;
    CloseHandle(tok);
    return !elevated;
#else
    return false;
#endif
}

std::shared_ptr<Feed> FrameWatch::watch(unsigned long long window, long long pid) {
    return Hub::get().watch(window, pid);
}
