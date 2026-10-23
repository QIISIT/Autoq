#pragma once
// Оформление AutoQ: стиль интерфейса (Студия / Стекло / Терминал), цвета (тема,
// акцент, фон), редактор (скругление, плотность, шрифты, рамки), анимации и
// размер интерфейса. Всё считается из настроек и применяется сразу.
#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>

class QApplication;
class QWidget;
class QJsonObject;

namespace Theme {
    // Вид мелких элементов (редактор интерфейса → щелчок по элементу): по группам —
    // все шаги одинаковые, все кнопки профиля одинаковые и т. д.
    struct ElementStyle {
        int look = 0;          // 0 — как в теме, дальше — варианты группы (elementLooks)
        QString color;         // свой цвет «#rrggbb» (пусто — акцент)
        int radius = -1;       // скругление, px (-1 — как в теме)
        int size = 1;          // 0 — мельче, 1 — обычный, 2 — крупнее
        bool hidden = false;   // спрятан (только у групп, где можно)
        bool isDefault() const { return look == 0 && color.isEmpty() && radius < 0 && size == 1 && !hidden; }
        bool operator==(const ElementStyle& o) const {
            return look == o.look && color == o.color && radius == o.radius && size == o.size && hidden == o.hidden;
        }
        bool operator!=(const ElementStyle& o) const { return !(*this == o); }
    };
    using Elements = QMap<QString, ElementStyle>;   // id группы → вид (только не «как в теме»)
    enum ElementCap { ElLook = 1, ElColor = 2, ElRadius = 4, ElSize = 8, ElHide = 16 };
    QStringList elementIds();                       // slot, key, mode, pause, plate, add, profile, seg, start, hint
    QString elementLabel(const QString& id);        // «Шаги и действия»
    QString elementNote(const QString& id);         // что это за элементы
    int elementCaps(const QString& id);             // что у группы можно менять (ElementCap)
    QStringList elementLooks(const QString& id);    // варианты вида; [0] — «Как в теме»
    QStringList elementSizes(const QString& id);    // «Мельче», «Обычный», «Крупнее»
    QList<int> elementRadii();                      // 0, 4, 8, 12, 16, 24 — варианты скругления
    // в допустимых пределах, без «как в теме» и чужих групп
    Elements normalizeElements(const Elements& e);
    QJsonObject elementsToJson(const Elements& e);
    Elements elementsFromJson(const QJsonObject& o);

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
        Elements elements;             // вид мелких элементов по группам
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
    QList<int> guiScaleChoices();               // 100, 110, 125, 150, 175, 200
    int guiScale();                             // %, 100 — как в системе
    void setGuiScale(int percent);
    int startupGuiScale();                      // с каким размером запущен AutoQ
    void applyGuiScaleEnv();                    // до QApplication: QT_SCALE_FACTOR

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

    // Вид мелких элементов сейчас: группа, её цвет (свой или акцент), скругление
    ElementStyle element(const QString& id);
    QColor elementColor(const QString& id, const QColor& fallback);
    int elementRadius(const QString& id, int base);      // base — как в теме (с учётом r())
    QColor mix(const QColor& a, const QColor& b, qreal t);   // a·t + b·(1−t)
    QColor onColor(const QColor& c);                         // текст на заливке цвета c
    QColor textColor(const QColor& c);                       // цвет c для текста на фоне темы

    // Перерисовать стиль виджета после смены динамического свойства (state="ok" и т.п.)
    void repolish(QWidget* w);
}
