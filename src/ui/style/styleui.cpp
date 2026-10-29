#include "ui/style/styleui.h"
#include "core/cardlayout.h"
#include "ui/dialogs/colorpicker.h"
#include "core/i18n.h"
#include "ui/style/widgets.h"
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
QString docsDir() { return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation); }
}

bool StyleUi::saveCurrent(QWidget* parent, const QString& layout, QString* savedName) {
    QDialog dlg(parent);
    dlg.setWindowTitle(T("Сохранить стиль — AutoQ"));
    dlg.setMinimumWidth(440);
    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(20, 18, 20, 16);
    lay->setSpacing(10);
    lay->addWidget(mkLabel(T("Сохранить текущий вид как стиль"), "cardTitle"));
    auto* pv = new ThemePreview();
    pv->setAppearance(Theme::current());
    pv->setFixedSize(253, 157);                 // макет 316×196 ×0.8
    lay->addWidget(pv, 0, Qt::AlignHCenter);
    lay->addWidget(mkLabel(T("Название"), "fieldLabel"));
    auto* nameEdit = new QLineEdit(Styles::uniqueName(T("Мой стиль")));
    nameEdit->setPlaceholderText(T("Например: Синий неон"));
    nameEdit->setMaxLength(40);
    lay->addWidget(nameEdit);
    ToggleSwitch* withLayout = nullptr;
    if (!layout.isEmpty()) {
        auto* row = new QHBoxLayout();
        row->setSpacing(10);
        withLayout = new ToggleSwitch();
        // своя раскладка — по умолчанию сохраняем и её (стандартную — незачем)
        CardLayout::Layout l;
        int side = 0;
        withLayout->setChecked(CardLayout::importJson(layout, &l, &side) && (!CardLayout::isDefault(l) || side != 0));
        withLayout->setAccessibleName(T("Вместе с раскладкой карточек этой игры"));
        row->addWidget(withLayout);
        auto* wl = mkLabel(T("Вместе с раскладкой карточек этой игры"), "muted");
        wl->setBuddy(withLayout);
        row->addWidget(wl, 1);
        lay->addLayout(row);
    }
    auto* note = mkLabel(T("В стиле — стиль интерфейса, тема, цвета, значки, анимации, шрифты, скругление, рамки и вид "
                         "мелких элементов. Размер интерфейса не входит. Файл стиля можно отправить другу."), "small");
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* foot = new QHBoxLayout();
    foot->addStretch(1);
    auto* cancel = new QPushButton(T("Отмена"));
    auto* save = new QPushButton(T("Сохранить"));
    save->setObjectName("primary");
    save->setDefault(true);
    foot->addWidget(cancel);
    foot->addWidget(save);
    lay->addLayout(foot);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dlg, [&]() {
        const QString n = nameEdit->text().trimmed();
        QString why;
        if (!Styles::validName(n, &why)) {
            QMessageBox::warning(&dlg, T("Стиль"), why);
            return;
        }
        if (Styles::exists(n) &&
            !askYesNo(&dlg, T("Стиль"), T("Стиль «%1» уже есть. Заменить его текущим видом?").arg(n),
                      T("Заменить"), T("Отмена"), false))
            return;
        dlg.accept();
    });
    nameEdit->selectAll();
    nameEdit->setFocus();
    if (dlg.exec() != QDialog::Accepted) return false;
    Styles::Profile p;
    p.name = nameEdit->text().trimmed();
    p.look = Theme::current();
    if (withLayout && withLayout->isChecked()) p.layout = layout;
    QString err;
    if (!Styles::save(p, &err)) {
        QMessageBox::warning(parent, T("Стиль"), err);
        return false;
    }
    if (savedName) *savedName = p.name;
    return true;
}

bool StyleUi::importFile(QWidget* parent, QString* name) {
    const QString file = QFileDialog::getOpenFileName(parent, T("Стиль из файла"), docsDir(),
                                                      T("Стиль AutoQ (*.aqstyle);;Все файлы (*)"));
    if (file.isEmpty()) return false;
    QFile f(file);
    Styles::Profile p;
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024 || !Styles::fromJson(QString::fromUtf8(f.readAll()), &p)) {
        QMessageBox::warning(parent, T("Стиль"), T("Это не файл стиля AutoQ."));
        return false;
    }
    p.name = Styles::uniqueName(p.name);
    QString err;
    if (!Styles::save(p, &err)) {
        QMessageBox::warning(parent, T("Стиль"), err);
        return false;
    }
    if (name) *name = p.name;
    return true;
}

bool StyleUi::exportFile(QWidget* parent, const QString& name) {
    Styles::Profile p;
    if (!Styles::load(name, &p)) return false;
    QString base = name;
    for (QChar& c : base)
        if (QStringLiteral("<>:\"/\\|?*").contains(c)) c = '_';
    const QString file = QFileDialog::getSaveFileName(parent, T("Стиль в файл"), docsDir() + "/" + base + Styles::extension(),
                                                      T("Стиль AutoQ (*.aqstyle)"));
    if (file.isEmpty()) return false;
    QSaveFile f(file);
    const QByteArray data = Styles::toJson(p).toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        QMessageBox::warning(parent, T("Стиль"), T("Не получилось записать файл: %1").arg(f.errorString()));
        return false;
    }
    return true;
}

bool StyleUi::askRename(QWidget* parent, const QString& name) {
    bool ok = false;
    const QString n = askText(parent, T("Переименовать стиль"), T("Название стиля:"), name, &ok).trimmed();
    if (!ok || n.isEmpty() || n == name) return false;
    QString err;
    if (!Styles::rename(name, n, &err)) {
        QMessageBox::warning(parent, T("Стиль"), err);
        return false;
    }
    return true;
}

bool StyleUi::askRemove(QWidget* parent, const QString& name) {
    if (!askYesNo(parent, T("Удалить стиль"),
                  T("Удалить стиль «%1»? Его файл удалится, а оформление сейчас не изменится.").arg(name),
                  T("Удалить"), T("Отмена"), false))
        return false;
    return Styles::remove(name);
}

bool StyleUi::update(QWidget* parent, const QString& name, const QString& layout) {
    Styles::Profile p;
    if (!Styles::load(name, &p)) return false;
    if (!askYesNo(parent, T("Стиль"),
                  p.hasLayout() && !layout.isEmpty() ? T("Записать в «%1» текущий вид и раскладку карточек?").arg(name)
                                : T("Записать в «%1» текущий вид?").arg(name),
                  T("Записать"), T("Отмена"), false))
        return false;
    p.look = Theme::current();
    if (p.hasLayout() && !layout.isEmpty()) p.layout = layout;
    QString err;
    if (!Styles::save(p, &err)) {
        QMessageBox::warning(parent, T("Стиль"), err);
        return false;
    }
    return true;
}

int StyleUi::askApply(QWidget* parent, const Styles::Profile& p, bool* allGames) {
    if (allGames) *allGames = false;
    if (!p.hasLayout()) return 1;
    QMessageBox box(QMessageBox::Question, T("Стиль «%1»").arg(p.name),
                    T("В стиле «%1» есть раскладка карточек. Поставить и её?").arg(p.name), QMessageBox::NoButton, parent);
    QPushButton* with = box.addButton(T("С раскладкой"), QMessageBox::YesRole);
    QPushButton* only = box.addButton(T("Только оформление"), QMessageBox::NoRole);
    QPushButton* cancel = box.addButton(T("Отмена"), QMessageBox::RejectRole);
    auto* all = new QCheckBox(T("Раскладку — у всех игр"));
    box.setCheckBox(all);
    box.setDefaultButton(with);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == with) {
        if (allGames) *allGames = all->isChecked();
        return 2;
    }
    return box.clickedButton() == only ? 1 : 0;
}
