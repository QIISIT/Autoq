// Проверка значков AutoQ: все рисуются, отличаются друг от друга, своя картинка
// приводится к 128×128. Запуск: QT_QPA_PLATFORM=offscreen test_icons [лист.png]
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QCryptographicHash>
#include <QBuffer>
#include <cstdio>
#include "ui/style/icons.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int opaquePixels(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 40) ++n;
    return n;
}

static QByteArray hashOf(const QImage& img) {
    QImage c = img.convertToFormat(QImage::Format_ARGB32);
    return QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(c.constBits()),
                                               static_cast<int>(c.sizeInBytes())),
                                    QCryptographicHash::Sha1);
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    const QStringList glyphs = AqIcons::gameGlyphs();
    CHECK(glyphs.size() == 16);

    // стили значков (1.9): по умолчанию «Глянец», неизвестный -> «Глянец»
    CHECK(AqIcons::iconStyle() == "gloss");
    CHECK(AqIcons::iconStyles() == QStringList({"gloss", "duo", "neon", "classic"}));
    CHECK(AqIcons::normalizeIconStyle("nope") == "gloss");
    CHECK(AqIcons::iconStyleLabel("duo") == QString::fromUtf8("Иллюстрация"));
    {
        QSet<QByteArray> styled;
        for (const QString& st : AqIcons::iconStyles()) {
            CHECK(!AqIcons::iconStyleNote(st).isEmpty());
            for (const QString& g : glyphs) {
                for (qreal dpr : {1.0, 2.0}) {
                    const QPixmap pm = AqIcons::gameTileStyled(st, g, QColor("#6FD3F2"), {}, 34, dpr, false, false);
                    CHECK(pm.size() == QSize(qRound(34 * dpr), qRound(34 * dpr)));
                    const QImage img = pm.toImage();
                    CHECK(opaquePixels(img) > img.width() * img.height() / 2);
                    if (dpr == 1.0) styled.insert(hashOf(img));
                }
            }
            // выбранная игра отличается рамкой
            CHECK(hashOf(AqIcons::gameTileStyled(st, "cube", QColor("#6EDC8C"), {}, 34, 1, true, false).toImage()) !=
                  hashOf(AqIcons::gameTileStyled(st, "cube", QColor("#6EDC8C"), {}, 34, 1, false, false).toImage()));
            // неизвестный значок -> геймпад
            CHECK(hashOf(AqIcons::gameTileStyled(st, "nope", QColor("#FF5A66"), {}, 34, 1, false, false).toImage()) ==
                  hashOf(AqIcons::gameTileStyled(st, "gamepad", QColor("#FF5A66"), {}, 34, 1, false, false).toImage()));
        }
        CHECK(styled.size() == glyphs.size() * AqIcons::iconStyles().size());   // все рисунки и стили разные
        // gameTile рисует текущим стилем
        AqIcons::setIconStyle("neon");
        CHECK(hashOf(AqIcons::gameTile("crown", QColor("#F2C66D"), {}, 40, 1, false, false).toImage()) ==
              hashOf(AqIcons::gameTileStyled("neon", "crown", QColor("#F2C66D"), {}, 40, 1, false, false).toImage()));
        AqIcons::setIconStyle("bad-value");
        CHECK(AqIcons::iconStyle() == "gloss");
    }

    // дальше — классический стиль (плитки зависят от светлой/тёмной темы)
    AqIcons::setIconStyle("classic");
    QSet<QByteArray> hashes;
    for (const QString& g : glyphs) {
        CHECK(AqIcons::isGlyph(g));
        CHECK(AqIcons::glyphLabel(g) != g);           // есть русское название
        for (bool light : {false, true}) {
            const QImage img = AqIcons::gameTile(g, QColor("#F2C66D"), {}, 48, 1.0, false, light).toImage();
            CHECK(img.size() == QSize(48, 48));
            CHECK(opaquePixels(img) > 48 * 48 / 2);   // плитка закрашена
            hashes.insert(hashOf(img));
        }
        // значок без подложки: что-то нарисовано, но не всё
        const QImage glyph = AqIcons::uiPixmap(g, Qt::white, 24, 1.0, 1.8).toImage();
        const int px = opaquePixels(glyph);
        CHECK(px > 20 && px < 24 * 24 * 3 / 4);
    }
    CHECK(hashes.size() == glyphs.size() * 2);          // все разные (и в тёмной, и в светлой теме)

    // неизвестный значок -> геймпад; активная плитка отличается рамкой
    CHECK(!AqIcons::isGlyph("genshin-logo"));
    CHECK(hashOf(AqIcons::gameTile("nope", QColor("#FF5A66"), {}, 34, 1, false, false).toImage()) ==
          hashOf(AqIcons::gameTile("gamepad", QColor("#FF5A66"), {}, 34, 1, false, false).toImage()));
    CHECK(hashOf(AqIcons::gameTile("cube", QColor("#6EDC8C"), {}, 34, 1, true, false).toImage()) !=
          hashOf(AqIcons::gameTile("cube", QColor("#6EDC8C"), {}, 34, 1, false, false).toImage()));
    // HiDPI: размер в пикселях устройства
    const QPixmap hi = AqIcons::gameTile("star", QColor("#B9A8FF"), {}, 34, 2.0, false, false);
    CHECK(hi.width() == 68 && hi.devicePixelRatio() == 2.0);

    // палитра: 8 цветов с названиями
    CHECK(AqIcons::palette().size() == 8);
    for (const QString& c : AqIcons::palette()) {
        CHECK(QColor(c).isValid());
        CHECK(AqIcons::paletteLabel(c) != c);
    }

    // своя картинка: прямоугольная -> квадрат 128×128 PNG, рисуется в плитке
    QImage photo(300, 200, QImage::Format_RGB32);
    photo.fill(QColor("#3366FF"));
    const QByteArray png = AqIcons::normalizeImage(photo);
    CHECK(png.startsWith("\x89PNG"));
    QImage back;
    CHECK(back.loadFromData(png));
    CHECK(back.size() == QSize(128, 128));
    const QImage withImg = AqIcons::gameTile("compass", QColor("#F2C66D"), png, 56, 1, false, false).toImage();
    const QRgb mid = withImg.pixel(28, 28);
    CHECK(qBlue(mid) > 200 && qRed(mid) < 90);          // в центре — синяя картинка, а не компас
    CHECK(AqIcons::normalizeImage(QImage()).isEmpty());
    // битые байты картинки -> обычный значок
    CHECK(hashOf(AqIcons::gameTile("compass", QColor("#F2C66D"), QByteArray("garbage"), 34, 1, false, false).toImage()) ==
          hashOf(AqIcons::gameTile("compass", QColor("#F2C66D"), {}, 34, 1, false, false).toImage()));

    // логотип: все размеры, серый (стоп) отличается от красного (работает)
    for (int s : {16, 20, 24, 32, 48, 64, 128, 256}) {
        const QImage red = AqIcons::logo(s, 1.0, false).toImage();
        const QImage grey = AqIcons::logo(s, 1.0, true).toImage();
        CHECK(red.size() == QSize(s, s));
        CHECK(opaquePixels(red) > s * s / 2);
        CHECK(hashOf(red) != hashOf(grey));
        const QRgb c = red.pixel(s / 6, s / 2);        // край клавиши — красный
        CHECK(qRed(c) > 150 && qGreen(c) < 80);
    }
    CHECK(!AqIcons::appIcon().isNull());
    CHECK(AqIcons::appIcon().availableSizes().size() >= 6);

    // значки интерфейса
    for (const QString& n : AqIcons::uiNames()) {
        const QImage img = AqIcons::uiPixmap(n, Qt::white, 24, 1.0).toImage();
        CHECK(opaquePixels(img) > 4);
        CHECK(!AqIcons::ui(n, Qt::white).isNull());
    }
    CHECK(AqIcons::uiNames().contains("target") && AqIcons::uiNames().contains("loop"));

    // --mac-icon-dir <папка>: PNG для AutoQ.icns (значок macOS: клавиша на сетке 1024
    // с полями ~10%, как у системных значков)
    if (argc > 2 && QString(argv[1]) == "--mac-icon-dir") {
        for (int s : {16, 32, 64, 128, 256, 512, 1024}) {
            QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            const int inner = qRound(s * 0.82);
            p.drawPixmap(QRect((s - inner) / 2, (s - inner) / 2, inner, inner), AqIcons::logo(inner));
            p.end();
            img.save(QString("%1/mac-%2.png").arg(QString::fromLocal8Bit(argv[2])).arg(s, 4, 10, QChar('0')));
        }
        return 0;
    }
    // --logo-dir <папка>: PNG логотипа для app.ico (сборка Windows)
    if (argc > 2 && QString(argv[1]) == "--logo-dir") {
        for (int s : {16, 20, 24, 32, 40, 48, 64, 128, 256})
            AqIcons::logo(s).save(QString("%1/logo-%2.png").arg(QString::fromLocal8Bit(argv[2])).arg(s, 3, 10, QChar('0')));
        return 0;
    }
    // лист для просмотра глазами
    if (argc > 1) {
        const int cell = 72;
        QImage sheet(cell * 8 + 16, cell * 5 + 16, QImage::Format_ARGB32);
        sheet.fill(QColor("#0B0B0D"));
        QPainter p(&sheet);
        const QStringList pal = AqIcons::palette();
        for (int i = 0; i < glyphs.size(); ++i)
            p.drawPixmap(8 + (i % 8) * cell + 4, 8 + (i / 8) * cell + 4,
                         AqIcons::gameTile(glyphs[i], QColor(pal[i % 8]), {}, 64, 1, i == 0, false));
        for (int i = 0; i < glyphs.size(); ++i)
            p.drawPixmap(8 + (i % 8) * cell + 4, 8 + (2 + i / 8) * cell + 4,
                         AqIcons::gameTile(glyphs[i], QColor(pal[i % 8]), {}, 64, 1, false, true));
        const int sizes[] = {256, 128, 64, 32, 24, 16};
        int x = 8;
        for (int s : {64, 48, 32, 24, 16}) { p.drawPixmap(x, 8 + 4 * cell + (64 - s), AqIcons::logo(s)); x += s + 12; }
        for (int s : {32, 16}) { p.drawPixmap(x, 8 + 4 * cell + (64 - s), AqIcons::logo(s, 1, true)); x += s + 12; }
        (void)sizes;
        int ux = x + 10;
        for (const QString& n : AqIcons::uiNames()) {
            p.drawPixmap(ux, 8 + 4 * cell + 20, AqIcons::uiPixmap(n, QColor("#D6D6DB"), 20));
            ux += 26;
            if (ux > sheet.width() - 26) break;
        }
        p.end();
        sheet.save(QString::fromLocal8Bit(argv[1]));
    }

    std::printf("\n%d проверок пройдено, %d провалено\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
