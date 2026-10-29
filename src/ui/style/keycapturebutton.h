#pragma once
#include <QPushButton>
#include "core/i18n.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QKeySequence>
#include <QStyle>
#include <QSet>
#include "core/keys.h"

// Кнопка «Своё сочетание»: нажимаешь — и жмёшь что хочешь: букву, цифру, F-клавишу,
// сочетание с Ctrl/Alt/Shift или две клавиши («держу Tab, жму F» — Tab+F). Одна
// клавиша записывается, когда её отпустят (пока держишь — можно нажать вторую).
// Понимает русскую раскладку, подсказывает про мультимедиа-клавиши ноутбука.
class KeyCaptureButton : public QPushButton {
    Q_OBJECT
public:
    explicit KeyCaptureButton(bool compact = false, QWidget* parent = nullptr)
        : QPushButton(parent), compact_(compact) {
        setText(idleText());
        setAccessibleName(T("Своё сочетание для старт/стоп"));
        setToolTip(T("Нажми и затем нажми свою клавишу или сочетание:\nбукву, цифру, F-клавишу, Ctrl+Shift+Z… (Esc — отмена)\nДве клавиши: зажми первую (например Tab) и нажми вторую — получится Tab+F.\nМожно и кнопку мыши: колёсико или боковые Mouse 4 / Mouse 5."));
        timeout_.setSingleShot(true);
        // время вышло: первую клавишу так и держат — её и записываем, иначе отмена
        connect(&timeout_, &QTimer::timeout, this, [this]() { pending_ ? finishPending() : finish(false, {}); });
        connect(this, &QPushButton::clicked, this, [this]() {
            if (capturing_) return;
            capturing_ = true;
            setProperty("capturing", true);
            style()->unpolish(this); style()->polish(this);
            setText(waitText());
            setFocus();
            grabKeyboard();             // все нажатия — сюда, даже если фокус ушёл
            grabMouse();                // и кнопки мыши (СКМ, Mouse 4/5)
            timeout_.start(15000);
            emit captureStarted();
        });
    }

    bool isCapturing() const { return capturing_; }
    void setLabels(const QString& idle, const QString& wait) {
        idle_ = idle; wait_ = wait;
        setText(capturing_ ? waitText() : idleText());
    }

signals:
    void captureStarted();
    void captureFinished(bool accepted, const HotkeySpec& spec);
    void hint(const QString& text);     // пояснение, почему клавиша не подошла

protected:
    // Tab и Shift+Tab Qt забирает под переход фокуса (и запись срывалась) — пока
    // ждём сочетание, это обычная клавиша
    bool event(QEvent* e) override {
        // клавиши, что держали при записи, — их повтор и отпускание кнопке не нужны
        // (Tab ушёл бы в переход фокуса, Enter нажал бы кнопку снова)
        if (!capturing_ && (e->type() == QEvent::KeyPress || e->type() == QEvent::ShortcutOverride ||
                            e->type() == QEvent::KeyRelease)) {
            auto* ke = static_cast<QKeyEvent*>(e);
            const quint32 id = keyId(ke);
            if (swallow_.contains(id)) {
                if (e->type() != QEvent::KeyRelease && !ke->isAutoRepeat()) {
                    swallow_.remove(id);        // отпустили где-то ещё и нажали заново — это уже новое нажатие
                } else {
                    if (e->type() == QEvent::KeyRelease && !ke->isAutoRepeat()) swallow_.remove(id);
                    e->accept();
                    return true;
                }
            }
        }
        // записываем — ни одно сочетание окна (Ctrl+Z редактора и т. п.) не забирает клавишу себе
        if (capturing_ && e->type() == QEvent::ShortcutOverride) { e->accept(); return true; }
        if (capturing_ && e->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
                e->accept();
                keyPressEvent(ke);
                return true;
            }
        }
        return QPushButton::event(e);
    }
    bool focusNextPrevChild(bool next) override { return capturing_ ? false : QPushButton::focusNextPrevChild(next); }
    void keyPressEvent(QKeyEvent* e) override {
        if (!capturing_) { QPushButton::keyPressEvent(e); return; }
        e->accept();
        if (e->isAutoRepeat()) return;          // держат первую клавишу — ждём вторую
        const int key = e->key();
        if (key == Qt::Key_Escape && e->modifiers() == Qt::NoModifier) { finish(false, {}); return; }
        // сами модификаторы — ждём основную клавишу
        if (key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift ||
            key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_unknown) {
            if (!compact_ && !pending_) setText(modsText(e->modifiers()) + "…");
            return;
        }
        const bool keypad = e->modifiers() & Qt::KeypadModifier;
        const KeyDef* k = Keys::fromKeyEvent(key, keypad, e->nativeVirtualKey());
        if (!k) {
            const QString name = QKeySequence(key).toString(QKeySequence::NativeText);
            if (isMediaKey(key))
                emit hint(T("Нажата «%1» — это мультимедиа-клавиша. На ноутбуке F-клавиши часто работают только вместе с Fn.").arg(name));
            else
                emit hint(T("Клавиша «%1» не поддерживается, нажми другую.")
                              .arg(name.isEmpty() ? QString::number(key, 16) : name));
            setText(waitText());
            return;
        }
        HotkeySpec sp;
        sp.key = QString::fromLatin1(k->name);
        const Qt::KeyboardModifiers m = e->modifiers();
        if (m & Qt::ControlModifier) sp.mods |= ModCtrl;
        if (m & Qt::AltModifier)     sp.mods |= ModAlt;
        if (m & Qt::ShiftModifier)   sp.mods |= ModShift;
        if (m & Qt::MetaModifier)    sp.mods |= ModWin;
        if (!sp.isGlobalSafe()) {
            emit hint(T("«%1» нельзя назначить — нажми клавишу вместе с ним.").arg(sp.display()));
            setText(waitText());
            return;
        }
        if (pending_) {
            if (k == pending_) return;
            // первая ещё зажата — получилось сочетание из двух: «Tab+F»
            HotkeySpec chord = sp;
            chord.hold = QString::fromLatin1(pending_->name);
            chord.mods = pendingMods_ | sp.mods;
            swallow_.insert(pendingId_);
            swallow_.insert(keyId(e));
            if (!chord.isValid() || !chord.isGlobalSafe()) {
                emit hint(T("«%1» нельзя назначить — выбери другие клавиши.").arg(chord.display()));
                finish(false, {});
                return;
            }
            finish(true, chord);
            return;
        }
        // с Ctrl/Alt/Win — обычное сочетание, сразу (вторую клавишу к нему не добавить)
        if ((sp.mods & (ModCtrl | ModAlt | ModWin)) || !HotkeySpec::holdAllowed(sp.key)) {
            swallow_.insert(keyId(e));
            finish(true, sp);
            return;
        }
        // одна клавиша: запишем, когда отпустят; нажмут вторую, не отпуская, — сочетание
        pending_ = k;
        pendingId_ = keyId(e);
        pendingMods_ = sp.mods;
        if (!compact_) setText(sp.display() + T(" + … (или отпусти)"));
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (!capturing_) { QPushButton::mousePressEvent(e); return; }
        e->accept();
        if (pending_) swallow_.insert(pendingId_);  // сочетания с мышью нет — записываем кнопку
        int button = 0;
        switch (e->button()) {
            case Qt::MiddleButton:  button = 2; break;
            case Qt::BackButton:    button = 8; break;
            case Qt::ForwardButton: button = 9; break;
            case Qt::LeftButton:    finish(false, {}); return;       // щёлкнул мимо — отмена
            default:
                emit hint(T("ЛКМ и ПКМ нужны в игре — выбери колёсико, Mouse 4 или Mouse 5, либо клавишу."));
                return;
        }
        HotkeySpec sp;
        sp.key = MouseButtons::itemName(button);
        const Qt::KeyboardModifiers m = e->modifiers();
        if (m & Qt::ControlModifier) sp.mods |= ModCtrl;
        if (m & Qt::AltModifier)     sp.mods |= ModAlt;
        if (m & Qt::ShiftModifier)   sp.mods |= ModShift;
        if (m & Qt::MetaModifier)    sp.mods |= ModWin;
        finish(true, sp);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (capturing_) { e->accept(); return; }
        QPushButton::mouseReleaseEvent(e);
    }
    void keyReleaseEvent(QKeyEvent* e) override {
        if (!capturing_) { QPushButton::keyReleaseEvent(e); return; }
        e->accept();
        if (e->isAutoRepeat() || !pending_) return;
        // отпустили первую, так и не нажав вторую, — это одна клавиша (Shift+Tab
        // при отпускании может прийти как Tab — сверяем и по коду клавиши)
        const bool keypad = e->modifiers() & Qt::KeypadModifier;
        if (keyId(e) == pendingId_ || Keys::fromKeyEvent(e->key(), keypad, e->nativeVirtualKey()) == pending_)
            finishPending();
    }
    void focusOutEvent(QFocusEvent* e) override {
        QPushButton::focusOutEvent(e);
        if (capturing_) finish(false, {});
    }

private:
    // клавиша — по коду на клавиатуре (на macOS кода нет — по виртуальному)
    static quint32 keyId(const QKeyEvent* e) {
        return e->nativeScanCode() ? e->nativeScanCode() : (0x80000000u | e->nativeVirtualKey());
    }
    void finishPending() {
        HotkeySpec sp;
        sp.key = QString::fromLatin1(pending_->name);
        sp.mods = pendingMods_;
        finish(true, sp);
    }
    void finish(bool accepted, const HotkeySpec& sp) {
        if (!capturing_) return;
        pending_ = nullptr;
        pendingId_ = 0;
        pendingMods_ = 0;
        capturing_ = false;
        timeout_.stop();
        releaseKeyboard();
        releaseMouse();
        setProperty("capturing", false);
        style()->unpolish(this); style()->polish(this);
        setText(idleText());
        emit captureFinished(accepted, sp);
    }
    static QString modsText(Qt::KeyboardModifiers m) {
        QString s;
        if (m & Qt::ControlModifier) s += "Ctrl+";
#ifdef __APPLE__
        if (m & Qt::AltModifier)     s += "Option+";
        if (m & Qt::ShiftModifier)   s += "Shift+";
        if (m & Qt::MetaModifier)    s += "Cmd+";
#else
        if (m & Qt::AltModifier)     s += "Alt+";
        if (m & Qt::ShiftModifier)   s += "Shift+";
        if (m & Qt::MetaModifier)    s += "Win+";
#endif
        return s;
    }
    static bool isMediaKey(int k) {
        return (k >= Qt::Key_Back && k <= Qt::Key_LaunchF) ||
               (k >= Qt::Key_MonBrightnessUp && k <= Qt::Key_Hibernate) ||
               k == Qt::Key_VolumeDown || k == Qt::Key_VolumeUp || k == Qt::Key_VolumeMute ||
               k == Qt::Key_MicMute || k == Qt::Key_TouchpadToggle ||
               k == Qt::Key_TouchpadOn || k == Qt::Key_TouchpadOff;
    }
    QString idleText() const { return compact_ ? QString() : (idle_.isEmpty() ? T("Своё сочетание") : idle_); }
    QString waitText() const { return compact_ ? QString("…") : (wait_.isEmpty() ? T("Нажми клавиши…") : wait_); }
    bool compact_ = false;
    QString idle_, wait_;
    bool capturing_ = false;
    QTimer timeout_;
    const KeyDef* pending_ = nullptr;   // первая клавиша — ещё зажата
    quint32 pendingId_ = 0;
    int pendingMods_ = 0;
    QSet<quint32> swallow_;             // зажаты с записи — их повтор и отпускание не наши
};
