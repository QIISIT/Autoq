#pragma once
#include <X11/Xlib.h>

// Клавиатура (фокус X) у окна handle — у него самого, его дочернего окна или рамки вокруг
// него. Подойдёт любое соединение с X: номера окон у сервера общие
bool x11FocusIn(Display* d, unsigned long long handle);
