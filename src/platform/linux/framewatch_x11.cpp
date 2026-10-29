#include "core/framewatch_os.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>

#include <poll.h>
#include <xcb/xcb.h>
#include <xcb/present.h>

// Расширение Present: подписка на PresentCompleteNotify окна игры и его дочерних —
// X-сервер шлёт событие каждому подписчику, когда кадр показан. Прав не нужно.

using namespace FrameWatch;
using namespace std::chrono_literals;

namespace {

struct Sub {
    xcb_window_t win = 0;
    uint32_t eid = 0;
    std::weak_ptr<Feed> feed;
    bool keep = false;
};

// окно и его дочерние (Wine / Proton рисуют в дочернем); false — окна больше нет
bool collect(xcb_connection_t* c, xcb_window_t top, std::vector<xcb_window_t>& out) {
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
Clock::time_point shownAt(uint64_t ust, Clock::time_point now) {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t ago = int64_t(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000 - int64_t(ust);
    if (ust == 0 || ago < 0 || ago > 2000000) return now;
    return now - std::chrono::microseconds(ago);
}

void sync(xcb_connection_t* c, const std::vector<Target>& want, std::vector<Sub>& subs) {
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

}

bool Os::supported() { return true; }
bool Os::needsAdmin() { return false; }
bool Os::byProcess() { return false; }

void Os::run(Hub& hub) {
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    bool ok = c && !xcb_connection_has_error(c);
    uint8_t opcode = 0;
    if (ok) {
        const xcb_query_extension_reply_t* e = xcb_get_extension_data(c, &xcb_present_id);
        ok = e && e->present;
        if (ok) opcode = e->major_opcode;
    }
    std::vector<Sub> subs;
    std::vector<Target> want;
    Clock::time_point nextSync{};
    bool changed = false;
    while (hub.next(want, changed, ok ? Clock::duration::zero() : Clock::duration(250ms))) {
        if (!ok) {
            for (const Target& t : want)
                if (auto f = t.feed.lock()) f->setStatus(Status::Failed);
            continue;
        }
        const Clock::time_point now = Clock::now();
        if (changed || now >= nextSync) {
            nextSync = now + 500ms;                 // новые дочерние окна, закрытое окно игры
            sync(c, want, subs);
        }
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
