#pragma once
// Подсказки (И2): как и что делать — короткие объяснения к функциям AutoQ.
// Видны в трёх местах: значок «?» рядом с функцией (подсказки по её теме), один раз
// при первом знакомстве с функцией (сообщение внизу окна) и все сразу, с поиском, —
// в Настройки → Подсказки. Там же их выключают и показывают заново.
#include <QList>
#include <QString>
#include <QStringList>

namespace Tips {
struct Tip {
    QString id;          // "groups.make" — раздел и что
    QString section;     // «Группы»
    QString title;
    QString text;
    bool isNull() const { return id.isEmpty(); }
};
QList<Tip> all();                          // по разделам, в порядке показа
QStringList sections();                    // разделы по порядку
Tip get(const QString& id);                // нет такой — пустая
// поиск без учёта регистра: все слова запроса — в названии, тексте или разделе
QList<Tip> find(const QString& query);

bool badges();                             // значки «?» рядом с функциями
void setBadges(bool on);
bool firstTime();                          // подсказывать при первом знакомстве
void setFirstTime(bool on);
// первое знакомство с функцией: true — показать сейчас (больше не покажется)
bool takeFirstTime(const QString& id);
bool seen(const QString& id);
int seenCount();
void resetSeen();                          // «Показать подсказки заново»
}
