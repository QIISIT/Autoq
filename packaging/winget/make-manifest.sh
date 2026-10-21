#!/usr/bin/env bash
# Манифест winget для AutoQ (установка командой: winget install Qisit.AutoQ).
# 1) Выложи AutoQ-Setup.exe в GitHub Releases (нужна прямая https-ссылка).
# 2) ./packaging/winget/make-manifest.sh 1.9.0 https://github.com/<ты>/autoq/releases/download/v1.9.0/AutoQ-Setup.exe
# 3) Проверка на Windows: winget validate manifests\q\Qisit\AutoQ\1.9.0
#    Отправка: форк github.com/microsoft/winget-pkgs → pull request с папкой манифеста
#    (или wingetcreate submit). Проверка Microsoft запускает Defender и SmartScreen.
set -euo pipefail
VER="${1:?версия, например 1.9.0}"
URL="${2:?прямая https-ссылка на AutoQ-Setup.exe}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SETUP="${3:-$ROOT/dist-windows/AutoQ-Setup.exe}"
SHA="$(sha256sum "$SETUP" | cut -d' ' -f1 | tr a-f A-F)"
OUT="$ROOT/packaging/winget/manifests/q/Qisit/AutoQ/$VER"
mkdir -p "$OUT"
cat > "$OUT/Qisit.AutoQ.yaml" <<Y
PackageIdentifier: Qisit.AutoQ
PackageVersion: $VER
DefaultLocale: ru-RU
ManifestType: version
ManifestVersion: 1.6.0
Y
cat > "$OUT/Qisit.AutoQ.installer.yaml" <<Y
PackageIdentifier: Qisit.AutoQ
PackageVersion: $VER
InstallerType: nullsoft
Scope: machine
ElevationRequirement: elevatesSelf
UpgradeBehavior: install
Installers:
- Architecture: x64
  InstallerUrl: $URL
  InstallerSha256: $SHA
ManifestType: installer
ManifestVersion: 1.6.0
Y
cat > "$OUT/Qisit.AutoQ.locale.ru-RU.yaml" <<Y
PackageIdentifier: Qisit.AutoQ
PackageVersion: $VER
PackageLocale: ru-RU
Publisher: Qisit
PackageName: AutoQ
License: Freeware
ShortDescription: Автокликер для игр — привязка к окну игры, профили и оверлей КПС
Tags:
- autoclicker
- clicker
- games
ManifestType: defaultLocale
ManifestVersion: 1.6.0
Y
echo "Манифест: $OUT (SHA256 $SHA)"
