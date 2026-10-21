#include "theme.h"
#include "i18n.h"
#include "icons.h"
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>
#include <QStyle>
#include <QWidget>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QHash>
#include <QSettings>
#include <QFile>
#include <QtMath>
#include <QProxyStyle>
#include <QLayout>
#include <QGridLayout>
#include <QBoxLayout>
#include <QEvent>
#include <QRegularExpression>
#include <QScreen>
#include <QGuiApplication>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "games.h"
#include "appinfo.h"

namespace {
Theme::Tokens g_tok;
bool g_dark = true;
QString g_display = "Unbounded", g_ui = "Onest", g_mono = "JetBrains Mono";
bool g_fontsLoaded = false;

QColor mixc(const QColor& a, const QColor& b, qreal t) {   // a*t + b*(1-t)
    return QColor::fromRgbF(a.redF() * t + b.redF() * (1 - t), a.greenF() * t + b.greenF() * (1 - t),
                            a.blueF() * t + b.blueF() * (1 - t));
}
qreal luminance(const QColor& c) {
    auto ch = [](qreal v) { return v <= 0.03928 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}
QColor withLightness(const QColor& c, qreal minL, qreal maxL) {
    float h, s, l, a;
    c.getHslF(&h, &s, &l, &a);
    return QColor::fromHslF(h < 0 ? 0 : h, s, qBound(float(minL), l, float(maxL)), a);
}

// Все цвета интерфейса считаются из двух: фона и акцента
Theme::Tokens buildTokens(bool dark, QColor bg, const QColor& A, int borders = 1) {
    Theme::Tokens t;
    // рамки: мягкие / обычные / чёткие (Редактор интерфейса)
    const qreal bf = borders <= 0 ? 0.7 : borders >= 2 ? 1.5 : 1.0;
    const QColor W("#FFFFFF"), K("#000000");
    t.light = !dark;
    if (dark) {
        bg = withLightness(bg, 0.0, 0.16);            // фон тёмной темы не светлее 16 %
        t.bg = bg;
        t.side = mixc(W, bg, 0.012);  t.bar = mixc(K, bg, 0.25);
        t.card = mixc(W, bg, 0.032);  t.cardBorder = mixc(W, bg, 0.094 * bf);
        t.field = mixc(W, bg, 0.063); t.fieldDeep = mixc(W, bg, 0.012);
        t.fieldBorder = mixc(W, bg, 0.13 * bf); t.fieldBorder2 = mixc(W, bg, qMin(0.4, 0.185 * bf));
        t.keycap = mixc(W, bg, 0.106); t.track = mixc(W, bg, 0.098);
        t.text = QColor("#F2F2F4");   t.text2 = QColor("#D6D6DB");
        t.muted = QColor("#9A9AA3");  t.faint = QColor("#85858E");
        t.red = A;
        t.redHi = withLightness(A, 0.60, 0.80);       // акцент для текста на тёмном
        t.redSoft = mixc(W, t.redHi, 0.42);
        t.redTint = mixc(A, bg, 0.15); t.redTint2 = mixc(A, bg, 0.06); t.redLine = mixc(A, bg, 0.38);
        t.ok = QColor("#3DDC84");     t.okBg = mixc(t.ok, bg, 0.06);  t.okLine = mixc(t.ok, bg, 0.16);
        t.warn = QColor("#F0B341");   t.warnBg = mixc(t.warn, bg, 0.07); t.warnLine = mixc(t.warn, bg, 0.22);
    } else {
        bg = withLightness(bg, 0.88, 1.0);            // фон светлой темы не темнее 88 %
        t.bg = bg;
        t.card = mixc(W, bg, 0.9);    t.side = t.card;  t.keycap = t.card;
        t.bar = mixc(K, bg, 0.035);   t.cardBorder = mixc(K, bg, 0.08 * bf);
        t.field = mixc(K, bg, 0.006); t.fieldDeep = mixc(K, bg, 0.035);
        t.fieldBorder = mixc(K, bg, 0.12 * bf); t.fieldBorder2 = mixc(K, bg, qMin(0.4, 0.2 * bf));
        t.track = mixc(K, bg, 0.09);
        t.text = QColor("#141416");   t.text2 = QColor("#2B2B31");
        t.muted = QColor("#5C5C66");  t.faint = QColor("#6A6A74");
        t.red = A;
        t.redHi = withLightness(A, 0.0, 0.40);        // акцент для текста на светлом
        t.redSoft = withLightness(A, 0.0, 0.36);
        t.redTint = mixc(A, W, 0.10); t.redTint2 = mixc(A, W, 0.05); t.redLine = mixc(A, W, 0.35);
        t.ok = QColor("#12803E");     t.okBg = QColor("#E8F6EE");  t.okLine = QColor("#B5E1C6");
        t.warn = QColor("#8F5B00");   t.warnBg = QColor("#FFF5E0"); t.warnLine = QColor("#EBCB86");
    }
    t.onRed = luminance(A) > 0.45 ? QColor("#121214") : QColor("#FFFFFF");
    return t;
}

Theme::Appearance g_app;
bool g_appLoaded = false;

// Стрелка для выпадающих списков — PNG во временной папке (QSS понимает только файлы)
QString chevronFile(const QColor& c) {
    QString dir = QDir::temp().absoluteFilePath(QString("autoq-ui-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(dir);
    QString path = dir + QString("/chevron-%1.png").arg(c.name().mid(1));
    if (!QFile::exists(path)) AqIcons::uiPixmap("chevron", c, 12, 2.0, 2.4).save(path, "PNG");
    return QDir::fromNativeSeparators(path);
}

const char* kQss = R"QSS(
* { font-family: "@ui@"; font-size: 13px; color: @text@; }
QMainWindow, QDialog, QWidget#root, QWidget#main, QScrollArea#mainScroll, QWidget#mainInner { background: @bg@; }
QWidget#miniRoot { background: @bg@; }
QToolTip { background: @card@; color: @text@; border: 1px solid @fieldBorder@; padding: 6px 8px; border-radius: 6px; }

QFrame#sidebar { background: @side@; border: none; border-right: 1px solid @cardBorder@; }
QFrame#statusbar { background: @bar@; border: none; border-top: 1px solid @cardBorder@; }
QLabel#brand { font-family: "@display@"; font-size: 15px; font-weight: 700; }
QLabel#brandBy, QLabel#footer { color: @faint@; font-size: 11px; }
QLabel#section { color: @muted@; font-size: 11px; font-weight: 600; }
QLabel#sectionCount { color: @muted@; font-family: "@mono@"; font-size: 11px; }

QFrame#card { background: @card@; border: 1px solid @cardBorder@; border-radius: 14px; }
QFrame#card QLabel { background: transparent; }
QLabel#num { font-family: "@mono@"; font-size: 11px; font-weight: 700; color: @redHi@; }
QLabel#cardTitle { font-size: 14px; font-weight: 600; }
QLabel#muted { color: @muted@; font-size: 12px; }
QLabel#faint { color: @faint@; font-size: 12px; }
QLabel#small { color: @muted@; font-size: 11px; }
QLabel#h1, QWidget#h1 { font-family: "@display@"; font-size: 24px; font-weight: 700; }
QLabel#counterCaption { color: @redSoft@; font-size: 10px; font-weight: 700; }
QLabel#counter { font-family: "@mono@"; font-size: 26px; font-weight: 700; color: @text@; }
QFrame#counterBox { background: @redTint2@; border: 1px solid @redLine@; border-radius: 12px; }
QFrame#counterBox[running="true"] { background: @redTint@; border: 1px solid @red@; }
QFrame#counterBox QLabel { background: transparent; }
QFrame#counterBox[running="true"] QLabel#counter { color: @redHi@; }
QFrame#toast { background: @card@; border: 1px solid @fieldBorder2@; border-left: 3px solid @red@; border-radius: 10px; }
QFrame#toast QLabel { background: transparent; color: @text@; font-size: 12px; }
QFrame#onbCard { background: @card@; border: 1px solid @cardBorder@; border-radius: 16px; }
QFrame#onbCard QLabel { background: transparent; }
QPushButton#choiceCard { background: @card@; border: 1px solid @cardBorder@; border-radius: 16px; padding: 0; text-align: left; }
QPushButton#choiceCard:hover { border-color: @fieldBorder2@; }
QPushButton#choiceCard:checked { background: @redTint2@; border: 2px solid @red@; }
QPushButton#choiceCard QLabel { background: transparent; }
QLabel#stepDot { background: @fieldBorder@; border-radius: 4px /*pill*/; min-width: 8px; max-width: 8px; min-height: 8px; max-height: 8px; }
QLabel#stepDot[on="true"] { background: @red@; min-width: 22px; max-width: 22px; }
QToolButton#popupX { background: transparent; border: none; border-radius: 6px; padding: 0; }
QToolButton#popupX:hover { background: @field@; }
QToolButton#resetMini { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 0; }
QToolButton#resetMini:hover { border-color: @fieldBorder2@; background: @field@; }
QFrame#editorBox { background: @fieldDeep@; border: 1px solid @fieldBorder@; border-radius: 12px; }
QFrame#editorBox QLabel { background: transparent; }
QLabel#rate { font-family: "@mono@"; font-size: 30px; font-weight: 700; color: @redHi@; }
QLabel#chipRed { background: @redTint@; border: 1px solid @redLine@; border-radius: 11px /*pill*/; padding: 3px 10px; color: @redSoft@; font-size: 12px; font-weight: 600; }
QLabel#chip { background: @field@; border: 1px solid @fieldBorder@; border-radius: 6px; padding: 2px 7px; font-family: "@mono@"; font-size: 11px; }
QLabel#chipText { background: @field@; border: 1px solid @fieldBorder@; border-radius: 6px; padding: 2px 7px; font-size: 11px; }
QLabel#hint { color: @warn@; font-size: 12px; }
QLabel#connectorText { font-family: "@mono@"; font-size: 12px; color: @text2@; }
QLabel#keycapLabel { background: @keycap@; border: 1px solid @fieldBorder2@; border-bottom: 3px solid @fieldBorder2@; border-radius: 6px; padding: 1px 8px; font-family: "@mono@"; font-size: 13px; font-weight: 700; }

QLabel#status { border-radius: 10px; padding: 7px 10px; font-size: 12px; }
QLabel#status[state="ok"]   { background: @okBg@; border: 1px solid @okLine@; color: @text@; }
QLabel#status[state="none"] { background: @field@; border: 1px solid @fieldBorder@; color: @text2@; }
QLabel#status[state="wait"] { background: @redTint2@; border: 1px solid @redLine@; color: @text@; }
QLabel#status[state="bad"]  { background: @redTint@; border: 1px solid @red@; color: @text@; }
QLabel#status[state="warn"] { background: @warnBg@; border: 1px solid @warnLine@; color: @text@; }

QFrame#card QPushButton { padding: 5px 8px; }
QFrame#slot { background: @field@; border: 1px solid @fieldBorder@; border-radius: 12px; }
QFrame#slot QLabel { background: transparent; }

QPushButton { background: @field@; border: 1px solid @fieldBorder@; border-radius: 9px; padding: 6px 12px; min-height: 22px; font-size: 12px; }
QPushButton:hover { border-color: @fieldBorder2@; }
QPushButton:pressed { background: @fieldDeep@; }
QPushButton:disabled { color: @faint@; background: @fieldDeep@; }
QPushButton:focus { outline: none; }
QPushButton#accent { background: @redTint@; border: 1px solid @red@; color: @redHi@; font-weight: 600; }
QPushButton#accent:hover { background: @redTint2@; }
QPushButton#accent:disabled { background: @fieldDeep@; border-color: @fieldBorder@; color: @faint@; }
QPushButton#primary { background: @red@; border: none; color: @onRed@; font-weight: 600; padding: 8px 20px; }
QPushButton#primary:hover { background: @redHi@; }
QPushButton#primary:disabled { background: @fieldDeep@; border: 1px solid @fieldBorder@; color: @faint@; }
QPushButton#ghost { background: transparent; color: @muted@; }
QPushButton#outline { background: transparent; border: 1px solid @fieldBorder2@; font-weight: 600; }
QPushButton#outline:hover { border-color: @red@; }
QPushButton#outline[counting="true"] { border: 1px solid @red@; color: @redSoft@; }
QPushButton#dashed { background: transparent; border: 1px dashed @fieldBorder2@; color: @text2@; min-height: 30px; font-size: 13px; }
QPushButton#dashed:hover { border-color: @red@; }
QPushButton#addSecond { background: transparent; border: 1px dashed @fieldBorder2@; border-radius: 12px; color: @muted@; }
QPushButton#addSecond:hover { border-color: @red@; color: @text@; }
QPushButton#start { background: @red@; border: none; border-radius: 12px; padding: 0 18px; }
QPushButton#start:hover { background: @redHi@; }
QPushButton#start[running="true"] { background: @redTint2@; border: 2px solid @red@; }
QPushButton#start QLabel { background: transparent; }
QLabel#startText { font-family: "@display@"; font-size: 16px; font-weight: 700; color: @onRed@; }
QPushButton#start[running="true"] QLabel#startText { color: @redHi@; }
QLabel#startChip { font-family: "@mono@"; font-size: 12px; font-weight: 700; color: @onRed@; background: rgba(0,0,0,70); border-radius: 6px; padding: 2px 7px; }
QPushButton#start[running="true"] QLabel#startChip { color: @redHi@; background: @redTint@; }
QLabel#startChip[len="m"] { font-size: 11px; }
QLabel#startChip[len="l"] { font-size: 10px; }

QFrame#segBox { background: @fieldDeep@; border: 1px solid @fieldBorder@; border-radius: 10px; }
QPushButton#seg { background: transparent; border: none; border-radius: 7px; padding: 4px 8px; min-height: 22px; color: @text2@; font-family: "@mono@"; font-size: 12px; font-weight: 700; }
QPushButton#seg:hover { background: @field@; }
QPushButton#seg:checked { background: @red@; color: @onRed@; }
QPushButton#segText { background: transparent; border: none; border-radius: 7px; padding: 4px 12px; min-height: 22px; color: @text2@; font-size: 12px; font-weight: 600; }
QPushButton#segText:hover { background: @field@; }
QPushButton#segText:checked { background: @red@; color: @onRed@; }

QToolButton#icon { background: transparent; border: 1px solid @fieldBorder@; border-radius: 8px; padding: 0; }
QToolButton#icon:hover { border-color: @fieldBorder2@; background: @field@; }
QToolButton#chip { background: @field@; border: 1px solid @fieldBorder@; border-radius: 11px /*pill*/; padding: 3px 10px; font-size: 12px; }
QToolButton#chip:hover { border-color: @fieldBorder2@; }
QToolButton#chip::menu-indicator { image: none; width: 0; }
QToolButton#pick { background: transparent; border: 2px solid transparent; border-radius: 11px; padding: 2px; }
QToolButton#pick:hover { border-color: @fieldBorder2@; }
QToolButton#pick:checked { border-color: @red@; }
QSpinBox#timeCustom { background: @field@; border: 1px solid @fieldBorder@; border-radius: 9px; padding: 4px 6px; min-height: 26px; font-family: "@mono@"; font-size: 13px; font-weight: 700; }
QSpinBox#timeCustom:focus { border-color: @red@; }
QToolButton#styleCard { background: @field@; border: 1px solid @fieldBorder@; border-radius: 12px; padding: 10px 4px 8px 4px; color: @text2@; font-size: 12px; font-weight: 600; }
QToolButton#styleCard:hover { border-color: @fieldBorder2@; }
QToolButton#styleCard:checked { background: @redTint@; border: 2px solid @red@; color: @text@; }
QToolButton#swatch { border-radius: 13px /*pill*/; border: 3px solid @card@; padding: 0; }
QToolButton#swatch:checked { border-color: @text@; }

QComboBox { background: @field@; border: 1px solid @fieldBorder@; border-radius: 9px; padding: 4px 10px; min-height: 24px; font-size: 13px; }
QComboBox:hover { border-color: @fieldBorder2@; }
QComboBox:focus { border-color: @red@; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: url("@chevron@"); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: @card@; border: 1px solid @fieldBorder@; selection-background-color: @redTint@; selection-color: @text@; outline: none; padding: 4px; }
QComboBox#kind { background: transparent; border: none; padding: 0 2px; min-height: 16px; color: @muted@; font-size: 11px; }
QComboBox#kind::drop-down { width: 14px; }
QComboBox#key { background: @keycap@; border: 1px solid @fieldBorder2@; border-bottom: 4px solid @fieldBorder2@; border-radius: 8px; padding: 2px 6px 2px 10px; min-height: 26px; font-family: "@mono@"; font-size: 18px; font-weight: 700; }
QComboBox#key:focus { border-color: @red@; }
QComboBox#keycap { background: @keycap@; border: 1px solid @fieldBorder2@; border-bottom: 3px solid @fieldBorder2@; border-radius: 7px; padding: 2px 4px 2px 10px; min-height: 22px; font-family: "@mono@"; font-size: 13px; font-weight: 700; }

QSpinBox, QDoubleSpinBox { background: @field@; border: 1px solid @fieldBorder@; border-radius: 8px; padding: 3px 8px; min-height: 22px; font-family: "@mono@"; font-size: 12px; font-weight: 500; }
QSpinBox:focus, QDoubleSpinBox:focus { border-color: @red@; }
QSpinBox#big { font-size: 15px; font-weight: 700; padding: 3px 8px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { subcontrol-origin: border; width: 0px; border: none; }

QLineEdit { background: @field@; border: 1px solid @fieldBorder@; border-radius: 10px; padding: 7px 12px; font-size: 14px; selection-background-color: @red@; }
QLineEdit:focus { border-color: @red@; }
QLineEdit#mono { font-family: "@mono@"; font-size: 13px; }

QSlider:horizontal { min-height: 28px; background: transparent; }
QSlider::groove:horizontal { height: 6px; margin: 0 2px; background: @track@; border-radius: 3px /*pill*/; }
QSlider::sub-page:horizontal { background: @red@; border-radius: 3px /*pill*/; margin-left: 2px; }
QSlider::handle:horizontal { width: 10px; height: 10px; margin: -6px -2px; background: #FFFFFF; border: 4px solid @red@; border-radius: 9px /*pill*/; }
QSlider::handle:horizontal:hover { background: @redSoft@; }

QSlider::sub-page:horizontal:disabled { background: @fieldBorder2@; }
QSlider::handle:horizontal:disabled { border-color: @fieldBorder2@; background: @field@; }
QSpinBox:disabled, QDoubleSpinBox:disabled { color: @faint@; background: @fieldDeep@; }
QComboBox:disabled { color: @faint@; }
QListWidget#gameList { background: transparent; border: none; outline: none; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @fieldBorder2@; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @fieldBorder2@; border-radius: 4px; min-width: 30px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

QMenu { background: @card@; border: 1px solid @fieldBorder@; padding: 6px; }
QMenu::item { padding: 7px 26px 7px 12px; border-radius: 6px; font-size: 13px; }
QMenu::item:selected { background: @field@; }
QMenu::item:disabled { color: @redSoft@; }
QMenu::separator { height: 1px; background: @cardBorder@; margin: 4px 6px; }
QMenu::icon { padding-left: 8px; }

QMessageBox QLabel { font-size: 13px; }
QCheckBox { spacing: 8px; }
QFrame#captureBox { background: @redTint2@; border: 1px solid @redLine@; border-radius: 12px; }
QFrame#captureBox QLabel { background: transparent; }
QFrame#divider { background: @cardBorder@; max-height: 1px; min-height: 1px; border: none; }
QLabel#fieldLabel { color: @muted@; font-size: 12px; }
QLabel#miniCounter { font-family: "@display@"; font-size: 50px; font-weight: 900; }
QFrame#pill { background: @redTint@; border: 1px solid @redLine@; border-radius: 14px /*pill*/; }
QFrame#pill[state="idle"] { background: @field@; border: 1px solid @fieldBorder@; }
QFrame#pill QLabel { background: transparent; }
QLabel#gameName { font-size: 13px; font-weight: 600; }
QFrame#summary { background: @card@; border: 1px solid @cardBorder@; border-radius: 12px; }
QFrame#summary QLabel { background: transparent; }

QFrame#popup { background: @card@; border: 1px solid @fieldBorder2@; }
QFrame#popup QLabel { background: transparent; }
QLineEdit#search { padding: 5px 8px; font-size: 13px; border-radius: 8px; }
QListWidget#keyList { background: transparent; border: none; outline: none; }
QListWidget#keyList::item { padding: 4px 8px; border-radius: 6px; }
QListWidget#keyList::item:selected { background: @redTint@; color: @text@; }
QListWidget#keyList::item:hover { background: @field@; }
QPushButton#keyPick { background: @keycap@; border: 1px solid @fieldBorder2@; border-bottom: 3px solid @fieldBorder2@; border-radius: 7px; padding: 2px 8px 2px 10px; min-height: 22px; font-family: "@mono@"; font-size: 13px; font-weight: 700; }
QPushButton#keyPick:hover { border-color: @red@; }
QPushButton#keyPickBig { background: @keycap@; border: 1px solid @fieldBorder2@; border-bottom: 4px solid @fieldBorder2@; border-radius: 8px; padding: 2px 8px 2px 10px; min-height: 26px; font-family: "@mono@"; font-size: 18px; font-weight: 700; }
QPushButton#keyPickBig:hover, QPushButton#keyPickBig:focus { border-color: @red@; }
QPushButton#keyPickMini { background: @keycap@; border: 1px solid @fieldBorder2@; border-bottom: 3px solid @fieldBorder2@; border-radius: 7px; padding: 1px 6px 1px 8px; min-height: 22px; font-family: "@mono@"; font-size: 13px; font-weight: 700; }
QPushButton#keyPickMini:hover { border-color: @red@; }
QPushButton#captureOval { background: transparent; border: 1px dashed @fieldBorder2@; border-radius: 14px /*pill*/; padding: 4px 12px; min-height: 20px; font-size: 12px; color: @text2@; }
QPushButton#captureOval:hover { border-color: @red@; color: @text@; }
QPushButton#captureOval[capturing="true"] { border: 1px solid @red@; background: @redTint2@; color: @redSoft@; }

QFrame#ruleChip { background: @field@; border: 1px solid @fieldBorder@; border-radius: 6px; }
QFrame#ruleChip QLabel { background: transparent; font-size: 11px; }
QFrame#ruleChip QLabel#chipMonoText { font-family: "@mono@"; }
QLineEdit#chipEdit[mono="true"] { font-family: "@mono@"; }
QToolButton#chipX { background: transparent; border: none; padding: 0; border-radius: 4px; }
QToolButton#chipX:hover { background: @fieldBorder@; }
QToolButton#chipAdd { background: transparent; border: 1px dashed @fieldBorder2@; border-radius: 6px; padding: 1px 7px 1px 4px; color: @muted@; font-size: 11px; }
QToolButton#chipAdd:hover { border-color: @red@; color: @text@; }
QLineEdit#chipEdit { padding: 1px 6px; border-radius: 6px; min-height: 18px; font-size: 11px; }

QToolButton#templateChip { background: @field@; border: 1px solid @fieldBorder@; border-radius: 10px; padding: 4px 10px 4px 6px; font-size: 12px; }
QToolButton#templateChip:hover { border-color: @fieldBorder2@; }
QToolButton#templateChip:checked { background: @redTint@; border: 1px solid @red@; }
QToolButton#templateChip:disabled { color: @faint@; background: @fieldDeep@; }
QPushButton#templateCard { background: @card@; border: 1px solid @cardBorder@; border-radius: 14px; padding: 0; min-height: 66px; }
QPushButton#templateCard:hover { border-color: @red@; background: @redTint2@; }
QPushButton#templateCard QLabel { background: transparent; }
QLabel#welcomeTitle { font-family: "@display@"; font-size: 26px; font-weight: 700; }

QToolButton#anchor { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 0; }
QToolButton#anchor:hover { border-color: @fieldBorder2@; }
QToolButton#anchor:checked { border-color: @red@; background: @redTint2@; }
QToolButton#icon:checked { border-color: @red@; background: @redTint@; }
QLabel#profileState { font-size: 11px; color: @muted@; }
QLabel#profileState[state="dirty"] { color: @warn@; }
QLabel#profileState[state="saved"] { color: @ok@; }
QPushButton#accent[dirty="true"] { background: @red@; color: @onRed@; border: none; }
)QSS";

// Дополнения к стилю для «Стекла» и «Терминала» (идут после основного — перекрывают его)
const char* kQssGlass = R"QSS(
QFrame#card { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @cardHi@, stop:1 @card@); border: 1px solid @glassLine@; }
QFrame#sidebar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @sideHi@, stop:1 @side@); border-right: 1px solid @glassLine@; }
QFrame#statusbar { background: @side@; }
QFrame#slot { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @fieldHi@, stop:1 @field@); }
QPushButton#start { border-radius: 28px; }
QPushButton#primary { border-radius: 17px; }
QPushButton#keyPick, QPushButton#keyPickMini, QPushButton#keyPickBig { border-bottom: 1px solid @fieldBorder2@; }
QLabel#keycapLabel { border-bottom: 1px solid @fieldBorder2@; }
QLabel#num { color: @redSoft@; }
QFrame#counterBox { border-radius: 18px; }
QPushButton#choiceCard, QFrame#onbCard { border-radius: 22px; }
)QSS";

const char* kQssSharp = R"QSS(
QFrame#card { border-left: 3px solid @red@; }
QLabel#section, QLabel#cardTitle, QLabel#counterCaption, QLabel#fieldLabel { font-family: "@mono@"; }
QLabel#cardTitle { font-weight: 700; }
QLabel#num { background: @red@; color: @onRed@; padding: 0 4px; }
QFrame#sidebar { border-right: 1px solid @fieldBorder2@; }
QFrame#statusbar { border-top: 1px solid @fieldBorder2@; }
QPushButton#keyPick, QPushButton#keyPickMini { border-bottom: 2px solid @fieldBorder2@; }
QPushButton#keyPickBig { border-bottom: 2px solid @fieldBorder2@; }
QLabel#keycapLabel { border-bottom: 2px solid @fieldBorder2@; }
QFrame#toast { border-left: 4px solid @red@; }
)QSS";

int scaledRadius(int base, int cardR) {
    if (base <= 0) return 0;
    if (cardR <= 4) return qMin(base, cardR);           // «Терминал» — почти прямые углы
    return qMax(0, qRound(base * cardR / 14.0));
}

QString buildQss(const Theme::Tokens& t, const Theme::Appearance& a) {
    QString q = QString::fromUtf8(kQss);
    // скругления — по стилю и редактору (база — «Студия», карточки 14 px)
    const int cardR = Theme::cardRadiusFor(a);
    if (cardR != 14) {
        // «/*pill*/» — кружки и «таблетки»: в «Стекле» не увеличиваем (иначе Qt рисует квадрат)
        static const QRegularExpression re("border-radius:\\s*(\\d+)px(\\s*/\\*pill\\*/)?");
        QString out;
        out.reserve(q.size());
        int last = 0;
        auto it = re.globalMatch(q);
        while (it.hasNext()) {
            const auto m = it.next();
            out += q.mid(last, m.capturedStart() - last);
            const int base = m.captured(1).toInt();
            const bool pill = !m.captured(2).isEmpty();
            const int v = pill ? (cardR <= 4 ? qMin(base, cardR) : base) : scaledRadius(base, cardR);
            out += QString("border-radius: %1px").arg(v);
            last = m.capturedEnd();
        }
        out += q.mid(last);
        q = out;
    }
    const QString v = Theme::normalizeVariant(a.variant);
    if (v == "glass") q += QString::fromUtf8(kQssGlass);
    else if (v == "sharp") q += QString::fromUtf8(kQssSharp);

    const QColor W("#FFFFFF");
    auto rep = [&](const char* k, const QColor& c) { q.replace(QString("@%1@").arg(k), c.name()); };
    rep("cardHi", mixc(W, t.card, t.light ? 0.6 : 0.05));
    rep("sideHi", mixc(W, t.side, t.light ? 0.6 : 0.035));
    rep("fieldHi", mixc(W, t.field, t.light ? 0.5 : 0.05));
    rep("glassLine", mixc(t.red, t.cardBorder, 0.22));
    rep("bg", t.bg); rep("side", t.side); rep("bar", t.bar); rep("card", t.card);
    rep("cardBorder", t.cardBorder); rep("fieldDeep", t.fieldDeep); rep("fieldBorder2", t.fieldBorder2);
    rep("fieldBorder", t.fieldBorder); rep("field", t.field); rep("keycap", t.keycap);
    rep("text2", t.text2); rep("text", t.text); rep("muted", t.muted); rep("faint", t.faint);
    rep("redHi", t.redHi); rep("redSoft", t.redSoft); rep("redTint2", t.redTint2); rep("redTint", t.redTint);
    rep("redLine", t.redLine); rep("red", t.red);
    rep("okBg", t.okBg); rep("okLine", t.okLine); rep("ok", t.ok);
    rep("warnBg", t.warnBg); rep("warnLine", t.warnLine); rep("warn", t.warn);
    rep("track", t.track);
    rep("onRed", t.onRed);
    q.replace("@display@", g_display);
    q.replace("@ui@", g_ui);
    q.replace("@mono@", g_mono);
    q.replace("@chevron@", chevronFile(t.muted));
    return q;
}

// Ползунок: щелчок по полосе ставит его сразу в это место (а не шагами)
class AqProxyStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint h, const QStyleOption* o, const QWidget* w, QStyleHintReturn* r) const override {
        if (h == QStyle::SH_Slider_AbsoluteSetButtons) return Qt::LeftButton | Qt::MiddleButton;
        if (h == QStyle::SH_Slider_PageSetButtons) return Qt::NoButton;
        return QProxyStyle::styleHint(h, o, w, r);
    }
};
bool g_styleSet = false;

// Плотность: отступы раскладок окна при его показе
qreal densityFactor(int d) { return d <= 0 ? 0.65 : d >= 2 ? 1.35 : 1.0; }

class DensityFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show && o->isWidgetType()) {
            auto* w = static_cast<QWidget*>(o);
            if (w->isWindow()) Theme::applyDensity(w);
        }
        return false;
    }
};
bool g_densityFilter = false;

QString g_srcDisplay = "Unbounded", g_srcUi = "Onest", g_srcMono = "JetBrains Mono";
} // namespace

void Theme::loadFonts() {
    if (g_fontsLoaded) return;
    g_fontsLoaded = true;
    auto load = [](const char* res) -> QString {
        const int id = QFontDatabase::addApplicationFont(res);
        const QStringList fams = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
        return fams.value(0);
    };
    QString d = load(":/fonts/Unbounded-Bold.ttf");
    load(":/fonts/Unbounded-Black.ttf");
    QString u = load(":/fonts/Onest-Regular.ttf");
    load(":/fonts/Onest-SemiBold.ttf");
    QString m = load(":/fonts/JetBrainsMono-Medium.ttf");
    load(":/fonts/JetBrainsMono-Bold.ttf");
    if (!d.isEmpty()) g_srcDisplay = d;
    if (!u.isEmpty()) g_srcUi = u;
    if (!m.isEmpty()) g_srcMono = m;
    g_display = g_srcDisplay;
    g_ui = g_srcUi;
    g_mono = g_srcMono;
}

// ---------------------------------------------------------------------------
// Стили интерфейса
// ---------------------------------------------------------------------------
QStringList Theme::variants() { return {"studio", "glass", "sharp"}; }

QString Theme::normalizeVariant(const QString& id) {
    return variants().contains(id) ? id : QString("studio");
}

QString Theme::variantLabel(const QString& id) {
    const QString v = normalizeVariant(id);
    if (v == "glass") return T("Стекло");
    if (v == "sharp") return T("Терминал");
    return T("Студия");
}

QString Theme::variantNote(const QString& id) {
    const QString v = normalizeVariant(id);
    if (v == "glass") return T("мягкие скругления, глубина, спокойные заголовки");
    if (v == "sharp") return T("прямые углы, моноширинный шрифт, чёткие линии");
    return T("фирменный стиль AutoQ: крупные заголовки, плитки, клавиши");
}

Theme::Appearance Theme::variantPreset(const QString& id) {
    Appearance a = current();
    a.variant = normalizeVariant(id);
    a.dark = true;
    a.radius = -1;
    a.displayFont = -1;
    a.textFont = -1;
    a.borders = 1;
    a.density = 1;
    if (a.variant == "glass") {
        a.accent = QColor("#A855F7");
        a.bgDark = QColor("#0B1020");
        a.bgLight = QColor("#F3EEF8");
        a.iconStyle = "duo";
    } else if (a.variant == "sharp") {
        a.accent = QColor("#22C55E");
        a.bgDark = QColor("#0A120E");
        a.bgLight = QColor("#EEF6F0");
        a.iconStyle = "neon";
        a.borders = 2;
    } else {
        a.accent = defaultAccent();
        a.bgDark = defaultBg(true);
        a.bgLight = defaultBg(false);
        a.iconStyle = "gloss";
    }
    return a;
}

int Theme::cardRadiusFor(const Appearance& a) {
    if (a.radius >= 0) return qBound(0, a.radius, 24);
    const QString v = normalizeVariant(a.variant);
    return v == "glass" ? 20 : v == "sharp" ? 3 : 14;
}

int Theme::radiusFor(const Appearance& a, int base) { return scaledRadius(base, cardRadiusFor(a)); }

QString Theme::displayFamilyFor(const Appearance& a) {
    loadFonts();
    int f = a.displayFont;
    if (f < 0) {
        const QString v = normalizeVariant(a.variant);
        f = v == "glass" ? 1 : v == "sharp" ? 2 : 0;
    }
    return f == 1 ? g_srcUi : f == 2 ? g_srcMono : g_srcDisplay;
}

QString Theme::uiFamilyFor(const Appearance& a) {
    loadFonts();
    if (a.textFont == 1) return g_srcMono;
    if (a.textFont == 2) {
        const QString sys = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
        return sys.isEmpty() ? g_srcUi : sys;
    }
    return g_srcUi;
}

// ---------------------------------------------------------------------------
// Анимации
// ---------------------------------------------------------------------------
QStringList Theme::animations() { return {"none", "fade", "slide", "cascade"}; }

QString Theme::normalizeAnim(const QString& id) {
    return animations().contains(id) ? id : QString("slide");
}

QString Theme::animLabel(const QString& id) {
    const QString v = normalizeAnim(id);
    if (v == "none") return T("Без анимаций");
    if (v == "fade") return T("Плавно");
    if (v == "cascade") return T("Каскад");
    return T("Сдвиг");
}

QString Theme::animNote(const QString& id) {
    const QString v = normalizeAnim(id);
    if (v == "none") return T("всё меняется сразу — для слабых ПК");
    if (v == "fade") return T("экраны и плашки плавно проявляются");
    if (v == "cascade") return T("карточки выезжают одна за другой");
    return T("экраны мягко выезжают снизу");
}

// ---------------------------------------------------------------------------
// Размер интерфейса
// ---------------------------------------------------------------------------
// Меньше 100 % — через QT_SCALE_FACTOR < 1. Но Qt не умеет рисовать мельче
// пикселя экрана: если итоговый масштаб окна (система × наш) меньше 1, QPainter
// считает его за 1, а обрезка — нет, и виджеты рисуются кусками (так было на 90 %
// при 100 % в Windows). Поэтому масштаб окна держим не меньше 1: 90 % работает,
// когда в системе 125 % и больше, а на экране 100 % остаётся 100 %.
static int g_startupScale = 100;
static double g_appliedFactor = 1.0;   // какой QT_SCALE_FACTOR поставили мы (1 — не ставили)

QList<int> Theme::guiScaleChoices() { return {90, 100, 110, 125, 150, 175, 200}; }

int Theme::guiScale() {
    auto s = AppSettings::open();
    const int v = s->value("guiScale", 100).toInt();
    return guiScaleChoices().contains(v) ? v : 100;
}

void Theme::setGuiScale(int percent) {
    if (!guiScaleChoices().contains(percent)) return;
    auto s = AppSettings::open();
    s->setValue("guiScale", percent);
    s->sync();
}

int Theme::startupGuiScale() { return g_startupScale; }

#ifdef _WIN32
// Наименьший масштаб среди мониторов (1.0 = 100 %, 1.25 = 125 %), 0 — не узнать.
// До QApplication процесс ещё «не знает» о DPI, поэтому спрашиваем в режиме
// Per-Monitor v2 только для этого потока и сразу возвращаем как было.
static BOOL CALLBACK minDpiProc(HMONITOR mon, HDC, LPRECT, LPARAM data) {
    using GetDpiFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static GetDpiFn getDpi = [] {
        HMODULE sh = LoadLibraryW(L"shcore.dll");
        return sh ? reinterpret_cast<GetDpiFn>(reinterpret_cast<void*>(GetProcAddress(sh, "GetDpiForMonitor"))) : nullptr;
    }();
    UINT x = 0, y = 0;
    if (getDpi && getDpi(mon, 0 /* MDT_EFFECTIVE_DPI */, &x, &y) == S_OK && x > 0) {
        UINT* minDpi = reinterpret_cast<UINT*>(data);
        if (*minDpi == 0 || x < *minDpi) *minDpi = x;
    }
    return TRUE;
}

static double windowsMinMonitorScale() {
    using SetCtxFn = void*(WINAPI*)(void*);
    HMODULE user = GetModuleHandleW(L"user32.dll");
    auto setCtx = user ? reinterpret_cast<SetCtxFn>(reinterpret_cast<void*>(GetProcAddress(user, "SetThreadDpiAwarenessContext")))
                       : nullptr;
    if (!setCtx) return 0;
    void* old = setCtx(reinterpret_cast<void*>(static_cast<intptr_t>(-4)));   // PER_MONITOR_AWARE_V2
    UINT minDpi = 0;
    EnumDisplayMonitors(nullptr, nullptr, minDpiProc, reinterpret_cast<LPARAM>(&minDpi));
    if (old) setCtx(old);
    return minDpi ? minDpi / 96.0 : 0;
}
#endif

void Theme::applyGuiScaleEnv() {
    QSettings pre(AQ_NAME, "config");
    int v = pre.value("guiScale", 100).toInt();
    if (!guiScaleChoices().contains(v)) v = 100;
    g_startupScale = v;
    // перезапуск из AutoQ: старое значение досталось от нас же — его можно менять
    if (qEnvironmentVariableIsSet("AQ_OWN_SCALE")) {
        qunsetenv("QT_SCALE_FACTOR");
        qunsetenv("AQ_OWN_SCALE");
    }
    double f = v / 100.0;
    // AutoQ перезапустился сам: экран оказался 100 % — мельче нельзя
    if (qEnvironmentVariableIsSet("AQ_SCALE_CLAMP")) {
        f = qMax(f, qEnvironmentVariable("AQ_SCALE_CLAMP").toDouble());
        qunsetenv("AQ_SCALE_CLAMP");
    }
#ifdef _WIN32
    if (f < 1.0) {
        const double minScale = windowsMinMonitorScale();
        if (minScale > 0) f = qMax(f, 1.0 / minScale);
    }
#endif
    if (qAbs(f - 1.0) > 0.001 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        qputenv("QT_SCALE_FACTOR", QByteArray::number(f, 'f', 4));
        qputenv("AQ_OWN_SCALE", "1");
        g_appliedFactor = f;
    }
}

// масштаб экранов без нашего множителя: наименьший (1.0 — 100 %)
static double baseScreenScale() {
    double m = 0;
    for (QScreen* sc : QGuiApplication::screens()) {
        const double d = sc->devicePixelRatio() / g_appliedFactor;
        if (m == 0 || d < m) m = d;
    }
    return m > 0 ? m : 1.0;
}

double Theme::scaleClampNeeded() {
    if (g_appliedFactor >= 1.0) return 0;
    double minDpr = 0;
    for (QScreen* sc : QGuiApplication::screens())
        if (minDpr == 0 || sc->devicePixelRatio() < minDpr) minDpr = sc->devicePixelRatio();
    if (minDpr <= 0 || minDpr >= 0.999) return 0;
    return qMin(1.0, g_appliedFactor / minDpr);
}

int Theme::minUsableGuiScale() {
    const double base = baseScreenScale();
    for (int v : guiScaleChoices())
        if (base * v / 100.0 >= 0.999) return v;
    return 100;
}

// ---------------------------------------------------------------------------
// Порядок карточек
// ---------------------------------------------------------------------------
QStringList Theme::cardIds() { return {"speed", "window", "profile"}; }

QString Theme::cardLabel(const QString& id) {
    if (id == "window") return T("Окно игры");
    if (id == "profile") return T("Профиль и запуск");
    return T("Скорость");
}

QStringList Theme::cardOrder() {
    auto s = AppSettings::open();
    QStringList out;
    for (const QString& id : s->value("cardOrder").toStringList())
        if (cardIds().contains(id) && !out.contains(id)) out << id;
    for (const QString& id : cardIds())
        if (!out.contains(id)) out << id;
    return out;
}

void Theme::setCardOrder(const QStringList& order) {
    auto s = AppSettings::open();
    s->setValue("cardOrder", order);
    s->sync();
}

// ---------------------------------------------------------------------------
int Theme::r(int base) { return scaledRadius(base, cardRadiusFor(current())); }

int Theme::sp(int base) { return qMax(0, qRound(base * densityFactor(current().density))); }

void Theme::applyDensity(QWidget* root) {
    if (!root) return;
    const qreal f = densityFactor(current().density);
    QList<QLayout*> layouts = root->findChildren<QLayout*>();
    for (QLayout* l : layouts) {
        if (l->property("aqF").isValid() && qFuzzyCompare(l->property("aqF").toDouble(), f)) continue;
        if (!l->property("aqM").isValid()) {
            const QMargins m = l->contentsMargins();
            l->setProperty("aqM", QRect(m.left(), m.top(), m.right(), m.bottom()));
            if (auto* g = qobject_cast<QGridLayout*>(l)) {
                l->setProperty("aqH", g->horizontalSpacing());
                l->setProperty("aqV", g->verticalSpacing());
            } else {
                l->setProperty("aqS", l->spacing());
            }
        }
        const QRect m = l->property("aqM").toRect();
        auto sc = [f](int v) { return v <= 0 ? v : qMax(1, qRound(v * f)); };
        l->setContentsMargins(sc(m.x()), sc(m.y()), sc(m.width()), sc(m.height()));
        if (auto* g = qobject_cast<QGridLayout*>(l)) {
            const int h = l->property("aqH").toInt(), v = l->property("aqV").toInt();
            if (h >= 0) g->setHorizontalSpacing(sc(h));
            if (v >= 0) g->setVerticalSpacing(sc(v));
        } else {
            const int s0 = l->property("aqS").toInt();
            if (s0 >= 0) l->setSpacing(sc(s0));
        }
        l->setProperty("aqF", f);
    }
}

QColor Theme::defaultAccent() { return QColor("#E11D2A"); }
QColor Theme::defaultBg(bool dark) { return dark ? QColor("#0B0B0D") : QColor("#F4F4F6"); }

QStringList Theme::accentPresets() {
    return {"#E11D2A", "#FF6A13", "#F5B700", "#22C55E", "#14B8A6",
            "#3B82F6", "#6366F1", "#A855F7", "#EC4899", "#E5E7EB"};
}
QStringList Theme::bgPresets(bool dark) {
    if (dark) return {"#0B0B0D", "#131418", "#0B1020", "#120C1A", "#0A120E", "#160B0B"};
    return {"#F4F4F6", "#FFFFFF", "#F6F1EA", "#EEF3FA", "#F3EEF8", "#EEF6F0"};
}
QString Theme::colorName(const QString& hex) {
    static const QHash<QString, QString> n = {
        {"#E11D2A", T("красный")}, {"#FF6A13", T("оранжевый")}, {"#F5B700", T("жёлтый")}, {"#22C55E", T("зелёный")},
        {"#14B8A6", T("бирюзовый")}, {"#3B82F6", T("синий")}, {"#6366F1", T("индиго")}, {"#A855F7", T("фиолетовый")},
        {"#EC4899", T("розовый")}, {"#E5E7EB", T("белый")},
        {"#0B0B0D", T("чёрный")}, {"#131418", T("графит")}, {"#0B1020", T("ночной синий")}, {"#120C1A", T("тёмно-фиолетовый")},
        {"#0A120E", T("тёмно-зелёный")}, {"#160B0B", T("тёмно-красный")},
        {"#F4F4F6", T("светло-серый")}, {"#FFFFFF", T("белый")}, {"#F6F1EA", T("тёплый")}, {"#EEF3FA", T("голубой")},
        {"#F3EEF8", T("лавандовый")}, {"#EEF6F0", T("мятный")}};
    return n.value(hex.toUpper(), hex.toUpper());
}

QString Theme::colorLabel(const QColor& c) {
    const QString hex = c.name().toUpper();
    const QString n = colorName(hex);
    return n == hex ? T("свой цвет (%1)").arg(hex) : QString("%1 (%2)").arg(n, hex);
}

Theme::Tokens Theme::tokensFor(const Appearance& a) {
    const QColor acc = a.accent.isValid() ? a.accent : defaultAccent();
    const QColor bg = a.dark ? (a.bgDark.isValid() ? a.bgDark : defaultBg(true))
                             : (a.bgLight.isValid() ? a.bgLight : defaultBg(false));
    return buildTokens(a.dark, bg, acc, a.borders);
}

static QColor readColor(QSettings& s, const char* key, const QColor& def) {
    const QColor c(s.value(key).toString());
    return c.isValid() ? c : def;
}

Theme::Appearance Theme::loadAppearance() {
    auto s = AppSettings::open();
    Appearance a;
    a.dark = s->value("theme", 0).toInt() != 1;
    a.accent = readColor(*s, "accentColor", defaultAccent());
    a.bgDark = readColor(*s, "bgDark", defaultBg(true));
    a.bgLight = readColor(*s, "bgLight", defaultBg(false));
    a.iconStyle = AqIcons::normalizeIconStyle(s->value("iconStyle", "gloss").toString());
    a.variant = normalizeVariant(s->value("uiVariant", "studio").toString());
    a.anim = normalizeAnim(s->value("animStyle", "slide").toString());
    a.radius = qBound(-1, s->value("uiRadius", -1).toInt(), 24);
    a.density = qBound(0, s->value("uiDensity", 1).toInt(), 2);
    a.displayFont = qBound(-1, s->value("uiDisplayFont", -1).toInt(), 2);
    a.textFont = qBound(-1, s->value("uiTextFont", -1).toInt(), 2);
    a.borders = qBound(0, s->value("uiBorders", 1).toInt(), 2);
    return a;
}

void Theme::saveAppearance(const Appearance& a) {
    auto s = AppSettings::open();
    s->setValue("theme", a.dark ? 0 : 1);
    s->setValue("accentColor", a.accent.name().toUpper());
    s->setValue("bgDark", a.bgDark.name().toUpper());
    s->setValue("bgLight", a.bgLight.name().toUpper());
    s->setValue("iconStyle", AqIcons::normalizeIconStyle(a.iconStyle));
    s->setValue("uiVariant", normalizeVariant(a.variant));
    s->setValue("animStyle", normalizeAnim(a.anim));
    s->setValue("uiRadius", a.radius);
    s->setValue("uiDensity", a.density);
    s->setValue("uiDisplayFont", a.displayFont);
    s->setValue("uiTextFont", a.textFont);
    s->setValue("uiBorders", a.borders);
    s->sync();
}

const Theme::Appearance& Theme::current() {
    if (!g_appLoaded) { g_app = loadAppearance(); g_appLoaded = true; }
    return g_app;
}

void Theme::apply(QApplication& app, bool dark) {
    Appearance a = current();
    a.dark = dark;
    apply(app, a);
}

void Theme::apply(QApplication& app, const Appearance& a) {
    loadFonts();
    g_app = a;
    g_appLoaded = true;
    if (!g_app.accent.isValid()) g_app.accent = defaultAccent();
    if (!g_app.bgDark.isValid()) g_app.bgDark = defaultBg(true);
    if (!g_app.bgLight.isValid()) g_app.bgLight = defaultBg(false);
    g_app.variant = normalizeVariant(g_app.variant);
    g_app.anim = normalizeAnim(g_app.anim);
    const qreal oldDensity = densityFactor(g_app.density);
    g_dark = g_app.dark;
    g_tok = buildTokens(g_dark, g_dark ? g_app.bgDark : g_app.bgLight, g_app.accent, g_app.borders);
    g_display = displayFamilyFor(g_app);
    g_ui = uiFamilyFor(g_app);
    AqIcons::setAccent(g_app.accent);
    AqIcons::setIconStyle(g_app.iconStyle);
    if (!g_styleSet) {
        app.setStyle(new AqProxyStyle(QStyleFactory::create("Fusion")));
        g_styleSet = true;
    }
    if (!g_densityFilter) {
        app.installEventFilter(new DensityFilter(&app));
        g_densityFilter = true;
    }
    (void)oldDensity;
    QPalette p;
    p.setColor(QPalette::Window, g_tok.bg);
    p.setColor(QPalette::WindowText, g_tok.text);
    p.setColor(QPalette::Base, g_tok.field);
    p.setColor(QPalette::AlternateBase, g_tok.card);
    p.setColor(QPalette::ToolTipBase, g_tok.card);
    p.setColor(QPalette::ToolTipText, g_tok.text);
    p.setColor(QPalette::Text, g_tok.text);
    p.setColor(QPalette::Button, g_tok.field);
    p.setColor(QPalette::ButtonText, g_tok.text);
    p.setColor(QPalette::Highlight, g_tok.red);
    p.setColor(QPalette::HighlightedText, g_tok.onRed);
    p.setColor(QPalette::PlaceholderText, g_tok.faint);
    p.setColor(QPalette::Disabled, QPalette::Text, g_tok.faint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, g_tok.faint);
    p.setColor(QPalette::Disabled, QPalette::WindowText, g_tok.faint);
    app.setPalette(p);
    QFont f(g_ui);
    f.setPixelSize(13);
    app.setFont(f);
    app.setStyleSheet(buildQss(g_tok, g_app));
    // плотность — сразу во всех открытых окнах
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w->isVisible()) applyDensity(w);
}

bool Theme::isDark() { return g_dark; }
const Theme::Tokens& Theme::t() {
    if (!g_tok.bg.isValid()) g_tok = buildTokens(true, Theme::defaultBg(true), Theme::defaultAccent());
    return g_tok;
}
QString Theme::displayFamily() { return g_display; }
QString Theme::uiFamily() { return g_ui; }
QString Theme::monoFamily() { return g_mono; }

void Theme::repolish(QWidget* w) {
    if (!w) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}
