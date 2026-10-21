# AutoQ 1.13 — created by Qisit

Автокликер с базой игр: скип диалогов в Genshin и похожих играх, спам,
комбинации и удержание клавиш в Minecraft и своих играх. Три режима —
«По очереди», «Вместе», «Зажать»; действий — сколько угодно, у каждого своё
«держать, мс» и пауза. Кликает только в выбранном окне игры.
Три стиля интерфейса — «Студия», «Стекло», «Терминал» (выбор при первом
запуске), анимации, редактор интерфейса (скругление, плотность, шрифты,
рамки, порядок карточек) и размер интерфейса 90–150 %.
У каждой игры своя клавиша старт/стоп (любая клавиша, сочетание, колёсико,
Mouse 4/5), режим «вкл/выкл» или «пока держу»; «активные» игры запускаются
своей клавишей, даже когда открыта другая, — можно несколько сразу.
Клавиши профилей и «активна / нет». Мини-окно, значок в трее.
Оверлей КПС поверх игры — клики в секунду (всего и по каждому действию),
счётчик, статус, график; место, цвета, прозрачность, размер и шрифт — свои.
Значки игр — четыре стиля на выбор: «Глянец» (по умолчанию), «Иллюстрация»,
«Неон», «Классика» (Настройки → Оформление).
Русский и английский интерфейс (Настройки → Основные → Язык).
Linux (X11 / XWayland), Windows 10/11 и macOS 12+.

## Linux (Arch)

```bash
sudo pacman -S --needed qt6-base libx11 libxtst libxi xcb-util-cursor cmake gcc ninja
tar -xf autoq-v112.tar.gz
cd autoq
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cp build/AutoQ ~/.local/bin/AutoQ
~/.local/bin/AutoQ
```

Перед обновлением закройте запущенный AutoQ (значок в трее → Выход).

Ubuntu/Debian: `sudo apt install qt6-base-dev libx11-dev libxtst-dev libxi-dev cmake g++ ninja-build`.

Под Wayland клики и глобальная клавиша работают с играми, которые идут через
XWayland (Proton/Wine, Minecraft Java) — это почти все игры. Окна AutoQ под
Wayland тоже открываются через XWayland — только так оверлей КПС встаёт поверх
игры в нужном месте. Отключить: Настройки → «Оверлей КПС» → «Linux · Wayland».

## Windows

Готовые файлы: `AutoQ-Windows-Portable.zip` (один exe) или `AutoQ-Setup.exe`. Инструкция —
`windows/README_WINDOWS.txt`. Пересборка exe из Linux: `windows/build-from-linux.sh`.

## macOS

Собирается на Маке (`macos/build-mac.sh`, нужен `brew install cmake ninja qt`) или
в GitHub Actions (`.github/workflows/macos.yml` — одна сборка для Apple Silicon и Intel).
Как открыть без платной подписи Apple и какие разрешения дать —
`macos/README_MAC.txt`. Про подпись Windows и macOS — `SIGNING.md`.

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
| `actionseditor.cpp` | действия игры: сколько угодно, «держать» и паузы |
| `hotkeydialog.cpp` | окно «назначить клавишу» (профили, «активна») |
| `keypicker.cpp` | выбор клавиши из всей клавиатуры с поиском |
| `colorpicker.cpp` | свой цвет с живым образцом окна, быстрое оформление в мини-окне |
| `overlay.cpp` | оверлей КПС поверх игры (прозрачный для мыши) и его образец в настройках |
| `cardlayout.cpp`, `board.cpp` | раскладка карточек у каждой игры: свои карточки, перенос блоков, сворачивание, режим редактирования |
| `sounds.cpp` | звуки старт/стоп: встроенный набор, «Мои звуки» (свои WAV), громкость |
| `framesync.cpp` | клики в такт с монитором (частота кадров) |
| `appsettings.cpp` | папка данных: settings.ini, профили файлами, портативный режим (AutoQ-data) |
| `mainwindow.cpp`, `miniwindow.cpp`, `gameeditdialog.cpp`, `settingsdialog.cpp`, `widgets.cpp` | интерфейс |

Шрифты Unbounded, Onest и JetBrains Mono — под лицензией SIL OFL 1.1
(тексты лицензий в `resources/fonts`).

## Тесты

```bash
cmake -S . -B build -G Ninja -DAQ_BUILD_TESTS=ON && cmake --build build
./build/tests/test_core                              # ядро
QT_QPA_PLATFORM=offscreen ./build/tests/test_icons   # значки
QT_QPA_PLATFORM=offscreen ./build/tests/test_overlay # оверлей КПС
./build/tests/inputtool probe                        # какое окно активно и какой игре подходит
```

`inputtool probe` полезен, если игра не распознаётся: запустите
`sleep 3; ./build/tests/inputtool probe`, переключитесь в игру и посмотрите
процесс и заголовок — их можно вписать в правила игры (карандаш у названия).
