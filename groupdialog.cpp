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
    // группы общие: в других играх её карточки остаются обычными действиями
    QMessageBox box(QMessageBox::Question, T("Удалить группу"),
                    uses > 0 ? T("Удалить группу «%1» во всех играх?\n\nКарточек этой группы в ряду: %2. Её действия можно оставить обычными "
                                 "карточками или убрать из ряда вместе с группой. В других играх они останутся обычными карточками.").arg(g.name).arg(uses)
                             : T("Удалить группу «%1» во всех играх? Там, где она стоит в ряду, её действия останутся обычными карточками.").arg(g.name),
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

GroupDialog::Result GroupDialog::edit(QWidget* parent, ActionGroup& g, int uses, bool isNew, bool stepsRow) {
    QDialog dlg(parent);
    dlg.setWindowTitle(T("Группа — AutoQ"));
    dlg.setMinimumSize(560, 360);
    dlg.resize(760, 460);
    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(20, 18, 20, 16);
    lay->setSpacing(10);

    // в ряду «По шагам» группа из действий сразу разложена на «нажать» и «отпустить»
    const bool split = stepsRow && !g.steps;
    bool asSteps = g.steps || split;
    QList<ActionStep> items = split ? Steps::fromActions(g.items) : g.items;
    int between = qMax(0, g.betweenMs);

    auto* top = new QHBoxLayout();
    top->setSpacing(10);
    auto* title = mkLabel(QString(), "cardTitle");
    top->addWidget(title, 1);
    auto* view = new Segmented({T("Действия"), T("Шаги ↓ ↑")}, false);
    view->setToolTip(T("Действия — клик или «держать N мс».\n"
                       "Шаги — каждая клавиша отдельно: нажать ↓ и отпустить ↑, с паузами между ними."));
    view->button(0)->setAccessibleName(T("Группа из действий"));
    view->button(1)->setAccessibleName(T("Группа из шагов: нажать и отпустить"));
    top->addWidget(view);
    lay->addLayout(top);
    lay->addWidget(mkLabel(T("Название"), "fieldLabel"));
    auto* nameEdit = new QLineEdit(g.name);
    nameEdit->setMaxLength(40);
    lay->addWidget(nameEdit);
    auto* listLabel = mkLabel(QString(), "fieldLabel");
    lay->addWidget(listLabel);
    // ряд длинный — прокручивается, окно не растёт за край экрана
    auto* scroll = new QScrollArea();
    scroll->setObjectName("mainScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* ed = new ActionsEditor();
    ed->setGroupMode(true);
    if (auto* row = qobject_cast<ActionsEditor*>(parent)) ed->setFrames(row->frames());   // «По кадрам»
    scroll->setWidget(ed);
    lay->addWidget(scroll, 1);
    auto* splitNote = mkLabel(T("Группа разложена на шаги: каждая клавиша — отдельно «нажать ↓» и «отпустить ↑», "
                                "их можно менять и переставлять. В рядах «По очереди» шаги снова соберутся в действия."), "small");
    splitNote->setWordWrap(true);
    splitNote->setVisible(split);
    lay->addWidget(splitNote);
    auto* pauses = mkLabel(T("«Повтор через» — пауза между повторами (×N на карточке группы). Пауза после группы в ряду "
                             "ждёт один раз, после последнего повтора, и скорость группы не меняет."), "small");
    pauses->setWordWrap(true);
    lay->addWidget(pauses);
    auto* note = mkLabel(isNew
        ? T("Группа встанет в ряд. Группы общие для всех игр: её можно поставить и в другой игре — кнопкой «Группа».")
        : uses > 1
        ? T("Группа стоит в ряду %1 раза — правка меняет все места сразу, и в других играх и профилях тоже.").arg(uses)
        : T("Правка меняет группу везде, где она стоит, — и в других играх и профилях."), "small");
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* foot = new QHBoxLayout();
    auto* del = new QPushButton(T("Удалить группу"));
    del->setObjectName("ghost");
    del->setVisible(!isNew);
    auto* cancel = new QPushButton(T("Отмена"));
    auto* ok = new QPushButton(isNew ? T("Создать") : T("Готово"));
    ok->setObjectName("primary");
    ok->setDefault(true);
    foot->addWidget(del);
    foot->addStretch(1);
    foot->addWidget(cancel);
    foot->addWidget(ok);
    lay->addLayout(foot);

    auto refresh = [&]() {
        title->setText(isNew ? (asSteps ? T("Новая группа шагов") : T("Новая группа действий"))
                             : (asSteps ? T("Группа шагов") : T("Группа действий")));
        listLabel->setText(asSteps ? T("Шаги — по очереди, с паузами") : T("Действия — по очереди, с паузами"));
        view->setCurrent(asSteps ? 1 : 0);
        ed->setState(items, asSteps ? GameMode::Steps : GameMode::Sequence, 0, between, 1);
    };
    refresh();
    QObject::connect(view, &Segmented::changed, &dlg, [&](int i) {
        const bool want = i == 1;
        if (want == asSteps) return;
        items = ed->actions();
        between = ed->interval();
        if (want) {
            items = Steps::fromActions(items);
        } else {
            bool exact = true;
            const QList<ActionStep> back = Steps::toActions(items, &exact);
            if (!exact && !askYesNo(&dlg, T("Группа"),
                    T("Не все шаги собираются в действия: «нажать» без «отпустить» сразу после него станет кликом, "
                      "лишнее «отпустить» пропадёт. Всё равно сделать действиями?"),
                    T("Сделать действиями"), T("Отмена"), false)) {
                view->setCurrent(1);
                return;
            }
            items = back;
        }
        if (items.isEmpty()) items = Groups::blank({}, want).items;
        asSteps = want;
        splitNote->hide();
        refresh();
    });

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
        g.steps = asSteps;
        g.items = ed->actions();
        if (!g.items.isEmpty()) g.items.last().afterMs = g.items.last().afterFr = 0;   // пауза после группы — у вызова
        g.betweenMs = ed->interval();                            // между повторами ×N
    }
    return res;
}
