#!/usr/bin/env bash
# =====================================================================
#  Сборка Windows-версии на Linux (Ubuntu/Debian) кросс-компиляцией.
#  Результат: dist-windows/AutoQ.exe (один файл, статический Qt),
#             dist-windows/AutoQ-Windows-Portable.zip,
#             dist-windows/AutoQ-Setup.exe
#
#  Обычному пользователю это НЕ нужно — он качает готовые файлы.
#  Скрипт для того, чтобы пересобрать exe после правок в коде.
# =====================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_VER="6.4.2"                    # должен совпадать с системным Qt (хост-инструменты)
QT_SRC="${QT_SRC:-$HOME/qt/qtbase}"
QT_BUILD="${QT_BUILD:-/opt/qtbuild/build-win}"
QT_PREFIX="${QT_PREFIX:-/opt/qt6-win-static}"
TOOLCHAIN="$ROOT/windows/mingw-toolchain.cmake"
OUT="$ROOT/dist-windows"

echo "== 1. Зависимости"
sudo apt-get install -y mingw-w64 g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix \
    ninja-build nsis zip cmake qt6-base-dev qt6-base-dev-tools

if [ ! -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "== 2. Статический Qt $QT_VER для Windows (один раз, ~30-60 мин)"
    [ -d "$QT_SRC" ] || git clone --depth 1 --branch "v$QT_VER" https://github.com/qt/qtbase "$QT_SRC"
    mkdir -p "$QT_BUILD"
    cmake -G Ninja -S "$QT_SRC" -B "$QT_BUILD" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake \
        -DQT_QMAKE_TARGET_MKSPEC=win32-g++ \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DFEATURE_static_runtime=ON \
        -DCMAKE_INSTALL_PREFIX="$QT_PREFIX" \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_BUILD_BENCHMARKS=OFF \
        -DFEATURE_sql=OFF -DFEATURE_network=OFF -DFEATURE_testlib=OFF -DFEATURE_dbus=OFF \
        -DFEATURE_opengl=OFF -DINPUT_opengl=no -DFEATURE_vulkan=OFF \
        -DFEATURE_printsupport=OFF -DFEATURE_concurrent=OFF -DFEATURE_xml=OFF \
        -DFEATURE_jpeg=OFF -DFEATURE_gif=OFF -DFEATURE_icu=OFF
    cmake --build "$QT_BUILD" --parallel
    cmake --install "$QT_BUILD"
fi

echo "== 3. Сборка AutoQ.exe"
cmake -G Ninja -S "$ROOT" -B "$ROOT/build-win" \
    -DCMAKE_TOOLCHAIN_FILE="$QT_PREFIX/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DQT_CHAINLOAD_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build-win"
x86_64-w64-mingw32-strip "$ROOT/build-win/AutoQ.exe"
# подпись (если настроена — см. windows/sign.sh; иначе пропускается)
"$ROOT/windows/sign.sh" "$ROOT/build-win/AutoQ.exe"

echo "== 4. Упаковка"
rm -rf "$OUT" && mkdir -p "$OUT/stage"
cp "$ROOT/build-win/AutoQ.exe" "$OUT/"
cp "$ROOT/build-win/AutoQ.exe" "$ROOT/windows/README_WINDOWS.txt" "$ROOT/windows/README_WINDOWS_EN.txt" "$OUT/stage/"
(cd "$OUT/stage" && zip -9 -q "../AutoQ-Windows-Portable.zip" AutoQ.exe README_WINDOWS.txt README_WINDOWS_EN.txt)
cp "$ROOT/windows/installer.nsi" "$ROOT/windows/app.ico" "$OUT/stage/"
if [ -n "${SIGN_PFX:-}${SIGN_PKCS11_MODULE:-}${SIGN_JSIGN:-}" ]; then
    (cd "$OUT/stage" && makensis -V2 -DSIGN_CMD="$ROOT/windows/sign.sh" installer.nsi && mv AutoQ-Setup.exe "$OUT/")
else
    (cd "$OUT/stage" && makensis -V2 installer.nsi && mv AutoQ-Setup.exe "$OUT/")
fi
(cd "$OUT" && sha256sum AutoQ-Setup.exe AutoQ-Windows-Portable.zip > SHA256SUMS.txt)
rm -rf "$OUT/stage"
ls -la "$OUT"
