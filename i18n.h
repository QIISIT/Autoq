#pragma once
// Язык интерфейса: русский (исходный текст) или английский.
// Все строки интерфейса пишутся по-русски и проходят через T("…"): в английском
// режиме T находит перевод в словаре (i18n_en.cpp), а если его нет — оставляет как есть.
// Язык выбирается в Настройки → Основные и меняется после перезапуска.
#include <QString>
#include <QLocale>

namespace I18n {
    enum Lang { Ru, En };
    void setLanguage(Lang l);             // один раз при запуске, до создания окон
    Lang language();
    bool english();
    Lang fromSetting(const QString& s);   // "ru" / "en" / "auto" (по системе)
    QLocale locale();                     // числа: «1 200» или «1,200»
    int dictionarySize();
    bool hasTranslation(const QString& ru);
}

QString T(const char* ru);
QString T(const QString& ru);
