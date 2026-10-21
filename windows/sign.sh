#!/usr/bin/env bash
# =====================================================================
#  Подпись AutoQ.exe / AutoQ-Setup.exe (Authenticode) прямо на Linux.
#  Использование:  windows/sign.sh <файл.exe> [ещё файлы…]
#  Файл подписывается на месте. Без настроек подписи скрипт ничего не
#  делает (и не мешает сборке) — так собираются неподписанные версии.
#
#  Способ подписи — переменные окружения (нужен ОДИН из трёх):
#   1) Файл сертификата .pfx/.p12 (тестовый или старый сертификат):
#        SIGN_PFX=/путь/cert.pfx  SIGN_PASS=пароль
#   2) Токен или облачный ключ через PKCS#11 (Certum SimplySign, SafeNet, YubiKey):
#        SIGN_PKCS11_MODULE=/usr/lib/…/libXXX.so  SIGN_PKCS11_KEY='pkcs11:…'
#        SIGN_CERTS=/путь/сертификат-с-цепочкой.pem  [SIGN_PASS=PIN]
#   3) jsign (облака: Azure Artifact Signing, Google/AWS KMS, SSL.com eSigner…):
#        SIGN_JSIGN='--storetype TRUSTEDSIGNING --keystore … --storepass … --alias …'
#  Необязательно:
#        SIGN_TS=http://timestamp.digicert.com   — сервер меток времени
#                 (SIGN_TS=none — без метки, только для проверки без интернета)
#        SIGN_NAME="AutoQ"  SIGN_URL=https://…    — имя и сайт в окне подписи
#
#  Метка времени обязательна: без неё подпись «умирает» вместе со сроком
#  сертификата, с ней — остаётся действительной навсегда.
# =====================================================================
set -euo pipefail

TS="${SIGN_TS:-http://timestamp.digicert.com}"
NAME="${SIGN_NAME:-AutoQ}"
URL="${SIGN_URL:-}"

if [ -z "${SIGN_PFX:-}${SIGN_PKCS11_MODULE:-}${SIGN_JSIGN:-}" ]; then
    echo "sign.sh: подпись не настроена (нет SIGN_PFX / SIGN_PKCS11_MODULE / SIGN_JSIGN) — пропускаю"
    exit 0
fi
[ $# -gt 0 ] || { echo "использование: $0 <файл.exe> [...]" >&2; exit 2; }

for f in "$@"; do
    [ -f "$f" ] || { echo "sign.sh: нет файла $f" >&2; exit 1; }
    if [ -n "${SIGN_JSIGN:-}" ]; then
        command -v jsign >/dev/null || { echo "sign.sh: нужен jsign (https://ebourg.github.io/jsign/)" >&2; exit 1; }
        # shellcheck disable=SC2086
        if [ "$TS" = "none" ]; then
            jsign $SIGN_JSIGN --alg SHA-256 --name "$NAME" ${URL:+--url "$URL"} "$f"
        else
            jsign $SIGN_JSIGN --tsaurl "$TS" --tsmode RFC3161 --alg SHA-256 --name "$NAME" ${URL:+--url "$URL"} "$f"
        fi
    else
        command -v osslsigncode >/dev/null || { echo "sign.sh: нужен osslsigncode (pacman -S osslsigncode / apt install osslsigncode)" >&2; exit 1; }
        tmp="$f.signed.tmp"
        args=(sign -h sha256 -n "$NAME")
        [ "$TS" != "none" ] && args+=(-ts "$TS")
        [ -n "$URL" ] && args+=(-i "$URL")
        if [ -n "${SIGN_PFX:-}" ]; then
            args+=(-pkcs12 "$SIGN_PFX")
            [ -n "${SIGN_PASS:-}" ] && args+=(-pass "$SIGN_PASS")
        else
            args+=(-pkcs11module "$SIGN_PKCS11_MODULE" -key "$SIGN_PKCS11_KEY" -certs "$SIGN_CERTS")
            [ -n "${SIGN_PASS:-}" ] && args+=(-pass "$SIGN_PASS")
        fi
        rm -f "$tmp"
        osslsigncode "${args[@]}" -in "$f" -out "$tmp"
        mv -f "$tmp" "$f"
    fi
    echo "sign.sh: подписан $f"
done
