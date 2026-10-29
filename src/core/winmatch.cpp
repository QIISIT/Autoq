#include "core/winmatch.h"
#include <QCoreApplication>
#include <QFileInfo>

static QString normProc(QString s) {
    s = s.trimmed().toLower();
    if (s.endsWith(".exe")) s.chop(4);
    return s;
}

bool processMatches(const QStringList& names, const WindowInfo& info) {
    for (const QString& rawName : names) {
        const QString n = normProc(rawName);
        if (n.isEmpty()) continue;
        for (const QString& c : info.candidates) {
            if (normProc(c) == n) return true;
            // /proc/<pid>/comm обрезается ядром до 15 символов: "GenshinImpact.e"
            if (c.length() == 15) {
                const QString cl = c.toLower();
                if ((n + ".exe").startsWith(cl) || n.startsWith(cl)) return true;
            }
        }
    }
    return false;
}

bool matchesRule(const WindowRule& rule, const WindowInfo& info) {
    if (!info.valid) return false;
    bool hasTitles = false, titleOk = false;
    for (const QString& t : rule.titles) {
        const QString tt = t.trimmed();
        if (tt.isEmpty()) continue;
        hasTitles = true;
        if (info.title.contains(tt, Qt::CaseInsensitive)) { titleOk = true; break; }
    }
    bool hasProcs = false;
    for (const QString& p : rule.processes) if (!p.trimmed().isEmpty()) { hasProcs = true; break; }

    if (!hasProcs && !hasTitles) return false;          // пустое правило — ни с чем
    if (!hasTitles) titleOk = true;
    // заголовок узнать нельзя, но процесс известен — решает процесс
    if (hasTitles && !info.titleKnown && hasProcs && info.processKnown) titleOk = true;
    if (hasProcs) {
        if (info.processKnown) return titleOk && processMatches(rule.processes, info);
        return hasTitles && titleOk;                    // процесс неизвестен — по заголовку
    }
    return titleOk;
}

QString ownProcessName() {
    return QFileInfo(QCoreApplication::applicationFilePath()).fileName();
}
