#include "cardlayout.h"
#include "games.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
const int kTitleMax = 40;

QString layoutKey(const QString& gameId) { return QString("games/%1/layout").arg(gameId); }

// порядок трёх нижних карточек из старой настройки (Оформление → Карточки, до 1.13)
QStringList legacyOrder() {
    const QStringList known{"speed", "window", "profile"};
    QStringList out;
    for (const QString& id : AppSettings::open()->value("cardOrder").toStringList())
        if (known.contains(id) && !out.contains(id)) out << id;
    for (const QString& id : known)
        if (!out.contains(id)) out << id;
    return out;
}
}

QStringList CardLayout::builtinCards() { return {"actions", "speed", "window", "profile"}; }

QStringList CardLayout::blockIds() {
    return {"actions", "speed", "framesync", "window", "capture", "profile", "hotkey", "armed"};
}

QString CardLayout::homeOf(const QString& block) {
    if (block == "actions") return "actions";
    if (block == "speed" || block == "framesync") return "speed";
    if (block == "window" || block == "capture") return "window";
    if (block == "profile" || block == "hotkey" || block == "armed") return "profile";
    return QString();
}

bool CardLayout::isBuiltin(const QString& cardId) { return builtinCards().contains(cardId); }

bool CardLayout::isCustom(const QString& cardId) {
    static const QRegularExpression re("^c[1-9][0-9]{0,3}$");
    return re.match(cardId).hasMatch();
}

CardLayout::Layout CardLayout::defaults() {
    Layout l;
    QStringList order{"actions"};
    order += legacyOrder();
    for (const QString& id : order) {
        Card c;
        c.id = id;
        for (const QString& b : blockIds())
            if (homeOf(b) == id) c.blocks << b;
        l << c;
    }
    return l;
}

CardLayout::Layout CardLayout::normalize(Layout in) {
    Layout out;
    QStringList seenCards, seenBlocks;
    for (Card c : in) {
        if (seenCards.contains(c.id) || (!isBuiltin(c.id) && !isCustom(c.id))) continue;
        seenCards << c.id;
        if (isBuiltin(c.id)) c.title.clear();
        else {
            c.title = c.title.simplified().left(kTitleMax);
            if (c.title.isEmpty()) c.title = QStringLiteral("…");
        }
        QStringList blocks;
        for (const QString& b : c.blocks)
            if (blockIds().contains(b) && !seenBlocks.contains(b)) { blocks << b; seenBlocks << b; }
        c.blocks = blocks;
        out << c;
    }
    // встроенной карточки нет — на её место по умолчанию (после соседей)
    const Layout def = defaults();
    for (int i = 0; i < def.size(); ++i) {
        if (indexOf(out, def[i].id) >= 0) continue;
        Card c;
        c.id = def[i].id;
        int at = 0;
        for (int j = i - 1; j >= 0; --j)
            if (const int k = indexOf(out, def[j].id); k >= 0) { at = k + 1; break; }
        out.insert(at, c);
    }
    // потерянные блоки — домой
    for (const QString& b : blockIds()) {
        if (seenBlocks.contains(b)) continue;
        Card& home = out[indexOf(out, homeOf(b))];
        // на своё место среди соседей по умолчанию
        int at = home.blocks.size();
        const QStringList all = blockIds();
        for (int k = 0; k < home.blocks.size(); ++k)
            if (all.indexOf(home.blocks[k]) > all.indexOf(b)) { at = k; break; }
        home.blocks.insert(at, b);
    }
    return out;
}

QString CardLayout::toJson(const Layout& l) {
    QJsonArray arr;
    for (const Card& c : l) {
        QJsonObject o;
        o["id"] = c.id;
        if (!c.title.isEmpty()) o["title"] = c.title;
        o["blocks"] = QJsonArray::fromStringList(c.blocks);
        if (c.collapsed) o["collapsed"] = true;
        arr << o;
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

CardLayout::Layout CardLayout::fromJson(const QString& json, bool* ok) {
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (ok) *ok = err.error == QJsonParseError::NoError && d.isArray();
    if (!d.isArray()) return defaults();
    Layout l;
    for (const QJsonValue& v : d.array()) {
        const QJsonObject o = v.toObject();
        Card c;
        c.id = o.value("id").toString();
        c.title = o.value("title").toString();
        for (const QJsonValue& b : o.value("blocks").toArray()) c.blocks << b.toString();
        c.collapsed = o.value("collapsed").toBool();
        l << c;
    }
    return normalize(l);
}

CardLayout::Layout CardLayout::load(const QString& gameId) {
    const QString v = AppSettings::open()->value(layoutKey(gameId)).toString();
    return v.isEmpty() ? defaults() : fromJson(v);
}

void CardLayout::save(const QString& gameId, const Layout& l) {
    if (gameId.isEmpty()) return;
    auto s = AppSettings::open();
    const Layout n = normalize(l);
    if (isDefault(n)) s->remove(layoutKey(gameId));
    else s->setValue(layoutKey(gameId), toJson(n));
    s->sync();
}

bool CardLayout::isDefault(const Layout& l) { return normalize(l) == defaults(); }

int CardLayout::indexOf(const Layout& l, const QString& cardId) {
    for (int i = 0; i < l.size(); ++i)
        if (l[i].id == cardId) return i;
    return -1;
}

QString CardLayout::cardOf(const Layout& l, const QString& block) {
    for (const Card& c : l)
        if (c.blocks.contains(block)) return c.id;
    return QString();
}

void CardLayout::moveCard(Layout& l, const QString& cardId, int toIndex) {
    const int from = indexOf(l, cardId);
    if (from < 0) return;
    toIndex = qBound(0, toIndex, int(l.size()));
    const Card c = l.takeAt(from);
    if (toIndex > from) --toIndex;      // место считалось до того, как карточку вынули
    l.insert(toIndex, c);
}

void CardLayout::moveBlock(Layout& l, const QString& block, const QString& toCard, int index) {
    const int to = indexOf(l, toCard);
    if (to < 0 || !blockIds().contains(block)) return;
    for (Card& c : l) {
        const int k = c.blocks.indexOf(block);
        if (k < 0) continue;
        if (c.id == toCard && index > k) --index;   // внутри той же карточки
        c.blocks.removeAt(k);
    }
    QStringList& dst = l[to].blocks;
    dst.insert(index < 0 ? dst.size() : qBound(0, index, int(dst.size())), block);
}

QString CardLayout::addCustom(Layout& l, const QString& title, int at) {
    int n = 1;
    while (indexOf(l, QString("c%1").arg(n)) >= 0) ++n;
    Card c;
    c.id = QString("c%1").arg(n);
    c.title = title.simplified().left(kTitleMax);
    if (c.title.isEmpty()) c.title = QStringLiteral("…");
    l.insert(at < 0 ? l.size() : qBound(0, at, int(l.size())), c);
    return c.id;
}

void CardLayout::removeCustom(Layout& l, const QString& cardId) {
    const int i = indexOf(l, cardId);
    if (i < 0 || !isCustom(cardId)) return;
    l.removeAt(i);
    l = normalize(l);                      // блоки сами вернутся домой
}

void CardLayout::setCollapsed(Layout& l, const QString& cardId, bool on) {
    const int i = indexOf(l, cardId);
    if (i >= 0) l[i].collapsed = on;
}

void CardLayout::rename(Layout& l, const QString& cardId, const QString& title) {
    const int i = indexOf(l, cardId);
    if (i < 0 || !isCustom(cardId)) return;
    const QString t = title.simplified().left(kTitleMax);
    if (!t.isEmpty()) l[i].title = t;
}
