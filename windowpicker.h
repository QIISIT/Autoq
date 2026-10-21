#pragma once
// Выбор процесса или заголовка из открытых окон (вместо того чтобы вписывать
// имя руками): всплывающий список с поиском, как у выбора клавиши.
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
    enum Mode { Process, Title };
    WindowPickerPopup(Mode mode, const QStringList& already, QWidget* parent = nullptr);
    void popupAt(QWidget* anchor);
    // Открыть список у кнопки anchor; выбранное значение — в onPicked
    static void open(QWidget* anchor, Mode mode, const QStringList& already,
                     std::function<void(const QString&)> onPicked);
signals:
    void picked(const QString& value);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    void rebuild(const QString& filter);
    void choose(QListWidgetItem* it);
    struct Entry { QString value, sub; bool already; };
    QList<Entry> entries_;
    Mode mode_;
    QLineEdit* search_;
    QListWidget* list_;
};
