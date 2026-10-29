#pragma once
// Выбор процесса или заголовка из открытых окон (вместо того чтобы вписывать
// имя руками): всплывающий список с поиском, как у выбора клавиши. Window — само окно
// («Выбрать процесс…» в карточке «Окно игры»): строка «процесс — заголовок», как в списке
// окон OBS, у каждого окна своя.
#include <QFrame>
#include <QList>
#include <functional>
#include "winmatch.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;

class WindowPickerPopup : public QFrame {
    Q_OBJECT
public:
    enum Mode { Process, Title, Window };
    WindowPickerPopup(Mode mode, const QStringList& already, QWidget* parent = nullptr,
                      unsigned long long current = 0);
    void popupAt(QWidget* anchor);
    // Открыть список у кнопки anchor; выбранное значение — в onPicked
    static void open(QWidget* anchor, Mode mode, const QStringList& already,
                     const std::function<void(const QString&)>& onPicked);
    // Список окон; current — уже выбранное окно (отмечено), выбранное — в onPicked
    static void openWindows(QWidget* anchor, unsigned long long current,
                            const std::function<void(const WindowInfo&)>& onPicked);
signals:
    void picked(const QString& value);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    void rebuild(const QString& filter);
    void choose(QListWidgetItem* it);
    struct Entry { QString value, sub; bool already; WindowInfo win; };
    QList<Entry> entries_;
    Mode mode_;
    QLineEdit* search_;
    QListWidget* list_;
    std::function<void(const WindowInfo&)> onWindow_;
};
