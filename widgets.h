#pragma once
// Небольшие виджеты оформления AutoQ
#include <QAbstractButton>
#include "i18n.h"
#include <QPushButton>
#include <QFrame>
#include <QLayout>
#include <QStyledItemDelegate>
#include <QButtonGroup>
#include <QList>
#include <QComboBox>
#include "sounds.h"

class QLabel;
class QLineEdit;
class QToolButton;

// Вопрос «да / нет» с русскими кнопками (без переводов Qt там было бы Yes/No)
bool askYesNo(QWidget* parent, const QString& title, const QString& text,
              const QString& yes = T("Да"), const QString& no = T("Нет"),
              bool defaultYes = true);

// Переключатель (тумблер) вместо галочки
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(40, 24); }
protected:
    void paintEvent(QPaintEvent*) override;
};

// Сегментный выбор: [3][5][7][10] сек, [Тёмная][Светлая]
class Segmented : public QFrame {
    Q_OBJECT
public:
    Segmented(const QStringList& labels, bool mono, const QString& suffix = QString(),
              QWidget* parent = nullptr);
    void setCurrent(int index);
    int current() const;
    QPushButton* button(int i) const { return buttons_.value(i); }
signals:
    void changed(int index);
private:
    QButtonGroup group_;
    QList<QPushButton*> buttons_;
};

// Время на переключение в игру: быстрые 3/5/7/10 и поле «своё» (1–120 сек)
class TimeChoice : public QWidget {
    Q_OBJECT
public:
    explicit TimeChoice(QWidget* parent = nullptr);
    void setSeconds(int s);
signals:
    void changed(int seconds);
private:
    Segmented* seg_;
    class QSpinBox* spin_;
};

// Раскладка "по строкам с переносом" — для фишек с именами процессов
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int hSpacing = 5, int vSpacing = 5);
    ~FlowLayout() override;
    void addItem(QLayoutItem* item) override;
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int i) const override { return items_.value(i); }
    QLayoutItem* takeAt(int i) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return doLayout(QRect(0, 0, w, 0), true); }
    void setGeometry(const QRect& r) override { QLayout::setGeometry(r); if (!frozen_) doLayout(r, false); }
    // пока виджеты плавно едут на новые места — раскладка их не двигает
    void setFrozen(bool on) { frozen_ = on; if (!on) doLayout(geometry(), false); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override;
    void clear();
private:
    int doLayout(const QRect& r, bool testOnly) const;
    QList<QLayoutItem*> items_;
    int h_, v_;
    bool frozen_ = false;
};

// Большая кнопка Старт/Стоп с плашкой клавиши
class StartButton : public QPushButton {
    Q_OBJECT
public:
    explicit StartButton(QWidget* parent = nullptr);
    // pending — ждём, пока выберут окно игры (обратный отсчёт), потом старт
    // unused — категория не используется: кнопка её включает
    void setState(bool running, const QString& hotkey, bool pending = false, bool unused = false);
    void refreshIcon();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
private:
    QLabel* icon_;
    QLabel* text_;
    QLabel* chip_;
    bool running_ = false;
    bool pending_ = false;
    bool unused_ = false;
};

// Надпись, которая сокращается многоточием, если не влезает
class ElideLabel : public QWidget {
public:
    explicit ElideLabel(QWidget* parent = nullptr);
    void setText(const QString& t) { text_ = t; setToolTip(t); updateGeometry(); update(); }
    QString text() const { return text_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString text_;
};

// Красная стрелка "→" между действиями
class ArrowLine : public QWidget {
public:
    explicit ArrowLine(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(92, 14); }
protected:
    void paintEvent(QPaintEvent*) override;
};

// Кружок с плюсом — "одновременно"
class PlusBadge : public QWidget {
public:
    explicit PlusBadge(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(34, 34); }
protected:
    void paintEvent(QPaintEvent*) override;
};

// Маленький «?» рядом с настройкой: щелчок (или Enter) — окошко с объяснением
// «почему так». Закрывается щелчком мимо или Esc.
class InfoBadge : public QAbstractButton {
public:
    InfoBadge(const QString& title, const QString& text, QWidget* parent = nullptr);
    void setInfo(const QString& title, const QString& text) { title_ = title; text_ = text; }
    QSize sizeHint() const override { return QSize(18, 18); }
    void showInfo();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString title_, text_;
};

// Точка статуса: кольцо (стоит) или красная мигающая (работает)
class StatusDot : public QWidget {
public:
    explicit StatusDot(QWidget* parent = nullptr);
    void setActive(bool on);
protected:
    void paintEvent(QPaintEvent*) override;
    void timerEvent(QTimerEvent*) override;
private:
    bool active_ = false;
    bool phase_ = false;
    int timer_ = 0;
};

// Обратный отсчёт на кнопке "переключись в игру" с полоской прогресса.
// Повторное нажатие кнопки — отмена (это делает владелец через toggle()).
class CaptureCountdown : public QObject {
    Q_OBJECT
public:
    CaptureCountdown(QPushButton* button, QObject* parent = nullptr);
    void setIdleText(const QString& text);
    void start(int seconds);
    void cancel();
    void toggle(int seconds) { if (active()) cancel(); else start(seconds); }
    bool active() const { return timer_ != 0; }
signals:
    void finished();
    void cancelled();
protected:
    void timerEvent(QTimerEvent*) override;
private:
    void restore();
    QPushButton* btn_;
    QString idle_;
    int timer_ = 0;
    qint64 endMs_ = 0;
    int totalMs_ = 0;
};

// Строка списка игр: значок (с клавишей запуска), название, режим / «работает»,
// справа кружок «используется»: закрашен — категорию можно запустить, пустой — нельзя никак
class GameListDelegate : public QStyledItemDelegate {
public:
    enum Roles { IdRole = Qt::UserRole, SubRole, TileRole, TileActiveRole, ArmedRole, RunningRole, KeyRole };
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(compact ? 56 : 200, 54); }
    // где кружок «используется»; compact — узкая панель: только значок, кружок — точкой в углу
    static QRect circleRect(const QRect& itemRect, bool compact = false);
    bool compact = false;
};

// Карточки в ряд, которые сами перестраиваются в 2 или 1 колонку, когда окно узкое
class CardFlow : public QWidget {
public:
    explicit CardFlow(QWidget* parent = nullptr);
    void setCards(const QList<QWidget*>& cards);
    int columns() const { return cols_; }
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
protected:
    void resizeEvent(QResizeEvent* e) override;
private:
    int columnsFor(int width) const;
    void relayout(int cols);
    class QGridLayout* grid_;
    QList<QWidget*> cards_;
    int cols_ = 0;
};

// Фишки правил окна (процессы / заголовки): × — убрать, «+» — дописать прямо тут
class ChipEditor : public QWidget {
    Q_OBJECT
public:
    ChipEditor(bool mono, const QString& addText, const QString& addTip, QWidget* parent = nullptr);
    void setItems(const QStringList& items);
    QStringList items() const { return items_; }
    void setSuggestions(const QStringList& s) { suggestions_ = s; }   // автодополнение
    // кнопка «выбрать из открытых окон» рядом с «+»
    void setPickButton(const QString& text, const QString& tip) { pickText_ = text; pickTip_ = tip; rebuild(); }
    bool editing() const { return edit_ != nullptr; }
signals:
    void itemsChanged(const QStringList& items);
    void pickRequested(QWidget* anchor);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    void rebuild();
    void startAdd();
    void finishAdd(bool commit);
    bool mono_;
    QString addText_, addTip_, pickText_, pickTip_;
    QStringList items_, suggestions_;
    FlowLayout* flow_;
    QLineEdit* edit_ = nullptr;
};

// Кнопка-карточка с раскладкой внутри: размер — по содержимому (а не по min-height из стиля)
class CardButton : public QPushButton {
public:
    using QPushButton::QPushButton;
    QSize sizeHint() const override {
        return layout() ? layout()->sizeHint().expandedTo(minimumSize()) : QPushButton::sizeHint();
    }
    QSize minimumSizeHint() const override {
        return layout() ? layout()->minimumSize() : QPushButton::minimumSizeHint();
    }
};

// Карточка игры-шаблона (экран «Добавь первую игру»)
class TemplateCard : public QPushButton {
    Q_OBJECT
public:
    TemplateCard(const QPixmap& tile, const QString& name, const QString& sub, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(180, sizeHint().height()); }
};

// Колёсико мыши над ползунками / списками / числами в прокручиваемой странице
// листает страницу, а не меняет значение (если поле не в фокусе)
class WheelGuard : public QObject {
public:
    explicit WheelGuard(QObject* parent) : QObject(parent) {}
    static void install(QWidget* root);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
};

// Выбор звука вкл/выкл: набор AutoQ, «Мои звуки», «Без звука» и «Добавить свой
// звук…» (файл копируется в «Мои звуки»). inherit — первый пункт «Как в настройках».
// Выбрал — звук сразу играет, чтобы было слышно, что выбрано.
class SoundBox : public QComboBox {
    Q_OBJECT
public:
    SoundBox(Sounds::Kind kind, bool inherit, QWidget* parent = nullptr);
    void setSound(const QString& id);     // "" — как в настройках
    QString sound() const { return cur_; }
    void refill();
    void showPopup() override;            // перед показом — свежий список «Моих звуков»
signals:
    void soundChosen(const QString& id);
private:
    Sounds::Kind kind_;
    bool inherit_;
    QString cur_;
};
