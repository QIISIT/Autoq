#pragma once
// Мини-окно поверх игры: всё самое нужное без главного окна —
// игра, клавиши, режим, скорость, профиль (сохранить/сбросить), выбор окна игры.
// Клавиша старт/стоп видна на «Старт», меняется в большом окне (карточка «Запуск»).
#include <QWidget>
#include <QPixmap>
#include <QList>
#include "games.h"
#include "keys.h"

class QLabel;
class QToolButton;
class QComboBox;
class QSpinBox;
class QPushButton;
class QFrame;
class StartButton;
class StatusDot;
class Segmented;
class CaptureCountdown;
class KeyPicker;

class MiniWindow : public QWidget {
    Q_OBJECT
public:
    struct GameItem { QString id; QString name; QPixmap tile; };

    explicit MiniWindow(QWidget* parent = nullptr);
    void setGames(const QList<GameItem>& games, const QString& currentId);
    void setState(const GameState& st, GameMode mode, int minInterval);
    void setProfiles(const QStringList& names, const QString& active, bool dirty);
    void setRunning(bool running, const QString& hotkey, bool pending = false, bool unused = false);
    void setCount(const QString& count, const QString& caption);
    void setWindowStatus(int state, const QString& text, const QString& process); // 0 нет, 1 ок, 2 предупреждение, 3 ждём
    void setCaptureSeconds(int sec);
    void startCapture();          // обратный отсчёт «переключись в игру» (запуск без выбранного окна)
    void cancelCapture();
    bool capturing() const;
    void refreshTheme();
    void setOverlayOn(bool on);
    void applyOnTop();            // «поверх всех окон» — из настроек
    void toast(const QString& text);
    void setCompact(bool on);     // свёрнуто: только статус, счётчик и Старт

signals:
    void toggleRequested();
    void expandRequested();
    void gameChosen(const QString& id);
    void actionChosen(int slot, bool use, bool mouse, int button, const QString& key);
    void modeChosen(int mode);
    void intervalChosen(int ms);
    void profileChosen(const QString& name);          // "" — без профиля
    void profileSaveRequested();
    void profileResetRequested(QWidget* anchor);
    void captureStarted();
    void captureFinished();
    void captureCancelled();
    void settingsRequested();
    void overlayMenuRequested(QWidget* anchor);
    void overlaySettingsRequested();

protected:
    void showEvent(QShowEvent* e) override;

private:
    void emitAction(int slot, const QString& name);

    QFrame* pill_;
    QLabel* pillText_;
    StatusDot* dot_;
    QToolButton* overlayBtn_;
    QToolButton* expandBtn_;
    QToolButton* settingsBtn_;
    QToolButton* collapseBtn_;
    QLabel* moreActs_;
    QList<QWidget*> full_;        // скрывается в свёрнутом виде
    bool compact_ = false;
    QComboBox* gameBox_;
    QLabel* counter_;
    QLabel* caption_;
    KeyPicker* act1_;
    QLabel* sep_;
    KeyPicker* act2_;
    QSpinBox* interval_;
    Segmented* modeSeg_;
    Segmented* singleSeg_;
    QComboBox* profileBox_;
    QToolButton* saveProfileBtn_;
    QToolButton* resetProfileBtn_;
    QLabel* winIcon_;
    QLabel* winText_;
    QLabel* winProc_;
    QPushButton* captureBtn_;
    CaptureCountdown* countdown_;
    StartButton* startBtn_;
    QLabel* footer_;
    int winState_ = 0;
    int captureSec_ = 3;
    bool profileDirty_ = false;
    bool running_ = false;
    bool pending_ = false;
    bool unused_ = false;
    QString hotkeyDisplay_;
    GameMode mode_ = GameMode::Sequence;
};
