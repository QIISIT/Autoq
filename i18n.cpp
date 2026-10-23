#include "i18n.h"
#include <QCoreApplication>
#include <QHash>
#include <QTranslator>

namespace {
I18n::Lang g_lang = I18n::Ru;
}

// словарь — в i18n_en.cpp
struct AqTranslation { const char* ru; const char* en; };
extern const AqTranslation kAqEnglish[];
extern const int kAqEnglishCount;

static const QHash<QString, QString>& englishDict() {
    static const QHash<QString, QString> d = [] {
        QHash<QString, QString> h;
        h.reserve(kAqEnglishCount);
        for (int i = 0; i < kAqEnglishCount; ++i)
            h.insert(QString::fromUtf8(kAqEnglish[i].ru), QString::fromUtf8(kAqEnglish[i].en));
        return h;
    }();
    return d;
}

void I18n::setLanguage(Lang l) { g_lang = l; }
I18n::Lang I18n::language() { return g_lang; }
bool I18n::english() { return g_lang == En; }

I18n::Lang I18n::fromSetting(const QString& s) {
    if (s == "ru") return Ru;
    if (s == "en") return En;
    // как в системе: русский для русско- и близкоязычных систем, иначе английский
    switch (QLocale::system().language()) {
        case QLocale::Russian: case QLocale::Ukrainian: case QLocale::Belarusian:
        case QLocale::Kazakh: case QLocale::Uzbek: case QLocale::Kyrgyz:
            return Ru;
        default:
            return En;
    }
}

QLocale I18n::locale() { return QLocale(g_lang == En ? QLocale::English : QLocale::Russian); }
int I18n::dictionarySize() { return englishDict().size(); }
bool I18n::hasTranslation(const QString& ru) { return englishDict().contains(ru); }

// ---------------------------------------------------------------------------
// Строки самого Qt. Файлы переводов Qt с AutoQ не идут (и на Windows Qt собран
// статически), поэтому нужные строки — здесь. Английский — как в Qt, без перевода
namespace {
struct QtRu { const char* en; const char* ru; };
const QtRu kQtRussian[] = {
    // меню правки у полей ввода (QLineEdit, QWidgetTextControl, QAbstractSpinBox)
    {"&Undo", "&Отменить"}, {"&Redo", "&Вернуть"}, {"Cu&t", "&Вырезать"}, {"&Copy", "&Копировать"},
    {"Copy &Link Location", "Копировать &адрес ссылки"}, {"&Paste", "Вст&авить"}, {"Delete", "Удалить"},
    {"Select All", "Выделить всё"}, {"&Select All", "&Выделить всё"},
    {"&Step up", "Шаг вве&рх"}, {"Step &down", "Шаг вн&из"},
    // полоса прокрутки (правый щелчок)
    {"Scroll here", "Прокрутить сюда"}, {"Left edge", "К левому краю"}, {"Top", "В начало"},
    {"Right edge", "К правому краю"}, {"Bottom", "В конец"}, {"Page left", "На страницу влево"},
    {"Page up", "На страницу вверх"}, {"Page right", "На страницу вправо"}, {"Page down", "На страницу вниз"},
    {"Scroll left", "Прокрутить влево"}, {"Scroll up", "Прокрутить вверх"}, {"Scroll right", "Прокрутить вправо"},
    {"Scroll down", "Прокрутить вниз"},
    // стандартные кнопки окон
    {"OK", "OK"}, {"Save", "Сохранить"}, {"Save All", "Сохранить все"}, {"Open", "Открыть"}, {"&Yes", "&Да"},
    {"Yes to &All", "Да для &всех"}, {"&No", "&Нет"}, {"N&o to All", "Н&ет для всех"}, {"Abort", "Прервать"},
    {"Retry", "Повторить"}, {"Ignore", "Пропустить"}, {"Close", "Закрыть"}, {"Cancel", "Отмена"},
    {"Discard", "Не сохранять"}, {"Help", "Справка"}, {"Apply", "Применить"}, {"Reset", "Сбросить"},
    {"Restore Defaults", "По умолчанию"}, {"Show Details...", "Подробнее…"}, {"Hide Details...", "Скрыть подробности"},
    // запасное окно выбора файла (если у системы нет своего)
    {"Look in:", "Папка:"}, {"File &name:", "&Имя файла:"}, {"Files of type:", "Тип файлов:"},
    {"&Open", "&Открыть"}, {"&Save", "&Сохранить"}, {"&Choose", "&Выбрать"}, {"Back", "Назад"},
    {"Forward", "Вперёд"}, {"Parent Directory", "Папка выше"}, {"Create New Folder", "Новая папка"},
    {"List View", "Список"}, {"Detail View", "Подробно"}, {"Name", "Имя"}, {"Size", "Размер"}, {"Type", "Тип"},
    {"Date Modified", "Изменён"}, {"Save As", "Сохранить как"}, {"All Files (*)", "Все файлы (*)"},
    {"Directory:", "Папка:"}, {"Recent Places", "Недавние"}, {"My Computer", "Мой компьютер"},
    {"Computer", "Компьютер"}, {"&Rename", "&Переименовать"}, {"&Delete", "&Удалить"},
    {"Show &hidden files", "Показывать &скрытые"}, {"&New Folder", "&Новая папка"}, {"New Folder", "Новая папка"},
    {"Are you sure you want to delete '%1'?", "Удалить «%1»?"},
    {"%1 already exists.\nDo you want to replace it?", "%1 уже есть.\nЗаменить?"},
};

const QHash<QString, QString>& qtRussian() {
    static const QHash<QString, QString> d = [] {
        QHash<QString, QString> h;
        for (const QtRu& s : kQtRussian) h.insert(QString::fromUtf8(s.en), QString::fromUtf8(s.ru));
        return h;
    }();
    return d;
}

class QtRussianTranslator : public QTranslator {
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override { return false; }
    QString translate(const char*, const char* source, const char* = nullptr, int = -1) const override {
        return source ? qtRussian().value(QString::fromUtf8(source)) : QString();
    }
};
}

void I18n::installQtTranslator() {
    static QtRussianTranslator* tr = nullptr;
    if (g_lang != Ru || tr || !QCoreApplication::instance()) return;
    tr = new QtRussianTranslator(QCoreApplication::instance());
    QCoreApplication::installTranslator(tr);
}

QString T(const QString& ru) {
    if (g_lang != I18n::En) return ru;
    const auto& d = englishDict();
    auto it = d.constFind(ru);
    return it == d.constEnd() ? ru : it.value();
}

QString T(const char* ru) { return T(QString::fromUtf8(ru)); }
