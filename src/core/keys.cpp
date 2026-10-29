#include "core/keys.h"
#include "core/i18n.h"
#include <Qt>
#include <QHash>

#if !defined(_WIN32) && !defined(__APPLE__)
    #include <X11/keysym.h>
    #define KS(x) static_cast<unsigned long>(XK_##x)
#else
    #define KS(x) 0UL
#endif

// Порядок строк = порядок в выпадающих списках.
//            name        label        Qt::Key           keypad  VK    ext   X11 keysym      hotkeyAlone action
#define LETTER(n, U) { #n, #U, Qt::Key_##U, false, static_cast<unsigned short>(#U[0]), false, KS(n), false, true }
#define DIGIT(d)     { #d, #d, Qt::Key_##d, false, static_cast<unsigned short>(0x30 + d), false, KS(d), false, true }
#define FKEY(i, vk)  { "f" #i, "F" #i, Qt::Key_F##i, false, vk, false, KS(F##i), true, (i) <= 12 }
#define NUM(d)       { "num" #d, "Num " #d, Qt::Key_##d, true, static_cast<unsigned short>(0x60 + d), false, KS(KP_##d), false, true }
// hotkeyAlone = true: клавишу можно «забрать» под старт/стоп (F-клавиши, Home…).
// false — «печатная» клавиша (буквы, цифры, знаки): хоткей на ней не мешает
// печатать и играть — нажатие доходит до программ как обычно.

static const QList<KeyDef> kTable = {
    // --- частые для игр ---
    { "space",  "Пробел", Qt::Key_Space,  false, 0x20, false, KS(space),  false, true },
    LETTER(f, F), LETTER(e, E), LETTER(q, Q), LETTER(r, R),
    { "enter",  "Enter",  Qt::Key_Return, false, 0x0D, false, KS(Return), false, true },
    { "escape", "Esc",    Qt::Key_Escape, false, 0x1B, false, KS(Escape), false, true },
    { "tab",    "Tab",    Qt::Key_Tab,    false, 0x09, false, KS(Tab),    false, true },
    { "shift",  "Shift",  Qt::Key_Shift,  false, 0x10, false, KS(Shift_L),   false, true },
    { "ctrl",   "Ctrl",   Qt::Key_Control,false, 0x11, false, KS(Control_L), false, true },
    { "alt",    "Alt",    Qt::Key_Alt,    false, 0x12, false, KS(Alt_L),     false, true },
    // --- цифры ---
    DIGIT(1), DIGIT(2), DIGIT(3), DIGIT(4), DIGIT(5),
    DIGIT(6), DIGIT(7), DIGIT(8), DIGIT(9), DIGIT(0),
    // --- остальные буквы ---
    LETTER(a, A), LETTER(b, B), LETTER(c, C), LETTER(d, D), LETTER(g, G),
    LETTER(h, H), LETTER(i, I), LETTER(j, J), LETTER(k, K), LETTER(l, L),
    LETTER(m, M), LETTER(n, N), LETTER(o, O), LETTER(p, P), LETTER(s, S),
    LETTER(t, T), LETTER(u, U), LETTER(v, V), LETTER(w, W), LETTER(x, X),
    LETTER(y, Y), LETTER(z, Z),
    // --- F1..F24 (F13+ часто назначают на боковые кнопки мыши) ---
    FKEY(1, 0x70), FKEY(2, 0x71), FKEY(3, 0x72), FKEY(4, 0x73),
    FKEY(5, 0x74), FKEY(6, 0x75), FKEY(7, 0x76), FKEY(8, 0x77),
    FKEY(9, 0x78), FKEY(10, 0x79), FKEY(11, 0x7A), FKEY(12, 0x7B),
    FKEY(13, 0x7C), FKEY(14, 0x7D), FKEY(15, 0x7E), FKEY(16, 0x7F),
    FKEY(17, 0x80), FKEY(18, 0x81), FKEY(19, 0x82), FKEY(20, 0x83),
    FKEY(21, 0x84), FKEY(22, 0x85), FKEY(23, 0x86), FKEY(24, 0x87),
    // --- стрелки и навигация (на Windows это "расширенные" клавиши) ---
    { "up",       "↑ Вверх",  Qt::Key_Up,       false, 0x26, true, KS(Up),        false, true },
    { "down",     "↓ Вниз",   Qt::Key_Down,     false, 0x28, true, KS(Down),      false, true },
    { "left",     "← Влево",  Qt::Key_Left,     false, 0x25, true, KS(Left),      false, true },
    { "right",    "→ Вправо", Qt::Key_Right,    false, 0x27, true, KS(Right),     false, true },
    { "insert",   "Insert",   Qt::Key_Insert,   false, 0x2D, true, KS(Insert),    true,  true },
    { "delete",   "Delete",   Qt::Key_Delete,   false, 0x2E, true, KS(Delete),    false, true },
    { "home",     "Home",     Qt::Key_Home,     false, 0x24, true, KS(Home),      true,  true },
    { "end",      "End",      Qt::Key_End,      false, 0x23, true, KS(End),       true,  true },
    { "pageup",   "PageUp",   Qt::Key_PageUp,   false, 0x21, true, KS(Prior),     true,  true },
    { "pagedown", "PageDown", Qt::Key_PageDown, false, 0x22, true, KS(Next),      true,  true },
    { "pause",    "Pause",    Qt::Key_Pause,    false, 0x13, false, KS(Pause),    true,  false },
    { "scrolllock","ScrollLock",Qt::Key_ScrollLock,false,0x91,false, KS(Scroll_Lock), true, false },
    // --- цифровой блок ---
    NUM(0), NUM(1), NUM(2), NUM(3), NUM(4), NUM(5), NUM(6), NUM(7), NUM(8), NUM(9),
    { "numadd",      "Num +", Qt::Key_Plus,     true, 0x6B, false, KS(KP_Add),      false, true },
    { "numsubtract", "Num −", Qt::Key_Minus,    true, 0x6D, false, KS(KP_Subtract), false, true },
    { "nummultiply", "Num *", Qt::Key_Asterisk, true, 0x6A, false, KS(KP_Multiply), false, true },
    { "numdivide",   "Num /", Qt::Key_Slash,    true, 0x6F, true,  KS(KP_Divide),   false, true },
    { "numdecimal",  "Num .", Qt::Key_Period,   true, 0x6E, false, KS(KP_Decimal),  false, true },
    // --- знаки ---
    { "minus",        "-",  Qt::Key_Minus,        false, 0xBD, false, KS(minus),        false, true },
    { "equal",        "=",  Qt::Key_Equal,        false, 0xBB, false, KS(equal),        false, true },
    { "bracketleft",  "[",  Qt::Key_BracketLeft,  false, 0xDB, false, KS(bracketleft),  false, true },
    { "bracketright", "]",  Qt::Key_BracketRight, false, 0xDD, false, KS(bracketright), false, true },
    { "backslash",    "\\", Qt::Key_Backslash,    false, 0xDC, false, KS(backslash),    false, true },
    { "semicolon",    ";",  Qt::Key_Semicolon,    false, 0xBA, false, KS(semicolon),    false, true },
    { "apostrophe",   "'",  Qt::Key_Apostrophe,   false, 0xDE, false, KS(apostrophe),   false, true },
    { "grave",        "`",  Qt::Key_QuoteLeft,    false, 0xC0, false, KS(grave),        false, true },
    { "comma",        ",",  Qt::Key_Comma,        false, 0xBC, false, KS(comma),        false, true },
    { "period",       ".",  Qt::Key_Period,       false, 0xBE, false, KS(period),       false, true },
    { "slash",        "/",  Qt::Key_Slash,        false, 0xBF, false, KS(slash),        false, true },
    // --- служебные ---
    { "backspace",  "Backspace",   Qt::Key_Backspace, false, 0x08, false, KS(BackSpace), false, true },
    { "capslock",   "CapsLock",    Qt::Key_CapsLock,  false, 0x14, false, KS(Caps_Lock), false, false },
    { "print",      "PrintScreen", Qt::Key_Print,     false, 0x2C, true,  KS(Print),     true,  false },
    { "menu",       "Menu",        Qt::Key_Menu,      false, 0x5D, true,  KS(Menu),      true,  true },
};

const QList<KeyDef>& Keys::all() { return kTable; }

// Коды клавиш macOS (kVK_* из Carbon/HIToolbox — физическое место клавиши,
// от раскладки не зависят). -1 — такой клавиши на Mac нет.
int Keys::macKeyCode(const QString& name) {
    static const QHash<QString, int> m = {
        {"a",0x00},{"s",0x01},{"d",0x02},{"f",0x03},{"h",0x04},{"g",0x05},{"z",0x06},{"x",0x07},
        {"c",0x08},{"v",0x09},{"b",0x0B},{"q",0x0C},{"w",0x0D},{"e",0x0E},{"r",0x0F},{"y",0x10},
        {"t",0x11},{"1",0x12},{"2",0x13},{"3",0x14},{"4",0x15},{"6",0x16},{"5",0x17},{"equal",0x18},
        {"9",0x19},{"7",0x1A},{"minus",0x1B},{"8",0x1C},{"0",0x1D},{"bracketright",0x1E},{"o",0x1F},
        {"u",0x20},{"bracketleft",0x21},{"i",0x22},{"p",0x23},{"enter",0x24},{"l",0x25},{"j",0x26},
        {"apostrophe",0x27},{"k",0x28},{"semicolon",0x29},{"backslash",0x2A},{"comma",0x2B},
        {"slash",0x2C},{"n",0x2D},{"m",0x2E},{"period",0x2F},{"tab",0x30},{"space",0x31},
        {"grave",0x32},{"backspace",0x33},{"escape",0x35},{"shift",0x38},{"capslock",0x39},
        {"alt",0x3A},{"ctrl",0x3B},
        {"numdecimal",0x41},{"nummultiply",0x43},{"numadd",0x45},{"numdivide",0x4B},
        {"numsubtract",0x4E},{"num0",0x52},{"num1",0x53},{"num2",0x54},{"num3",0x55},
        {"num4",0x56},{"num5",0x57},{"num6",0x58},{"num7",0x59},{"num8",0x5B},{"num9",0x5C},
        {"f1",0x7A},{"f2",0x78},{"f3",0x63},{"f4",0x76},{"f5",0x60},{"f6",0x61},{"f7",0x62},
        {"f8",0x64},{"f9",0x65},{"f10",0x6D},{"f11",0x67},{"f12",0x6F},{"f13",0x69},{"f14",0x6B},
        {"f15",0x71},{"f16",0x6A},{"f17",0x40},{"f18",0x4F},{"f19",0x50},{"f20",0x5A},
        {"insert",0x72},   // Help — на месте Insert у PC-клавиатур
        {"home",0x73},{"pageup",0x74},{"delete",0x75},{"end",0x77},{"pagedown",0x79},
        {"left",0x7B},{"right",0x7C},{"down",0x7D},{"up",0x7E},
        {"menu",0x6E},
    };
    return m.value(name.trimmed().toLower(), -1);
}

const KeyDef* Keys::byMacKeyCode(int code) {
    if (code < 0) return nullptr;
    for (const KeyDef& k : kTable)
        if (macKeyCode(QString::fromLatin1(k.name)) == code) return &k;
    return nullptr;
}

bool Keys::available(const QString& name) {
#ifdef __APPLE__
    return macKeyCode(name) >= 0;
#else
    return byName(name) != nullptr;
#endif
}

const KeyDef* Keys::byName(const QString& name) {
    static QHash<QString, int> idx = [] {
        QHash<QString, int> h;
        for (int i = 0; i < kTable.size(); ++i) h.insert(QString::fromLatin1(kTable[i].name), i);
        return h;
    }();
    auto it = idx.constFind(name.trimmed().toLower());
    return it == idx.constEnd() ? nullptr : &kTable[it.value()];
}

const KeyDef* Keys::byQtKey(int qtKey, bool keypad) {
    // Enter на цифровом блоке — тот же Enter
    if (qtKey == Qt::Key_Enter) qtKey = Qt::Key_Return;
    // Буквы приходят и как Key_A, и (с Shift) так же — регистр не важен
    for (const KeyDef& k : kTable) {
        if (k.qtKey != qtKey) continue;
        if (k.qtKeypad == keypad) return &k;
    }
    // клавиша цифрового блока, у которой нет Num-версии, — обычная клавиша
    // (обратно нельзя: Shift+8 на US-раскладке — это «*», но не Num *)
    if (keypad)
        for (const KeyDef& k : kTable)
            if (k.qtKey == qtKey && !k.qtKeypad) return &k;
    return nullptr;
}

const KeyDef* Keys::byWinVK(unsigned int vk) {
    if (vk == 0) return nullptr;
    for (const KeyDef& k : kTable)
        if (k.winVK == vk) return &k;
    return nullptr;
}

const KeyDef* Keys::fromKeyEvent(int qtKey, bool keypad, unsigned int nativeVirtualKey) {
    if (qtKey == Qt::Key_Backtab) qtKey = Qt::Key_Tab;     // Shift+Tab приходит отдельной клавишей
    if (const KeyDef* k = byQtKey(qtKey, keypad)) return k;
#ifdef _WIN32
    // На Windows виртуальный код не зависит от раскладки (VK 'F' и в русской)
    if (const KeyDef* k = byWinVK(nativeVirtualKey)) return k;
#elif defined(__APPLE__)
    // На macOS nativeVirtualKey — код kVK_*: физическая клавиша, как и на Windows
    if (const KeyDef* k = byMacKeyCode(static_cast<int>(nativeVirtualKey))) return k;
#else
    Q_UNUSED(nativeVirtualKey);
#endif
    // Русская раскладка ЙЦУКЕН -> физическая клавиша QWERTY
    static const QHash<int, const char*> ru = {
        {0x0419,"q"},{0x0426,"w"},{0x0423,"e"},{0x041A,"r"},{0x0415,"t"},{0x041D,"y"},
        {0x0413,"u"},{0x0428,"i"},{0x0429,"o"},{0x0417,"p"},{0x0424,"a"},{0x042B,"s"},
        {0x0412,"d"},{0x0410,"f"},{0x041F,"g"},{0x0420,"h"},{0x041E,"j"},{0x041B,"k"},
        {0x0414,"l"},{0x042F,"z"},{0x0427,"x"},{0x0421,"c"},{0x041C,"v"},{0x0418,"b"},
        {0x0422,"n"},{0x042C,"m"},
        {0x0425,"bracketleft"},{0x042A,"bracketright"},{0x0416,"semicolon"},{0x042D,"apostrophe"},
        {0x0411,"comma"},{0x042E,"period"},{0x0401,"grave"},
    };
    auto it = ru.constFind(qtKey);
    if (it != ru.constEnd()) return byName(QString::fromLatin1(it.value()));
    // Shift+цифра на US-раскладке
    static const QHash<int, const char*> shifted = {
        {Qt::Key_Exclam,"1"},{Qt::Key_At,"2"},{Qt::Key_NumberSign,"3"},{Qt::Key_Dollar,"4"},
        {Qt::Key_Percent,"5"},{Qt::Key_AsciiCircum,"6"},{Qt::Key_Ampersand,"7"},
        {Qt::Key_Asterisk,"8"},{Qt::Key_ParenLeft,"9"},{Qt::Key_ParenRight,"0"},
        // Shift+знак
        {Qt::Key_Underscore,"minus"},{Qt::Key_Plus,"equal"},{Qt::Key_BraceLeft,"bracketleft"},
        {Qt::Key_BraceRight,"bracketright"},{Qt::Key_Bar,"backslash"},{Qt::Key_Colon,"semicolon"},
        {Qt::Key_QuoteDbl,"apostrophe"},{Qt::Key_AsciiTilde,"grave"},{Qt::Key_Less,"comma"},
        {Qt::Key_Greater,"period"},{Qt::Key_Question,"slash"},
    };
    auto s = shifted.constFind(qtKey);
    if (s != shifted.constEnd()) return byName(QString::fromLatin1(s.value()));
    return nullptr;
}

QStringList Keys::actionKeyNames() {
    QStringList out;
    for (const KeyDef& k : kTable)
        if (k.action && available(QString::fromLatin1(k.name))) out << QString::fromLatin1(k.name);
    return out;
}

QStringList Keys::hotkeyPresetNames() {
    QStringList out;
    for (const char* n : { "f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12",
                           "insert","home","end","pageup","pagedown","pause","scrolllock" })
        if (available(QString::fromLatin1(n))) out << QString::fromLatin1(n);
    return out;
}

QStringList Keys::hotkeyKeyNames() {
    // всё, кроме самих модификаторов (и клавиш, которых нет на этой системе)
    QStringList out;
    for (const KeyDef& k : kTable)
        if (k.qtKey != Qt::Key_Shift && k.qtKey != Qt::Key_Control && k.qtKey != Qt::Key_Alt &&
            available(QString::fromLatin1(k.name)))
            out << QString::fromLatin1(k.name);
    return out;
}

bool Keys::isTypingKey(const QString& name) {
    const KeyDef* k = byName(name);
    return k && !k->hotkeyAlone;
}

QString Keys::groupOf(const QString& name) {
    const KeyDef* k = byName(name);
    if (!k) return QString();
    const QString n = QString::fromLatin1(k->name);
    if (n.size() == 1 && n[0].isLetter()) return T("Буквы");
    if (n.size() == 1 && n[0].isDigit()) return T("Цифры");
    if (n.startsWith("num")) return T("Цифровой блок");
    if (n.size() >= 2 && n[0] == 'f' && n[1].isDigit()) return T("F-клавиши");
    static const QStringList nav = {"up","down","left","right","insert","delete","home","end",
                                    "pageup","pagedown","pause","scrolllock","print","menu"};
    if (nav.contains(n)) return T("Навигация");
    static const QStringList sym = {"minus","equal","bracketleft","bracketright","backslash",
                                    "semicolon","apostrophe","grave","comma","period","slash"};
    if (sym.contains(n)) return T("Знаки");
    return T("Основные");
}

QString Keys::label(const QString& name) {
    const KeyDef* k = byName(name);
    return k ? T(k->label) : name;
}

// ---------------------------------------------------------------------------
QList<int> MouseButtons::all() { return {1, 3, 2, 8, 9}; }
bool MouseButtons::valid(int b) { return b == 1 || b == 2 || b == 3 || b == 8 || b == 9; }
QString MouseButtons::shortLabel(int b) {
    switch (b) {
        case 3: return T("ПКМ");
        case 2: return T("СКМ");
        case 8: return QStringLiteral("Mouse 4");
        case 9: return QStringLiteral("Mouse 5");
        default: return T("ЛКМ");
    }
}
QString MouseButtons::longLabel(int b) {
    switch (b) {
        case 3: return T("Правая кнопка мыши");
        case 2: return T("Средняя кнопка мыши (колёсико)");
        case 8: return T("Боковая кнопка мыши «назад» (Mouse 4)");
        case 9: return T("Боковая кнопка мыши «вперёд» (Mouse 5)");
        default: return T("Левая кнопка мыши");
    }
}
bool MouseButtons::hotkeyAllowed(int b) { return b == 2 || b == 8 || b == 9; }
QString MouseButtons::itemName(int b) { return QString("mouse:%1").arg(b); }
int MouseButtons::fromItemName(const QString& name) {
    if (!name.startsWith("mouse:")) return 0;
    const int b = name.mid(6).toInt();
    return valid(b) ? b : 0;
}

// ---------------------------------------------------------------------------
int HotkeySpec::mouseButton() const { return MouseButtons::fromItemName(key); }

bool HotkeySpec::holdAllowed(const QString& name) {
    const KeyDef* k = Keys::byName(name);
    if (!k) return false;
    // модификаторы — это mods; CapsLock переключает раскладку/регистр — держать его неудобно
    return k->qtKey != Qt::Key_Shift && k->qtKey != Qt::Key_Control && k->qtKey != Qt::Key_Alt &&
           k->qtKey != Qt::Key_CapsLock;
}

bool HotkeySpec::isValid() const {
    // сочетание — только из клавиш клавиатуры, и две разные
    if (isChord() && (isMouse() || hold == key || !holdAllowed(hold))) return false;
    if (isMouse()) return MouseButtons::hotkeyAllowed(mouseButton());
    return Keys::byName(key) != nullptr;
}

bool HotkeySpec::isGlobalSafe() const {
    if (isChord() && !isValid()) return false;
    if (isMouse()) return MouseButtons::hotkeyAllowed(mouseButton());
    const KeyDef* k = Keys::byName(key);
    if (!k) return false;
    if (k->qtKey == Qt::Key_Shift || k->qtKey == Qt::Key_Control || k->qtKey == Qt::Key_Alt)
        return false; // сам модификатор хоткеем быть не может
    return true;
}

bool HotkeySpec::canBlock() const {
    if (isChord() || !isGlobalSafe()) return false;
    return isMouse() || (Keys::isTypingKey(key) && !(mods & (ModCtrl | ModAlt | ModWin)));
}

bool HotkeySpec::passthrough() const {
    // «держу A, жму B»: ловим по хукам / сырым событиям — обе клавиши доходят до игры
    if (isChord()) return isGlobalSafe();
    // «не пропускать в игру» — забираем нажатие себе, как у F-клавиш
    if (block && canBlock()) return false;
    // кнопка мыши — всегда «сквозная»: нажатие доходит до игры
    if (isMouse()) return isGlobalSafe();
    // буква/цифра/знак без Ctrl, Alt и Win: ловим, не отнимая у программ
    return isGlobalSafe() && Keys::isTypingKey(key) && !(mods & (ModCtrl | ModAlt | ModWin));
}

QString HotkeySpec::toString() const {
    QString s;
    if (mods & ModCtrl)  s += "ctrl+";
    if (mods & ModAlt)   s += "alt+";
    if (mods & ModShift) s += "shift+";
    if (mods & ModWin)   s += "win+";
    if (isChord()) s += hold + "+";
    return s + key;
}

QString HotkeySpec::display() const {
    QString s;
    if (mods & ModCtrl)  s += "Ctrl+";
#ifdef __APPLE__
    if (mods & ModAlt)   s += "Option+";
    if (mods & ModShift) s += "Shift+";
    if (mods & ModWin)   s += "Cmd+";
#else
    if (mods & ModAlt)   s += "Alt+";
    if (mods & ModShift) s += "Shift+";
    if (mods & ModWin)   s += "Win+";
#endif
    if (isMouse()) return s + MouseButtons::shortLabel(mouseButton());
    if (isChord()) s += Keys::label(hold) + "+";
    return s + Keys::label(key);
}

HotkeySpec HotkeySpec::parse(const QString& str) {
    HotkeySpec h;
    const QStringList parts = str.trimmed().toLower().split('+', Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString p = parts[i].trimmed();
        if (i + 1 < parts.size()) {
            if (p == "ctrl" || p == "control") h.mods |= ModCtrl;
            else if (p == "alt") h.mods |= ModAlt;
            else if (p == "shift") h.mods |= ModShift;
            else if (p == "win" || p == "meta" || p == "super") h.mods |= ModWin;
            // не модификатор — клавиша, которую держат («tab+f»); такая одна
            else if (h.hold.isEmpty() && holdAllowed(p)) h.hold = p;
            else return HotkeySpec{}; // мусор в модификаторах
        } else if (p.startsWith("mouse:")) {
            h.key = MouseButtons::hotkeyAllowed(MouseButtons::fromItemName(p)) ? p : QString();
        } else {
            h.key = Keys::byName(p) ? p : QString();
        }
    }
    // сам модификатор основной клавишей хоткея быть не может ("ctrl+" и т.п.)
    if (h.key == "ctrl" || h.key == "alt" || h.key == "shift") h.key.clear();
    // сочетание с кнопкой мыши или из одной и той же клавиши — не годится
    if (h.isChord() && (h.isMouse() || h.hold == h.key)) h.key.clear();
    if (h.key.isEmpty()) { h.mods = 0; h.hold.clear(); }
    return h;
}
