#pragma once
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <vector>
#include <QList>
#include <QString>
#include "core/keys.h"

// Глобальные клавиши (работают, когда окно программы не в фокусе). Их может быть
// несколько: у каждой игры своя клавиша старт/стоп, клавиши профилей и т.д.
// Windows — RegisterHotKey, Linux — XGrabKey (X11/XWayland), macOS — CGEventTap.
// Буквы, цифры, знаки и кнопки мыши (СКМ, Mouse 4/5) — «сквозные»: Windows —
// хуки клавиатуры и мыши, Linux — сырые события XInput2, macOS — тот же
// CGEventTap без перехвата; нажатие при этом доходит до игры.
// Сообщает и нажатие, и отпускание — для режима «работает, пока держишь».
// Поток с перехватом (loop) у каждой ОС свой — platform/*/hotkey_*.cpp.
class GlobalHotkey {
public:
    enum class Status { Ok, Busy, Invalid, Unavailable, Suspended };
    // spec — какая клавиша из списка setBindings, down — нажата / отпущена
    using Callback = std::function<void(const HotkeySpec& spec, bool down)>;

    explicit GlobalHotkey(Callback cb, const QList<HotkeySpec>& bindings = {});
    ~GlobalHotkey();

    void setBindings(const QList<HotkeySpec>& specs);   // сменить на лету
    QList<HotkeySpec> bindings() const;
    void setSuspended(bool s);              // временно снять (пока ловим новую клавишу)
    bool suspended() const { return suspended_; }
    Status status(int id) const;
    Status statusOf(const HotkeySpec& sp) const;   // Unavailable, если такой клавиши нет в списке
    bool applied() const { return !rebind_; }      // новый список уже применён потоком

    // Вызвать в main() сразу после QApplication (Qt ставит свой обработчик ошибок X11).
    // Linux: обработчик ошибок X11, который не роняет программу и узнаёт, что клавиша занята
    static void init();

    // Лишние модификаторы: в игре держат Shift (присесть) или Ctrl (бег) и жмут F6 — включено,
    // F6 срабатывает и так; выключено — только когда, кроме F6, ничего не зажато.
    static void setAnyMods(bool on) { anyMods_ = on; }
    static bool anyMods() { return anyMods_; }
    // Подходят ли зажатые модификаторы held привязке с mods need. sameKey — модификаторы всех
    // привязок на той же клавише (можно и её саму): из подходящих срабатывает самая точная —
    // при F6 и Shift+F6 нажатие Shift+F6 включит только Shift+F6.
    static bool modsMatch(int held, int need, const std::vector<int>& sameKey = {});

private:
    void loop();
    void fire(int id, bool down);
    void setStatus(int id, Status s);
    void resetStatuses(Status s);
    Callback callback_;
    std::atomic<bool> running_{false};
    std::atomic<bool> rebind_{true};
    std::atomic<bool> suspended_{false};
    std::thread worker_;
    mutable std::mutex mutex_;
    QList<HotkeySpec> specs_;
    std::vector<Status> statuses_;
    QList<HotkeySpec> active_;      // список, по которому сейчас стоят перехваты (только поток)
    static std::atomic<bool> anyMods_;
};
