#pragma once
// «Настройки»: вкладка «Оформление» (тема, акцент, фон), вкладка «Оверлей КПС»
// (что показывать, где, как выглядит — с живым образцом) и «Подсказки» (все
// подсказки с поиском, значки «?» и подсказки при первом знакомстве).
// Всё меняется и сохраняется сразу.
#include <QDialog>
#include <QButtonGroup>
#include <QList>
#include <functional>
#include "ui/style/theme.h"
#include "ui/overlay.h"

class QLabel;
class QToolButton;
class QHBoxLayout;
class QStackedWidget;
class QLineEdit;
class QSpinBox;
class QSlider;
class QComboBox;
class QPushButton;
class Segmented;
class StartButton;
class ToggleSwitch;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    enum Page { General = 0, Appearance = 1, Overlay = 2, TipsPage = 3 };
    SettingsDialog(OverlayWindow* overlay, Page page = Appearance, QWidget* parent = nullptr);
    ~SettingsDialog() override;
    // раскладка открытой игры (для «Сохранить текущий вид» — по желанию со стилем)
    void setLayoutSource(const std::function<QString()>& f) { layoutSource_ = f; }
    void reloadAppearance();                // оформление сменили снаружи (поставили стиль)

signals:
    void appearanceChanged();
    void overlayChanged();
    void generalChanged();
    void restartRequested();
    void adminRestartRequested();   // Windows: «Перезапустить от имени администратора»
    void onboardingRequested();     // «Пройти первый запуск заново»
    void editLayoutRequested();   // «Режим редактирования» карточек — в главном окне
    void styleApplyRequested(const QString& name);   // «Мои стили»: поставить стиль (с раскладкой — главное окно)
    void tipsChanged();             // значки «?» включили или выключили

private:
    QWidget* buildGeneralPage();
    QWidget* buildAppearancePage();
    QWidget* buildOverlayPage();
    QWidget* buildTipsPage();
    void apply(const Theme::Appearance& a);
    void refresh();                         // отметить выбранные цвета, перекрасить образцы
    void rebuildBgRow();
    void refreshEditor();
    void rebuildAnimDemo();
    void rebuildStyles();                   // «Мои стили»: карточки сохранённых стилей
    void showStyleMenu(const QString& name, const QPoint& global);
    QList<QToolButton*> makeSwatches(const QStringList& colors, QButtonGroup& group, QHBoxLayout* row);

    // --- оверлей ---
    void overlayToUi();
    void overlayFromUi();                   // поля -> настройки -> оверлей и диск
    void overlayValuesToUi();               // подписи ползунков и доступность полей
    void applyOverlay();
    void pickOverlayColor(int which);       // 0 — текст, 1 — число, 2 — фон
    void paintColorButtons();

    OverlayWindow* overlay_;
    OverlaySettings os_;
    bool loadingOverlay_ = false;

    Segmented* tabs_;
    QStackedWidget* pages_;

    // оформление
    Segmented* themeSeg_;
    QButtonGroup accentGroup_;
    QButtonGroup bgGroup_;
    QList<QToolButton*> accentBtns_;
    QList<QToolButton*> bgBtns_;
    QHBoxLayout* bgRow_;
    QLabel* accentInfo_;
    QLabel* bgInfo_;
    QLabel* bgTitle_;
    QButtonGroup styleGroup_;
    QList<QToolButton*> styleBtns_;
    QLabel* styleInfo_;
    QLabel* headIcon_;
    QList<class QPushButton*> variantCards_;
    QList<class ThemePreview*> variantPreviews_;
    Segmented* animSeg_ = nullptr;
    QLabel* animInfo_ = nullptr;
    QSlider* animSpeed_ = nullptr;
    QLabel* animSpeedVal_ = nullptr;
    class AnimDemo* animDemo_ = nullptr;
    QSlider* radiusSlider_ = nullptr;
    QLabel* radiusVal_ = nullptr;
    Segmented* densitySeg_ = nullptr;
    Segmented* displayFontSeg_ = nullptr;
    Segmented* textFontSeg_ = nullptr;
    Segmented* bordersSeg_ = nullptr;
    Segmented* scaleSeg_ = nullptr;
    QLabel* scaleNote_ = nullptr;
    QPushButton* scaleRestart_ = nullptr;
    bool loadingEditor_ = false;
    // мои стили
    class FlowLayout* stylesFlow_ = nullptr;
    QLabel* stylesEmpty_ = nullptr;
    QList<QPair<QPushButton*, Theme::Appearance>> styleCards_;
    std::function<QString()> layoutSource_;

    // оверлей
    OverlayPreview* oPreview_;
    ToggleSwitch* oEnabled_;
    Segmented* oWhen_;
    ToggleSwitch* oShowCps_;
    ToggleSwitch* oShowLabel_;
    QLineEdit* oLabel_;
    ToggleSwitch* oShowActions_;
    ToggleSwitch* oShowTotal_;
    ToggleSwitch* oShowStatus_;
    ToggleSwitch* oShowGame_;
    ToggleSwitch* oShowGraph_;
    QButtonGroup oAnchorGroup_;
    QList<QToolButton*> oAnchorBtns_;
    QSpinBox* oOffX_;
    QSpinBox* oOffY_;
    QComboBox* oScreen_;
    QPushButton* oMoveBtn_;
    QToolButton* oTextColor_;
    QToolButton* oNumColor_;
    QToolButton* oNumAuto_;
    QToolButton* oBgColor_;
    QSlider* oBgOpacity_;
    QLabel* oBgOpacityVal_;
    QSlider* oOpacity_;
    QLabel* oOpacityVal_;
    QSlider* oScale_;
    QLabel* oScaleVal_;
    QSlider* oRadius_;
    QLabel* oRadiusVal_;
    Segmented* oFont_;
    Segmented* oLayout_;
    ToggleSwitch* oOutline_;
    Segmented* oWindow_;
    ToggleSwitch* oXWayland_ = nullptr;
    QLabel* oPlatformNote_ = nullptr;
};
