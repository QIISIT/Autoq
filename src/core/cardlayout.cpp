#include "core/cardlayout.h"
#include "core/games.h"
#include "core/i18n.h"
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

bool validColor(const QString& c) {
    static const QRegularExpression re("^#[0-9a-fA-F]{6}$");
    return re.match(c).hasMatch();
}

// место блока среди соседей по умолчанию
int defaultSlot(const QStringList& blocks, const QString& b) {
    const QStringList all = CardLayout::blockIds();
    for (int k = 0; k < blocks.size(); ++k)
        if (all.indexOf(blocks[k]) > all.indexOf(b)) return k;
    return blocks.size();
}

QJsonObject cardJson(const CardLayout::Card& c) {
    QJsonObject o;
    o["id"] = c.id;
    if (!c.title.isEmpty()) o["title"] = c.title;
    o["blocks"] = QJsonArray::fromStringList(c.blocks);
    if (c.collapsed) o["collapsed"] = true;
    if (c.span) o["span"] = c.span;
    if (c.height) o["height"] = c.height;
    if (c.style) o["style"] = c.style;
    if (c.hideTitle) o["hideTitle"] = true;
    if (c.hideNumber) o["hideNumber"] = true;
    if (!c.color.isEmpty()) o["color"] = c.color;
    if (c.row) o["row"] = true;
    return o;
}

CardLayout::Layout cardsFrom(const QJsonArray& arr) {
    CardLayout::Layout l;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        CardLayout::Card c;
        c.id = o.value("id").toString();
        c.title = o.value("title").toString();
        for (const QJsonValue& b : o.value("blocks").toArray()) c.blocks << b.toString();
        c.collapsed = o.value("collapsed").toBool();
        c.span = o.value("span").toInt();
        c.height = o.value("height").toInt();
        c.style = o.value("style").toInt();
        c.hideTitle = o.value("hideTitle").toBool();
        c.hideNumber = o.value("hideNumber").toBool();
        c.color = o.value("color").toString();
        c.row = o.value("row").toBool();
        l << c;
    }
    return l;
}
}

QStringList CardLayout::builtinCards() { return {"head", "launch", "actions", "speed", "window", "profile"}; }

QStringList CardLayout::blockIds() {
    return {"game", "counter", "tools", "start",
            "actions", "monitor",
            "speed", "interval", "jitter", "framesync",
            "window", "rules", "capture",
            "profile", "preset",
            "hotkey", "keymode", "sounds", "armed"};
}

// «Процесс и заголовок» — то же, что в окне «Изменить игру»; «Используется» — то же, что кружок
// у игры в списке: на карточках их по умолчанию нет
QStringList CardLayout::defaultHidden() { return {"monitor", "rules", "armed"}; }

QString CardLayout::homeOf(const QString& block) {
    static const QHash<QString, QString> home = {
        {"game", "head"}, {"counter", "head"}, {"tools", "head"}, {"start", "head"},
        {"actions", "actions"}, {"monitor", "actions"},
        {"speed", "speed"}, {"interval", "speed"}, {"jitter", "speed"}, {"framesync", "speed"},
        {"window", "window"}, {"rules", "window"}, {"capture", "window"},
        {"profile", "profile"}, {"preset", "profile"},
        {"hotkey", "launch"}, {"keymode", "launch"}, {"sounds", "launch"}, {"armed", "launch"},
    };
    return home.value(block);
}

bool CardLayout::isBuiltin(const QString& cardId) { return builtinCards().contains(cardId); }

bool CardLayout::isCustom(const QString& cardId) {
    static const QRegularExpression re("^c[1-9][0-9]{0,3}$");
    return re.match(cardId).hasMatch();
}

CardLayout::Card CardLayout::defaultCard(const QString& id) {
    Card c;
    c.id = id;
    for (const QString& b : blockIds())
        if (homeOf(b) == id && !defaultHidden().contains(b)) c.blocks << b;
    if (id == "head") {
        // шапка: прозрачная, без заголовка и номера, блоки — в ряд
        c.row = true;
        c.style = Clear;
        c.hideTitle = c.hideNumber = true;
    }
    if (id == "launch") c.row = true;   // клавиша, «как работает» и звук — в одну строку (узко — переносятся)
    return c;
}

CardLayout::Layout CardLayout::defaults() {
    Layout l;
    // «Запуск» (клавиша, как работает, звук) — сразу под шапкой, рядом с «Старт»
    QStringList order{"head", "launch", "actions"};
    order += legacyOrder();
    for (const QString& id : order) l << defaultCard(id);
    Card hidden;
    hidden.id = kHidden;
    hidden.blocks = defaultHidden();
    l << hidden;
    return l;
}

CardLayout::Layout CardLayout::normalize(const Layout& in) {
    Layout out;
    QStringList seenCards, seenBlocks;
    Card hidden;
    hidden.id = kHidden;
    // раскладка до 1.17: «Профиль и запуск» — одна карточка. Блоки запуска из неё — в новую
    // «Запуск» («Используется» — в «Скрытые»); перенесённые в другие карточки остаются там
    const bool splitLaunch = indexOf(in, "launch") < 0;
    for (Card c : in) {
        const bool isHidden = c.id == kHidden;
        if (seenCards.contains(c.id) || (!isHidden && !isBuiltin(c.id) && !isCustom(c.id))) continue;
        seenCards << c.id;
        QStringList blocks;
        for (const QString& b : c.blocks) {
            if (splitLaunch && c.id == "profile" && homeOf(b) == "launch") continue;
            if (blockIds().contains(b) && !seenBlocks.contains(b)) { blocks << b; seenBlocks << b; }
        }
        if (isHidden) { hidden.blocks = blocks; continue; }
        c.blocks = blocks;
        if (isBuiltin(c.id)) c.title.clear();
        else {
            c.title = c.title.simplified().left(kTitleMax);
            if (c.title.isEmpty()) c.title = QStringLiteral("…");
        }
        c.span = qBound(0, c.span, 4);
        c.height = qBound(0, c.height, kMaxHeight);
        if (c.style < Normal || c.style > Clear) c.style = Normal;
        if (!validColor(c.color)) c.color.clear();
        c.color = c.color.toLower();
        out << c;
    }
    // встроенной карточки нет — на её место по умолчанию (после соседей)
    const Layout def = defaults();
    for (int i = 0; i < def.size(); ++i) {
        if (def[i].id == kHidden || indexOf(out, def[i].id) >= 0) continue;
        Card c = def[i];
        c.blocks.clear();
        int at = 0;
        for (int j = i - 1; j >= 0; --j)
            if (const int k = indexOf(out, def[j].id); k >= 0) { at = k + 1; break; }
        out.insert(at, c);
    }
    // потерянные блоки — домой (новые необязательные — в «Скрытые»)
    for (const QString& b : blockIds()) {
        if (seenBlocks.contains(b)) continue;
        if (defaultHidden().contains(b)) { hidden.blocks << b; continue; }
        QStringList& home = out[indexOf(out, homeOf(b))].blocks;
        home.insert(defaultSlot(home, b), b);
    }
    out << hidden;                      // служебная — всегда последней
    return out;
}

QString CardLayout::toJson(const Layout& l) {
    QJsonArray arr;
    for (const Card& c : l) arr << cardJson(c);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

CardLayout::Layout CardLayout::fromJson(const QString& json, bool* ok) {
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (ok) *ok = err.error == QJsonParseError::NoError && d.isArray();
    if (!d.isArray()) return defaults();
    return normalize(cardsFrom(d.array()));
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
    if (from < 0 || cardId == kHidden) return;
    // не дальше служебной «hidden» — она всегда последняя
    int end = l.size();
    if (end > 0 && l.last().id == kHidden) --end;
    toIndex = qBound(0, toIndex, end);
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
    // перед служебной «hidden» — она всегда последняя
    int end = l.size();
    if (end > 0 && l.last().id == kHidden) --end;
    l.insert(at < 0 ? end : qBound(0, at, end), c);
    return c.id;
}

void CardLayout::removeCustom(Layout& l, const QString& cardId) {
    const int i = indexOf(l, cardId);
    if (i < 0 || !isCustom(cardId)) return;
    const QStringList blocks = l[i].blocks;
    l.removeAt(i);
    // блоки — домой, на свои места (те, которых по умолчанию не видно, — в «Скрытые»)
    for (const QString& b : blocks) {
        const int h = indexOf(l, defaultHidden().contains(b) ? kHidden : homeOf(b));
        if (h < 0) continue;
        l[h].blocks.insert(defaultSlot(l[h].blocks, b), b);
    }
    l = normalize(l);
}

void CardLayout::setCollapsed(Layout& l, const QString& cardId, bool on) {
    const int i = indexOf(l, cardId);
    if (i >= 0 && cardId != kHidden) l[i].collapsed = on;
}

void CardLayout::rename(Layout& l, const QString& cardId, const QString& title) {
    const int i = indexOf(l, cardId);
    if (i < 0 || !isCustom(cardId)) return;
    const QString t = title.simplified().left(kTitleMax);
    if (!t.isEmpty()) l[i].title = t;
}

void CardLayout::hideBlock(Layout& l, const QString& block) {
    if (indexOf(l, kHidden) < 0) l = normalize(l);
    moveBlock(l, block, kHidden, -1);
}

void CardLayout::showBlock(Layout& l, const QString& block) {
    const int h = indexOf(l, homeOf(block));
    if (h < 0 || !blockIds().contains(block)) return;
    QStringList dst = l[h].blocks;
    dst.removeAll(block);
    moveBlock(l, block, l[h].id, defaultSlot(dst, block));
}

QStringList CardLayout::hiddenBlocks(const Layout& l) {
    const int i = indexOf(l, kHidden);
    return i < 0 ? QStringList() : l[i].blocks;
}

// ---------------------------------------------------------------------------
QString CardLayout::templateName(int index) {
    switch (index) {
        case 1: return T("Компакт");
        case 2: return T("Монитор");
        case 3: return T("Одна колонка");
        default: return T("Классика");
    }
}

CardLayout::Layout CardLayout::templateLayout(int index, int* sidebar) {
    Layout l = defaults();
    int side = 0;
    auto card = [&l](const QString& id) -> Card& { return l[indexOf(l, id)]; };
    switch (index) {
        case 1: {   // Компакт: только главное, узкая панель с играми, скорость и окно — рядом
            for (const QString& b : {"jitter", "framesync", "rules", "preset", "keymode", "sounds", "armed"})
                hideBlock(l, b);
            card("speed").span = 2;
            card("window").span = 2;
            card("profile").collapsed = true;
            side = 2;
            break;
        }
        case 2: {   // Монитор: крупный монитор запуска сразу под шапкой
            const QString id = addCustom(l, T("Монитор запуска"), 1);
            moveBlock(l, "monitor", id, 0);
            Card& m = card(id);
            m.style = Accent;
            m.span = 4;
            card("profile").collapsed = true;
            break;
        }
        case 3: {   // Одна колонка: каждая карточка — на всю ширину
            for (Card& c : l)
                if (c.id != kHidden) c.span = 4;
            break;
        }
        default:
            break;
    }
    if (sidebar) *sidebar = side;
    return normalize(l);
}

QString CardLayout::exportJson(const Layout& l, int sidebar) {
    QJsonObject o;
    o["autoq"] = "layout";
    o["version"] = 1;
    o["sidebar"] = qBound(0, sidebar, 3);
    QJsonArray arr;
    for (const Card& c : normalize(l)) arr << cardJson(c);
    o["cards"] = arr;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

bool CardLayout::importJson(const QString& text, Layout* l, int* sidebar) {
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return false;
    const QJsonObject o = d.object();
    if (o.value("autoq").toString() != "layout" || !o.value("cards").isArray()) return false;
    if (l) *l = normalize(cardsFrom(o.value("cards").toArray()));
    if (sidebar && o.value("sidebar").isDouble()) *sidebar = qBound(0, o.value("sidebar").toInt(), 3);   // нет — как сейчас
    return true;
}
