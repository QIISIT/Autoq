#include "icons.h"
#include "i18n.h"
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QBuffer>
#include <QHash>
#include <QVector>
#include <QtMath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <algorithm>

namespace {

// Простейшие фигуры в системе координат 24×24 (как viewBox у SVG)
struct Prim {
    enum Kind { Path, Circle, Rect, Arc } kind = Path;
    const char* d = nullptr;     // для Path: M L H V C Z (и строчные — относительные)
    qreal x = 0, y = 0, w = 0, h = 0, r = 0;
    qreal a0 = 0, span = 0;      // для Arc: градусы Qt (против часовой)
    bool fill = false;           // залить цветом без обводки
};

Prim P(const char* d)  { Prim p; p.kind = Prim::Path; p.d = d; return p; }
Prim PF(const char* d) { Prim p = P(d); p.fill = true; return p; }
Prim C(qreal cx, qreal cy, qreal r) { Prim p; p.kind = Prim::Circle; p.x = cx; p.y = cy; p.r = r; return p; }
Prim CF(qreal cx, qreal cy, qreal r) { Prim p = C(cx, cy, r); p.fill = true; return p; }
Prim R(qreal x, qreal y, qreal w, qreal h, qreal rx) {
    Prim p; p.kind = Prim::Rect; p.x = x; p.y = y; p.w = w; p.h = h; p.r = rx; return p;
}
Prim RF(qreal x, qreal y, qreal w, qreal h, qreal rx) { Prim p = R(x, y, w, h, rx); p.fill = true; return p; }
Prim A(qreal cx, qreal cy, qreal r, qreal a0, qreal span) {
    Prim p; p.kind = Prim::Arc; p.x = cx; p.y = cy; p.r = r; p.a0 = a0; p.span = span; return p;
}

using Glyph = QVector<Prim>;

// Значки игр — свои рисунки (жанровые пиктограммы), не логотипы игр
const QHash<QString, Glyph>& gameSet() {
    static const QHash<QString, Glyph> g = {
        {"compass", {C(12, 12, 9), P("M12 5.5 L14.2 12 L12 18.5 L9.8 12 Z"),
                     PF("M12 5.5 L14.2 12 L9.8 12 Z")}},
        {"train",   {R(5.5, 3, 13, 14, 3.5), R(8, 6, 8, 4.5, 1), CF(9, 13.8, 1.1), CF(15, 13.8, 1.1),
                     P("M8.5 17 L6.5 21 M15.5 17 L17.5 21")}},
        {"city",    {P("M3 21 H21 M5 21 V11 H9 V21 M9 21 V4 H15 V21 M15 21 V9 H19 V21 "
                       "M11.5 8 H12.5 M11.5 11.5 H12.5 M11.5 15 H12.5")}},
        {"waves",   {P("M2 9.5 C4.5 7 7.5 7 10 9.5 C12.5 12 15.5 12 18 9.5 C19.3 8.2 20.7 7.8 22 8"),
                     P("M2 15.5 C4.5 13 7.5 13 10 15.5 C12.5 18 15.5 18 18 15.5 C19.3 14.2 20.7 13.8 22 14")}},
        {"cube",    {P("M12 3 L20 7.5 V16.5 L12 21 L4 16.5 V7.5 Z"), P("M4 7.5 L12 12 L20 7.5 M12 12 V21")}},
        {"layers",  {P("M12 3 L20 7.5 V16.5 L12 21 L4 16.5 V7.5 Z"), P("M4 7.5 L12 12 L20 7.5 M12 12 V21"),
                     P("M4 12 L12 16.5 L20 12")}},
        {"users",   {C(9, 8, 3.5), P("M3.5 20 C3.5 16.5 6 14.5 9 14.5 C12 14.5 14.5 16.5 14.5 20"),
                     P("M15.5 4.8 C17.4 5.2 18.6 6.8 18.4 8.7 C18.2 10.3 17 11.4 15.6 11.7"),
                     P("M17.5 14.6 C19.4 15.2 20.5 17 20.5 19.5")}},
        {"gamepad", {P("M7 8 H17 C19.8 8 21.5 10.5 21.5 13.5 C21.5 16 20 17.5 18.3 17.5 C17 17.5 16.2 16.6 "
                       "15.5 15.5 H8.5 C7.8 16.6 7 17.5 5.7 17.5 C4 17.5 2.5 16 2.5 13.5 C2.5 10.5 4.2 8 7 8 Z"),
                     P("M7 10.8 V14.2 M5.3 12.5 H8.7"), CF(16, 11.6, 1), CF(18, 13.4, 1)}},
        {"sword",   {P("M20 4 H16 L7.5 12.5 L11.5 16.5 L20 8 Z"), P("M5 14 L10 19 M7.5 16.5 L4 20")}},
        {"target",  {C(12, 12, 8.5), C(12, 12, 4.5), CF(12, 12, 1.2)}},
        {"bolt",    {P("M13 2.5 L5 13.5 H11.5 L10.5 21.5 L19 10 H12.5 Z")}},
        {"star",    {P("M12 3.5 L14.35 9.26 L20.56 9.72 L15.8 13.74 L17.29 19.78 L12 16.5 L6.71 19.78 "
                       "L8.2 13.74 L3.44 9.72 L9.65 9.26 Z")}},
        {"shield",  {P("M12 3 L19.5 6 V11.5 C19.5 16 16.3 19.6 12 21 C7.7 19.6 4.5 16 4.5 11.5 V6 Z")}},
        {"flame",   {P("M12 3 C13 6.5 17.5 8.5 17.5 13.5 C17.5 17.5 15 20.5 12 20.5 C9 20.5 6.5 17.5 6.5 14 "
                       "C6.5 11.5 8 10 9 9 C9.3 11 10.3 12 11.3 12.3 C10.8 9 11.5 5.5 12 3 Z")}},
        {"rocket",  {P("M12 2.5 C15.5 5 17 8.5 17 12.5 V16 H7 V12.5 C7 8.5 8.5 5 12 2.5 Z"),
                     P("M7 13 L4.5 15.5 V19 L7 17 M17 13 L19.5 15.5 V19 L17 17 M10 19 L12 21.5 L14 19"),
                     C(12, 9.5, 1.8)}},
        {"crown",   {P("M3.5 8 L8 12 L12 5 L16 12 L20.5 8 L19 18 H5 Z"), P("M5 21 H19")}},
    };
    return g;
}

const QHash<QString, Glyph>& uiSet() {
    static const QHash<QString, Glyph> g = {
        {"plus",      {P("M12 5 V19 M5 12 H19")}},
        {"pencil",    {P("M4 20 H8 L19 9 L15 5 L4 16 Z")}},
        {"trash",     {P("M4 7 H20 M9 7 V4 H15 V7 M6 7 L7 20 H17 L18 7")}},
        {"target",    {C(12, 12, 8), CF(12, 12, 2), P("M12 2 V5 M12 19 V22 M2 12 H5 M19 12 H22")}},
        {"keyboard",  {R(2, 6, 20, 12, 2), P("M6 10 H6.01 M10 10 H10.01 M14 10 H14.01 M18 10 H18.01 M7 14 H17")}},
        {"play",      {PF("M7 4 L20 12 L7 20 Z")}},
        {"stop",      {RF(4, 4, 16, 16, 3)}},
        {"chevron",   {P("M6 9 L12 15 L18 9")}},
        {"chevron-right", {P("M9 6 L15 12 L9 18")}},
        {"chevron-up", {P("M6 15 L12 9 L18 15")}},
        {"mini",      {P("M4 14 H10 V20 M20 10 H14 V4 M10 14 L3 21 M14 10 L21 3")}},
        {"expand",    {P("M14 4 H20 V10 M10 20 H4 V14 M20 4 L13 11 M4 20 L11 13")}},
        {"check",     {P("M5 12 L10 17 L19 7")}},
        {"x",         {P("M6 6 L18 18 M18 6 L6 18")}},
        {"loop",      {A(12, 12, 8, 0, -315), P("M20 4 V9 H15")}},
        {"mouse",     {R(6, 3, 12, 18, 6), P("M12 3 V9")}},
        {"folder",    {P("M4 7 H9 L11 9 H20 V19 H4 Z")}},
        {"power",     {P("M12 3 V12"), A(12, 12.5, 8, 135, 270)}},
        {"window",    {R(3, 4, 18, 16, 2), P("M3 9 H21")}},
        {"minus",     {P("M5 12 H19")}},
        {"dot",       {CF(12, 12, 5)}},
        {"ring",      {C(12, 12, 5)}},
        {"arrow",     {P("M3 12 H20 M15 7 L20 12 L15 17")}},
        {"arrow-down", {P("M12 4 V20 M6 14 L12 20 L18 14")}},
        {"arrow-up",  {P("M12 20 V4 M6 10 L12 4 L18 10")}},
        {"press-tap", {P("M8 4 V16 M4.5 12.5 L8 16 L11.5 12.5"), P("M16 20 V8 M12.5 11.5 L16 8 L19.5 11.5")}},
        {"click",     {P("M10 10 L20 14 L15.5 15.5 L14 20 Z"), P("M5 5 L7 7 M10 3 V5.5 M3 10 H5.5 M14.5 5.5 L13 7 M5.5 14.5 L7 13")}},
        {"press-hold", {P("M12 3 V13 M7.5 8.5 L12 13 L16.5 8.5"), R(5, 16, 14, 4.5, 2.2)}},
        {"eye",       {P("M2.5 12 C5 7.5 8.3 5.5 12 5.5 C15.7 5.5 19 7.5 21.5 12 C19 16.5 15.7 18.5 12 18.5 C8.3 18.5 5 16.5 2.5 12 Z"),
                       C(12, 12, 3)}},
        {"eye-off",   {P("M2.5 12 C5 7.5 8.3 5.5 12 5.5 C15.7 5.5 19 7.5 21.5 12 C19 16.5 15.7 18.5 12 18.5 C8.3 18.5 5 16.5 2.5 12 Z"),
                       C(12, 12, 3), P("M4 4 L20 20")}},
        {"redo",      {P("M15 14 L20 9 L15 4"), P("M20 9 H10 C6.5 9 4 11.5 4 15 C4 18.5 6.5 21 10 21 H14")}},
        {"timer",     {C(12, 13.5, 7.5), P("M12 13.5 V9.5 M9.5 3 H14.5 M18 6.5 L19.5 5")}},
        {"reset",     {A(12, 12, 8, 90, 270), P("M4 4 V10 H10")}},
        {"settings",  {P("M4 6 H20 M4 12 H20 M4 18 H20"), CF(9, 6, 2.4), CF(15, 12, 2.4), CF(7, 18, 2.4)}},
        {"gauge",     {A(12, 14, 8.5, 200, -220), P("M12 14 L16.5 8.5"), CF(12, 14, 1.8)}},
        {"gauge-off", {A(12, 14, 8.5, 200, -220), P("M12 14 L16.5 8.5"), CF(12, 14, 1.8), P("M4 4 L20 20")}},
        {"save",      {P("M5 4 H16 L20 8 V20 H4 V5 Z"), P("M8 4 V9 H15 V4"), R(8, 13, 8, 7, 1)}},
        {"search",    {C(10.5, 10.5, 6.5), P("M15.5 15.5 L20.5 20.5")}},
        {"undo",      {P("M9 14 L4 9 L9 4"), P("M4 9 H14 C17.5 9 20 11.5 20 15 C20 18.5 17.5 21 14 21 H10")}},
        {"shield",    {P("M12 3 L19.5 6 V11.5 C19.5 16 16.3 19.6 12 21 C7.7 19.6 4.5 16 4.5 11.5 V6 Z"), P("M8.8 12 L11 14.2 L15.3 9.8")}},
        {"more",      {CF(6, 12, 1.7), CF(12, 12, 1.7), CF(18, 12, 1.7)}},
        {"grip",      {CF(9, 6, 1.6), CF(15, 6, 1.6), CF(9, 12, 1.6), CF(15, 12, 1.6), CF(9, 18, 1.6), CF(15, 18, 1.6)}},
        {"layout",    {R(3.5, 4, 7.5, 7, 1.5), R(13, 4, 7.5, 7, 1.5), R(3.5, 13, 17, 7, 1.5)}},
        {"help",      {C(12, 12, 9.5), P("M9.3 9.4 C9.4 7.9 10.6 6.9 12.1 6.9 C13.7 6.9 14.8 8 14.8 9.3 C14.8 11.2 12.2 11.4 12.2 13.6"),
                       CF(12.2, 16.9, 1.15)}},
        {"layers2",   {P("M12 4 L21 9 L12 14 L3 9 Z"), P("M3 13.5 L12 18.5 L21 13.5")}},
        {"palette",   {P("M12 3 C7 3 3 7 3 12 C3 17 7 21 12 21 C13.3 21 14 20.2 14 19.2 C14 18.3 13.4 17.8 13.4 16.9 "
                         "C13.4 15.9 14.2 15.2 15.2 15.2 H17 C19.2 15.2 21 13.4 21 11.2 C21 6.6 17 3 12 3 Z"),
                       CF(7.5, 11, 1.3), CF(10, 7, 1.3), CF(14.5, 7, 1.3), CF(17, 10.5, 1.3)}},
    };
    return g;
}

// ===========================================================================
// Значки игр 2.0 — подробные рисунки в сетке 48×48 для стилей «Глянец»,
// «Иллюстрация» и «Неон». У каждой фигуры роль цвета, а цвета роли задаёт стиль:
//   F1/F2/F3 — заливки (основная и два оттенка), DT — детали, SH — тень внутри
//   фигуры, LN — линии, FS — обводка заливок (только у неона).
// ===========================================================================
enum Role : quint8 { RNone, RF1, RF2, RF3, RDT, RSH, RLN, RFS };

struct RPrim {
    bool circle = false;
    const char* d = nullptr;
    qreal cx = 0, cy = 0, r = 0;
    Role fill = RNone;
    Role stroke = RNone;
    qreal w = 0;          // толщина обводки DT в единицах сетки
};

RPrim Fill(const char* d, Role f, bool outline = true) {
    RPrim p; p.d = d; p.fill = f; p.stroke = outline ? RFS : RNone; return p;
}
RPrim FillC(qreal cx, qreal cy, qreal r, Role f, bool outline = true) {
    RPrim p; p.circle = true; p.cx = cx; p.cy = cy; p.r = r; p.fill = f; p.stroke = outline ? RFS : RNone; return p;
}
RPrim Line(const char* d) { RPrim p; p.d = d; p.stroke = RLN; return p; }
RPrim LineC(qreal cx, qreal cy, qreal r) { RPrim p; p.circle = true; p.cx = cx; p.cy = cy; p.r = r; p.stroke = RLN; return p; }
RPrim Detail(const char* d, qreal w) { RPrim p; p.d = d; p.stroke = RDT; p.w = w; return p; }
RPrim DetailC(qreal cx, qreal cy, qreal r, qreal w) {
    RPrim p; p.circle = true; p.cx = cx; p.cy = cy; p.r = r; p.stroke = RDT; p.w = w; return p;
}
RPrim Outline(const char* d) { RPrim p; p.d = d; p.stroke = RFS; return p; }

using RGlyph = QVector<RPrim>;

const QHash<QString, RGlyph>& richSet() {
    static const QHash<QString, RGlyph> g = {
        {"compass", {
            LineC(24, 24, 17.5),
            Line("M24 3.5 V7.5 M24 40.5 V44.5 M3.5 24 H7.5 M40.5 24 H44.5"),
            Fill("M24 9 L27.2 20.8 L39 24 L27.2 27.2 L24 39 L20.8 27.2 L9 24 L20.8 20.8 Z", RF1),
            Fill("M24 9 L27.2 20.8 L24 24 L20.8 20.8 Z", RDT, false),
            FillC(24, 24, 2.6, RDT, false)}},
        {"train", {
            Line("M24 6 V2.5 M18 36 L13 43.5 M30 36 L35 43.5 M15.2 40 H32.8"),
            Fill("M14 11 C14 8.2 16.2 6 19 6 H29 C31.8 6 34 8.2 34 11 V30 C34 33.3 31.3 36 28 36 H20 "
                 "C16.7 36 14 33.3 14 30 Z", RF1),
            Fill("M18 10.5 H30 C31.1 10.5 32 11.4 32 12.5 V19.5 C32 20.6 31.1 21.5 30 21.5 H18 "
                 "C16.9 21.5 16 20.6 16 19.5 V12.5 C16 11.4 16.9 10.5 18 10.5 Z", RDT, false),
            FillC(19.5, 28.5, 2.3, RDT, false), FillC(28.5, 28.5, 2.3, RDT, false),
            Detail("M22 25 H26", 2)}},
        {"city", {
            Fill("M7 41 V22 C7 21.4 7.4 21 8 21 H16 V41 Z", RF2),
            Fill("M31 41 V17 H39 C39.6 17 40 17.4 40 18 V41 Z", RF3),
            Fill("M17 41 V9 C17 8.4 17.4 8 18 8 H29 C29.6 8 30 8.4 30 9 V41 Z", RF1),
            Line("M23.5 8 V3.5 M4 41.5 H44"),
            Fill("M20.3 12 h2.8 v2.8 h-2.8 Z M24.9 12 h2.8 v2.8 h-2.8 Z M20.3 17.5 h2.8 v2.8 h-2.8 Z "
                 "M24.9 17.5 h2.8 v2.8 h-2.8 Z M20.3 23 h2.8 v2.8 h-2.8 Z M24.9 23 h2.8 v2.8 h-2.8 Z "
                 "M20.3 28.5 h2.8 v2.8 h-2.8 Z M24.9 28.5 h2.8 v2.8 h-2.8 Z M10 26 h2.6 v2.6 h-2.6 Z "
                 "M10 31.5 h2.6 v2.6 h-2.6 Z M34.2 22 h2.6 v2.6 h-2.6 Z M34.2 27.5 h2.6 v2.6 h-2.6 Z "
                 "M34.2 33 h2.6 v2.6 h-2.6 Z", RDT, false)}},
        {"waves", {
            FillC(32, 14, 6.5, RF2),
            Fill("M12.5 7 L13.8 11.2 L18 12.5 L13.8 13.8 L12.5 18 L11.2 13.8 L7 12.5 L11.2 11.2 Z", RF1),
            Line("M5 25.5 C9 21.5 13 21.5 17 25.5 C21 29.5 25 29.5 29 25.5 C33 21.5 37 21.5 43 25"),
            Line("M5 33.5 C9 29.5 13 29.5 17 33.5 C21 37.5 25 37.5 29 33.5 C33 29.5 37 29.5 43 33"),
            Line("M11 41.5 C14.5 38.5 18 38.5 21.5 41.5 C25 44.5 28.5 44.5 32 41.5")}},
        {"cube", {
            Fill("M24 6 L40 15 L24 24 L8 15 Z", RF1),
            Fill("M8 15 L24 24 V42 L8 33 Z", RF2),
            Fill("M40 15 V33 L24 42 V24 Z", RF3),
            Fill("M24 11.2 L28.2 13.6 L24 16 L19.8 13.6 Z M11.5 22.2 L15.5 24.4 V28.6 L11.5 26.4 Z "
                 "M15 31 L18.5 33 V36.6 L15 34.6 Z M36.5 24.4 L32.5 26.6 V30.8 L36.5 28.6 Z", RDT, false)}},
        {"layers", {
            Fill("M7 31 L24 40.5 L41 31 V34.5 L24 44 L7 34.5 Z", RF3),
            Fill("M7 22.5 L24 32 L41 22.5 V26 L24 35.5 L7 26 Z", RF2),
            Fill("M24 6 L41 15.5 L24 25 L7 15.5 Z", RF1),
            Fill("M24 11.5 L30.5 15.5 L24 19.5 L17.5 15.5 Z", RDT, false)}},
        {"users", {
            FillC(32.5, 15, 5.5, RF2),
            Fill("M26.5 25.3 C28.3 24.4 30.3 24 32.5 24 C38.5 24 42.5 28 42.5 34 V37 H32.6 V35.5 "
                 "C32.6 31.2 30.4 27.6 26.5 25.3 Z", RF2),
            FillC(19, 17, 7, RF1),
            Fill("M5.5 41 V37.5 C5.5 31 11 26.5 19 26.5 C27 26.5 32.5 31 32.5 37.5 V41 Z", RF1),
            FillC(16.2, 17.9, 1.3, RDT, false), FillC(21.8, 17.9, 1.3, RDT, false)}},
        {"gamepad", {
            Fill("M14 13 H34 C40 13 44 18.5 44 25.5 C44 31 41 34.5 37 34.5 C34.4 34.5 32.8 33 31.4 30.8 "
                 "H16.6 C15.2 33 13.6 34.5 11 34.5 C7 34.5 4 31 4 25.5 C4 18.5 8 13 14 13 Z", RF1),
            Fill("M12.6 19.6 H15.4 V22.6 H18.4 V25.4 H15.4 V28.4 H12.6 V25.4 H9.6 V22.6 H12.6 Z", RDT, false),
            FillC(32.5, 20.8, 2, RDT, false), FillC(36.2, 24.5, 2, RDT, false),
            FillC(32.5, 28.2, 2, RDT, false), FillC(28.8, 24.5, 2, RDT, false)}},
        {"sword", {
            Fill("M38 5 H43 V10 L21.5 31.5 L16.5 26.5 Z", RF1),
            Fill("M12.5 24.5 L23.5 35.5 L21 38 L10 27 Z", RF2),
            Fill("M15.4 31.4 L18.6 34.6 L11.6 41.6 L8.4 38.4 Z", RF3),
            FillC(8.6, 41.4, 2.8, RF1),
            Fill("M39.8 7.6 L40.4 8.2 L20.4 28.2 L19.8 27.6 Z", RDT, false)}},
        {"target", {
            Line("M24 2.5 V6.5 M24 41.5 V45.5 M2.5 24 H6.5 M41.5 24 H45.5"),
            FillC(24, 24, 16, RF1),
            DetailC(24, 24, 10, 3),
            FillC(24, 24, 4.2, RDT, false)}},
        {"bolt", {
            Fill("M27.5 4 L11 27.5 H22.5 L19 44 L37 19 H26 Z", RF1),
            Fill("M25.2 10 L15.4 24.6 H19.6 Z", RDT, false),
            Line("M38.5 29.5 L42.5 27.5 M37.5 35 L42 35.5 M8.5 14.5 L5.5 11.5")}},
        {"star", {
            Fill("M24 7 L28.7 18.5 L41.1 19.4 L31.6 27.5 L34.6 39.6 L24 33 L13.4 39.6 L16.4 27.5 "
                 "L6.9 19.4 L19.3 18.5 Z", RF1, false),
            Fill("M24 7 L28.7 18.5 L41.1 19.4 L31.6 27.5 L34.6 39.6 L24 33 Z", RSH, false),
            Outline("M24 7 L28.7 18.5 L41.1 19.4 L31.6 27.5 L34.6 39.6 L24 33 L13.4 39.6 L16.4 27.5 "
                    "L6.9 19.4 L19.3 18.5 Z"),
            Line("M40.5 4.5 V10.5 M37.5 7.5 H43.5 M8 35.5 V40.5 M5.5 38 H10.5")}},
        {"shield", {
            Fill("M24 4.5 L39.5 10 V22.5 C39.5 32 33 39.6 24 43.5 C15 39.6 8.5 32 8.5 22.5 V10 Z", RF1, false),
            Fill("M24 4.5 L39.5 10 V22.5 C39.5 32 33 39.6 24 43.5 Z", RSH, false),
            Outline("M24 4.5 L39.5 10 V22.5 C39.5 32 33 39.6 24 43.5 C15 39.6 8.5 32 8.5 22.5 V10 Z"),
            Fill("M24 14.5 L26.2 21.8 L33.5 24 L26.2 26.2 L24 33.5 L21.8 26.2 L14.5 24 L21.8 21.8 Z", RDT, false)}},
        {"flame", {
            Fill("M24 5.5 C26.3 12.5 36.6 16.5 36.6 27 C36.6 35.5 31 42 24 42 C17 42 11.4 35.5 11.4 28 "
                 "C11.4 23 14.8 19.5 17.1 17.5 C17.8 21.8 20.1 24 22.4 24.6 C21.2 18 22.8 11 24 5.5 Z", RF1),
            Fill("M24.5 25.5 C26 29.5 30 31 30 35.2 C30 38.6 27.4 41 24 41 C20.6 41 18 38.4 18 35.2 "
                 "C18 32.5 19.6 31 21 30 C21.4 32 22.4 33 23.6 33.4 C23 30.5 23.6 28 24.5 25.5 Z", RDT, false),
            Line("M38.5 9.5 L40 6.5 M41.5 15 L44.5 13.8 M8 13 L6 10")}},
        {"rocket", {
            Fill("M19 35 H29 L24 45 Z", RF3),
            Fill("M13.5 25 L7 32 V40 L13.5 35.5 Z M34.5 25 L41 32 V40 L34.5 35.5 Z", RF2),
            Fill("M24 4 C31 9 34.5 16.5 34.5 25 V33 H13.5 V25 C13.5 16.5 17 9 24 4 Z", RF1),
            FillC(24, 19, 4.6, RDT, false),
            Detail("M18 28 H30", 2)}},
        {"crown", {
            Fill("M9.5 39 H38.5 V43.5 H9.5 Z", RF2),
            Fill("M6 17 L15 26 L24 10 L33 26 L42 17 L38.5 36.5 H9.5 Z", RF1),
            FillC(6, 16, 2.6, RF1), FillC(24, 8.5, 2.6, RF1), FillC(42, 16, 2.6, RF1),
            FillC(16.5, 31, 2, RDT, false), FillC(24, 30.2, 2.5, RDT, false), FillC(31.5, 31, 2, RDT, false)}},
    };
    return g;
}

// --- разбор пути в духе SVG (M L H V C Z, с относительными вариантами) ---
QPainterPath parsePath(const char* d) {
    QPainterPath path;
    const QByteArray s(d);
    int i = 0;
    const int n = s.size();
    auto skip = [&]() { while (i < n && (s[i] == ' ' || s[i] == ',' || s[i] == '\n')) ++i; };
    auto isNumStart = [&](int j) {
        return j < n && (isdigit(static_cast<unsigned char>(s[j])) || s[j] == '-' || s[j] == '+' || s[j] == '.');
    };
    auto num = [&]() -> qreal {
        skip();
        int st = i;
        if (i < n && (s[i] == '-' || s[i] == '+')) ++i;
        bool dot = false;
        while (i < n && (isdigit(static_cast<unsigned char>(s[i])) || (s[i] == '.' && !dot))) {
            if (s[i] == '.') dot = true;
            ++i;
        }
        return s.mid(st, i - st).toDouble();
    };
    char cmd = 0;
    QPointF cur, start;
    while (true) {
        skip();
        if (i >= n) break;
        if (isalpha(static_cast<unsigned char>(s[i]))) cmd = s[i++];
        else if (!isNumStart(i)) { ++i; continue; }
        const bool rel = (cmd >= 'a' && cmd <= 'z');
        switch (cmd) {
            case 'M': case 'm': {
                qreal x = num(), y = num();
                cur = rel ? cur + QPointF(x, y) : QPointF(x, y);
                path.moveTo(cur); start = cur;
                cmd = rel ? 'l' : 'L';     // следующие пары — линии
                break;
            }
            case 'L': case 'l': {
                qreal x = num(), y = num();
                cur = rel ? cur + QPointF(x, y) : QPointF(x, y);
                path.lineTo(cur);
                break;
            }
            case 'H': case 'h': {
                qreal x = num();
                cur.setX(rel ? cur.x() + x : x);
                path.lineTo(cur);
                break;
            }
            case 'V': case 'v': {
                qreal y = num();
                cur.setY(rel ? cur.y() + y : y);
                path.lineTo(cur);
                break;
            }
            case 'C': case 'c': {
                // по одному числу за шаг: порядок вычисления аргументов в C++ не задан
                const qreal x1 = num(); const qreal y1 = num();
                const qreal x2 = num(); const qreal y2 = num();
                const qreal x3 = num(); const qreal y3 = num();
                QPointF c1(x1, y1), c2(x2, y2), e(x3, y3);
                if (rel) { c1 += cur; c2 += cur; e += cur; }
                path.cubicTo(c1, c2, e);
                cur = e;
                break;
            }
            case 'Z': case 'z':
                path.closeSubpath();
                cur = start;
                break;
            default:
                ++i;
                break;
        }
    }
    return path;
}

void paintGlyph(QPainter& p, const Glyph& g, const QRectF& rect, const QColor& color, qreal stroke) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.topLeft());
    p.scale(rect.width() / 24.0, rect.height() / 24.0);
    QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    for (const Prim& pr : g) {
        QPainterPath path;
        switch (pr.kind) {
            case Prim::Path: path = parsePath(pr.d); break;
            case Prim::Circle: path.addEllipse(QPointF(pr.x, pr.y), pr.r, pr.r); break;
            case Prim::Rect: path.addRoundedRect(QRectF(pr.x, pr.y, pr.w, pr.h), pr.r, pr.r); break;
            case Prim::Arc: {
                QRectF box(pr.x - pr.r, pr.y - pr.r, pr.r * 2, pr.r * 2);
                path.arcMoveTo(box, pr.a0);
                path.arcTo(box, pr.a0, pr.span);
                break;
            }
        }
        if (pr.fill) {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
        } else {
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
        }
        p.drawPath(path);
    }
    p.restore();
}

QColor mix(const QColor& a, const QColor& b, qreal t) {   // a*t + b*(1-t)
    return QColor::fromRgbF(a.redF() * t + b.redF() * (1 - t),
                            a.greenF() * t + b.greenF() * (1 - t),
                            a.blueF() * t + b.blueF() * (1 - t));
}

QColor g_accent("#E11D2A");

QPixmap makePixmap(int px, qreal dpr) {
    QPixmap pm(qMax(1, qRound(px * dpr)), qMax(1, qRound(px * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    return pm;
}

// ---------------------------------------------------------------------------
// Стили плиток
// ---------------------------------------------------------------------------
QString g_iconStyle = "gloss";

qreal lumLinear(const QColor& c) {   // относительная яркость (как в CSS/WCAG)
    auto ch = [](qreal v) { return v <= 0.03928 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}

QColor withAlpha(QColor c, qreal a) { c.setAlphaF(a); return c; }

struct RichColors {
    QColor f1, f2, f3, dt, sh, ln, fs;
    qreal sw = 3, fsw = 0;
};

RichColors richColors(const QString& style, const QColor& a) {
    RichColors c;
    if (style == "duo") {
        const QColor base("#141417");
        c.f1 = a;
        c.f2 = mix(a, base, 0.62);
        c.f3 = mix(a, base, 0.38);
        c.ln = mix(a, QColor(Qt::white), 0.55);
        c.dt = withAlpha(QColor(Qt::white), 0.92);
        c.sh = withAlpha(QColor(Qt::black), 0.24);
        c.sw = 3;
    } else if (style == "neon") {
        const QColor line = mix(a, QColor(Qt::white), 0.72);
        c.f1 = withAlpha(a, 0.18);
        c.f2 = withAlpha(a, 0.10);
        c.f3 = withAlpha(a, 0.05);
        c.ln = line;
        c.fs = line;
        c.dt = QColor(Qt::white);
        c.sh = withAlpha(a, 0.16);
        c.sw = 2.4;
        c.fsw = 2.2;
    } else {   // gloss
        const bool light = lumLinear(a) > 0.62;
        const QColor g = light ? mix(a, QColor(Qt::black), 0.28) : QColor(Qt::white);
        c.f1 = g;
        c.f2 = withAlpha(g, 0.8);
        c.f3 = withAlpha(g, 0.58);
        c.ln = g;
        c.dt = light ? withAlpha(QColor(Qt::white), 0.9) : mix(a, QColor(Qt::black), 0.5);
        c.sh = withAlpha(QColor(Qt::black), 0.18);
        c.sw = 3.2;
    }
    return c;
}

QColor roleColor(const RichColors& c, Role r) {
    switch (r) {
        case RF1: return c.f1;
        case RF2: return c.f2;
        case RF3: return c.f3;
        case RDT: return c.dt;
        case RSH: return c.sh;
        case RLN: return c.ln;
        case RFS: return c.fs;
        default: return QColor();
    }
}

// minUnit — самая тонкая линия в единицах сетки (чтобы на 20 px не пропадали)
void paintRich(QPainter& p, const RGlyph& g, const QRectF& rect, const RichColors& c, qreal minUnit) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.topLeft());
    p.scale(rect.width() / 48.0, rect.height() / 48.0);
    for (const RPrim& pr : g) {
        QPainterPath path;
        if (pr.circle) path.addEllipse(QPointF(pr.cx, pr.cy), pr.r, pr.r);
        else path = parsePath(pr.d);
        path.setFillRule(Qt::WindingFill);
        if (pr.fill != RNone) p.fillPath(path, roleColor(c, pr.fill));
        if (pr.stroke != RNone) {
            qreal w = pr.stroke == RLN ? c.sw : pr.stroke == RFS ? c.fsw : (pr.w > 0 ? pr.w : 2.0);
            if (w <= 0) continue;
            w = qMax(w, minUnit);
            const QColor col = roleColor(c, pr.stroke);
            if (!col.isValid()) continue;
            p.strokePath(path, QPen(col, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
    }
    p.restore();
}

// Размытие альфа-канала (три прохода «коробкой» ≈ гаусс) — для тени и свечения
void boxBlurAlpha(QImage& img, int r) {
    if (r < 1) return;
    const int w = img.width(), h = img.height();
    QVector<int> buf(qMax(w, h));
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {               // по горизонтали
            uchar* line = img.scanLine(y);
            for (int x = 0; x < w; ++x) buf[x] = line[x];
            int sum = 0;
            for (int x = -r; x <= r; ++x) sum += buf[qBound(0, x, w - 1)];
            for (int x = 0; x < w; ++x) {
                line[x] = uchar(sum / (2 * r + 1));
                sum += buf[qMin(x + r + 1, w - 1)] - buf[qMax(x - r, 0)];
            }
        }
        for (int x = 0; x < w; ++x) {               // по вертикали
            for (int y = 0; y < h; ++y) buf[y] = img.scanLine(y)[x];
            int sum = 0;
            for (int y = -r; y <= r; ++y) sum += buf[qBound(0, y, h - 1)];
            for (int y = 0; y < h; ++y) {
                img.scanLine(y)[x] = uchar(sum / (2 * r + 1));
                sum += buf[qMin(y + r + 1, h - 1)] - buf[qMax(y - r, 0)];
            }
        }
    }
}

// Силуэт слоя -> размытый и окрашенный (тень / свечение)
QImage softShadow(const QImage& layer, const QColor& color, qreal radiusPx) {
    QImage a = layer.convertToFormat(QImage::Format_Alpha8);
    boxBlurAlpha(a, qMax(1, qRound(radiusPx)));
    QImage out(layer.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(color);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, a);
    p.end();
    return out;
}

QPixmap styledTile(const QString& style, const QString& glyph, const QColor& a, int px, qreal dpr, bool active) {
    const int S = qMax(1, qRound(px * dpr));
    QImage img(S, S, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.scale(dpr, dpr);

    const qreal k = qBound<qreal>(0.55, px / 56.0, 1.4);   // масштаб теней (рисунок задуман на ~56 px)
    const qreal radius = px * 0.28;
    const QRectF full(0, 0, px, px);
    QPainterPath tile;
    tile.addRoundedRect(full, radius, radius);

    // --- подложка ---
    if (style == "duo") {
        p.fillPath(tile, mix(a, QColor("#141417"), 0.1));
        const QPointF c(px * 0.28, px * 0.22);
        QRadialGradient rg(c, px * 1.0615);
        rg.setColorAt(0.0, withAlpha(a, 0.42));
        rg.setColorAt(0.45, withAlpha(a, 0.12));
        rg.setColorAt(0.72, withAlpha(a, 0.0));
        rg.setColorAt(1.0, withAlpha(a, 0.0));
        p.fillPath(tile, rg);
        p.save();
        p.setClipPath(tile);
        p.setPen(QPen(withAlpha(QColor(Qt::white), 0.07), 1));
        p.drawLine(QPointF(radius * 0.6, 1.5), QPointF(px - radius * 0.6, 1.5));
        p.restore();
    } else if (style == "neon") {
        p.fillPath(tile, QColor("#0B0B0E"));
        if (px >= 28) {
            p.save();
            p.setClipPath(tile);
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(a, 0.13));
            const qreal step = 6.0 * px / 64.0, rr = qMax<qreal>(0.5, 1.1 * px / 64.0);
            for (qreal y = step / 2; y < px; y += step)
                for (qreal x = step / 2; x < px; x += step) p.drawEllipse(QPointF(x, y), rr, rr);
            p.restore();
        }
        // свечение по краю изнутри
        QImage ring(S, S, QImage::Format_ARGB32_Premultiplied);
        ring.fill(Qt::transparent);
        {
            QPainter rp(&ring);
            rp.setRenderHint(QPainter::Antialiasing);
            rp.scale(dpr, dpr);
            rp.setPen(QPen(Qt::black, 4.0 * k));
            rp.drawPath(tile);
        }
        p.save();
        p.setClipPath(tile);
        p.resetTransform();
        p.drawImage(0, 0, softShadow(ring, withAlpha(a, 0.35), 5.0 * k * dpr));
        p.restore();
    } else {   // gloss
        const qreal len = px * (0.5 + 0.8660254);
        const QPointF c(px / 2.0, px / 2.0), dir(0.5, 0.8660254);
        QLinearGradient lg(c - dir * (len / 2), c + dir * (len / 2));
        lg.setColorAt(0.0, mix(a, QColor(Qt::white), 0.78));
        lg.setColorAt(0.5, a);
        lg.setColorAt(1.0, mix(a, QColor(Qt::black), 0.6));
        p.fillPath(tile, lg);
        // нижний внутренний край темнее — плитка «выпуклая»
        p.fillPath(tile.subtracted(tile.translated(0, -2.0 * k)), withAlpha(QColor(Qt::black), 0.18));
    }

    // --- рисунок ---
    const auto& set = richSet();
    const RGlyph& gl = set.contains(glyph) ? set[glyph] : set["gamepad"];
    const RichColors rc = richColors(style, a);
    const qreal gs = qRound(px * 0.66);
    const QRectF gr((px - gs) / 2.0, (px - gs) / 2.0, gs, gs);
    const qreal unitPx = gs / 48.0 * dpr;                  // пикселей экрана на единицу сетки
    QImage layer(S, S, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.scale(dpr, dpr);
        paintRich(lp, gl, gr, rc, 1.15 / unitPx);
    }
    p.save();
    p.setClipPath(tile);
    p.resetTransform();
    if (style == "neon") {
        p.drawImage(0, 0, softShadow(layer, withAlpha(a, 0.85), 4.0 * k * dpr));
        p.drawImage(0, 0, softShadow(layer, a, 1.2 * k * dpr));
    } else {
        const bool duo = style == "duo";
        const QImage sh = softShadow(layer, withAlpha(QColor(Qt::black), duo ? 0.45 : 0.32),
                                     (duo ? 2.2 : 1.2) * k * dpr);
        p.drawImage(QPointF(0, (duo ? 2.0 : 1.5) * k * dpr), sh);
    }
    p.drawImage(0, 0, layer);
    p.restore();

    // --- блик и рамка ---
    if (style == "gloss") {
        QLinearGradient hl(0, 0, 0, px);
        hl.setColorAt(0.0, withAlpha(QColor(Qt::white), 0.36));
        hl.setColorAt(0.46, withAlpha(QColor(Qt::white), 0.08));
        hl.setColorAt(0.5, withAlpha(QColor(Qt::white), 0.0));
        hl.setColorAt(1.0, withAlpha(QColor(Qt::white), 0.0));
        p.fillPath(tile, hl);
    }
    const QRectF in1 = full.adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(Qt::NoBrush);
    if (style == "gloss") p.setPen(QPen(withAlpha(QColor(Qt::white), 0.30), 1));
    else if (style == "duo") p.setPen(QPen(withAlpha(a, 0.40), 1));
    else p.setPen(QPen(withAlpha(a, 0.75), 1.5));
    if (style == "neon") p.drawRoundedRect(full.adjusted(0.75, 0.75, -0.75, -0.75), radius - 0.75, radius - 0.75);
    else p.drawRoundedRect(in1, radius - 0.5, radius - 0.5);

    if (active) {   // выбранная игра: кольцо цвета акцента с тёмным зазором
        p.setPen(QPen(QColor(10, 10, 11, 230), 1.2));
        p.drawRoundedRect(full.adjusted(2.4, 2.4, -2.4, -2.4), radius - 2.4, radius - 2.4);
        p.setPen(QPen(g_accent, 2));
        p.drawRoundedRect(full.adjusted(1, 1, -1, -1), radius - 1, radius - 1);
    }
    p.end();
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

} // namespace

// ===========================================================================
void AqIcons::setAccent(const QColor& c) { if (c.isValid()) g_accent = c; }
QColor AqIcons::accent() { return g_accent; }

QStringList AqIcons::gameGlyphs() {
    return {"compass", "train", "city", "waves", "cube", "layers", "users", "gamepad",
            "sword", "target", "bolt", "star", "shield", "flame", "rocket", "crown"};
}

QString AqIcons::glyphLabel(const QString& name) {
    static const QHash<QString, QString> l = {
        {"compass", T("компас")}, {"train", T("поезд")}, {"city", T("город")}, {"waves", T("волны")},
        {"cube", T("куб")}, {"layers", T("слои")}, {"users", T("люди")}, {"gamepad", T("геймпад")},
        {"sword", T("меч")}, {"target", T("мишень")}, {"bolt", T("молния")}, {"star", T("звезда")},
        {"shield", T("щит")}, {"flame", T("пламя")}, {"rocket", T("ракета")}, {"crown", T("корона")}};
    return l.value(name, name);
}

bool AqIcons::isGlyph(const QString& name) { return gameSet().contains(name); }

QStringList AqIcons::palette() {
    return {"#F2C66D", "#B9A8FF", "#FF9F43", "#6FD3F2", "#6EDC8C", "#FF5A66", "#FF8AD8", "#E6E6EA"};
}

QString AqIcons::paletteLabel(const QString& hex) {
    static const QHash<QString, QString> l = {
        {"#F2C66D", T("золотой")}, {"#B9A8FF", T("лавандовый")}, {"#FF9F43", T("оранжевый")},
        {"#6FD3F2", T("голубой")}, {"#6EDC8C", T("зелёный")}, {"#FF5A66", T("красный")},
        {"#FF8AD8", T("розовый")}, {"#E6E6EA", T("белый")}};
    return l.value(hex.toUpper(), hex);
}

QStringList AqIcons::iconStyles() { return {"gloss", "duo", "neon", "classic"}; }

QString AqIcons::iconStyleLabel(const QString& id) {
    static const QHash<QString, QString> l = {
        {"gloss", T("Глянец")}, {"duo", T("Иллюстрация")}, {"neon", T("Неон")}, {"classic", T("Классика")}};
    return l.value(id, id);
}

QString AqIcons::iconStyleNote(const QString& id) {
    static const QHash<QString, QString> l = {
        {"gloss", T("объёмные плитки с бликом, как значки приложений")},
        {"duo", T("тёмная плитка с цветным свечением, рисунок в 2–3 тона")},
        {"neon", T("светящиеся контуры на тёмной плитке")},
        {"classic", T("простые линии, как в прошлых версиях")}};
    return l.value(id);
}

QString AqIcons::normalizeIconStyle(const QString& id) {
    return iconStyles().contains(id) ? id : QString("gloss");
}

void AqIcons::setIconStyle(const QString& id) { g_iconStyle = normalizeIconStyle(id); }
QString AqIcons::iconStyle() { return g_iconStyle; }

QPixmap AqIcons::gameTile(const QString& glyph, const QColor& colorIn, const QByteArray& image,
                          int px, qreal dpr, bool active, bool light) {
    return gameTileStyled(g_iconStyle, glyph, colorIn, image, px, dpr, active, light);
}

QPixmap AqIcons::gameTileStyled(const QString& style, const QString& glyph, const QColor& colorIn,
                                const QByteArray& image, int px, qreal dpr, bool active, bool light) {
    if (style != "classic" && image.isEmpty()) {
        const QColor color = colorIn.isValid() ? colorIn : QColor("#FF5A66");
        // объёмные стили рисуются с размытием (несколько мс) — запоминаем готовые
        static QHash<QString, QPixmap> cache;
        const QString key = QString("%1|%2|%3|%4|%5|%6|%7").arg(style, glyph, color.name()).arg(px)
                                .arg(dpr).arg(active).arg(active ? g_accent.name() : QString());
        auto it = cache.constFind(key);
        if (it != cache.constEnd()) return it.value();
        if (cache.size() > 400) cache.clear();
        const QPixmap pm = styledTile(normalizeIconStyle(style), glyph, color, px, dpr, active);
        cache.insert(key, pm);
        return pm;
    }
    QPixmap pm = makePixmap(px, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QColor color = colorIn.isValid() ? colorIn : QColor("#FF5A66");
    const QColor base = light ? QColor("#FFFFFF") : QColor("#141417");
    const qreal radius = px * 0.27;
    const qreal bw = active ? 2.0 : 1.0;
    const QRectF outer(bw / 2, bw / 2, px - bw, px - bw);

    QImage img;
    if (!image.isEmpty()) img.loadFromData(image);
    if (!img.isNull()) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, px, px), radius, radius);
        p.setClipPath(clip);
        const QSize s = img.size();
        const int side = qMin(s.width(), s.height());
        const QRect src((s.width() - side) / 2, (s.height() - side) / 2, side, side);
        p.drawImage(QRectF(0, 0, px, px), img, src);
        p.setClipping(false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(active ? g_accent : QColor(0, 0, 0, light ? 40 : 90), bw));
        p.drawRoundedRect(outer, radius, radius);
        return pm;
    }

    const QColor bg = mix(color, base, light ? 0.18 : 0.16);
    const QColor border = active ? g_accent : mix(color, base, light ? 0.40 : 0.28);
    const QColor tint = light ? mix(color, QColor("#000000"), 0.55) : color;
    p.setPen(QPen(border, bw));
    p.setBrush(bg);
    p.drawRoundedRect(outer, radius, radius);
    const qreal g = px * 0.56;
    const auto& set = gameSet();
    const Glyph& gl = set.contains(glyph) ? set[glyph] : set["gamepad"];
    paintGlyph(p, gl, QRectF((px - g) / 2, (px - g) / 2, g, g), tint, px <= 24 ? 2.2 : 1.8);
    return pm;
}

QByteArray AqIcons::normalizeImage(const QImage& in) {
    if (in.isNull()) return {};
    const int side = qMin(in.width(), in.height());
    QImage sq = in.copy((in.width() - side) / 2, (in.height() - side) / 2, side, side)
                  .scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .convertToFormat(QImage::Format_ARGB32);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    sq.save(&buf, "PNG");
    return out;
}

// ===========================================================================
QPixmap AqIcons::logoWithAccent(int px, qreal dpr, const QColor& accent) {
    const QColor keep = g_accent;
    if (accent.isValid()) g_accent = accent;
    QPixmap pm = logo(px, dpr, false);
    g_accent = keep;
    return pm;
}

QPixmap AqIcons::logo(int px, qreal dpr, bool idle) {
    QPixmap pm = makePixmap(px, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(px / 100.0, px / 100.0);
    // по умолчанию красная клавиша; цвет меняется вместе с акцентом из настроек
    const QColor side = idle ? QColor("#34343B") : mix(QColor("#000000"), g_accent, 0.45);
    const QColor top  = idle ? QColor("#5A5A64") : g_accent;
    const QColor face = idle ? QColor("#64646E") : mix(QColor("#FFFFFF"), g_accent, 0.07);
    const qreal accLum = 0.2126 * g_accent.redF() + 0.7152 * g_accent.greenF() + 0.0722 * g_accent.blueF();
    const QColor qColor = (!idle && accLum > 0.62) ? QColor("#141416") : QColor(Qt::white);
    p.setPen(Qt::NoPen);
    p.setBrush(side);
    p.drawRoundedRect(QRectF(8, 12, 84, 80), 18, 18);
    p.setBrush(top);
    p.drawRoundedRect(QRectF(8, 6, 84, 78), 18, 18);
    const bool small = px <= 32;
    if (!small) {
        p.setBrush(face);
        p.drawRoundedRect(QRectF(16, 11, 68, 64), 13, 13);
        QPen hl(idle ? QColor("#8A8A94") : mix(QColor("#FFFFFF"), g_accent, 0.45), 1.6, Qt::SolidLine, Qt::RoundCap);
        p.setPen(hl);
        p.setOpacity(0.7);
        p.drawLine(QPointF(27, 15.5), QPointF(73, 15.5));
        p.setOpacity(1.0);
    }
    const qreal w = small ? (px <= 18 ? 10.5 : 9.0) : 8.0;
    QPen q(qColor, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(q);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(47, 41), 16, 16);
    p.drawLine(QPointF(54, 49), QPointF(66, 61));
    return pm;
}

QIcon AqIcons::appIcon(bool idle) {
    QIcon icon;
    for (int s : {16, 20, 22, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(logo(s, 1.0, idle));
    return icon;
}

// ===========================================================================
QStringList AqIcons::uiNames() { return uiSet().keys(); }

QPixmap AqIcons::uiPixmap(const QString& name, const QColor& color, int px, qreal dpr, qreal stroke) {
    QPixmap pm = makePixmap(px, dpr);
    QPainter p(&pm);
    const auto& set = uiSet();
    if (set.contains(name)) paintGlyph(p, set[name], QRectF(0, 0, px, px), color, stroke);
    else if (gameSet().contains(name)) paintGlyph(p, gameSet()[name], QRectF(0, 0, px, px), color, stroke);
    return pm;
}

QIcon AqIcons::ui(const QString& name, const QColor& color, int px, qreal stroke) {
    QIcon icon;
    icon.addPixmap(uiPixmap(name, color, px, 1.0, stroke));
    icon.addPixmap(uiPixmap(name, color, px, 2.0, stroke));
    return icon;
}

bool AqIcons::paint(QPainter& p, const QString& name, const QRectF& rect, const QColor& color, qreal stroke) {
    if (uiSet().contains(name)) { paintGlyph(p, uiSet()[name], rect, color, stroke); return true; }
    if (gameSet().contains(name)) { paintGlyph(p, gameSet()[name], rect, color, stroke); return true; }
    return false;
}
