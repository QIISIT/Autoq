#pragma once
// Выбор клавиши из всей клавиатуры с поиском: кнопка-«клавиша», по нажатию —
// всплывающий список (частые сверху, дальше по группам), строка поиска
// понимает «F5», «num», «пробел», «стрелка», одну букву и т.п.
#include <QPushButton>
#include <QFrame>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

class KeyPickerPopup : public QFrame {
    Q_OBJECT
public:
    KeyPickerPopup(const QStringList& keys, const QStringList& common, const QString& current,
                   QWidget* parent = nullptr);
    void popupAt(QWidget* anchor);
signals:
    void picked(const QString& keyName);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    void rebuild(const QString& filter);
    void choose(QListWidgetItem* it);
    QStringList keys_, common_;
    QString current_;
    QLineEdit* search_;
    QListWidget* list_;
};

class KeyPicker : public QPushButton {
    Q_OBJECT
public:
    explicit KeyPicker(QWidget* parent = nullptr);
    void setKeys(const QStringList& names, const QStringList& common = {});
    void setCurrent(const QString& keyName, const QString& display);
    void setCurrentItem(const QString& name);            // подпись — сама
    QString currentKey() const { return current_; }
    // Для действий: «mouse:1/3/2» — кнопки мыши, «none» — нет действия
    static QString labelFor(const QString& name);
    static QStringList actionItems(bool allowNone);      // мышь + все клавиши действий
    static QStringList actionCommon();                    // частые: ЛКМ, ПКМ, F, E, Пробел
    static QStringList hotkeyItems();                     // клавиши + СКМ и боковые кнопки мыши
signals:
    void picked(const QString& keyName);
private:
    void openPopup();
    QStringList keys_, common_;
    QString current_;
};
