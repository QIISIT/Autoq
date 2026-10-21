#pragma once
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <memory>
#include <map>
#include "clicker.h"
#include "hotkey.h"
#include "games.h"
#include "winmatch.h"

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QToolButton;
class QLabel;
class QMenu;
class QAction;
class QFrame;
class QSlider;
class QTimer;
class QCloseEvent;
class QListWidget;
class QWidget;
class QStackedWidget;
class QGridLayout;
class KeyCaptureButton;
class KeyPicker;
class TimeChoice;
class ToggleSwitch;
class Segmented;
class ChipEditor;
class StartButton;
class ElideLabel;
class StatusDot;
class CaptureCountdown;
class MiniWindow;
class OverlayWindow;
class ActionsEditor;
class CardFlow;
class OnboardingPage;
struct OverlayData;

// Кликер одной игры: у каждой игры свой — игры можно запускать вместе
struct GameRunner {
    Clicker clicker;
    WindowInfo bound;          // выбранное окно игры (handle == 0 — не выбрано)
    GameDef def;               // с какими настройками запущен (для оверлея и проверок)
    GameState st;
    bool holdStarted = false;  // запущен удержанием клавиши (отпустил — стоп)
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void attachHotkey(GlobalHotkey* hk);

    bool restartRequested() const { return restart_; }
public slots:
    void restartApp();          // смена языка/размера: выйти и запуститься заново
    void toggleRunning();          // кнопка Старт, трей, мини-окно — для открытой игры
    void onHotkeyEvent(const QString& spec, bool down);   // глобальная клавиша (из потока хоткеев)
    void showMain();
    void showMini();
    void openSettings();
    void openOverlaySettings();
    void startOnboarding();

private slots:
    void onGameRowChanged(int row);
    void addGame();
    void editGame();
    void removeGame();
    void startWindowCheck();
    void checkWindowDone();
    void onCaptureStarted();
    void onCaptureFinished(bool accepted, const HotkeySpec& spec);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void quitApp();
    void refreshLive();

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void buildUi();
    QWidget* buildSidebar();
    QWidget* buildWelcome();
    QWidget* buildHeader();
    QFrame* buildActionsCard();
    QFrame* buildSpeedCard();
    QFrame* buildWindowCard();
    QFrame* buildProfileCard();
    QWidget* buildStatusBar();
    void buildTray();
    void connectSignals();
    void connectMini();
    void applyCardOrder();

    // --- игры ---
    void reloadGames(const QString& selectId);
    void rebuildWelcome();
    void addTemplate(const QString& id);
    void selectGame(const QString& id);       // из мини-окна или трея
    void switchToGame(const QString& id);
    bool hasGame() const { return !game_.id.isEmpty(); }
    void updateHeader();
    void updateGameItems();                   // кружки, клавиши, «работает» в списке игр
    void showGameMenu(const QPoint& pos);
    void setArmed(const QString& id, bool on, bool announce);
    void removeGameById(const QString& id);

    // --- действия ---
    void stateToUi();
    void applyModeLayout();
    void setGameMode(GameMode m, bool announce = true);
    void commit();                 // текущее состояние -> диск + кликер + подписи
    void pushConfig();
    void reloadPresets();
    void applyPreset(int index);
    void updatePresetLabel();
    void updateRate();
    void syncSlider();
    void syncSpeedBox();
    double perSecond(const GameDef& g, const GameState& st) const;

    // --- профили ---
    void updateProfileState();
    bool profileDirty() const;
    bool confirmLeaveProfile();    // несохранённые изменения: сохранить / не сохранять / отмена
    void switchProfile(const QString& name);   // "" — без профиля
    void loadProfile(const QString& name);
    void newProfile(int base = 0);             // 0 — текущие, 1 — стандартные, 2+ — пресет
    void saveProfile();
    void showResetMenu(QWidget* anchor);
    void deleteProfile();
    void renameProfile();
    void editProfileKey();
    void applyProfileFor(const QString& gameId, const QString& profile);   // клавишей профиля
    GameState standardState() const;
    GameMode standardMode() const;

    // --- окно игры ---
    void updateRules();
    void setRules(const QStringList& procs, const QStringList& titles);
    void bindWindow(const WindowInfo& w);
    void unbindWindow();
    bool bound() const;
    void beginCapture();           // обратный отсчёт: в мини-окне или в главном
    void rememberWindow(const WindowInfo& w);

    // --- запуск: у каждой игры свой кликер ---
    GameRunner& runner(const QString& id);
    GameRunner* findRunner(const QString& id) const;
    Clicker& clk() { return runner(game_.id).clicker; }
    WindowInfo& bnd() { return runner(game_.id).bound; }
    bool isRunning(const QString& id) const;
    QStringList runningIds() const;
    QString focusRunnerId() const;             // чьи цифры показывать в оверлее
    OverlayData overlayData();
    static ClickerConfig buildConfig(const GameDef& g, const GameState& st, const WindowInfo& w);
    void toggleFrom(bool fromHotkey);
    void startClicker();
    void stopGame(const QString& id);
    bool startBackground(const QString& id, bool fromHotkey);   // игра не открыта — без отсчёта
    void toggleGame(const QString& id, bool fromHotkey);
    void cancelPending();
    QString startProblem() const;
    QString startProblemFor(const GameDef& g, const GameState& st) const;

    // --- клавиши ---
    struct HkTarget { int kind = 0; QString game; QString profile; };   // 0 старт, 1 «активна», 2 профиль
    struct HkBinding { HotkeySpec spec; QList<HkTarget> targets; };
    void rebuildBindings();
    void setHotkey(const HotkeySpec& sp, bool save);   // клавиша старт/стоп открытой игры
    void updateHotkeyStatus();
    void suspendHotkeys(bool on);
    bool capturingAnyKey() const;

    void updateTrayState();
    void rebuildTrayGames();
    void rebuildTrayProfiles();
    void setStatusBox(const QString& state, const QString& text);
    void setCaptureSeconds(int sec);
    void syncMini();               // всё состояние -> мини-окно
    void updateMiniLive();         // счётчик и статус окна -> мини-окно
    void refreshIcons();           // после смены темы / цвета
    QString actionsLabel(const GameState& st, int from, int to) const;
    QWidget* dialogParent();
    void notify(const QString& text);
    void openSettingsPage(int page);
    void setOverlayMode(int mode);   // 0 — выкл, 1 — пока кликер работает, 2 — всегда
    void showOverlayMenu(QWidget* anchor);
    void fillOverlayMenu(QMenu* m);
    void syncOverlayUi();          // кнопки «оверлей вкл/выкл» в шапке, трее и мини-окне
    void appearanceChanged();      // тема/стиль сменились — перерисовать
    void finishOnboarding();
    void updateResponsive();

    std::map<QString, std::unique_ptr<GameRunner>> runners_;
    GlobalHotkey* hotkey_ = nullptr;
    QList<HkBinding> bindings_;
    std::unique_ptr<ActiveWindowProbe> probe_;
    GameDef game_;
    GameState cur_;
    HotkeySpec hotkeySpec_;        // клавиша старт/стоп открытой игры
    bool pendingStart_ = false;    // после выбора окна — сразу старт (открытая игра)
    bool loading_ = false;
    bool quitting_ = false;
    bool hintShown_ = false;
    qint64 resultUntil_ = 0;       // до этого момента в статусе окна держится результат выбора
    int miniResultState_ = 0;
    QString miniResultText_;
    bool miniMode_ = false;
    QStringList recentProcs_, recentTitles_;   // недавние окна — подсказки для «+ процесс»
    struct RateSample { qint64 t; unsigned long long n; };
    QList<RateSample> rateHist_;   // для «сейчас: N в секунду»

    QStackedWidget* rootStack_;
    OnboardingPage* onboarding_ = nullptr;

    // --- боковая панель ---
    QListWidget* gameList_;
    QPushButton* addGameBtn_;
    QLabel* brandLogo_;
    QLabel* gameCount_;
    QToolButton* settingsBtn_;
    QLabel* emptyHint_;

    // --- страницы ---
    QStackedWidget* pages_;
    QGridLayout* welcomeGrid_;
    QLabel* welcomeLogo_;
    QWidget* inner_ = nullptr;
    class QScrollArea* mainScroll_ = nullptr;
    class QBoxLayout* headerLay_ = nullptr;

    // --- шапка ---
    QLabel* headerIcon_;
    ElideLabel* nameLabel_;
    QToolButton* editGameBtn_;
    QToolButton* delGameBtn_;
    QToolButton* miniBtn_;
    QToolButton* overlayBtn_;
    QToolButton* presetBtn_;
    QMenu* presetMenu_;
    QFrame* counterBox_;
    QLabel* counterCaption_;
    QLabel* counterLabel_;
    QLabel* counterRate_;
    StartButton* startBtn_;

    // --- 01 действия ---
    QFrame* actionsCard_;
    QLabel* actionsTitle_;
    Segmented* modeSeg_;
    Segmented* singleSeg_;
    QLabel* actionsHint_;
    ActionsEditor* actionsEdit_;

    // --- 02 скорость ---
    QFrame* speedCard_;
    QLabel* rateLabel_;
    QLabel* rateCaption_;
    QSlider* slider_;
    QLabel* speedCaption_;
    QDoubleSpinBox* speedBox_;
    Segmented* unitSeg_;
    ToggleSwitch* randomizeSwitch_;
    QSpinBox* jitterBox_;
    QLabel* perMinuteLabel_;

    // --- 03 окно игры ---
    QFrame* windowCard_;
    ToggleSwitch* lockSwitch_;
    QLabel* statusBox_;
    ChipEditor* procEdit_;
    ChipEditor* titleEdit_;
    TimeChoice* timeSeg_;
    QPushButton* checkBtn_;
    CaptureCountdown* countdown_;

    // --- 04 профиль и запуск ---
    QFrame* profileCard_;
    CardFlow* cardFlow_;
    QComboBox* profileBox_;
    QToolButton* renameProfileBtn_;
    QToolButton* profileKeyBtn_;
    QLabel* profileState_;
    QPushButton* newProfileBtn_;
    QPushButton* saveProfileBtn_;
    QPushButton* resetProfileBtn_;
    QPushButton* delProfileBtn_;
    KeyPicker* hotkeyPick_;
    KeyCaptureButton* captureBtn_;
    Segmented* keyModeSeg_;
    ToggleSwitch* armedSwitch_;
    QLabel* hotkeyStatus_;
    QLabel* hotkeyInfo_;

    // --- строка состояния ---
    StatusDot* statusDot_;
    QLabel* statusText_;
    QLabel* footerLabel_;

    QTimer* liveTimer_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    QMenu* trayGameMenu_ = nullptr;
    QMenu* trayProfileMenu_ = nullptr;
    QAction* trayStatusAction_ = nullptr;
    QAction* trayToggleAction_ = nullptr;
    QAction* trayMiniAction_ = nullptr;
    QAction* trayCheckAction_ = nullptr;
    QAction* trayOverlayAction_ = nullptr;
    QMenu* trayOverlayMenu_ = nullptr;
    bool restart_ = false;
    MiniWindow* mini_ = nullptr;
    OverlayWindow* overlay_ = nullptr;
};
