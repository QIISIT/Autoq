#include "core/hotkey.h"
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

void GlobalHotkey::init() {}

// «Сквозные» клавиши (буква, цифра…) и кнопки мыши: низкоуровневые хуки видят
// нажатие и пропускают его дальше — оно доходит до игры. Хуки вызываются в
// потоке, который их поставил (пока он разбирает сообщения), — блокировки не нужны.
namespace {
// hold — vk клавиши, которую держат («Tab+F»: hold = Tab, code = F); 0 — обычный хоткей
// block — «не пропускать в игру»: сработавшее нажатие (и его отпускание) хук не отдаёт дальше
struct LLBind {
    unsigned code = 0; int mods = 0; int id = 0; unsigned hold = 0; bool down = false; bool fired = false;
    bool block = false; bool swallowUp = false;
};
std::vector<LLBind> g_llKeys;    // клавиши: виртуальный код
std::vector<LLBind> g_llReg;     // клавиши RegisterHotKey — когда сама Windows их не поймает (см. llRegistered)
std::vector<LLBind> g_llMouse;   // кнопки мыши: 2, 8, 9
std::vector<std::pair<int, bool>> g_llEvents;
bool g_llDown[256] = {};         // что зажал человек (нажатия кликера — SendInput — не считаются)
bool g_kbHooked = false;         // хук клавиатуры стоит — g_llDown знает модификаторы человека

bool keyHeld(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
// первая клавиша сочетания зажата: по хуку (не кликером) и по состоянию клавиатуры
// (если отпускание хук пропустил — клавиша не «залипает»)
bool holdOk(unsigned vk) { return vk < 256 && g_llDown[vk] && keyHeld(static_cast<int>(vk)); }

// Модификаторы по состоянию клавиатуры — вместе с зажатыми кликером (SendInput меняет и его).
// По ним Windows сверяет RegisterHotKey.
int systemMods() {
    int m = 0;
    if (keyHeld(VK_CONTROL)) m |= ModCtrl;
    if (keyHeld(VK_MENU)) m |= ModAlt;
    if (keyHeld(VK_SHIFT)) m |= ModShift;
    if (keyHeld(VK_LWIN) || keyHeld(VK_RWIN)) m |= ModWin;
    return m;
}

// Модификаторы, зажатые человеком: по хуку (нажатия кликера не в счёт) и по состоянию
// клавиатуры (пропущенное хуком отпускание не оставит модификатор «залипшим»)
int currentMods() {
    if (!g_kbHooked) return systemMods();
    auto human = [](int l, int r) { return (g_llDown[l] && keyHeld(l)) || (g_llDown[r] && keyHeld(r)); };
    int m = 0;
    if (human(VK_LCONTROL, VK_RCONTROL)) m |= ModCtrl;
    if (human(VK_LMENU, VK_RMENU)) m |= ModAlt;
    if (human(VK_LSHIFT, VK_RSHIFT)) m |= ModShift;
    if (human(VK_LWIN, VK_RWIN)) m |= ModWin;
    return m;
}

// модификаторы подходят привязке: точно или с лишними — по настройке, и только когда на той
// же клавише нет привязки точнее (GlobalHotkey::modsMatch). Клавиши — из обоих списков: «E»
// (хук) молчит, когда нажали Ctrl+E и он есть у RegisterHotKey
bool modsOk(const LLBind& b, bool mouse, int held) {
    std::vector<int> sameKey;
    for (const auto* list : {&g_llKeys, &g_llReg, &g_llMouse}) {
        if ((list == &g_llMouse) != mouse) continue;
        for (const LLBind& o : *list)
            if (o.code == b.code && o.hold == b.hold) sameKey.push_back(o.mods);
    }
    return GlobalHotkey::modsMatch(held, b.mods, sameKey);
}

// true — нажатие забираем себе (не отдаём игре и системе)
bool llEvent(std::vector<LLBind>& binds, unsigned code, bool down) {
    bool swallow = false;
    if (down) {
        // сначала сочетания «держу A, жму B»: сработало — обычный хоткей на B молчит
        bool chord = false;
        for (LLBind& b : binds) {
            if (!b.hold || b.code != code) continue;
            if (b.down) { chord = chord || b.fired; continue; }    // автоповтор
            b.down = true;
            if (holdOk(b.hold) && modsOk(b, &binds == &g_llMouse, currentMods())) {
                b.fired = chord = true;
                g_llEvents.push_back({b.id, true});
            }
        }
        for (LLBind& b : binds) {
            if (b.hold || b.code != code) continue;
            if (b.down) { swallow = swallow || b.swallowUp; continue; }    // автоповтор — как само нажатие
            b.down = true;
            if (!chord && modsOk(b, &binds == &g_llMouse, currentMods())) { b.fired = true; g_llEvents.push_back({b.id, true}); }
            b.swallowUp = b.block && b.fired;
            swallow = swallow || b.swallowUp;
        }
        return swallow;
    }
    for (LLBind& b : binds) {
        if (b.code == code && b.down) {
            b.down = false;
            if (b.swallowUp) { b.swallowUp = false; swallow = true; }    // забрали нажатие — забираем и отпускание
            if (b.fired) { b.fired = false; g_llEvents.push_back({b.id, false}); }
        } else if (b.hold && b.hold == code && b.fired) {
            // отпустили первую клавишу сочетания — тоже «отпущено» (B ещё зажата: снова
            // сработает, только когда её нажмут заново)
            b.fired = false;
            g_llEvents.push_back({b.id, false});
        }
    }
    return swallow;
}

// Клавиши RegisterHotKey. Windows сверяет модификаторы с состоянием клавиатуры, а его меняет и
// кликер: держит Shift — и F6 человека для Windows уже Shift+F6, хоткей молчит (или наоборот,
// сработал бы чужой Ctrl+F6). Решаем по модификаторам человека, как на Linux: Windows поймает
// сама — не мешаем; не поймает — срабатываем отсюда; поймает не то — забираем нажатие.
// true — нажатие (и его отпускание) забираем себе.
bool llRegistered(unsigned code, bool down) {
    bool swallow = false;
    for (LLBind& b : g_llReg) {
        if (b.code != code) continue;
        if (!down) {
            if (!b.down) continue;
            b.down = false;
            if (b.swallowUp) { b.swallowUp = false; swallow = true; }
            if (b.fired) { b.fired = false; g_llEvents.push_back({b.id, false}); }
            continue;
        }
        if (b.down) { swallow = swallow || b.swallowUp; continue; }    // автоповтор — как само нажатие
        b.down = true;
        // RegisterHotKey сверяет модификаторы точно; человек — как в настройке (лишние Shift/Ctrl
        // не мешают — тогда F6 при зажатом Shift ловим отсюда)
        const bool human = modsOk(b, false, currentMods()), system = systemMods() == b.mods;
        if (human == system) continue;                                 // решит RegisterHotKey
        b.swallowUp = true;
        b.fired = human;
        if (human) g_llEvents.push_back({b.id, true});
        swallow = true;
    }
    return swallow;
}

LRESULT CALLBACK llKeyboardProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l);
        // свои же нажатия (SendInput кликера) не считаем
        if (!(k->flags & LLKHF_INJECTED)) {
            const bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN;
            if (k->vkCode < 256) g_llDown[k->vkCode] = down;
            llEvent(g_llKeys, k->vkCode, down);
            if (llRegistered(k->vkCode, down)) return 1;
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}

LRESULT CALLBACK llMouseProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(l);
        if (!(m->flags & LLMHF_INJECTED)) {
            unsigned btn = 0;
            bool down = false;
            switch (w) {
                case WM_MBUTTONDOWN: btn = 2; down = true; break;
                case WM_MBUTTONUP:   btn = 2; break;
                case WM_XBUTTONDOWN: btn = HIWORD(m->mouseData) == XBUTTON1 ? 8 : 9; down = true; break;
                case WM_XBUTTONUP:   btn = HIWORD(m->mouseData) == XBUTTON1 ? 8 : 9; break;
                default: break;
            }
            if (btn && llEvent(g_llMouse, btn, down)) return 1;     // «не пропускать в игру»
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
} // namespace

void GlobalHotkey::loop() {
    // Очередь сообщений потока должна существовать ДО RegisterHotKey:
    // WM_HOTKEY приходит именно в неё.
    MSG msg;
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    std::vector<int> registered;                       // id+1 у RegisterHotKey
    struct Held { int id; unsigned vk; };
    std::vector<Held> held;                            // ждём отпускания (RegisterHotKey его не сообщает)
    std::vector<unsigned> regVk;                       // vk по id
    HHOOK kbHook = nullptr, mouseHook = nullptr;

    auto clear = [&]() {
        for (int r : registered) UnregisterHotKey(NULL, r);
        registered.clear();
        if (kbHook) { UnhookWindowsHookEx(kbHook); kbHook = nullptr; }
        g_kbHooked = false;
        if (mouseHook) { UnhookWindowsHookEx(mouseHook); mouseHook = nullptr; }
        // не оставить «зажатой»: и клавиши RegisterHotKey, и сквозные (хуки)
        for (const auto* list : {&g_llKeys, &g_llReg, &g_llMouse})
            for (const LLBind& b : *list) if (b.fired) fire(b.id, false);
        g_llKeys.clear();
        g_llReg.clear();
        g_llMouse.clear();
        g_llEvents.clear();
        // что человек держит прямо сейчас (Tab у «Tab+F») — помним и через переустановку хуков:
        // повторы идут от последней нажатой клавиши, новое «нажато» для Tab хук может не увидеть.
        // Отпущенное за это время — забываем (как на Linux: там зажатое сверяется с XQueryKeymap)
        for (int vk = 0; vk < 256; ++vk) g_llDown[vk] = g_llDown[vk] && keyHeld(vk);
        for (const Held& h : held) fire(h.id, false);
        held.clear();
    };
    auto apply = [&]() {
        clear();
        const QList<HotkeySpec> specs = bindings();
        active_ = specs;
        if (suspended_) { resetStatuses(Status::Suspended); return; }
        regVk.assign(static_cast<size_t>(specs.size()), 0);
        for (int id = 0; id < specs.size(); ++id) {
            const HotkeySpec& sp = specs[id];
            if (!sp.isValid() || !sp.isGlobalSafe()) { setStatus(id, Status::Invalid); continue; }
            if (sp.isMouse()) {
                LLBind b{static_cast<unsigned>(sp.mouseButton()), sp.mods, id};
                b.block = !sp.passthrough();
                g_llMouse.push_back(b);
                continue;
            }
            const KeyDef* k = Keys::byName(sp.key);
            if (!k) { setStatus(id, Status::Invalid); continue; }
            if (sp.passthrough()) {
                unsigned hold = 0;
                if (sp.isChord()) {
                    const KeyDef* h = Keys::byName(sp.hold);
                    if (!h || !h->winVK) { setStatus(id, Status::Invalid); continue; }
                    hold = h->winVK;
                }
                g_llKeys.push_back({k->winVK, sp.mods, id, hold});
                continue;
            }
            UINT mods = MOD_NOREPEAT;     // удержание не дёргает старт/стоп много раз
            if (sp.mods & ModCtrl)  mods |= MOD_CONTROL;
            if (sp.mods & ModAlt)   mods |= MOD_ALT;
            if (sp.mods & ModShift) mods |= MOD_SHIFT;
            if (sp.mods & ModWin)   mods |= MOD_WIN;
            // F12 без модификаторов Windows резервирует под отладчик — RegisterHotKey
            // вернёт ошибку, и мы честно покажем "занята".
            if (RegisterHotKey(NULL, id + 1, mods, k->winVK)) {
                registered.push_back(id + 1);
                regVk[static_cast<size_t>(id)] = k->winVK;
                g_llReg.push_back({k->winVK, sp.mods, id});
                setStatus(id, Status::Ok);
            } else {
                setStatus(id, Status::Busy);
            }
        }
        // зажатое через переустановку — уже нажато: его автоповтор не новое нажатие (иначе «Tab+1»
        // у «используется», если держать 1, переключал бы туда-сюда с частотой повтора)
        for (auto* list : {&g_llKeys, &g_llReg})
            for (LLBind& b : *list) if (b.code < 256 && g_llDown[b.code]) b.down = true;
        // хук клавиатуры — и для RegisterHotKey, и для кнопок мыши: модификаторы человека видны только по нему
        if (!g_llKeys.empty() || !g_llReg.empty() || !g_llMouse.empty()) {
            kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, llKeyboardProc, GetModuleHandleW(nullptr), 0);
            g_kbHooked = kbHook != nullptr;
            for (const LLBind& b : g_llKeys) setStatus(b.id, kbHook ? Status::Ok : Status::Busy);
        }
        if (!g_llMouse.empty()) {
            mouseHook = SetWindowsHookExW(WH_MOUSE_LL, llMouseProc, GetModuleHandleW(nullptr), 0);
            for (const LLBind& b : g_llMouse) setStatus(b.id, mouseHook ? Status::Ok : Status::Busy);
        }
    };

    while (running_) {
        if (rebind_.exchange(false)) apply();
        // ждём сообщений (хуки требуют быстрого ответа — не спим вслепую)
        MsgWaitForMultipleObjectsEx(0, nullptr, 15, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY) {
                const int id = static_cast<int>(msg.wParam) - 1;
                bool already = false;
                for (const Held& h : held) already = already || h.id == id;
                // эта же клавиша вторая в сочетании («Tab+F6»), и первая зажата — сработало сочетание
                if (id >= 0 && id < static_cast<int>(regVk.size()))
                    for (const LLBind& b : g_llKeys)
                        already = already || (b.hold && b.code == regVk[static_cast<size_t>(id)] &&
                                              holdOk(b.hold) && modsOk(b, false, currentMods()));
                if (!already && id >= 0 && id < static_cast<int>(regVk.size())) {
                    held.push_back({id, regVk[static_cast<size_t>(id)]});
                    fire(id, true);
                }
            }
        }
        // отпустили клавишу RegisterHotKey — узнаём по состоянию клавиатуры
        for (size_t i = 0; i < held.size();) {
            if (!keyHeld(static_cast<int>(held[i].vk))) {
                const int id = held[i].id;
                held.erase(held.begin() + static_cast<long>(i));
                fire(id, false);
            } else {
                ++i;
            }
        }
        if (!g_llEvents.empty()) {
            const auto events = g_llEvents;
            g_llEvents.clear();
            for (const auto& e : events) fire(e.first, e.second);
        }
    }
    clear();
}
