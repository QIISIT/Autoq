#include "updates.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
const char* const kRepo = "QIISIT/Autoq";

// "Version 1.17.0 beta" -> "1.17.0 beta"; без номера — пусто
QString versionText(const QString& s) {
    static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)+)(\\s*beta)?"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(s);
    if (!m.hasMatch()) return QString();
    return m.captured(2).isEmpty() ? m.captured(1) : m.captured(1) + QStringLiteral(" beta");
}
}

QString Updates::latestApiUrl() {
    return QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(kRepo));
}

QString Updates::releasesPageUrl() {
    return QStringLiteral("https://github.com/%1/releases").arg(QLatin1String(kRepo));
}

Updates::Release Updates::parseRelease(const QByteArray& json, QString* error) {
    auto fail = [error](const char* why) {
        if (error) *error = QString::fromLatin1(why);
        return Release();
    };
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return fail("not json");
    const QJsonObject o = doc.object();
    if (o.value("draft").toBool() || o.value("prerelease").toBool()) return fail("prerelease");
    Release r;
    r.tag = o.value("tag_name").toString().trimmed();
    if (versionNumbers(r.tag).isEmpty()) return fail("no version");
    // в названии релиза номер с «beta» ("Version 1.17.0 beta"); номер берём из тега
    const QString fromName = versionText(o.value("name").toString());
    const QString fromTag = versionText(r.tag);
    r.version = compareVersions(fromName, fromTag) == 0 ? fromName : fromTag;
    r.page = o.value("html_url").toString();
    if (!r.page.startsWith(QLatin1String("https://"))) r.page = releasesPageUrl();
    for (const QJsonValue& v : o.value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        Asset as;
        as.name = a.value("name").toString();
        as.url = a.value("browser_download_url").toString();
        as.size = qint64(a.value("size").toDouble());
        const QString digest = a.value("digest").toString();
        if (digest.startsWith(QLatin1String("sha256:"), Qt::CaseInsensitive)) as.sha256 = digest.mid(7).toLower();
        if (!as.name.isEmpty() && as.url.startsWith(QLatin1String("https://"))) r.assets << as;
    }
    return r;
}

QList<int> Updates::versionNumbers(const QString& text) {
    static const QRegularExpression re(QStringLiteral("\\d+(?:\\.\\d+)*"));
    const auto m = re.match(text);
    QList<int> out;
    if (!m.hasMatch()) return out;
    for (const QString& p : m.captured(0).split('.')) out << p.toInt();
    return out;
}

int Updates::compareVersions(const QString& a, const QString& b) {
    const QList<int> x = versionNumbers(a), y = versionNumbers(b);
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const int p = x.value(i, 0), q = y.value(i, 0);
        if (p != q) return p < q ? -1 : 1;
    }
    static const QRegularExpression test(QStringLiteral("\\btest\\b"), QRegularExpression::CaseInsensitiveOption);
    const bool ta = test.match(a).hasMatch(), tb = test.match(b).hasMatch();
    return ta == tb ? 0 : (ta ? -1 : 1);
}

bool Updates::isNewer(const QString& candidate, const QString& current) {
    return !versionNumbers(candidate).isEmpty() && compareVersions(candidate, current) > 0;
}

const Updates::Asset* Updates::pickAsset(const Release& r, Package p) {
    static const QRegularExpression source(QStringLiteral("^autoq-v[\\d.]+\\.tar\\.gz$"),
                                           QRegularExpression::CaseInsensitiveOption);
    for (const Asset& a : r.assets) {
        const QString n = a.name.toLower();
        switch (p) {
            case Package::Setup:
                if (n.endsWith(QLatin1String("-windows-setup.exe")) || n == QLatin1String("autoq-setup.exe")) return &a;
                break;
            case Package::Portable:
                if (n.endsWith(QLatin1String("-windows-portable.zip"))) return &a;
                break;
            case Package::Source:
                if (source.match(n).hasMatch()) return &a;
                break;
            case Package::Dmg:
                if (n.endsWith(QLatin1String(".dmg"))) return &a;
                break;
            case Package::None:
                return nullptr;
        }
    }
    return nullptr;
}

QString Updates::packageKey(Package p) {
    switch (p) {
        case Package::Setup: return QStringLiteral("setup");
        case Package::Portable: return QStringLiteral("portable");
        case Package::Source: return QStringLiteral("source");
        case Package::Dmg: return QStringLiteral("dmg");
        case Package::None: break;
    }
    return QString();
}

Updates::Package Updates::packageFromKey(const QString& key) {
    for (Package p : {Package::Setup, Package::Portable, Package::Source, Package::Dmg})
        if (packageKey(p) == key) return p;
    return Package::None;
}

QString Updates::sha256OfFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return QString();
    return QString::fromLatin1(h.result().toHex());
}

QString Updates::sumFor(const QByteArray& sums, const QString& fileName) {
    static const QRegularExpression line(QStringLiteral("^([0-9a-fA-F]{64})\\s+\\*?(.+?)\\s*$"));
    for (const QByteArray& raw : sums.split('\n')) {
        const auto m = line.match(QString::fromUtf8(raw));
        if (m.hasMatch() && m.captured(2) == fileName) return m.captured(1).toLower();
    }
    return QString();
}
