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

Exe и установщик собираются из Linux: `windows/build-from-linux.sh`.
Инструкция для пользователей — `windows/README_WINDOWS.txt`.

### macOS

Собирается на Маке (`macos/build-mac.sh`, нужен `brew install cmake ninja qt`) или
в GitHub Actions (`.github/workflows/macos.yml` — одна сборка для Apple Silicon и Intel,
для тега `v*` dmg сам попадает в релиз). Как открыть без платной подписи Apple и
какие разрешения дать — `macos/README_MAC.txt`. Про подпись Windows и macOS — [SIGNING.md](../SIGNING.md).

## Как устроено

| Файл | Что делает |
|---|---|
| `keys.cpp` | единая таблица клавиш: имя → код Windows, X11, Qt |
| `games.cpp` | база игр, значки игр, режимы, пресеты, профили, перенос старых настроек |
| `winmatch.cpp` | какое окно активно и принадлежит ли оно игре (процесс + заголовок) |
| `clicker.cpp` | отправка нажатий (SendInput со скан-кодами / XTest / CGEvent) |
| `hotkey.cpp` | глобальные клавиши: старт/стоп игр, профили (RegisterHotKey / XGrabKey / CGEventTap; буквы и кнопки мыши — хуки / XInput2), нажатие и отпускание |
| `macsupport.mm`, `macperm.cpp` | macOS: активное приложение, окна поверх полноэкранных игр, разрешения |
| `icons.cpp` | логотип и значки, нарисованные кодом (свой набор, без логотипов игр; 4 стиля) |
| `i18n.cpp`, `i18n_en.cpp` | язык интерфейса: строки пишутся по-русски через `T("…")`, английский словарь — в `i18n_en.cpp` (тест проверяет, что переведено всё) |
| `theme.cpp` | стили «Студия» / «Стекло» / «Терминал», тема из цвета акцента и фона, редактор интерфейса, размер, шрифты |
| `animations.cpp` | анимации переходов и сообщения внутри окна |
| `onboarding.cpp` | первый запуск: выбор стиля и анимаций |
| `actionseditor.cpp` | действия игры: сколько угодно, «держать» и паузы, группы (выделить, «Сделать группой», «Создать группу…», ×N; группы общие для всех игр) |
| `groupdialog.cpp` | окно группы действий: имя, действия или шаги ↓ ↑, пауза между повторами, удалить |
| `tips.cpp` | подсказки к функциям: значки «?», первое знакомство, вкладка «Подсказки» в настройках |
| `hotkeydialog.cpp` | окно «назначить клавишу» (профили, «активна») |
| `keypicker.cpp` | выбор клавиши из всей клавиатуры с поиском |
| `colorpicker.cpp` | свой цвет с живым образцом окна |
| `overlay.cpp` | оверлей КПС поверх игры (прозрачный для мыши) и его образец в настройках |
| `cardlayout.cpp`, `board.cpp` | редактор интерфейса: раскладка у каждой игры (карточки, блоки, ширина, высота, вид, цвет, скрытые блоки), вид мелких элементов по группам, шаблоны, файл раскладки, отмена, боковая панель |
| `styles.cpp`, `styleui.cpp` | «Мои стили»: оформление (и по желанию раскладка) в файле `.aqstyle` — сохранить, поставить, переименовать, в файл и из файла |
| `sounds.cpp` | звуки старт/стоп: встроенный набор, «Мои звуки» (свои WAV), громкость |
| `framesync.cpp` | клики в такт с монитором (частота кадров) |
| `framewatch.cpp` | «По кадрам»: кадры окна игры — Linux через расширение X11 Present (libxcb-present; без него собирается без «По кадрам»), Windows через события ETW о показе кадра (DXGI, D3D9, DxgKrnl; нужны права администратора) |
| `appsettings.cpp` | папка данных: settings.ini, профили файлами, портативный режим (AutoQ-data), правка файла снаружи |
| `mainwindow.cpp`, `miniwindow.cpp`, `gameeditdialog.cpp`, `settingsdialog.cpp`, `widgets.cpp` | интерфейс |

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
