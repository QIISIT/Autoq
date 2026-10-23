#!/usr/bin/env bash
# =====================================================================
#  Сборка AutoQ для macOS: AutoQ.app и AutoQ-macOS.dmg.
#  Запускать НА МАКЕ (или в GitHub Actions — см. .github/workflows/macos.yml):
#      ./macos/build-mac.sh
#  Нужно: Xcode Command Line Tools (xcode-select --install), CMake, Ninja, Qt 6.
#    Проще всего:  brew install cmake ninja qt
#  Универсальная сборка (Intel + Apple Silicon) — с официальным Qt из
#  установщика qt.io или install-qt-action:  ARCHS="arm64;x86_64" ./macos/build-mac.sh
#
#  Подпись. Без сертификата — «ad-hoc» (минимум, без которого Apple Silicon
#  не запускает программу). С самоподписанным сертификатом (см.
#  macos/make-cert.sh) разрешения macOS не слетают после обновлений:
#      MAC_SIGN_IDENTITY="AutoQ Self-Signed (Qisit)" ./macos/build-mac.sh
#  Ни то, ни другое не убирает окно «не удалось проверить разработчика» —
#  для этого нужна платная учётная запись Apple Developer и нотаризация.
# =====================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-macos"
DIST="$ROOT/dist-macos"
VERSION="$(sed -n 's/^#define AQ_VERSION *"\(.*\)"/\1/p' "$ROOT/appinfo.h")"
FILEVER="${VERSION// /-}"     # «1.16.2 beta» -> «1.16.2-beta» для имени файла

# Qt: из install-qt-action (QT_ROOT_DIR), иначе Homebrew
QT_DIR="${QT_ROOT_DIR:-}"
if [ -z "$QT_DIR" ] && command -v brew >/dev/null; then QT_DIR="$(brew --prefix qt 2>/dev/null || true)"; fi
[ -n "$QT_DIR" ] && [ -x "$QT_DIR/bin/macdeployqt" ] || {
    echo "Не найден Qt 6: brew install qt  (или укажи QT_ROOT_DIR=/путь/к/Qt/6.x/macos)" >&2; exit 1; }

echo "== 1. Сборка AutoQ $VERSION (Qt: $QT_DIR)"
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    ${ARCHS:+-DCMAKE_OSX_ARCHITECTURES="$ARCHS"}
cmake --build "$BUILD"
APP="$BUILD/AutoQ.app"

echo "== 2. Qt внутрь AutoQ.app"
"$QT_DIR/bin/macdeployqt" "$APP" -always-overwrite

echo "== 3. Подпись (последним шагом: любая правка после неё ломает подпись)"
IDENTITY="${MAC_SIGN_IDENTITY:--}"
sign() {
    codesign --force --deep --timestamp=none --identifier com.qisit.autoq \
        ${MAC_SIGN_KEYCHAIN:+--keychain "$MAC_SIGN_KEYCHAIN"} --sign "$1" "$APP"
}
if ! sign "$IDENTITY"; then
    echo "!! Не удалось подписать сертификатом «$IDENTITY» — подписываю ad-hoc" >&2
    IDENTITY="-"
    sign "-"
fi
codesign --verify --deep --strict --verbose=2 "$APP"
codesign -dv "$APP" 2>&1 | grep -E "Identifier|Authority|Signature" || true

echo "== 4. DMG"
rm -rf "$DIST" && mkdir -p "$DIST/stage"
cp -R "$APP" "$DIST/stage/"
ln -s /Applications "$DIST/stage/Программы"
cp "$ROOT/macos/README_MAC.txt" "$DIST/stage/Как открыть AutoQ.txt"
hdiutil create -volname "AutoQ $VERSION" -srcfolder "$DIST/stage" -ov -format UDZO \
    "$DIST/AutoQ-$FILEVER-macOS.dmg"
rm -rf "$DIST/stage"
(cd "$DIST" && shasum -a 256 ./*.dmg > SHA256SUMS.txt)
ls -la "$DIST"
echo "Готово: $DIST/AutoQ-$FILEVER-macOS.dmg (подпись: $([ "$IDENTITY" = "-" ] && echo ad-hoc || echo "$IDENTITY"))"
