#pragma once
#include <QDialog>
#include <QButtonGroup>
#include "games.h"

class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QToolButton;
class Segmented;
class TimeChoice;
class CaptureCountdown;

// Добавление / изменение игры: значок, название, режим, правила окна
class GameEditDialog : public QDialog {
    Q_OBJECT
public:
    GameEditDialog(const GameDef& game, bool isNew, QWidget* parent = nullptr);
    GameDef result() const { return game_; }
    bool removeRequested() const { return removeRequested_; }   // нажали «Убрать игру…»

private slots:
    void captureNow();
    void chooseImage();
    void resetIcon();
    void resetDefaults();
    void accept() override;

private:
    void fill(const GameDef& g);
    void updatePreview();
    void syncPickers();
    void chooseTemplate(const QString& id);   // "" — своя игра
    GameDef game_;
    bool isNew_;
    bool removeRequested_ = false;
    QString customId_;                        // id своей игры, если шаблон не выбран
    QButtonGroup templateGroup_;
    QList<QToolButton*> templateButtons_;
    QString icon_;
    QString color_;
    QByteArray image_;

    QLabel* preview_;
    QButtonGroup iconGroup_;
    QButtonGroup colorGroup_;
    QList<QToolButton*> iconButtons_;
    QList<QToolButton*> colorButtons_;
    QLineEdit* nameEdit_;
    Segmented* modeSeg_;
    QLineEdit* procEdit_;
    QLineEdit* titleEdit_;
    QSpinBox* minIntervalBox_;
    QLabel* infoLabel_;
    TimeChoice* timeSeg_;
    QPushButton* captureBtn_;
    CaptureCountdown* countdown_;
};
