#include "i18n.h"
#include <QHash>

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

QString T(const QString& ru) {
    if (g_lang != I18n::En) return ru;
    const auto& d = englishDict();
    auto it = d.constFind(ru);
    return it == d.constEnd() ? ru : it.value();
}

QString T(const char* ru) { return T(QString::fromUtf8(ru)); }
