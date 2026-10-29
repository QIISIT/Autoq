# AutoQ для разработчиков

Как собрать AutoQ, как устроен код и как запускать тесты. Описание для
пользователей — в [README](../README.md).

## Сборка

### Linux

```bash
# Arch / Manjaro
sudo pacman -S --needed qt6-base libx11 libxtst libxi xcb-util-cursor cmake gcc ninja
# Ubuntu 24.04+ / Debian 13+
sudo apt install qt6-base-dev libx11-dev libxtst-dev libxi-dev libxcb-present-dev cmake g++ ninja-build

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
install -Dm755 build/AutoQ ~/.local/bin/AutoQ
```

Перед обновлением закройте запущенный AutoQ (значок в трее → Выход).

Под Wayland клики и глобальная клавиша работают с играми, которые идут через
XWayland (Proton/Wine, Minecraft Java) — это почти все игры. Окна AutoQ под
Wayland тоже открываются через XWayland — только так оверлей КПС встаёт поверх
игры в нужном месте. Отключить: Настройки → «Оверлей КПС» → «Linux · Wayland».

### Windows

Exe и установщик собираются из Linux: `packaging/windows/build-from-linux.sh`.
Инструкция для пользователей — `packaging/windows/README_WINDOWS.txt`.

### macOS

Собирается на Маке (`packaging/macos/build-mac.sh`, нужен `brew install cmake ninja qt`) или
в GitHub Actions (`.github/workflows/macos.yml` — одна сборка для Apple Silicon и Intel,
для тега `v*` dmg сам попадает в релиз). Как открыть без платной подписи Apple и
какие разрешения дать — `packaging/macos/README_MAC.txt`. Про подпись Windows и macOS — [SIGNING.md](../SIGNING.md).

## Как устроено

```
src/
  main.cpp, appinfo.h   запуск: права администратора, один экземпляр, язык, главное окно
  core/                 ядро без интерфейса (только Qt Core) — его же проверяют тесты
  services/             обновления и автозапуск
  platform/windows|linux|macos/   код под одну ОС: собирается только своя папка
  ui/                   окна; ui/dialogs — диалоги и всплывающие списки; ui/style — оформление
resources/fonts/        встроенные шрифты
packaging/              сборка и установщики: windows (exe, NSIS), macos (app, dmg), winget
tests/                  автотесты и inputtool
```

Код под ОС — по одному правилу: общее в `core` или `services`, а то, что даёт система, —
в `platform/<ОС>/<модуль>_<ос>.cpp`. Интерфейс — в заголовке модуля или в `<модуль>_os.h`
рядом с ним (`core/clicker_os.h`, `core/framewatch_os.h`, `services/updater_os.h`).
Поведение одно на всех ОС; где система чего-то не умеет — об этом сказано в заголовке.

| Модуль | Что делает | Windows / Linux / macOS |
|---|---|---|
| `core/keys` | единая таблица клавиш: имя → код Windows, X11, Qt, macOS; захват клавиши по физической клавише | — / `keys_x11` / — |
| `core/games` | база игр, режимы, группы, «По шагам», пресеты, профили файлами, перенос старых настроек | |
| `core/appsettings` | папка данных: settings.ini, профили, портативный режим (AutoQ-data), правка файла снаружи; общие настройки | |
| `core/winmatch` | какое окно активно и принадлежит ли оно игре (процесс + заголовок), список окон | `winmatch_win` / `winmatch_x11` / `winmatch_mac` |
| `core/clicker` | цикл кликера: режимы, паузы, такт монитора, «По кадрам», окно игры | `clicker_*`: SendInput со скан-кодами / XTest / CGEvent |
| `core/hotkey` | глобальные клавиши: старт/стоп, профили, «используется»; нажатие и отпускание | `hotkey_*`: RegisterHotKey и хуки / XGrabKey и XInput2 / CGEventTap |
| `core/framesync` | клики в такт с монитором | `framesync_*`: DWM / центр окна X11 / центр окна CGWindowList |
| `core/framewatch` | «По кадрам»: кадры окна игры. Считает только показанные кадры одного окна, по времени показа из события | `framewatch_win` (ETW: DXGI, D3D9, DxgKrnl; нужны права администратора) / `framewatch_x11` (Present; без libxcb-present — `framewatch_none`) / `framewatch_none` |
| `core/sounds` | звуки старт/стоп: встроенный набор, «Мои звуки», громкость | |
| `core/cardlayout` | раскладка карточек и блоков у каждой игры, шаблоны | |
| `core/tips` | подсказки к функциям: «?», первое знакомство | |
| `core/updates` | разбор релиза GitHub, версии, выбор файла, SHA-256 | |
| `core/i18n`, `core/i18n_en.cpp` | язык интерфейса: строки по-русски через `T("…")`, английский словарь (тест проверяет, что переведено всё) | |
| `services/updater` | автообновление: проверить, скачать, сверить SHA-256, поставить | `updater_*`: установщик или portable / сборка из исходников / dmg |
| `services/autostart` | автозапуск и права администратора | `autostart_*`: ключ Run + Планировщик / ~/.config/autostart / LaunchAgent |
| `platform/macos/macsupport.mm`, `macperm` | macOS: активное приложение, окна поверх полноэкранных игр, разрешения | |
| `ui/mainwindow`, `ui/miniwindow` | главное и мини-окно, трей | |
| `ui/board` | редактор интерфейса: карточки, блоки, ширина, вид, отмена, боковая панель | |
| `ui/actionseditor` | действия игры: «держать», паузы, группы (×N, общие для всех игр) | |
| `ui/overlay` | оверлей КПС поверх игры (прозрачный для мыши) и его образец в настройках | |
| `ui/onboarding` | первый запуск: выбор стиля и анимаций | |
| `ui/dialogs/*` | настройки, игра, группа, клавиша, выбор клавиши, окна и процесса, цвет | |
| `ui/style/*` | стили «Студия» / «Стекло» / «Терминал», «Мои стили» (`.aqstyle`), анимации, значки кодом, общие виджеты | |

Шрифты Unbounded, Onest и JetBrains Mono — под лицензией SIL OFL 1.1
(тексты лицензий в `resources/fonts`).

## Тесты

```bash
cmake -S . -B build -G Ninja -DAQ_BUILD_TESTS=ON && cmake --build build
./build/tests/test_core                              # ядро
QT_QPA_PLATFORM=offscreen ./build/tests/test_icons   # значки
QT_QPA_PLATFORM=offscreen ./build/tests/test_overlay # оверлей КПС, вид элементов, стили
./build/tests/inputtool probe                        # какое окно активно и какой игре подходит
```

`inputtool probe` полезен, если игра не распознаётся: запустите
`sleep 3; ./build/tests/inputtool probe`, переключитесь в игру и посмотрите
процесс и заголовок — их можно вписать в правила игры (карандаш у названия).
