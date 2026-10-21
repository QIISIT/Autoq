#include "keypicker.h"
#include "i18n.h"
#include "keys.h"
#include "theme.h"
#include "icons.h"
#include <QVBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QKeyEvent>
#include <QScreen>
#include <QHash>
#include <QHBoxLayout>
#include <QToolButton>

namespace {
enum { KeyRole = Qt::UserRole + 1 };

// Кроме клавиш список умеет кнопки мыши («mouse:1» ЛКМ, «mouse:3» ПКМ, «mouse:2» СКМ)
// и пункт «нет» («none» — без второго действия)
bool isMouse(const QString& n) { return n.startsWith("mouse:"); }

QString itemGroup(const QString& n) {
    if (isMouse(n)) return T("Мышь");
    if (n == "none") return QString();
    return Keys::groupOf(n);
}

QString itemTip(const QString& n) {
    if (const int b = MouseButtons::fromItemName(n)) return MouseButtons::longLabel(b);
    if (n == "none") return T("Без второго действия");
    return Keys::groupOf(n);
}

// русские подсказки для поиска
QString searchText(const QString& name) {
    if (isMouse(name)) {
        const int b = MouseButtons::fromItemName(name);
        const char* extra = b == 1 ? "лкм lmb left" : b == 3 ? "пкм rmb right" : b == 2 ? "скм mmb middle колесо"
                          : b == 8 ? "mouse4 m4 mb4 боковая назад back x1" : "mouse5 m5 mb5 боковая вперёд вперед forward x2";
        return (KeyPicker::labelFor(name) + " " + itemTip(name) + QString::fromUtf8(" мышь mouse клик click ") +
                QString::fromUtf8(extra)).toLower();
    }
    if (name == "none") return QString::fromUtf8("нет none без");
    static const QHash<QString, QString> extra = {
        {"space", QString::fromUtf8("пробел space")}, {"enter", QString::fromUtf8("ввод enter return")}, {"escape", QString::fromUtf8("esc escape выход")},
        {"tab", QString::fromUtf8("таб tab")}, {"backspace", QString::fromUtf8("backspace стереть")}, {"capslock", QString::fromUtf8("caps капс")},
        {"up", QString::fromUtf8("стрелка вверх up")}, {"down", QString::fromUtf8("стрелка вниз down")}, {"left", QString::fromUtf8("стрелка влево left")},
        {"right", QString::fromUtf8("стрелка вправо right")}, {"delete", QString::fromUtf8("del удалить delete")},
        {"pageup", "pgup page up"}, {"pagedown", "pgdn page down"}, {"print", "prtsc print screen"},
        {"menu", QString::fromUtf8("menu меню")}, {"minus", QString::fromUtf8("минус дефис")}, {"equal", QString::fromUtf8("равно плюс")},
        {"bracketleft", QString::fromUtf8("скобка х")}, {"bracketright", QString::fromUtf8("скобка ъ")}, {"backslash", T("обратный слэш")},
        {"semicolon", QString::fromUtf8("точка с запятой ж")}, {"apostrophe", QString::fromUtf8("кавычка апостроф э")}, {"grave", QString::fromUtf8("тильда ё")},
        {"comma", QString::fromUtf8("запятая б")}, {"period", QString::fromUtf8("точка ю")}, {"slash", T("слэш")},
    };
    return (name + " " + Keys::label(name) + " " + extra.value(name) + " " + Keys::groupOf(name)).toLower();
}

void addHeader(QListWidget* list, const QString& title) {
    auto* it = new QListWidgetItem(title.toUpper());
    it->setFlags(Qt::NoItemFlags);
    QFont f(Theme::uiFamily());
    f.setPixelSize(10);
    f.setWeight(QFont::DemiBold);
    it->setFont(f);
    it->setForeground(Theme::t().muted);
    list->addItem(it);
}

QListWidgetItem* addKey(QListWidget* list, const QString& name, bool current) {
    auto* it = new QListWidgetItem(KeyPicker::labelFor(name));
    it->setData(KeyRole, name);
    QFont f(Theme::monoFamily());
    f.setPixelSize(13);
    f.setWeight(current ? QFont::Bold : QFont::Medium);
    it->setFont(f);
    if (current) it->setForeground(Theme::t().redHi);
    it->setToolTip(itemTip(name));
    list->addItem(it);
    return it;
}
} // namespace

KeyPickerPopup::KeyPickerPopup(const QStringList& keys, const QStringList& common, const QString& current,
                               QWidget* parent)
    : QFrame(parent, Qt::Popup), keys_(keys), common_(common), current_(current) {
    setObjectName("popup");
    setAttribute(Qt::WA_DeleteOnClose);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    search_ = new QLineEdit();
    search_->setObjectName("search");
    search_->setPlaceholderText(T("Поиск: Z, F5, num, пробел…"));
    search_->setClearButtonEnabled(true);
    search_->addAction(AqIcons::ui("search", Theme::t().muted, 14), QLineEdit::LeadingPosition);
    auto* top = new QHBoxLayout();
    top->setSpacing(4);
    top->addWidget(search_, 1);
    auto* x = new QToolButton();
    x->setObjectName("popupX");
    x->setFixedSize(26, 26);
    x->setIcon(AqIcons::ui("x", Theme::t().muted, 12, 2.4));
    x->setToolTip(T("Закрыть"));
    x->setAccessibleName(T("Закрыть"));
    x->setCursor(Qt::PointingHandCursor);
    connect(x, &QToolButton::clicked, this, &QWidget::close);
    top->addWidget(x);
    lay->addLayout(top);
    list_ = new QListWidget();
    list_->setObjectName("keyList");
    list_->setUniformItemSizes(false);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lay->addWidget(list_, 1);
    setFixedSize(250, 340);

    connect(search_, &QLineEdit::textChanged, this, &KeyPickerPopup::rebuild);
    connect(list_, &QListWidget::itemClicked, this, &KeyPickerPopup::choose);
    connect(list_, &QListWidget::itemActivated, this, &KeyPickerPopup::choose);
    search_->installEventFilter(this);
    rebuild(QString());
}

void KeyPickerPopup::rebuild(const QString& filterIn) {
    const QString filter = filterIn.trimmed().toLower();
    list_->clear();
    QListWidgetItem* toSelect = nullptr;
    if (filter.isEmpty()) {
        if (!common_.isEmpty()) {
            addHeader(list_, T("Частые"));
            for (const QString& k : common_) {
                QListWidgetItem* it = addKey(list_, k, k == current_);
                if (k == current_ && !toSelect) toSelect = it;
            }
        }
        if (keys_.contains("none") && !common_.contains("none")) {
            QListWidgetItem* it = addKey(list_, "none", current_ == "none");
            if (current_ == "none") toSelect = it;
        }
        const QStringList order = {T("Мышь"), T("Основные"), T("Буквы"), T("Цифры"), T("F-клавиши"), T("Навигация"),
                                   T("Цифровой блок"), T("Знаки")};
        for (const QString& g : order) {
            bool header = false;
            for (const QString& k : keys_) {
                if (itemGroup(k) != g || common_.contains(k)) continue;
                if (!header) { addHeader(list_, g); header = true; }
                QListWidgetItem* it = addKey(list_, k, k == current_);
                if (k == current_ && !toSelect) toSelect = it;
            }
        }
    } else {
        // точное совпадение (одна буква, «f5») — первым
        QStringList exact, rest;
        for (const QString& k : keys_) {
            if (k == filter || KeyPicker::labelFor(k).toLower() == filter) exact << k;
            else if (searchText(k).contains(filter)) rest << k;
        }
        for (const QString& k : exact + rest) {
            QListWidgetItem* it = addKey(list_, k, k == current_);
            if (!toSelect) toSelect = it;
        }
        if (exact.isEmpty() && rest.isEmpty()) {
            auto* it = new QListWidgetItem(T("Ничего не нашлось"));
            it->setFlags(Qt::NoItemFlags);
            it->setForeground(Theme::t().faint);
            list_->addItem(it);
        }
    }
    if (toSelect) {
        list_->setCurrentItem(toSelect);
        list_->scrollToItem(toSelect, QAbstractItemView::PositionAtCenter);
    }
}

void KeyPickerPopup::choose(QListWidgetItem* it) {
    if (!it) return;
    const QString k = it->data(KeyRole).toString();
    if (k.isEmpty()) return;
    emit picked(k);
    close();
}

bool KeyPickerPopup::eventFilter(QObject* o, QEvent* e) {
    if (o == search_ && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) {
            // стрелки из поиска — по списку, пропуская заголовки групп
            int row = list_->currentRow();
            const int step = ke->key() == Qt::Key_Down ? 1 : -1;
            for (int r = row + step; r >= 0 && r < list_->count(); r += step)
                if (list_->item(r)->flags() & Qt::ItemIsSelectable) { list_->setCurrentRow(r); break; }
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            choose(list_->currentItem());
            return true;
        }
        if (ke->key() == Qt::Key_Escape) { close(); return true; }
    }
    return QFrame::eventFilter(o, e);
}

void KeyPickerPopup::popupAt(QWidget* anchor) {
    QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 4));
    if (QScreen* scr = anchor->screen()) {
        const QRect g = scr->availableGeometry();
        if (pos.y() + height() > g.bottom()) pos.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 4);
        pos.setX(qBound(g.left() + 4, pos.x(), g.right() - width() - 4));
    }
    move(pos);
    show();
    search_->setFocus();
}

// ===========================================================================
QString KeyPicker::labelFor(const QString& name) {
    if (const int b = MouseButtons::fromItemName(name)) return MouseButtons::shortLabel(b);
    if (name == "none") return QString::fromUtf8("—");
    return Keys::label(name);
}

QStringList KeyPicker::actionItems(bool allowNone) {
    QStringList out;
    if (allowNone) out << "none";
    for (int b : MouseButtons::all()) out << MouseButtons::itemName(b);
    out += Keys::actionKeyNames();
    return out;
}

QStringList KeyPicker::hotkeyItems() {
    QStringList out = Keys::hotkeyKeyNames();
    for (int b : MouseButtons::all())
        if (MouseButtons::hotkeyAllowed(b)) out << MouseButtons::itemName(b);
    return out;
}

QStringList KeyPicker::actionCommon() {
    return {"mouse:1", "mouse:3", "f", "e", "space"};
}

void KeyPicker::setCurrentItem(const QString& name) { setCurrent(name, labelFor(name)); }

KeyPicker::KeyPicker(QWidget* parent) : QPushButton(parent) {
    setObjectName("keyPick");
    setCursor(Qt::PointingHandCursor);
    setLayoutDirection(Qt::RightToLeft);      // стрелка справа от названия клавиши
    setIcon(AqIcons::ui("chevron", Theme::t().muted, 12, 2.4));
    setToolTip(T("Выбрать клавишу из списка (есть поиск)"));
    connect(this, &QPushButton::clicked, this, &KeyPicker::openPopup);
}

void KeyPicker::setKeys(const QStringList& names, const QStringList& common) {
    keys_ = names;
    common_ = common;
}

void KeyPicker::setCurrent(const QString& keyName, const QString& display) {
    current_ = keyName;
    setText(display);
    // длинное сочетание (Ctrl+Shift+X) — мельче, чтобы не распирать карточку
    const bool big = objectName() == "keyPickBig";
    setStyleSheet(display.size() > 7 ? QString("QPushButton#%1 { font-size: %2px; }")
                                           .arg(objectName()).arg(big ? 13 : 11)
                                     : QString());
    setIcon(AqIcons::ui("chevron", Theme::t().muted, 12, 2.4));
}

void KeyPicker::openPopup() {
    auto* pop = new KeyPickerPopup(keys_, common_, current_, this);
    connect(pop, &KeyPickerPopup::picked, this, &KeyPicker::picked);
    pop->popupAt(this);
}
