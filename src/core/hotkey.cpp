#include "core/hotkey.h"
#include <chrono>
#include <algorithm>
#include <iterator>

GlobalHotkey::GlobalHotkey(Callback cb, const QList<HotkeySpec>& bindings) : callback_(std::move(cb)) {
    specs_ = bindings;
    statuses_.assign(static_cast<size_t>(bindings.size()), Status::Ok);
    running_ = true;
    worker_ = std::thread(&GlobalHotkey::loop, this);
}

GlobalHotkey::~GlobalHotkey() {
    running_ = false;
    // цикл опрашивает флаг каждые ~15 мс, поэтому ждём поток честно
    if (worker_.joinable()) worker_.join();
}

void GlobalHotkey::setBindings(const QList<HotkeySpec>& specs) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        specs_ = specs;
        statuses_.assign(static_cast<size_t>(specs.size()), Status::Ok);
    }
    rebind_ = true;
}

QList<HotkeySpec> GlobalHotkey::bindings() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return specs_;
}

void GlobalHotkey::setSuspended(bool s) {
    suspended_ = s;
    rebind_ = true;
}

GlobalHotkey::Status GlobalHotkey::status(int id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    if (id < 0 || id >= static_cast<int>(statuses_.size())) return Status::Unavailable;
    return statuses_[static_cast<size_t>(id)];
}

GlobalHotkey::Status GlobalHotkey::statusOf(const HotkeySpec& sp) const {
    std::lock_guard<std::mutex> lk(mutex_);
    for (int i = 0; i < specs_.size(); ++i)
        if (specs_[i] == sp) return i < static_cast<int>(statuses_.size()) ? statuses_[static_cast<size_t>(i)] : Status::Ok;
    return Status::Unavailable;
}

void GlobalHotkey::setStatus(int id, Status s) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (id >= 0 && id < static_cast<int>(statuses_.size())) statuses_[static_cast<size_t>(id)] = s;
}

void GlobalHotkey::resetStatuses(Status s) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (Status& st : statuses_) st = s;
}

void GlobalHotkey::fire(int id, bool down) {
    if (id < 0 || id >= active_.size()) return;
    // отпускание сообщаем всегда (даже на паузе) — чтобы ничего не осталось «зажатым»
    if ((!suspended_ || !down) && callback_) callback_(active_[id], down);
}
