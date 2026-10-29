#pragma once
// Единая таблица клавиш: одно имя -> код Windows, X11 KeySym и Qt::Key.
// Всё (действия, глобальный хоткей, захват клавиши, списки в интерфейсе)
// берёт клавиши отсюда, поэтому они не могут "разъехаться" между собой.
#include <QString>
#include <QStringList>
#include <QList>

struct KeyDef {
    const char* name;      // каноническое имя: "a", "f1", "space", "up", "num5"
    const char* label;     // для показа: "A", "F1", "Пробел", "↑", "Num 5"
    int qtKey;             // Qt::Key
    bool qtKeypad;         // клавиша цифрового блока (Qt::KeypadModifier)
    unsigned short winVK;  // виртуальный код Windows
    bool winExtended;      // KEYEVENTF_EXTENDEDKEY (стрелки, Insert, Home...)
    unsigned long x11Sym;  // X11 KeySym (0 в Windows-сборке)
    bool hotkeyAlone;      // хоткей можно «забрать» себе (F-клавиши); false — печатная клавиша
    bool action;           // есть в списке клавиш для действий
};

namespace Keys {
    const QList<KeyDef>& all();
    const KeyDef* byName(const QString& name);           // без учёта регистра
    const KeyDef* byQtKey(int qtKey, bool keypad);       // точное совпадение
    // Для захвата клавиши: понимает русскую раскладку (Ф -> A),
    // Shift+цифры (! -> 1) и, на Windows, физический код клавиши.
    const KeyDef* fromKeyEvent(int qtKey, bool keypad, unsigned int nativeVirtualKey);
    const KeyDef* byWinVK(unsigned int vk);
    int macKeyCode(const QString& name);                 // kVK_* на macOS, -1 — нет на Mac
    const KeyDef* byMacKeyCode(int code);
    bool available(const QString& name);                 // есть ли клавиша на этой системе
    QStringList actionKeyNames();                        // список для действий
    QStringList hotkeyPresetNames();                     // частые клавиши хоткея (F1–F12…)
    QStringList hotkeyKeyNames();                        // вся клавиатура (без Shift/Ctrl/Alt)
    bool isTypingKey(const QString& name);               // буква, цифра, знак, стрелка…
    QString groupOf(const QString& name);                // "Буквы", "F-клавиши", …
    QString label(const QString& name);
}

// Кнопки мыши (номер как в X11: 1 — левая, 2 — средняя, 3 — правая,
// 8 — боковая «назад» (Mouse 4), 9 — боковая «вперёд» (Mouse 5))
namespace MouseButtons {
    QList<int> all();              // 1, 3, 2, 8, 9
    bool valid(int button);
    QString shortLabel(int button);    // "ЛКМ", "Mouse 4"
    QString longLabel(int button);     // "Левая кнопка мыши"
    // можно ли сделать хоткеем: средняя и боковые (ЛКМ и ПКМ нужны в игре)
    bool hotkeyAllowed(int button);
    QString itemName(int button);      // "mouse:8" — имя в списках выбора
    int fromItemName(const QString& name);   // "mouse:8" -> 8, иначе 0
}

// Модификаторы хоткея (на macOS: Ctrl = Control, Alt = Option, Win = Command)
enum HotkeyMod { ModCtrl = 1, ModAlt = 2, ModShift = 4, ModWin = 8 };

// Глобальный хоткей: клавиша + модификаторы. Хранится строкой "ctrl+alt+f7".
// Сочетание из двух клавиш («держу Tab, жму F») — "tab+f": hold = "tab", key = "f".
struct HotkeySpec {
    QString key;   // каноническое имя клавиши или кнопка мыши: "mouse:2", "mouse:8", "mouse:9"
    int mods = 0;
    QString hold;  // клавиша, которую держат, пока жмут key ("" — обычный хоткей)
    // «Не пропускать нажатие в игру»: кнопка мыши или буква/цифра (обычно сквозные)
    // только включает кликер — игра и другие программы нажатие не получают. В строке
    // (toString) не хранится — у клавиши старта игры отдельно. В == не участвует.
    bool block = false;

    bool isChord() const { return !hold.isEmpty(); }
    // годится ли клавиша быть первой в сочетании (не Shift/Ctrl/Alt, не CapsLock, не мышь)
    static bool holdAllowed(const QString& name);

    bool isMouse() const { return key.startsWith("mouse:"); }
    int mouseButton() const;    // 0 — не мышь

    bool isValid() const;
    // Годится ли в глобальные клавиши (любая клавиша, кроме самих Shift/Ctrl/Alt)
    bool isGlobalSafe() const;
    // Буква, цифра или знак без Ctrl/Alt/Win: хоткей «сквозной» — нажатие
    // доходит до игры и других программ (клавиша продолжает печататься).
    // Сочетание из двух клавиш — всегда сквозное: обе доходят до игры.
    bool passthrough() const;
    bool canBlock() const;      // сквозная сама по себе (не сочетание) — её можно не пропускать
    QString toString() const;   // "ctrl+alt+f7", "tab+f"
    QString display() const;    // "Ctrl+Alt+F7", "Tab+F"
    static HotkeySpec parse(const QString& s);
    bool operator==(const HotkeySpec& o) const { return key == o.key && mods == o.mods && hold == o.hold; }
    bool operator!=(const HotkeySpec& o) const { return !(*this == o); }
};
