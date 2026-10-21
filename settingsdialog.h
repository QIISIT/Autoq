#pragma once
// «Настройки»: вкладка «Оформление» (тема, акцент, фон) и вкладка
// «Оверлей КПС» (что показывать, где, как выглядит — с живым образцом).
// Всё меняется и сохраняется сразу.
#include <QDialog>
#include <QButtonGroup>
#include <QList>
#include "theme.h"
#include "overlay.h"

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
    enum Page { General = 0, Appearance = 1, Overlay = 2 };
    SettingsDialog(OverlayWindow* overlay, Page page = Appearance, QWidget* parent = nullptr);
    ~SettingsDialog() override;

signals:
    void appearanceChanged();
    void overlayChanged();
    void generalChanged();
    void restartRequested();
    void onboardingRequested();     // «Пройти первый запуск заново»
    void cardOrderChanged();

private:
    QWidget* buildGeneralPage();
    QWidget* buildAppearancePage();
    QWidget* buildOverlayPage();
    void apply(const Theme::Appearance& a);
    void refresh();                         // отметить выбранные цвета, перекрасить образцы
    void rebuildBgRow();
    void rebuildCardOrder();
    void refreshEditor();
    QList<QToolButton*> makeSwatches(const QStringList& colors, QButtonGroup& group, QHBoxLayout* row);

    // --- оверлей ---
    void overlayToUi();
    void overlayFromUi();                   // поля -> настройки -> оверлей и диск
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
    QSlider* radiusSlider_ = nullptr;
    QLabel* radiusVal_ = nullptr;
    Segmented* densitySeg_ = nullptr;
    Segmented* displayFontSeg_ = nullptr;
    Segmented* textFontSeg_ = nullptr;
    Segmented* bordersSeg_ = nullptr;
    class QVBoxLayout* orderBox_ = nullptr;
    Segmented* scaleSeg_ = nullptr;
    QLabel* scaleNote_ = nullptr;
    QPushButton* scaleRestart_ = nullptr;
    bool loadingEditor_ = false;

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
