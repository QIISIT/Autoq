#pragma once
// Оформление AutoQ: стиль интерфейса (Студия / Стекло / Терминал), цвета (тема,
// акцент, фон), редактор (скругление, плотность, шрифты, рамки), анимации и
// размер интерфейса. Всё считается из настроек и применяется сразу.
#include <QColor>
#include <QString>
#include <QStringList>

class QApplication;
class QWidget;

namespace Theme {
    // Оформление, которое выбирает пользователь (окно «Настройки», первый запуск)
    struct Appearance {
        bool dark = true;
        QColor accent;          // цвет акцента (кнопки, выделение, логотип)
        QColor bgDark;          // фон тёмной темы
        QColor bgLight;         // фон светлой темы
        QString iconStyle = "gloss";   // стиль значков игр (AqIcons::iconStyles)
        QString variant = "studio";    // стиль интерфейса: studio / glass / sharp
        QString anim = "slide";        // анимации: none / fade / slide / cascade
        int animSpeed = 100;           // скорость анимаций, %: 50 — вдвое медленнее, 200 — вдвое быстрее
        // редактор интерфейса (-1 — как у стиля)
        int radius = -1;               // скругление карточек 0–24 px
        int density = 1;               // 0 — компактно, 1 — обычно, 2 — просторно
        int displayFont = -1;          // заголовки: 0 Unbounded, 1 Onest, 2 JetBrains Mono
        int textFont = -1;             // текст: 0 Onest, 1 JetBrains Mono, 2 системный
        int borders = 1;               // рамки: 0 мягкие, 1 обычные, 2 чёткие
    };
    QColor defaultAccent();
    QColor defaultBg(bool dark);
    QStringList accentPresets();                // готовые цвета акцента
    QStringList bgPresets(bool dark);           // готовые цвета фона
    QString colorName(const QString& hex);      // "красный" (для неизвестного — сам hex)
    QString colorLabel(const QColor& c);        // "красный (#E11D2A)" / "свой цвет (#B4F1EC)"
    Appearance loadAppearance();
    void saveAppearance(const Appearance& a);
    void apply(QApplication& app, const Appearance& a);
    const Appearance& current();

    // --- стили интерфейса ---
    QStringList variants();                     // studio, glass, sharp
    QString variantLabel(const QString& id);    // «Студия»
    QString variantNote(const QString& id);     // короткое описание
    QString normalizeVariant(const QString& id);
    // стиль вместе с его цветами (первый запуск): тема, акцент, фон
    Appearance variantPreset(const QString& id);
    int cardRadiusFor(const Appearance& a);     // скругление карточек с учётом редактора
    int radiusFor(const Appearance& a, int base);   // любое скругление (база — для «Студии»)
    QString displayFamilyFor(const Appearance& a);
    QString uiFamilyFor(const Appearance& a);

    // --- анимации ---
    QStringList animations();                   // none, fade, slide, cascade
    QString animLabel(const QString& id);
    QString animNote(const QString& id);
    QString normalizeAnim(const QString& id);
    QList<int> animSpeedChoices();              // 50, 75, 100, 150, 200, 300 — шаги ползунка
    QString animSpeedLabel(int percent);        // «×1,5 — быстрее»

    // --- размер интерфейса (меняется после перезапуска) ---
    QList<int> guiScaleChoices();               // 90, 100, 110, 125, 150, 175, 200
    int guiScale();                             // %, 100 — как в системе
    void setGuiScale(int percent);
    int startupGuiScale();                      // с каким размером запущен AutoQ
    void applyGuiScaleEnv();                    // до QApplication: QT_SCALE_FACTOR
    // после QApplication: экран мельче 1:1 (Qt так не рисует) — с каким множителем
    // перезапуститься (0 — всё хорошо)
    double scaleClampNeeded();
    int minUsableGuiScale();                    // меньше этого на этих экранах нельзя (обычно 100 %)

    // --- порядок карточек на главном экране ---
    QStringList cardIds();                      // speed, window, profile
    QString cardLabel(const QString& id);
    QStringList cardOrder();
    void setCardOrder(const QStringList& order);

    struct Tokens {
        bool light = false;
        QColor onRed;                           // текст на кнопке цвета акцента
        QColor bg, side, bar, card, cardBorder, field, fieldDeep, fieldBorder, fieldBorder2, keycap;
        QColor text, text2, muted, faint;
        QColor red, redHi, redSoft, redTint, redTint2, redLine;
        QColor ok, okBg, okLine, warn, warnBg, warnLine;
        QColor track;
    };

    // Цвета интерфейса для любого оформления — для образцов (не меняет текущую тему)
    Tokens tokensFor(const Appearance& a);

    void loadFonts();                     // встроенные шрифты (Unbounded, Onest, JetBrains Mono)
    void apply(QApplication& app, bool dark);
    bool isDark();
    const Tokens& t();

    QString displayFamily();              // заголовки, логотип
    QString uiFamily();                   // основной текст
    QString monoFamily();                 // клавиши, числа

    // Скругление для виджетов, которые рисуются кодом (база — как у «Студии»)
    int r(int base);
    // Отступы с учётом плотности (Редактор → Плотность)
    int sp(int base);
    // Отступы всех раскладок окна — по текущей плотности (вызывается само при показе окон)
    void applyDensity(QWidget* root);

    // Перерисовать стиль виджета после смены динамического свойства (state="ok" и т.п.)
    void repolish(QWidget* w);
}
