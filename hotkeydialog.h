#pragma once
// Окно «Назначить клавишу»: из списка (с поиском) или своё сочетание,
// можно и кнопку мыши (колёсико, Mouse 4/5). Для клавиш профилей и
// клавиши «активна / неактивна» у игры.
#include <QDialog>
#include <functional>
#include "keys.h"

class KeyPicker;
class KeyCaptureButton;
class QLabel;

class HotkeyDialog : public QDialog {
    Q_OBJECT
public:
    // suspend(true/false) — снять глобальные клавиши на время ввода своей
    static bool ask(QWidget* parent, const QString& title, const QString& note, HotkeySpec& spec,
                    bool allowNone, const std::function<void(bool)>& suspend = {});
private:
    HotkeyDialog(const QString& title, const QString& note, const HotkeySpec& spec, bool allowNone,
                 const std::function<void(bool)>& suspend, QWidget* parent);
    void setSpec(const HotkeySpec& sp);
    HotkeySpec spec_;
    KeyPicker* pick_;
    KeyCaptureButton* capture_;
    QLabel* hint_;
    QLabel* current_;
    std::function<void(bool)> suspend_;
};
