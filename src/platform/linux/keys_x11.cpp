#include "core/keys.h"

#include <X11/Xlib.h>

// Кликер жмёт клавишу keycode'ом XKeysymToKeycode(её KeySym): обратное к этому — та же
// клавиша при любой раскладке (украинская, греческая…), а не только при латинской и русской
const KeyDef* Keys::byX11Keycode(unsigned int keycode) {
    static Display* d = XOpenDisplay(nullptr);     // поток интерфейса, на всё время работы
    if (!d || keycode == 0) return nullptr;
    for (const KeyDef& k : all())
        if (k.x11Sym && XKeysymToKeycode(d, k.x11Sym) == keycode) return &k;
    return nullptr;
}
