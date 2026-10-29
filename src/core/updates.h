#pragma once
// Обновления без сети и интерфейса: разбор ответа GitHub о последнем релизе,
// сравнение версий, выбор файла релиза под эту копию AutoQ, SHA-256.
// Скачивание и установка — в services/updater.cpp.
#include <QByteArray>
#include <QList>
#include <QString>

namespace Updates {
    QString latestApiUrl();               // API GitHub: последний релиз (без пререлизов и черновиков)
    QString releasesPageUrl();            // страница всех релизов

    struct Asset {
        QString name;
        QString url;                      // browser_download_url
        qint64 size = 0;
        QString sha256;                   // из поля digest ("sha256:…"), строчными; бывает пустым
    };
    struct Release {
        QString tag;                      // "v1.17.1" — по нему сравниваются версии
        QString version;                  // для показа: "1.17.1 beta"
        QString page;                     // страница релиза
        QList<Asset> assets;
        bool valid() const { return !tag.isEmpty(); }
    };
    // Ответ /releases/latest -> Release. Черновик, пререлиз, нет номера — пустой (error — почему)
    Release parseRelease(const QByteArray& json, QString* error = nullptr);

    // Номер из строки: "1.17.0 beta" / "v1.17.1" -> {1, 17, 1}; нет цифр — пусто
    QList<int> versionNumbers(const QString& text);
    // <0 — a старше b, 0 — одна версия, >0 — a новее. Недостающие части — 0 (1.17 = 1.17.0).
    // При одном номере тестовая сборка («1.18.0 beta test») старше релиза («v1.18.0»)
    int compareVersions(const QString& a, const QString& b);
    bool isNewer(const QString& candidate, const QString& current);

    // Какой файл релиза ставит эта копия AutoQ. None — только сказать о новой версии
    // (сборка из папки build, программа в системной папке и т. п.)
    enum class Package { None, Setup, Portable, Source, Dmg };
    const Asset* pickAsset(const Release& r, Package p);
    QString packageKey(Package p);        // для settings.ini: "setup", "portable", "source", "dmg"
    Package packageFromKey(const QString& key);

    QString sha256OfFile(const QString& path);                     // строчные hex; пусто — не прочитать
    QString sumFor(const QByteArray& sums, const QString& fileName);   // строка из SHA256SUMS.txt
}
