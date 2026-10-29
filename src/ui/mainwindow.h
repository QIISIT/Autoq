#pragma once
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QPointer>
#include <memory>
#include <map>
#include "core/clicker.h"
#include "core/hotkey.h"
#include "core/games.h"
#include "core/winmatch.h"

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
class Board;
class OnboardingPage;
class GameListDelegate;
class QHBoxLayout;
class QBoxLayout;
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
    void restartAsAdmin();      // Windows: копия с правами администратора (UAC), эта выходит
    void toggleRunning();          // кнопка Старт, трей, мини-окно — для открытой игры
    void startPressed();           // кнопка Старт: не используется — включить, иначе старт/стоп
    void onHotkeyEvent(const QString& spec, bool down);   // глобальная клавиша (из потока хоткеев)
    void showMain();
    void restoreFromTray();      // значок трея, Dock на macOS: мини-окно (в мини-режиме) или главное
    void showMini();
    void openSettings();
    void openOverlaySettings();
    void startOnboarding();

private slots:
    void onGameRowChanged(int row);
    void addGame();
    void editGame();
    void removeGame();
    void notifyAdded(const GameDef& g, int profiles);   // «Игра добавлена» (и профили, если вернулись)
    void startWindowCheck();
    void checkWindowDone();
    void pickWindowFromList();          // «Выбрать процесс…»: окно из списка, без отсчёта
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
    void buildHeader();            // шапка — тоже блоки: игра, счётчик, кнопки, «Старт»
    void buildActionsCard();
    void buildSpeedCard();
    void buildWindowCard();
    void buildProfileCard();
    QWidget* buildStatusBar();
    void buildTray();
    void connectUpdater();           // автообновление: сообщения, перезапуск, пункт в трее
    void connectSignals();
    void connectMini();
    void setLayoutEditing(bool on);  // редактор интерфейса (Д.И.1)
    void applySidebarMode(int mode); // 0 слева, 1 справа, 2 узкая, 3 скрыта
    void showSwitchMenu();           // панель скрыта: игры, «добавить», настройки — из шапки
    void updateSidebarVisible();     // скрытая панель всё же видна, если без неё не добраться до игр
    // где идёт переход при смене игры: при скрытой панели она может появиться — тогда по всему окну
    QWidget* pageArea() const;
    void updateMonitor(bool full);   // блок «Монитор запуска» (full — и числа, иначе только шаг)
    void applyLayoutToAll();

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
    void updateActionsHint();
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
    int repeatMin() const { return cur_.repeatMin(game_.mode, game_.minIntervalMs); }   // «повтор через» от
    int minCycle() const { return cur_.minCycle(game_.mode, game_.minIntervalMs); }     // круг целиком — не быстрее
    // «По кадрам»: включено, привязка к окну включена и эта ОС видит кадры игры (в «Зажать» — нет).
    // frameView — как его видят редактор и кликер
    bool framesOn() const { return framesOn(cur_) && game_.mode != GameMode::Hold; }
    static bool framesOn(const GameState& st);
    static GameState frameView(const GameState& st);
    int repeatMinFr() const { return cur_.repeatMin(game_.mode, kFrameUnit); }   // «повтор через» в кадрах — от
    void updateFramesUi();             // переключатель, подпись (кадров/с, нет окна, нет прав)
    double framesFps() const;          // кадров в секунду у окна игры (0 — не знаем)
    double sliderLo() const { return qMax(1, repeatMin()); }   // левый край шкалы скорости (самый быстрый)

    // --- профили ---
    void updateProfileState();
    bool profileDirty() const;
    bool confirmLeaveProfile();    // несохранённые изменения: сохранить / не сохранять / отмена
    void switchProfile(const QString& name);   // "" — без профиля
    void loadProfile(const QString& name, bool quiet = false);
    void newProfile(int base = 0);             // 0 — текущие, 1 — стандартные, 2+ — пресет
    void saveProfile();
    void showResetMenu(QWidget* anchor);
    void updateSoundBtn();
    void fillSoundMenu();
    void deleteProfile();
    void renameProfile();
    void editProfileKey();
    void exportProfile();       // профиль в файл (поделиться)
    void importProfile();       // профиль из файла
    void applyProfileFor(const QString& gameId, const QString& profile);   // клавишей профиля
    // Файлы профилей положили, поправили или убрали снаружи: система сообщает об этом сама
    // (без проверок по таймеру), плюс «Обновить список» в меню ⋯
    void watchProfiles();          // какие папки слушать (папка данных могла смениться)
    void unwatchProfiles();        // Windows: слушаемую папку нельзя удалить — отпустить перед удалением
    void profilesChangedOnDisk(bool byButton);
    // settings.ini поправили снаружи — перечитать и применить сразу, без перезапуска
    void configChangedOnDisk();
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
    // окно выбрали (отсчётом — активное, или из списка): подходит под правила — привязать,
    // нет — дописать процесс в правила (из отсчёта — спросив «это игра?»)
    void useWindow(const WindowInfo& w, bool picked, bool wantStart);

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
    // «В такт с монитором»: частота монитора, где окно игры (0 — окно не выбрано: основной экран)
    static double monitorHz(unsigned long long windowHandle);
    static double syncHz(const GameState& st, unsigned long long windowHandle);   // своя или монитора
    unsigned long long boundHandle(const QString& gameId) const;
    void fillHzBox();
    void toggleFrom(bool fromHotkey);
    void startClicker();
    void stopGame(const QString& id);
    bool startBackground(const QString& id, bool fromHotkey);   // игра не открыта — без отсчёта
    void toggleGame(const QString& id, bool fromHotkey);
    bool refuseUnused(const QString& id);       // категория не используется — сказать и не запускать
    void cancelPending();
    QString startProblem() const;
    QString startProblemFor(const GameDef& g, const GameState& st) const;

    // --- клавиши ---
    struct HkTarget { int kind = 0; QString game; QString profile; };   // 0 старт, 1 «активна», 2 профиль
    struct HkBinding { HotkeySpec spec; QList<HkTarget> targets; };
    void rebuildBindings();
    void setHotkey(const HotkeySpec& sp, bool save);   // клавиша старт/стоп открытой игры
    void updateHotkeyStatus();
    void updateHotkeyInfo();
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
    QString actionsLabel(const QList<ActionStep>& list, int from, int to) const;
    QWidget* dialogParent();
    void notify(const QString& text);
    // подсказки (И2): «?» с подсказками по теме (прячутся в Настройки → Подсказки) и
    // подсказка при первом знакомстве с функцией — один раз, внизу окна
    class InfoBadge* tipBadge(const QStringList& ids);
    void applyTipBadges();
    bool firstTimeTip(const QString& id);
    void openSettingsPage(int page);
    void setOverlayMode(int mode);   // 0 — выкл, 1 — пока кликер работает, 2 — всегда
    void showOverlayMenu(QWidget* anchor);
    void fillOverlayMenu(QMenu* m);
    void syncOverlayUi();          // кнопки «оверлей вкл/выкл» в шапке, трее и мини-окне
    void appearanceChanged();      // тема/стиль сменились — перерисовать
    void applyElementLook();       // вид мелких элементов: спрятанные подсказки и кнопки профиля
    QColor saveIconColor(bool dirty) const;
    // профили стиля: раскладка открытой игры (JSON), поставить стиль (спросит про раскладку), меню «Стили»
    QString currentLayoutJson() const;
    bool applyStyle(const QString& name, QWidget* parent);
    void fillStylesMenu(class QMenu* m);
    void finishOnboarding();

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
    QList<QPointer<class InfoBadge>> tipBadges_;
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
    QFrame* sidebar_ = nullptr;
    QHBoxLayout* bodyLay_ = nullptr;
    QBoxLayout* brandLay_ = nullptr;
    QWidget* brandNames_ = nullptr;
    QWidget* sideSection_ = nullptr;          // «МОИ ИГРЫ» и число
    GameListDelegate* gameDelegate_ = nullptr;
    int sideMode_ = 0;

    // --- страницы ---
    QStackedWidget* pages_;
    QWidget* root_ = nullptr;        // боковая панель + страницы
    QGridLayout* welcomeGrid_;
    QLabel* welcomeLogo_;
    QWidget* inner_ = nullptr;
    class QScrollArea* mainScroll_ = nullptr;

    // --- шапка ---
    QLabel* headerIcon_;
    ElideLabel* nameLabel_;
    QToolButton* editGameBtn_;
    QToolButton* switchGameBtn_;      // панель скрыта — список игр из шапки
    QToolButton* layoutBtn_;          // редактор интерфейса
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
    QLabel* actionsTitle_;
    Segmented* modeSeg_;
    Segmented* singleSeg_;
    QLabel* actionsHint_;
    ActionsEditor* actionsEdit_;

    // --- монитор запуска ---
    QLabel* monTime_;
    QLabel* monState_;
    QLabel* monCycles_;
    QLabel* monPresses_;
    QLabel* monRate_;
    QLabel* monStep_;

    // --- 02 скорость ---
    QLabel* rateLabel_;
    QLabel* rateCaption_;
    QSlider* slider_;
    QLabel* speedCaption_;
    QDoubleSpinBox* speedBox_;
    Segmented* unitSeg_;
    ToggleSwitch* randomizeSwitch_;
    QSpinBox* jitterBox_;
    ToggleSwitch* frameSyncSwitch_;
    QComboBox* hzBox_;
    QLabel* hzCaption_;
    QWidget* hzRow_;            // «Частота кадров» — только при «В такт с монитором»
    QLabel* perMinuteLabel_;
    // «По кадрам»: включается, когда окно игры выбрано (Linux X11 / XWayland, Windows)
    QWidget* framesRow_;
    ToggleSwitch* framesSwitch_;
    QLabel* framesStatus_;
    QPushButton* framesAdminBtn_;              // Windows: без прав администратора кадров не видно
    std::shared_ptr<FrameWatch::Feed> framesFeed_;   // кадры окна открытой игры (для подписи и скорости)

    // --- 03 окно игры ---
    ToggleSwitch* lockSwitch_;
    QLabel* statusBox_;
    ChipEditor* procEdit_;
    ChipEditor* titleEdit_;
    TimeChoice* timeSeg_;
    QPushButton* checkBtn_;
    QPushButton* pickWindowBtn_;
    CaptureCountdown* countdown_;

    // --- 04 профиль и запуск ---
    Board* board_;
    QComboBox* profileBox_;
    QToolButton* profileMoreBtn_;      // ⋯ — переименовать, клавиша, оверлей, сбросить, удалить, файл, папка
    QAction* renameProfileAct_;
    QAction* profileKeyAct_;
    QAction* profileOverlayAct_;       // «показывать в оверлее КПС» у профиля
    QAction* delProfileAct_;
    QAction* exportProfileAction_ = nullptr;
    QList<QAction*> profileMenuActs_;   // «Новый / Сохранить» в ⋯ — когда кнопки спрятаны
    class QFileSystemWatcher* profileWatch_ = nullptr;
    QTimer* profileWatchTimer_ = nullptr;   // события идут пачкой — разбираем один раз, когда стихнут
    class QFileSystemWatcher* configWatch_ = nullptr;   // settings.ini и папка данных (файл пишется заменой)
    QTimer* configWatchTimer_ = nullptr;
    QString cleanProfile_;                  // «игра/профиль» без несохранённых правок…
    GameState cleanState_;                  // …и настройки тогда: файл поменяли снаружи — подхватить
    QLabel* profileState_;
    QPushButton* newProfileBtn_;
    QPushButton* saveProfileBtn_;
    KeyPicker* hotkeyPick_;
    KeyCaptureButton* captureBtn_;
    Segmented* keyModeSeg_;
    ToggleSwitch* armedSwitch_;
    QLabel* hotkeyStatus_;
    QLabel* hotkeyInfo_;
    QWidget* blockRow_;                // «Не пропускать нажатие в игру» (Mouse 4, буква…)
    ToggleSwitch* blockSwitch_;
    QToolButton* soundBtn_;          // звук старт/стоп у игры и профиля: со звуком / без звука вкл, выкл, совсем

    // --- строка состояния ---
    StatusDot* statusDot_;
    QLabel* statusText_;
    QLabel* footerLabel_;

    QTimer* liveTimer_ = nullptr;
    QTimer* stepTimer_ = nullptr;          // подсветка шага, который жмётся сейчас
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    QMenu* trayGameMenu_ = nullptr;
    QMenu* trayProfileMenu_ = nullptr;
    QAction* trayStatusAction_ = nullptr;
    QAction* trayToggleAction_ = nullptr;
    QAction* trayMiniAction_ = nullptr;
    QAction* trayCheckAction_ = nullptr;
    QAction* trayOverlayAction_ = nullptr;
    QAction* trayUpdateAction_ = nullptr;   // «Обновить до …» — вышла новая версия
    QMenu* trayOverlayMenu_ = nullptr;
    bool restart_ = false;
    MiniWindow* mini_ = nullptr;
    OverlayWindow* overlay_ = nullptr;
};
