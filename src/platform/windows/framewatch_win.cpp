#include "core/framewatch_os.h"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>

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

// Сессия ETW в реальном времени: события показа кадра приходят от всех процессов,
// считаются только процессы игр, на которые смотрят. Нужны права администратора.

using namespace FrameWatch;
using namespace std::chrono_literals;

#ifndef EVENT_TRACE_USE_MS_FLUSH_TIMER
#define EVENT_TRACE_USE_MS_FLUSH_TIMER 0x00000010     // FlushTimer — в мс (Windows 8+)
#endif
#ifndef EVENT_FILTER_TYPE_EVENT_ID
#define EVENT_FILTER_TYPE_EVENT_ID 0x80000200
#endif

namespace {

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

// процессы игр — для обработчика событий (поток ETW)
struct Etw {
    std::mutex m;
    std::vector<PidFeed> pids;
    std::atomic<bool> readerDone{false};
    void event(PEVENT_RECORD r);
};

void WINAPI onEtwEvent(PEVENT_RECORD r) { static_cast<Etw*>(r->UserContext)->event(r); }

// когда событие случилось: время события — QueryPerformanceCounter (ClientContext = 1 и
// PROCESS_TRACE_MODE_RAW_TIMESTAMP). Странное — время прихода
Clock::time_point eventTime(LONGLONG qpc, Clock::time_point now) {
    static const LONGLONG freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    const LONGLONG ago = q.QuadPart - qpc;
    if (freq <= 0 || ago < 0 || ago > 2 * freq) return now;
    return now - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(ago) / double(freq)));
}

// поле события по имени из манифеста поставщика (как у PresentMon); не вышло — 0
uint64_t field(PEVENT_RECORD r, const wchar_t* name) {
    PROPERTY_DATA_DESCRIPTOR d{};
    d.PropertyName = reinterpret_cast<ULONGLONG>(name);
    d.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    uint64_t v = 0;
    if (TdhGetPropertySize(r, 0, nullptr, 1, &d, &size) == ERROR_SUCCESS && size <= sizeof(v))
        if (TdhGetProperty(r, 0, nullptr, 1, &d, size, reinterpret_cast<PBYTE>(&v)) != ERROR_SUCCESS) v = 0;
    return v;
}

std::vector<char> sessionProps() {
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

void stopSession() {
    std::vector<char> buf = sessionProps();
    ControlTraceW(0, kSession, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data()), EVENT_TRACE_CONTROL_STOP);
}

// включить поставщика: только нужные события (фильтр — Windows 8.1+; не вышло — без
// фильтра, лишнее отсеет обработчик)
void enableProvider(TRACEHANDLE s, const GUID& g, ULONGLONG keywords, std::initializer_list<USHORT> ids) {
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

bool processAlive(long long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;   // чужой защищённый процесс — живой
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

void Etw::event(PEVENT_RECORD r) {
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
        std::lock_guard<std::mutex> lk(m);
        auto p = std::find_if(pids.begin(), pids.end(), [&](const PidFeed& x) { return x.pid == h.ProcessId; });
        if (p == pids.end()) return;
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

}

bool Os::supported() { return true; }
bool Os::byProcess() { return true; }

bool Os::needsAdmin() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return true;
    TOKEN_ELEVATION e{};
    DWORD n = 0;
    const bool elevated = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n) && e.TokenIsElevated;
    CloseHandle(tok);
    return !elevated;
}

void Os::run(Hub& hub) {
    Etw etw;
    TRACEHANDLE session = 0, trace = INVALID_PROCESSTRACE_HANDLE;
    std::thread reader;
    bool up = false;
    Status fail = Status::Waiting;
    Clock::time_point retryAt{};
    auto shutdown = [&]() {
        if (!up) return;
        stopSession();
        CloseTrace(trace);
        if (reader.joinable()) reader.join();
        up = false;
    };
    std::vector<Target> want;
    bool changed = false;
    while (hub.next(want, changed, 250ms)) {
        const Clock::time_point now = Clock::now();
        if (up && etw.readerDone) shutdown();             // сессию остановили снаружи — поднимем заново
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
                log.Context = &etw;
                trace = OpenTraceW(&log);
                if (trace == INVALID_PROCESSTRACE_HANDLE) {
                    rc = GetLastError();
                    stopSession();
                } else {
                    etw.readerDone = false;
                    reader = std::thread([&etw, trace]() {
                        TRACEHANDLE h = trace;
                        ProcessTrace(&h, 1, nullptr, nullptr);   // до остановки сессии
                        etw.readerDone = true;
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
            std::lock_guard<std::mutex> lk(etw.m);
            std::vector<PidFeed> next;
            for (const Target& t : want) {
                PidFeed p;
                p.pid = static_cast<DWORD>(t.pid);
                for (const PidFeed& old : etw.pids)
                    if (old.pid == p.pid) p = old;     // что видели и начатые вызовы
                p.feed = t.feed;
                next.push_back(p);
            }
            etw.pids.swap(next);
        }
        for (const Target& t : want)
            if (auto f = t.feed.lock())
                f->setStatus(!up ? fail : processAlive(t.pid) ? Status::Waiting : Status::NoWindow);
    }
    shutdown();
}

