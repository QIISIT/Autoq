#include "macperm.h"
#include "i18n.h"
#ifdef __APPLE__
#include "macsupport.h"
#include "hotkey.h"
#include "appinfo.h"
#include <QMessageBox>
#include <QPushButton>

void MacPermissions::checkAndExplain(QWidget* parent, GlobalHotkey* hotkey) {
    Q_UNUSED(hotkey);   // поток горячей клавиши сам подхватит разрешение через пару секунд
    if (AqMac::accessibilityTrusted(false)) return;

    QMessageBox box(parent);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(T("AutoQ — разрешение macOS"));
    box.setText(T("Разреши AutoQ «Универсальный доступ»"));
    box.setInformativeText(
        T("Без него macOS не даёт программам нажимать за тебя и ловить горячую клавишу.\n\n1. Нажми «Открыть настройки».\n2. Включи переключатель у AutoQ. Если его нет в списке — нажми «+» и выбери AutoQ в папке «Программы».\n3. Вернись в AutoQ: горячая клавиша заработает сама через пару секунд. Если клики не идут — перезапусти AutoQ.\n\nЕсли macOS спросит про «Мониторинг ввода» — тоже разреши, это для горячей клавиши."));
    QPushButton* open = box.addButton(T("Открыть настройки"), QMessageBox::AcceptRole);
    box.addButton(T("Позже"), QMessageBox::RejectRole);
    box.setDefaultButton(open);
    box.exec();
    if (box.clickedButton() == open) {
        AqMac::accessibilityTrusted(true);              // добавит AutoQ в список
        AqMac::openPrivacyPane("Privacy_Accessibility");
    }
}
#endif
