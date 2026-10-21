#pragma once
#include <QPushButton>
#include "i18n.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QKeySequence>
#include <QStyle>
#include "keys.h"

// Кнопка «Своё сочетание»: нажимаешь — и жмёшь что хочешь: букву, цифру, F-клавишу
// или сочетание с Ctrl/Alt/Shift. Понимает русскую раскладку, подсказывает про
// мультимедиа-клавиши ноутбука.
class KeyCaptureButton : public QPushButton {
    Q_OBJECT
public:
    explicit KeyCaptureButton(bool compact = false, QWidget* parent = nullptr)
        : QPushButton(parent), compact_(compact) {
        setText(idleText());
        setAccessibleName(T("Своё сочетание для старт/стоп"));
        setToolTip(T("Нажми и затем нажми свою клавишу или сочетание:\nбукву, цифру, F-клавишу, Ctrl+Shift+Z… (Esc — отмена)\nМожно и кнопку мыши: колёсико или боковые Mouse 4 / Mouse 5."));
        timeout_.setSingleShot(true);
        connect(&timeout_, &QTimer::timeout, this, [this]() { finish(false, {}); });
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
        if (capturing_ && (e->type() == QEvent::KeyPress || e->type() == QEvent::ShortcutOverride)) {
            auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
                e->accept();
                if (e->type() == QEvent::KeyPress) keyPressEvent(ke);
                return true;
            }
        }
        return QPushButton::event(e);
    }
    bool focusNextPrevChild(bool next) override { return capturing_ ? false : QPushButton::focusNextPrevChild(next); }
    void keyPressEvent(QKeyEvent* e) override {
        if (!capturing_) { QPushButton::keyPressEvent(e); return; }
        e->accept();
        const int key = e->key();
        if (key == Qt::Key_Escape && e->modifiers() == Qt::NoModifier) { finish(false, {}); return; }
        // сами модификаторы — ждём основную клавишу
        if (key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift ||
            key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_unknown) {
            if (!compact_) setText(modsText(e->modifiers()) + "…");
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
        finish(true, sp);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (!capturing_) { QPushButton::mousePressEvent(e); return; }
        e->accept();
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
        if (capturing_) { e->accept(); return; }
        QPushButton::keyReleaseEvent(e);
    }
    void focusOutEvent(QFocusEvent* e) override {
        QPushButton::focusOutEvent(e);
        if (capturing_) finish(false, {});
    }

private:
    void finish(bool accepted, const HotkeySpec& sp) {
        if (!capturing_) return;
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
};
