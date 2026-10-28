#include "groupdialog.h"
#include "actionseditor.h"
#include "i18n.h"
#include "widgets.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

GroupDialog::Result GroupDialog::askDelete(QWidget* parent, const ActionGroup& g, int uses) {
    QMessageBox box(QMessageBox::Question, T("Удалить группу"),
                    uses > 0 ? T("Удалить группу «%1»?\n\nКарточек этой группы в ряду: %2. Её действия можно оставить обычными "
                                 "карточками или убрать из ряда вместе с группой.").arg(g.name).arg(uses)
                             : T("Удалить группу «%1»?").arg(g.name),
                    QMessageBox::NoButton, parent);
    QPushButton* keep = uses > 0 ? box.addButton(T("Оставить действия"), QMessageBox::AcceptRole) : nullptr;
    QPushButton* all = box.addButton(uses > 0 ? T("Удалить с карточками") : T("Удалить"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(T("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == keep) return Deleted;
    if (box.clickedButton() == all) return DeletedWithCards;
    return Cancelled;
}

GroupDialog::Result GroupDialog::edit(QWidget* parent, ActionGroup& g, int uses) {
    QDialog dlg(parent);
    dlg.setWindowTitle(T("Группа — AutoQ"));
    dlg.setMinimumSize(560, 360);
    dlg.resize(760, 420);
    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(20, 18, 20, 16);
    lay->setSpacing(10);
    lay->addWidget(mkLabel(g.steps ? T("Группа шагов") : T("Группа действий"), "cardTitle"));
    lay->addWidget(mkLabel(T("Название"), "fieldLabel"));
    auto* nameEdit = new QLineEdit(g.name);
    nameEdit->setMaxLength(40);
    lay->addWidget(nameEdit);
    lay->addWidget(mkLabel(g.steps ? T("Шаги — по очереди, с паузами") : T("Действия — по очереди, с паузами"), "fieldLabel"));
    // ряд длинный — прокручивается, окно не растёт за край экрана
    auto* scroll = new QScrollArea();
    scroll->setObjectName("mainScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* ed = new ActionsEditor();
    ed->setGroupMode(true);
    ed->setState(g.items, g.steps ? GameMode::Steps : GameMode::Sequence, 0, qMax(0, g.betweenMs), 1);
    scroll->setWidget(ed);
    lay->addWidget(scroll, 1);
    auto* pauses = mkLabel(T("«Повтор через» — пауза между повторами (×N на карточке группы). Пауза после группы в ряду "
                             "ждёт один раз, после последнего повтора, и скорость группы не меняет."), "small");
    pauses->setWordWrap(true);
    lay->addWidget(pauses);
    auto* note = mkLabel(uses > 1
        ? T("Группа стоит в ряду %1 раза — правка меняет все места сразу, и в других профилях этой игры тоже.").arg(uses)
        : T("Правка меняет группу везде, где она стоит, — и в других профилях этой игры."), "small");
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* foot = new QHBoxLayout();
    auto* del = new QPushButton(T("Удалить группу"));
    del->setObjectName("ghost");
    auto* cancel = new QPushButton(T("Отмена"));
    auto* ok = new QPushButton(T("Готово"));
    ok->setObjectName("primary");
    ok->setDefault(true);
    foot->addWidget(del);
    foot->addStretch(1);
    foot->addWidget(cancel);
    foot->addWidget(ok);
    lay->addLayout(foot);

    Result res = Cancelled;
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(del, &QPushButton::clicked, &dlg, [&]() {
        const Result r = askDelete(&dlg, g, uses);
        if (r == Cancelled) return;
        res = r;
        dlg.accept();
    });
    QObject::connect(ok, &QPushButton::clicked, &dlg, [&]() {
        QString why;
        if (!Groups::validName(nameEdit->text(), &why)) {
            QMessageBox::warning(&dlg, T("Группа"), why);
            return;
        }
        res = Saved;
        dlg.accept();
    });
    nameEdit->setFocus();
    nameEdit->selectAll();
    if (dlg.exec() != QDialog::Accepted || res == Cancelled) return Cancelled;
    if (res == Saved) {
        g.name = nameEdit->text().trimmed();
        g.items = ed->actions();
        if (!g.items.isEmpty()) g.items.last().afterMs = 0;     // пауза после группы — у вызова
        g.betweenMs = ed->interval();                            // между повторами ×N
    }
    return res;
}
