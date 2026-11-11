// Редактор действий (ряд карточек): карточка группы — «×N» и «повтор через», предел ряда.
// Qt Widgets, без экрана — QT_QPA_PLATFORM=offscreen
#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <cstdio>
#include "ui/actionseditor.h"
#include "ui/style/theme.h"
#include "core/appsettings.h"
#include "core/games.h"
#include "core/i18n.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static ActionStep key(const QString& k, int hold, int after) {
    ActionStep a = ActionStep::fromItem(k); a.holdMs = hold; a.afterMs = after; return a;
}
static ActionStep call(const QString& id, int rep, int after) {
    ActionStep c; c.group = id; c.mouse = false; c.key.clear(); c.repeat = rep; c.afterMs = after; return c;
}
// ряд пересобрали — старые карточки удаляются отложенно (deleteLater): убрать их сразу,
// чтобы поиск видел только новые
static void settle() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
// поля карточек групп по порядку карточек в ряду
static QList<QSpinBox*> spins(ActionsEditor& ed, const char* name) {
    settle();
    return ed.findChildren<QSpinBox*>(QString::fromLatin1(name));
}
// «добавить» и «Группа» в конце ряда
static QList<QPushButton*> addButtons(ActionsEditor& ed) {
    settle();
    return ed.findChildren<QPushButton*>("addSecond");
}
static QPushButton* addButton(ActionsEditor& ed) { return addButtons(ed).value(0); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppSettings::setScope("AutoQTest", QString("ed_%1").arg(QCoreApplication::applicationPid()));
    { auto s = AppSettings::open(); s->clear(); s->sync(); }
    Theme::apply(app, true);

    ActionGroup jey; jey.id = "aaaaaaaa"; jey.name = "JEY";
    jey.items = {key("j", 5, 360), key("e", 5, 360), key("y", 5, 0)};
    jey.betweenMs = 500;
    ActionGroup old; old.id = "bbbbbbbb"; old.name = "Старая";      // группа из 1.17.0: своей паузы нет
    old.items = {key("c", 0, 10)};
    old.betweenMs = -1;

    // ряд: J, [JEY ×3], [JEY ×1], [Старая ×2, пауза 250], C
    const QList<ActionStep> row{key("j", 5, 360), call(jey.id, 3, 1000), call(jey.id, 1, 0), call(old.id, 2, 250), key("c", 5, 0)};
    {
        ActionsEditor ed;
        int changed = 0;
        QObject::connect(&ed, &ActionsEditor::changed, &ed, [&changed]() { ++changed; });
        ed.setGroups({jey, old});
        ed.setState(row, GameMode::Sequence, 0, 1000, 5);
        QList<QSpinBox*> every = spins(ed, "groupEvery"), rep = spins(ed, "groupRepeat");
        CHECK(every.size() == 3 && rep.size() == 3);
        if (every.size() == 3 && rep.size() == 3) {
            // на карточке — пауза группы; у группы из 1.17.0 — пауза после вызова (так она и жмётся)
            CHECK(every[0]->value() == 500 && every[1]->value() == 500 && every[2]->value() == 250);
            CHECK(rep[0]->value() == 3 && rep[1]->value() == 1 && rep[2]->value() == 2);
            // ×1 — повторов нет: поле серое, но подсказка говорит почему
            CHECK(every[0]->isEnabled() && !every[1]->isEnabled() && every[2]->isEnabled());
            CHECK(!every[0]->toolTip().isEmpty() && !every[1]->toolTip().isEmpty() && every[0]->toolTip() != every[1]->toolTip());
            CHECK(every[0]->suffix() == T(" мс"));
            CHECK(changed == 0);                                     // загрузка ряда — не правка

            // поменяли на карточке — поменялась пауза группы: на второй её карточке то же число
            every[0]->setValue(700);
            CHECK(changed == 1);
            const ActionGroup* g = Groups::find(ed.groups(), jey.id);
            CHECK(g && g->betweenMs == 700);
            CHECK(every[1]->value() == 700 && every[2]->value() == 250);
            CHECK(Groups::find(ed.groups(), old.id)->betweenMs == -1);      // чужую группу не тронули
            CHECK(ed.actions() == row);                                      // ряд тот же: пауза — у группы
            // кликер жмёт с новой паузой: между повторами 700, после последнего — пауза после вызова
            const QList<ActionStep> run = Groups::expand(ed.actions(), ed.groups(), false);
            CHECK(run.size() == 1 + 3 * 3 + 3 + 2 + 1);
            CHECK(run[3].afterMs == 700 && run[6].afterMs == 700 && run[9].afterMs == 1000);

            // ×1 → ×2: пауза между повторами нужна — поле включилось; обратно — выключилось
            rep[1]->setValue(2);
            CHECK(changed == 2 && every[1]->isEnabled() && ed.actions()[2].repeat == 2);
            rep[1]->setValue(1);
            CHECK(changed == 3 && !every[1]->isEnabled());

            // группа из 1.17.0: число с карточки становится её собственной паузой
            every[2]->setValue(40);
            CHECK(changed == 4 && Groups::find(ed.groups(), old.id)->betweenMs == 40);
            CHECK(ed.actions()[3].afterMs == 250);                           // пауза после вызова — прежняя
            const QList<ActionStep> run2 = Groups::expand(ed.actions(), ed.groups(), false);
            CHECK(run2[13].key == "c" && run2[13].afterMs == 40 && run2[14].afterMs == 250);

            // то же значение ещё раз — не правка
            every[2]->setValue(40);
            CHECK(changed == 4);
        }
        // тот же ряд с другими числами — без пересборки: поля те же, значения новые
        ActionGroup jey2 = jey; jey2.betweenMs = 90;
        ed.setGroups({jey2, old});
        QList<ActionStep> row2 = row;
        row2[2].repeat = 5;
        ed.setState(row2, GameMode::Sequence, 0, 1000, 5);
        const QList<QSpinBox*> every2 = spins(ed, "groupEvery");
        CHECK(every2 == every && every2.size() == 3 && every2[0]->value() == 90 && every2[1]->value() == 90 && every2[1]->isEnabled());
        CHECK(changed == 4);
    }
    {   // «По кадрам»: в поле — кадры (числа редактору дают уже в кадрах, см. FrameUnits)
        ActionsEditor ed;
        ed.setFrames(true);
        ed.setGroups({jey, old});
        ed.setState(row, GameMode::Sequence, 0, 10, kFrameUnit);
        const QList<QSpinBox*> every = spins(ed, "groupEvery");
        CHECK(every.size() == 3 && every[0]->suffix() == T(" кадр.") && every[0]->value() == 500);
    }
    {   // «По шагам»: у вызова группы — те же поля
        ActionsEditor ed;
        ed.setGroups({jey, old});
        ActionStep d = key("j", 0, 50), u = key("j", 0, 20);
        d.press = ActionStep::Down;
        u.press = ActionStep::Up;
        ed.setState({d, u, call(jey.id, 2, 100)}, GameMode::Steps, 0, 1000, 5);
        const QList<QSpinBox*> every = spins(ed, "groupEvery");
        CHECK(every.size() == 1 && every[0]->isEnabled() && every[0]->value() == 500);
        // группа, которой нет: карточка ничего не жмёт, поле паузы выключено
        ed.setState({d, u, call("cccccccc", 2, 100)}, GameMode::Steps, 0, 1000, 5);
        const QList<QSpinBox*> none = spins(ed, "groupEvery");
        CHECK(none.size() == 1 && !none[0]->isEnabled());
        if (none.size() == 1) none[0]->setValue(5);
        CHECK(ed.groups().size() == 2 && Groups::find(ed.groups(), jey.id)->betweenMs == 500);
    }
    {   // «Вместе» и «Зажать» — без повторов; окно группы — без групп внутри
        ActionsEditor ed;
        ed.setGroups({jey});
        ed.setState({key("j", 0, 0), key("c", 0, 0)}, GameMode::Combo, 0, 100, 5);
        CHECK(spins(ed, "groupEvery").isEmpty() && spins(ed, "groupRepeat").isEmpty());
        ed.setState({key("j", 0, 0)}, GameMode::Hold, 0, 100, 5);
        CHECK(spins(ed, "groupEvery").isEmpty());
    }

    // предел ряда: 64 действия — уже не предел (до 1.18.4), kMaxActions — предел
    {
        ActionsEditor ed;
        QList<ActionStep> many;
        for (int i = 0; i < kMaxActions + 40; ++i) many << key(i % 2 ? "c" : "j", 5, 360);
        ed.setState(many.mid(0, 70), GameMode::Sequence, 0, 1000, 5);
        CHECK(ed.actions().size() == 70);
        QPushButton* add = addButton(ed);
        CHECK(add && add->isEnabled());
        CHECK(addButtons(ed).size() == 2);                                   // «Ещё» и «Группа»
        ed.setState(many, GameMode::Sequence, 0, 1000, 5);
        CHECK(ed.actions().size() == kMaxActions);
        // ряд полон: «добавить» на месте, но не нажимается и говорит почему; «Группа» — нет
        add = addButton(ed);
        CHECK(add && !add->isEnabled() && add->toolTip().contains(QString::number(kMaxActions)));
        CHECK(addButtons(ed).size() == 1);
        // шагов — вдвое больше
        QList<ActionStep> steps;
        for (int i = 0; i < kMaxSteps + 40; ++i) {
            ActionStep s = key("j", 0, 1);
            s.press = i % 2 ? ActionStep::Up : ActionStep::Down;
            steps << s;
        }
        ed.setState(steps, GameMode::Steps, 0, 1000, 5);
        CHECK(ed.actions().size() == kMaxSteps);
        add = addButton(ed);
        CHECK(add && !add->isEnabled() && add->toolTip().contains(QString::number(kMaxSteps)));
    }

    AppSettings::dropScope("AutoQTest", QString("ed_%1").arg(QCoreApplication::applicationPid()));
    std::printf("\n%d проверок пройдено, %d провалено\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
