#pragma once
// Какое окно сейчас активно и принадлежит ли оно игре.
// Windows: заголовок + имя exe процесса.
// Linux (X11/XWayland): заголовок + класс окна (у Wine/Proton это имя exe,
// например "genshinimpact.exe") + процесс по _NET_WM_PID.
// macOS: активное приложение (NSWorkspace) + его окно из CGWindowList;
// заголовки чужих окон видны только с разрешением «Запись экрана».
#include <QString>
#include <QStringList>
#include <QList>

struct WindowInfo {
    bool valid = false;           // окно найдено
    QString title;                // заголовок
    QString process;              // имя процесса для показа ("GenshinImpact.exe")
    QStringList candidates;       // все имена, по которым можно опознать процесс
    bool processKnown = false;    // удалось ли узнать процесс
    bool titleKnown = true;       // заголовок прочитан (macOS без «Записи экрана» — нет)
    long long pid = 0;
    unsigned long long handle = 0; // HWND / X11 Window / CGWindowID — какое именно это окно
};

// Правило "это окно игры": имена процессов и/или фрагменты заголовка.
struct WindowRule {
    QStringList processes;        // без учёта регистра, ".exe" можно не писать
    QStringList titles;           // фрагменты заголовка, без учёта регистра
    bool isEmpty() const { return processes.isEmpty() && titles.isEmpty(); }
};

// Совпадает ли окно с правилом.
//  * если у правила есть процессы и процесс окна известен — процесс обязан совпасть
//    (так вкладка браузера "Genshin Impact - Wiki" игрой не считается);
//  * если процесс узнать не удалось — решает заголовок (если он задан);
//  * если заданы заголовки — заголовок обязан содержать один из них;
//  * заголовок прочитать нельзя (macOS без разрешения) — решает процесс.
bool matchesRule(const WindowRule& rule, const WindowInfo& info);

// Совпадает ли процесс окна хоть с одним именем из списка
bool processMatches(const QStringList& names, const WindowInfo& info);

class ActiveWindowProbe {
public:
    ActiveWindowProbe();
    ~ActiveWindowProbe();
    ActiveWindowProbe(const ActiveWindowProbe&) = delete;
    ActiveWindowProbe& operator=(const ActiveWindowProbe&) = delete;

    bool available() const;       // есть ли доступ к оконной системе
    WindowInfo query();           // активное окно сейчас
    QList<WindowInfo> listWindows();   // все открытые окна программ (для выбора процесса)
    bool exists(unsigned long long handle);   // окно ещё открыто (не закрыли игру)
    bool pointerOver(unsigned long long handle);   // курсор мыши над этим окном

private:
    void* display_ = nullptr;     // Display* на Linux
    long long ownPid_ = 0;
};

// Имя исполняемого файла этой программы (чтобы не "захватить" саму себя)
QString ownProcessName();
