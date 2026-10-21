# Подпись AutoQ и предупреждения Windows / macOS

## Windows
* Окно SmartScreen «Windows защитил ваш компьютер» у новой неподписанной программы — норма:
  «Подробнее → Выполнить в любом случае». С 2024 года даже подписанные (OV и EV) файлы
  показывают его, пока у издателя мало скачиваний.
* Сертификаты Certum, DigiCert, Sectigo и Azure Artifact Signing жителям России сейчас не выдают.
* Бесплатно и сразу:
  1. Каждую новую сборку отправлять в Microsoft как ложное срабатывание:
     https://www.microsoft.com/wdsi/filesubmission → Software developer → Incorrectly detected.
  2. winget: `packaging/winget/make-manifest.sh <версия> <ссылка на AutoQ-Setup.exe>`
     (установщик поддерживает тихую установку `/S`).
  3. Microsoft Store (регистрация физлица бесплатна) — Store сам подписывает пакет MSIX.
  4. Когда у проекта будет аудитория: SignPath Foundation (бесплатно для open source,
     нужна OSI-лицензия и сборка в CI).
* Когда появится сертификат — сборка подписывает всё сама:
  `SIGN_PFX=cert.pfx SIGN_PASS=… windows/build-from-linux.sh`
  (или токен PKCS#11 / jsign — см. `windows/sign.sh`). Подписываются AutoQ.exe,
  деинсталлятор и AutoQ-Setup.exe, с меткой времени.

## macOS
* Без Apple Developer ($99/год, из России сейчас почти не оплатить) окно «не удалось проверить
  разработчика» не убрать. Инструкция для пользователей — `macos/README_MAC.txt`.
* Сборка — `macos/build-mac.sh` на Маке или GitHub Actions (`.github/workflows/macos.yml`).
* `macos/make-cert.sh` — бесплатный самоподписанный сертификат: с ним разрешения
  «Универсальный доступ» не слетают после обновлений. Ключ не выкладывать в репозиторий.
