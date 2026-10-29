#pragma once
// Автообновление (Настройки → Основные → «Обновления»). Раз в 6 часов AutoQ смотрит
// последний релиз на GitHub, скачивает файл под эту систему, сверяет SHA-256 с тем,
// что пишет GitHub, и ставит новую версию:
//   Windows, установщик — тихая установка (/S /UPDATE) при следующем запуске AutoQ
//                         или сразу по кнопке; установщик сам запускает новую версию;
//   Windows, portable   — новый AutoQ.exe встаёт на место старого при запуске или по кнопке;
//   Linux               — исходники собираются в фоне (как при установке), программа
//                         заменяется сразу и заработает после перезапуска;
//   macOS               — скачанный .dmg открывается, AutoQ перетаскивают в «Программы».
// Выключено — AutoQ сам в интернет не ходит, только по кнопке «Проверить сейчас».
// Сеть — через curl (есть в Windows 10/11, macOS и почти в любом Linux): Qt для
// Windows собран без модуля Network.
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <functional>
#include "core/updates.h"

class QProcess;
class QTimer;

class Updater : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Building, Ready, Failed };
    static Updater& instance();           // после QApplication
    ~Updater() override;

    static bool autoEnabled();            // по умолчанию включено
    void setAutoEnabled(bool on);

    // main(), до окон: скачанное обновление ставится при запуске (Windows). true — эта копия выходит
    static bool installPendingAtStart();

    void start();                         // автопроверка: первая через 20 с, потом раз в 6 часов
    void checkNow();                      // «Проверить сейчас»
    void update();                        // «Обновить»: скачать (собрать) и поставить
    void installNow();                    // скачанное — поставить сейчас

    State state() const { return state_; }
    bool busy() const;
    QString version() const { return release_.version; }  // новая версия; пусто — не знаем о ней
    QString releasePage() const;
    bool canUpdate() const;               // есть новая версия, и её можно поставить отсюда
    QString statusText() const;           // строка под переключателем
    QString updateButtonText() const;
    QString startNotice() const { return startNotice_; }  // «AutoQ обновлена до …» после запуска

signals:
    void changed();
    void notice(const QString& text);
    void restartRequested();              // новая версия на месте — перезапустить AutoQ
    void quitRequested();                 // установщик сам запустит новую версию — выйти

private:
    Updater();
    void autoTick();
    void check(bool manual);
    void gotRelease(const Updates::Release& r, bool manual);
    void download(bool installAfter);
    void downloaded(const QString& part, const Updates::Asset& asset, bool installAfter);
    void verified(const QString& part, const Updates::Asset& asset, bool installAfter);
    void build(const QString& tarball, bool installAfter);
    void ready(const QString& file, bool installAfter);
    void fail(const QString& text);
    void setState(State s);
    void announce(const QString& text);   // сообщение один раз на версию
    bool startsAtLaunch() const;          // скачанное поставится само при следующем запуске
    QString workDir() const;
    void run(const QString& program, const QStringList& args, int timeoutMs,
             const std::function<void(bool ok, const QByteArray& out)>& done, const QString& logFile = QString());

    State state_ = State::Idle;
    Updates::Package package_ = Updates::Package::None;
    Updates::Release release_;
    QString error_;
    QString pendingFile_;                 // скачанный файл (Linux — пусто: программа уже заменена)
    QString exePath_;
    QString startNotice_;
    QDateTime lastCheck_;
    bool triedAtStart_ = false;           // эту версию уже пробовали поставить при запуске
    QTimer* timer_ = nullptr;
    QPointer<QProcess> proc_;
};
