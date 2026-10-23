#!/usr/bin/env bash
# =====================================================================
#  Самоподписанный сертификат для подписи AutoQ на macOS (бесплатно).
#  Зачем: macOS запоминает разрешения («Универсальный доступ», «Мониторинг
#  ввода») по подписи программы. С ad-hoc-подписью каждая новая версия для
#  macOS — «другая программа», и разрешения приходится выдавать заново.
#  С одним и тем же сертификатом они сохраняются между обновлениями.
#  Окно «не удалось проверить разработчика» он НЕ убирает.
#
#  Запуск (Linux или Mac, нужен openssl):  ./macos/make-cert.sh
#  Создаёт в macos/cert/:
#    autoq-signing.p12      — сертификат с ключом (храни в секрете, не выкладывай!)
#    autoq-signing.p12.b64  — он же в base64 — для секрета MAC_CERT_P12 в GitHub
#    password.txt           — пароль — для секрета MAC_CERT_PASSWORD
# =====================================================================
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)/cert"
NAME="AutoQ Self-Signed (Qisit)"
mkdir -p "$DIR"
cd "$DIR"
[ -f autoq-signing.p12 ] && { echo "Уже есть $DIR/autoq-signing.p12 — не перезаписываю (удали сам, если нужен новый)."; exit 0; }
PASS="$(openssl rand -hex 16)"
openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
    -keyout key.pem -out cert.pem -subj "/CN=$NAME" \
    -addext "basicConstraints=critical,CA:false" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=critical,codeSigning" 2>/dev/null
# 3DES/SHA1 — такой .p12 понимает «Связка ключей» любой версии macOS
openssl pkcs12 -export -inkey key.pem -in cert.pem -name "$NAME" \
    -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1 \
    -passout "pass:$PASS" -out autoq-signing.p12
base64 < autoq-signing.p12 | tr -d '\n' > autoq-signing.p12.b64
printf '%s' "$PASS" > password.txt
rm -f key.pem
chmod 600 autoq-signing.p12 autoq-signing.p12.b64 password.txt
echo "Готово: $DIR"
echo " • GitHub → Settings → Secrets → Actions:"
echo "     MAC_CERT_P12      = содержимое autoq-signing.p12.b64"
echo "     MAC_CERT_PASSWORD = содержимое password.txt"
echo " • На своём Маке: двойной клик по autoq-signing.p12 (пароль из password.txt),"
echo "   затем MAC_SIGN_IDENTITY=\"$NAME\" ./macos/build-mac.sh"
