#include "ui/style/styles.h"
#include "core/cardlayout.h"
#include "core/games.h"
#include "core/i18n.h"
#include "ui/style/icons.h"
#include "appinfo.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
const char* kFormat = "autoq-style";
const int kMaxFile = 1024 * 1024;

QString colorOr(const QJsonValue& v, const QColor& def) {
    const QColor c(v.toString());
    return (c.isValid() && v.toString().startsWith('#') ? c : def).name().toUpper();
}

struct StyleFile { QString name, path; };

QList<StyleFile> styleFiles() {
    QList<StyleFile> out;
    const QDir d(Styles::dir());
    for (const QFileInfo& fi : d.entryInfoList({"*" + Styles::extension()}, QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (fi.size() > kMaxFile || !f.open(QIODevice::ReadOnly)) continue;
        Styles::Profile p;
        if (!Styles::fromJson(QString::fromUtf8(f.readAll()), &p)) continue;
        bool dup = false;
        for (const StyleFile& s : out) dup = dup || s.name.compare(p.name, Qt::CaseInsensitive) == 0;
        if (!dup) out << StyleFile{p.name, fi.absoluteFilePath()};
    }
    return out;
}

StyleFile findStyle(const QString& name) {
    const QString n = name.trimmed();
    for (const StyleFile& s : styleFiles())
        if (s.name.compare(n, Qt::CaseInsensitive) == 0) return s;
    return StyleFile{};
}

bool writeFile(const QString& path, const QByteArray& data, QString* error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error) *error = T("Не получилось записать файл: %1").arg(f.errorString());
        return false;
    }
    return true;
}
}

QString Styles::dir() { return AppSettings::dataDir() + "/styles"; }
QString Styles::extension() { return QStringLiteral(".aqstyle"); }

QStringList Styles::names() {
    QStringList out;
    for (const StyleFile& s : styleFiles()) out << s.name;
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

bool Styles::exists(const QString& name) { return !findStyle(name).path.isEmpty(); }

bool Styles::validName(const QString& name, QString* why) {
    const QString n = name.trimmed();
    QString err;
    if (n.isEmpty()) err = T("Имя не может быть пустым.");
    else if (n.length() > 40) err = T("Слишком длинное имя (максимум 40 символов).");
    if (why) *why = err;
    return err.isEmpty();
}

QString Styles::uniqueName(const QString& name) {
    const QString base = name.trimmed();
    if (!exists(base)) return base;
    const QString stem = base.left(34);             // «Имя (12)» — не длиннее 40
    for (int i = 2;; ++i) {
        const QString n = QString("%1 (%2)").arg(stem).arg(i);
        if (!exists(n)) return n;
    }
}

QString Styles::toJson(const Profile& p) {
    const Theme::Appearance& a = p.look;
    QJsonObject look;
    look["theme"] = a.dark ? "dark" : "light";
    look["accent"] = a.accent.name().toUpper();
    look["bgDark"] = a.bgDark.name().toUpper();
    look["bgLight"] = a.bgLight.name().toUpper();
    look["icons"] = a.iconStyle;
    look["variant"] = a.variant;
    look["anim"] = a.anim;
    look["animSpeed"] = a.animSpeed;
    look["radius"] = a.radius;
    look["density"] = a.density;
    look["displayFont"] = a.displayFont;
    look["textFont"] = a.textFont;
    look["borders"] = a.borders;
    const QJsonObject el = Theme::elementsToJson(a.elements);
    if (!el.isEmpty()) look["elements"] = el;
    QJsonObject o;
    o["format"] = kFormat;
    o["version"] = 1;
    o["app"] = QString(AQ_NAME " " AQ_VERSION);
    o["name"] = p.name.trimmed();
    o["look"] = look;
    if (p.hasLayout()) {
        const QJsonObject l = QJsonDocument::fromJson(p.layout.toUtf8()).object();
        if (!l.isEmpty()) o["layout"] = l;
    }
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

bool Styles::fromJson(const QString& text, Profile* p) {
    if (text.size() > kMaxFile) return false;
    const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    if (o.value("format").toString() != kFormat || !o.value("look").isObject()) return false;
    const QString name = o.value("name").toString().trimmed();
    if (!validName(name)) return false;
    const QJsonObject l = o.value("look").toObject();
    Theme::Appearance a;
    a.dark = l.value("theme").toString() != "light";
    a.accent = QColor(colorOr(l.value("accent"), Theme::defaultAccent()));
    a.bgDark = QColor(colorOr(l.value("bgDark"), Theme::defaultBg(true)));
    a.bgLight = QColor(colorOr(l.value("bgLight"), Theme::defaultBg(false)));
    a.iconStyle = AqIcons::normalizeIconStyle(l.value("icons").toString("gloss"));
    a.variant = Theme::normalizeVariant(l.value("variant").toString("studio"));
    a.anim = Theme::normalizeAnim(l.value("anim").toString("slide"));
    a.animSpeed = qBound(25, l.value("animSpeed").toInt(100), 400);
    a.radius = qBound(-1, l.value("radius").toInt(-1), 24);
    a.density = qBound(0, l.value("density").toInt(1), 2);
    a.displayFont = qBound(-1, l.value("displayFont").toInt(-1), 2);
    a.textFont = qBound(-1, l.value("textFont").toInt(-1), 2);
    a.borders = qBound(0, l.value("borders").toInt(1), 2);
    a.elements = Theme::elementsFromJson(l.value("elements").toObject());
    QString layout;
    if (o.value("layout").isObject()) {
        // раскладка — только если это правда раскладка AutoQ (иначе стиль — без неё)
        const QString lj = QString::fromUtf8(QJsonDocument(o.value("layout").toObject()).toJson(QJsonDocument::Compact));
        CardLayout::Layout cl;
        int side = 0;
        if (CardLayout::importJson(lj, &cl, &side)) layout = CardLayout::exportJson(cl, side);
    }
    if (p) *p = Profile{name, a, layout};
    return true;
}

QString Styles::filePath(const QString& name) { return findStyle(name).path; }

bool Styles::load(const QString& name, Profile* p) {
    const StyleFile s = findStyle(name);
    if (s.path.isEmpty()) return false;
    QFile f(s.path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return fromJson(QString::fromUtf8(f.readAll()), p);
}

bool Styles::save(const Profile& in, QString* error) {
    Profile p = in;
    p.name = p.name.trimmed();
    if (!validName(p.name, error)) return false;
    QString path = findStyle(p.name).path;
    if (path.isEmpty()) {
        const QString base = dir() + "/" + AppSettings::safeFileName(p.name);
        path = base + extension();
        for (int i = 2; QFile::exists(path); ++i) path = QString("%1 (%2)%3").arg(base).arg(i).arg(extension());
    }
    return writeFile(path, toJson(p).toUtf8(), error);
}

bool Styles::remove(const QString& name) {
    const QString path = findStyle(name).path;
    return !path.isEmpty() && QFile::remove(path);
}

bool Styles::rename(const QString& from, const QString& to, QString* error) {
    Profile p;
    const StyleFile src = findStyle(from);
    if (src.path.isEmpty() || !load(from, &p)) {
        if (error) *error = T("Стиль «%1» не найден.").arg(from);
        return false;
    }
    const QString n = to.trimmed();
    if (!validName(n, error)) return false;
    if (n.compare(src.name, Qt::CaseInsensitive) != 0 && exists(n)) {
        if (error) *error = T("Стиль «%1» уже есть.").arg(n);
        return false;
    }
    p.name = n;
    if (!writeFile(src.path, toJson(p).toUtf8(), error)) return false;
    // файл — под новым именем, если оно свободно (иначе остаётся старое имя файла)
    const QString want = dir() + "/" + AppSettings::safeFileName(n) + extension();
    if (QFileInfo(want) != QFileInfo(src.path) && !QFile::exists(want)) QFile::rename(src.path, want);
    return true;
}

bool Styles::sameLook(const Theme::Appearance& a, const Theme::Appearance& b) {
    auto col = [](const QColor& c) { return c.name().toUpper(); };
    return a.dark == b.dark && col(a.accent) == col(b.accent) && col(a.bgDark) == col(b.bgDark) &&
           col(a.bgLight) == col(b.bgLight) && a.iconStyle == b.iconStyle &&
           Theme::normalizeVariant(a.variant) == Theme::normalizeVariant(b.variant) &&
           Theme::normalizeAnim(a.anim) == Theme::normalizeAnim(b.anim) && a.animSpeed == b.animSpeed &&
           a.radius == b.radius && a.density == b.density && a.displayFont == b.displayFont &&
           a.textFont == b.textFont && a.borders == b.borders &&
           Theme::normalizeElements(a.elements) == Theme::normalizeElements(b.elements);
}
