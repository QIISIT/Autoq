#include "hotkeydialog.h"
#include "i18n.h"
#include "keypicker.h"
#include "keycapturebutton.h"
#include "icons.h"
#include "theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

HotkeyDialog::HotkeyDialog(const QString& title, const QString& note, const HotkeySpec& spec, bool allowNone,
                           const std::function<void(bool)>& suspend, QWidget* parent)
    : QDialog(parent), spec_(spec), suspend_(suspend) {
    setWindowTitle(title + " — AutoQ");
    setMinimumWidth(420);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 18, 20, 16);
    lay->setSpacing(10);
    auto* t = new QLabel(title);
    t->setObjectName("cardTitle");
    lay->addWidget(t);
    if (!note.isEmpty()) {
        auto* n = new QLabel(note);
        n->setObjectName("muted");
        n->setWordWrap(true);
        lay->addWidget(n);
    }
    current_ = new QLabel();
    current_->setObjectName("small");
    lay->addWidget(current_);
    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    pick_ = new KeyPicker();
    pick_->setKeys(KeyPicker::hotkeyItems(), Keys::hotkeyPresetNames());
    pick_->setAccessibleName(T("Клавиша"));
    row->addWidget(pick_);
    capture_ = new KeyCaptureButton(false);
    capture_->setObjectName("captureOval");
    capture_->setIcon(AqIcons::ui("keyboard", Theme::t().muted, 16));
    row->addWidget(capture_);
    row->addStretch(1);
    lay->addLayout(row);
    hint_ = new QLabel();
    hint_->setObjectName("hint");
    hint_->setWordWrap(true);
    hint_->hide();
    lay->addWidget(hint_);

    auto* foot = new QHBoxLayout();
    foot->setSpacing(8);
    if (allowNone) {
        auto* none = new QPushButton(T("Без клавиши"));
        none->setObjectName("ghost");
        connect(none, &QPushButton::clicked, this, [this]() { spec_ = HotkeySpec{}; accept(); });
        foot->addWidget(none);
    }
    foot->addStretch(1);
    auto* cancel = new QPushButton(T("Отмена"));
    auto* ok = new QPushButton(T("Готово"));
    ok->setObjectName("primary");
    ok->setDefault(true);
    foot->addWidget(cancel);
    foot->addWidget(ok);
    lay->addLayout(foot);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, [this]() {
        if (!spec_.isValid()) { hint_->setText(T("Выбери клавишу из списка или нажми «Своё сочетание».")); hint_->show(); return; }
        accept();
    });
    connect(pick_, &KeyPicker::picked, this, [this](const QString& k) { HotkeySpec sp; sp.key = k; setSpec(sp); });
    connect(capture_, &KeyCaptureButton::captureStarted, this, [this]() {
        if (suspend_) suspend_(true);
        hint_->setText(T("Нажми свою клавишу, сочетание или две клавиши — зажми Tab и нажми F (Esc — отмена)…"));
        hint_->show();
    });
    connect(capture_, &KeyCaptureButton::captureFinished, this, [this](bool okk, const HotkeySpec& sp) {
        if (suspend_) suspend_(false);
        hint_->hide();
        if (okk) setSpec(sp);
    });
    connect(capture_, &KeyCaptureButton::hint, this, [this](const QString& h) { hint_->setText(h); hint_->show(); });
    setSpec(spec);
}

void HotkeyDialog::setSpec(const HotkeySpec& sp) {
    spec_ = sp;
    if (sp.isValid()) {
        pick_->setCurrent(sp.isChord() ? QString() : sp.key, sp.display());   // в списке сочетаний нет
        current_->setText(T("Сейчас: %1").arg(sp.display()));
    } else {
        pick_->setCurrent(QString(), T("выбрать…"));
        current_->setText(T("Сейчас: без клавиши"));
    }
}

bool HotkeyDialog::ask(QWidget* parent, const QString& title, const QString& note, HotkeySpec& spec,
                       bool allowNone, const std::function<void(bool)>& suspend) {
    HotkeyDialog dlg(title, note, spec, allowNone, suspend, parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    spec = dlg.spec_;
    return true;
}
