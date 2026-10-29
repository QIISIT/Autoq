#include "ui/dialogs/windowpicker.h"
#include "core/i18n.h"
#include "ui/style/theme.h"
#include "ui/style/icons.h"
#include "ui/style/widgets.h"
#include <QVBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QKeyEvent>
#include <QScreen>
#include <QSet>
#include <functional>

namespace { enum { ValueRole = Qt::UserRole + 1 }; }

WindowPickerPopup::WindowPickerPopup(Mode mode, const QStringList& already, QWidget* parent,
                                     unsigned long long current)
    : QFrame(parent, Qt::Popup), mode_(mode) {
    setObjectName("popup");
    setAttribute(Qt::WA_DeleteOnClose);
    // открытые окна: процессы без повторов (или заголовки, или все окна), своё окно не предлагаем
    ActiveWindowProbe probe;
    const QString own = ownProcessName();
    QSet<QString> seen;
    for (const WindowInfo& w : probe.listWindows()) {
        if (w.processKnown && processMatches({own}, w)) continue;
        if (mode == Window) {
            const QString name = w.processKnown && !w.process.isEmpty() ? w.process : w.title;
            if (name.trimmed().isEmpty()) continue;
            const QString title = w.title.isEmpty() ? T("без заголовка") : w.title;
            entries_ << Entry{name, w.processKnown ? title : QString(), w.handle == current, w};
            continue;
        }
        const QString value = mode == Process ? w.process : w.title;
        if (value.trimmed().isEmpty() || seen.contains(value.toLower())) continue;
        seen.insert(value.toLower());
        bool has = false;
        for (const QString& a : already) if (a.compare(value, Qt::CaseInsensitive) == 0) has = true;
        entries_ << Entry{value, mode == Process ? w.title : w.process, has, w};
    }

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    search_ = new QLineEdit();
    addPopupSearch(lay, this, search_, mode == Title ? T("Поиск по заголовку…") : T("Поиск: имя процесса или окна…"));
    list_ = new QListWidget();
    list_->setObjectName("keyList");
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setWordWrap(false);
    lay->addWidget(list_, 1);
    setFixedSize(330, 360);
    connect(search_, &QLineEdit::textChanged, this, &WindowPickerPopup::rebuild);
    connect(list_, &QListWidget::itemClicked, this, &WindowPickerPopup::choose);
    connect(list_, &QListWidget::itemActivated, this, &WindowPickerPopup::choose);
    search_->installEventFilter(this);
    rebuild(QString());
}

void WindowPickerPopup::rebuild(const QString& filterIn) {
    const QString filter = filterIn.trimmed().toLower();
    list_->clear();
    const auto& t = Theme::t();
    QFont mono(mode_ == Title ? Theme::uiFamily() : Theme::monoFamily());
    mono.setPixelSize(13);
    mono.setWeight(QFont::DemiBold);
    QListWidgetItem* first = nullptr;
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        if (!filter.isEmpty() && !e.value.toLower().contains(filter) && !e.sub.toLower().contains(filter)) continue;
        const QString mark = !e.already ? QString() : mode_ == Window ? T("✓ выбрано · ") : T("✓ уже добавлен · ");
        const QString sub = QFontMetrics(QFont(Theme::uiFamily())).elidedText(mark + e.sub, Qt::ElideRight, 280);
        auto* it = new QListWidgetItem(e.value + (sub.isEmpty() ? QString() : "\n" + sub));
        it->setData(ValueRole, mode_ == Window ? QString::number(i) : e.value);
        it->setFont(mono);
        it->setToolTip(!e.already ? e.sub : mode_ == Window ? T("Это окно уже выбрано") : T("Уже в правилах игры"));
        if (e.already) it->setForeground(t.faint);
        list_->addItem(it);
        if (!first && !e.already) first = it;
    }
    if (list_->count() == 0) {
        auto* it = new QListWidgetItem(entries_.isEmpty() ? T("Открытых окон не нашлось — запусти игру")
                                                          : T("Ничего не нашлось"));
        it->setFlags(Qt::NoItemFlags);
        it->setForeground(t.faint);
        list_->addItem(it);
    }
    if (first) list_->setCurrentItem(first);
}

void WindowPickerPopup::choose(QListWidgetItem* it) {
    if (!it) return;
    const QString v = it->data(ValueRole).toString();
    if (v.isEmpty()) return;
    if (mode_ == Window) {
        const WindowInfo w = entries_.value(v.toInt()).win;
        const auto f = onWindow_;
        close();
        if (f && w.valid) f(w);
        return;
    }
    emit picked(v);
    close();
}

bool WindowPickerPopup::eventFilter(QObject* o, QEvent* e) {
    if (o == search_ && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) {
            const int step = ke->key() == Qt::Key_Down ? 1 : -1;
            const int r = qBound(0, list_->currentRow() + step, list_->count() - 1);
            list_->setCurrentRow(r);
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

void WindowPickerPopup::popupAt(QWidget* anchor) {
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

void WindowPickerPopup::open(QWidget* anchor, Mode mode, const QStringList& already,
                             const std::function<void(const QString&)>& onPicked) {
    auto* pop = new WindowPickerPopup(mode, already, anchor);
    QObject::connect(pop, &WindowPickerPopup::picked, anchor, [onPicked](const QString& v) { onPicked(v); });
    pop->popupAt(anchor);
}

void WindowPickerPopup::openWindows(QWidget* anchor, unsigned long long current,
                                    const std::function<void(const WindowInfo&)>& onPicked) {
    auto* pop = new WindowPickerPopup(Window, {}, anchor, current);
    pop->onWindow_ = onPicked;
    pop->popupAt(anchor);
}
