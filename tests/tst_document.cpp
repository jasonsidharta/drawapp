// Undo history, project files and PNG export, without any UI.

#include "ProjectFile.h"
#include "RasterDocument.h"
#include "TileSnapshot.h"
#include "UndoHistory.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {

constexpr qsizetype TileBytes = qsizetype(TileSnapshot::TileSize) * TileSnapshot::TileSize * 4;

// Paints one dab and records it as an undo step. Returns false if nothing changed.
bool paintStep(RasterDocument &doc, UndoHistory &history, QPointF center, qreal diameter, QColor color,
               BrushMode mode = BrushMode::Paint)
{
    TileSnapshot before;
    doc.stampDabs({{center, diameter}}, color, mode, &before);
    before.dropUnchanged(doc.image());
    if (before.isEmpty())
        return false;
    history.push(std::move(before), QStringLiteral("stroke"));
    return true;
}

// Random image with valid premultiplied pixels (each color channel <= alpha), including
// fully transparent and partially transparent pixels.
QImage randomPremultiplied(int w, int h, quint32 seed)
{
    QRandomGenerator rng(seed);
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = (x + y) % 7 == 0 ? 0 : int(rng.bounded(256));
            line[x] = qRgba(int(rng.bounded(a + 1)), int(rng.bounded(a + 1)), int(rng.bounded(a + 1)), a);
        }
    }
    return image;
}

// Builds a project file from explicit header fields, recomputing the checksum unless told not to.
QByteArray makeFile(quint32 version, quint32 w, quint32 h, quint32 format, quint32 compression,
                    const QByteArray &payload, bool validChecksum = true)
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("DRAWAPP\n", 8);
    s << version << w << h << format << compression << quint64(payload.size());
    QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    if (!validChecksum)
        digest[0] = char(digest[0] ^ 0xff);
    s.writeRawData(digest.constData(), 32);
    out.append(payload);
    return out;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    // --- undo / redo ---
    void strokeUndoRedoRestoresPixels()
    {
        RasterDocument doc(QSize(300, 200));
        UndoHistory history;
        const QImage empty = doc.image().copy();
        TileSnapshot before;
        doc.stampDabs({{QPointF(50, 50), 20}, {QPointF(150, 120), 30}}, Qt::red, BrushMode::Paint, &before);
        history.push(std::move(before), QStringLiteral("Brush stroke"));
        const QImage painted = doc.image().copy();
        QVERIFY(painted != empty);

        QVERIFY(!doc.undo(history).isEmpty());
        QCOMPARE(doc.image(), empty);
        QVERIFY(history.canRedo());
        QVERIFY(!doc.redo(history).isEmpty());
        QCOMPARE(doc.image(), painted);
        QCOMPARE(history.undoLabel(), QStringLiteral("Brush stroke"));
    }

    void eraserUndoRestoresPaint()
    {
        RasterDocument doc(QSize(200, 200));
        UndoHistory history;
        QVERIFY(paintStep(doc, history, QPointF(100, 100), 80, Qt::blue));
        const QImage painted = doc.image().copy();
        QVERIFY(paintStep(doc, history, QPointF(100, 100), 30, Qt::black, BrushMode::Erase));
        QCOMPARE(qAlpha(doc.image().pixel(100, 100)), 0);
        doc.undo(history);
        QCOMPARE(doc.image(), painted);
        doc.redo(history);
        QCOMPARE(qAlpha(doc.image().pixel(100, 100)), 0);
    }

    void clearUndoRestoresArtwork()
    {
        RasterDocument doc(QSize(500, 300));
        UndoHistory history;
        QVERIFY(paintStep(doc, history, QPointF(30, 30), 20, Qt::green));
        QVERIFY(paintStep(doc, history, QPointF(470, 270), 20, Qt::green));
        const QImage painted = doc.image().copy();
        TileSnapshot before;
        doc.clear(&before);
        QCOMPARE(before.tileCount(), 2); // only painted tiles are kept
        history.push(std::move(before), QStringLiteral("Clear canvas"));
        doc.undo(history);
        QCOMPARE(doc.image(), painted);
        doc.redo(history);
        QCOMPARE(qAlpha(doc.image().pixel(30, 30)), 0);
        QCOMPARE(qAlpha(doc.image().pixel(470, 270)), 0);
    }

    void newEditInvalidatesRedo()
    {
        RasterDocument doc(QSize(200, 200));
        UndoHistory history;
        QVERIFY(paintStep(doc, history, QPointF(30, 30), 20, Qt::red));
        const QImage afterFirst = doc.image().copy();
        QVERIFY(paintStep(doc, history, QPointF(150, 150), 20, Qt::red));
        doc.undo(history);
        QVERIFY(history.canRedo());
        QVERIFY(paintStep(doc, history, QPointF(150, 30), 20, Qt::blue));
        QVERIFY(!history.canRedo());
        QCOMPARE(history.redoCount(), 0);
        QVERIFY(doc.redo(history).isEmpty());
        doc.undo(history);
        QCOMPARE(doc.image(), afterFirst);
    }

    void snapshotsStoreOnlyChangedTiles()
    {
        RasterDocument doc; // 2048x1536
        TileSnapshot small;
        doc.stampDabs({{QPointF(1000, 700), 10}}, Qt::black, BrushMode::Paint, &small);
        QVERIFY(small.tileCount() <= 4);

        // A full-width diagonal stroke covers the whole canvas bounding box but few tiles.
        std::vector<Dab> diagonal;
        for (int i = 0; i <= 2000; i += 2)
            diagonal.push_back({QPointF(20 + i, 20 + i * 0.74), 12});
        TileSnapshot stroke;
        doc.stampDabs(diagonal, Qt::black, BrushMode::Paint, &stroke);
        const qsizetype fullCanvas = qsizetype(2048) * 1536 * 4;
        QVERIFY2(stroke.byteSize() < fullCanvas / 10,
                 qPrintable(QString("%1 bytes for a diagonal stroke").arg(stroke.byteSize())));
    }

    void unchangedTilesAreDropped()
    {
        RasterDocument doc(QSize(200, 200));
        TileSnapshot before;
        doc.stampDabs({{QPointF(100, 100), 50}}, Qt::black, BrushMode::Erase, &before); // erase nothing
        QVERIFY(!before.isEmpty());
        before.dropUnchanged(doc.image());
        QVERIFY(before.isEmpty());
        QCOMPARE(before.byteSize(), 0);
    }

    void budgetEvictsOldestUndoEntries()
    {
        RasterDocument doc(QSize(640, 64));
        UndoHistory history(3 * TileBytes);
        for (int i = 0; i < 5; ++i)
            QVERIFY(paintStep(doc, history, QPointF(32 + 128 * i, 32), 10, Qt::red)); // one tile each
        QCOMPARE(history.undoCount(), 3);
        QVERIFY(history.bytesUsed() <= history.budgetBytes());
        QCOMPARE(history.bytesUsed(), 3 * TileBytes);

        for (int i = 0; i < 3; ++i)
            QVERIFY(!doc.undo(history).isEmpty());
        QVERIFY(!history.canUndo());
        // The two evicted edits stay on the canvas; the three newest were undone.
        QCOMPARE(qAlpha(doc.image().pixel(32, 32)), 255);
        QCOMPARE(qAlpha(doc.image().pixel(160, 32)), 255);
        for (int i = 2; i < 5; ++i)
            QCOMPARE(qAlpha(doc.image().pixel(32 + 128 * i, 32)), 0);
    }

    void budgetCountsRedoEntries()
    {
        RasterDocument doc(QSize(640, 64));
        UndoHistory history(3 * TileBytes);
        for (int i = 0; i < 3; ++i)
            paintStep(doc, history, QPointF(32 + 128 * i, 32), 10, Qt::red);
        doc.undo(history);
        doc.undo(history);
        QCOMPARE(history.redoCount(), 2);
        QCOMPARE(history.bytesUsed(), 3 * TileBytes); // redo data still counts
        paintStep(doc, history, QPointF(32 + 128 * 4, 32), 10, Qt::blue);
        QCOMPARE(history.redoCount(), 0);
        QCOMPARE(history.undoCount(), 2);
        QCOMPARE(history.bytesUsed(), 2 * TileBytes);
    }

    void editLargerThanBudgetIsNotUndoable()
    {
        RasterDocument doc(QSize(256, 256));
        UndoHistory history(TileBytes);
        const quint64 start = history.stateId();
        QVERIFY(paintStep(doc, history, QPointF(128, 128), 200, Qt::red)); // spans many tiles
        QVERIFY(!history.canUndo());
        QCOMPARE(history.bytesUsed(), 0);
        QVERIFY(history.stateId() != start); // still counts as a change
        QCOMPARE(qAlpha(doc.image().pixel(128, 128)), 255);
    }

    void stateIdMatchesSavedStateAfterUndoAndEviction()
    {
        RasterDocument doc(QSize(640, 64));
        UndoHistory history(2 * TileBytes);
        paintStep(doc, history, QPointF(32, 32), 10, Qt::red);
        const quint64 saved = history.stateId();
        const QImage savedPixels = doc.image().copy();
        paintStep(doc, history, QPointF(160, 32), 10, Qt::red);
        QVERIFY(history.stateId() != saved);
        paintStep(doc, history, QPointF(288, 32), 10, Qt::red); // evicts the saved edit's entry
        QCOMPARE(history.undoCount(), 2);
        doc.undo(history);
        QVERIFY(history.stateId() != saved);
        doc.undo(history);
        QCOMPARE(history.stateId(), saved); // back at the saved pixels
        QCOMPARE(doc.image(), savedPixels);
        doc.redo(history);
        QVERIFY(history.stateId() != saved);
        history.clear();
        QVERIFY(!history.canUndo() && !history.canRedo());
        QCOMPARE(history.bytesUsed(), 0);
    }

    // --- project files ---
    void roundTripPreservesSizeColorsAndAlpha()
    {
        const QImage image = randomPremultiplied(37, 23, 7);
        const ProjectFile::LoadResult result = ProjectFile::decode(ProjectFile::encode(image));
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.image.size(), QSize(37, 23));
        QCOMPARE(result.image.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(result.image, image);
    }

    void roundTripThroughFile()
    {
        QTemporaryDir dir;
        RasterDocument doc;
        doc.stampDabs({{QPointF(100, 100), 40}}, QColor(10, 200, 30), BrushMode::Paint);
        doc.stampDabs({{QPointF(110, 100), 12}}, Qt::black, BrushMode::Erase); // partial alpha edges
        const QString path = dir.filePath("art.drawapp");
        QCOMPARE(ProjectFile::save(doc.image(), path), QString());
        const ProjectFile::LoadResult result = ProjectFile::load(path);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.image.size(), QSize(2048, 1536));
        QCOMPARE(result.image, doc.image());
        QVERIFY(QFileInfo(path).size() < 200 * 1024); // mostly transparent canvas compresses well
    }

    void rejectsDamagedFiles_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("expected");

        const QImage image = randomPremultiplied(16, 8, 3);
        const QByteArray good = ProjectFile::encode(image);
        const QByteArray payload = good.mid(ProjectFile::HeaderSize);

        QTest::newRow("empty") << QByteArray() << "not a DrawApp project";
        QTest::newRow("png file") << QByteArray("\x89PNG\r\n\x1a\n0000000000", 18) << "not a DrawApp project";
        QTest::newRow("header cut short") << good.left(40) << "damaged";
        QTest::newRow("payload truncated") << good.left(good.size() - 5) << "damaged";
        QTest::newRow("trailing bytes") << good + QByteArray("xx") << "damaged";
        QByteArray flipped = good;
        flipped[ProjectFile::HeaderSize + 10] = char(flipped[ProjectFile::HeaderSize + 10] ^ 0x55);
        QTest::newRow("payload byte flipped") << flipped << "checksum";
        QTest::newRow("newer version") << makeFile(2, 16, 8, 1, 1, payload) << "newer version";
        QTest::newRow("version zero") << makeFile(0, 16, 8, 1, 1, payload) << "damaged";
        QTest::newRow("zero width") << makeFile(1, 0, 8, 1, 1, payload) << "damaged";
        QTest::newRow("huge dimensions") << makeFile(1, 100000, 100000, 1, 1, payload) << "too large";
        QTest::newRow("too many pixels") << makeFile(1, 16384, 16384, 1, 1, payload) << "too large";
        QTest::newRow("unknown pixel format") << makeFile(1, 16, 8, 7, 1, payload) << "pixel format";
        QTest::newRow("unknown compression") << makeFile(1, 16, 8, 1, 9, payload) << "pixel format";
        QTest::newRow("bad checksum") << makeFile(1, 16, 8, 1, 1, payload, false) << "checksum";
        // Header says 32x32 but the pixel data is for 16x8: rejected before decompressing.
        QTest::newRow("size mismatch") << makeFile(1, 32, 32, 1, 1, payload) << "does not match";
        // A compressed stream claiming a huge raw size must not be decompressed.
        QByteArray liar = payload;
        qToBigEndian<quint32>(0x7fffffff, liar.data());
        QTest::newRow("claims huge raw size") << makeFile(1, 16, 8, 1, 1, liar) << "does not match";
        QByteArray garbage(4, '\0');
        qToBigEndian<quint32>(16 * 8 * 4, garbage.data());
        garbage.append("not zlib data at all");
        QTest::newRow("corrupt zlib") << makeFile(1, 16, 8, 1, 1, garbage) << "decompressed";
        // Valid zlib of pixels that break the premultiplied rule (red 200 > alpha 10).
        QByteArray invalidPixels;
        for (int i = 0; i < 16 * 8; ++i)
            invalidPixels.append(QByteArray::fromHex("c800000a"));
        QTest::newRow("invalid premultiplied pixels") << makeFile(1, 16, 8, 1, 1, qCompress(invalidPixels)) << "invalid pixel";
    }

    void rejectsDamagedFiles()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, expected);
        const ProjectFile::LoadResult result = ProjectFile::decode(data);
        QVERIFY(!result.ok());
        QVERIFY(result.image.isNull());
        QVERIFY2(result.error.contains(expected, Qt::CaseInsensitive), qPrintable(result.error));
    }

    void rejectsMissingAndOversizedFiles()
    {
        QTemporaryDir dir;
        QVERIFY(!ProjectFile::load(dir.filePath("missing.drawapp")).ok());

        const QString big = dir.filePath("big.drawapp");
        {
            QFile f(big);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("DRAWAPP\n");
            QVERIFY(f.resize(ProjectFile::MaxFileBytes + 1)); // sparse/zero-filled, not written
        }
        QElapsedTimer timer;
        timer.start();
        const ProjectFile::LoadResult result = ProjectFile::load(big);
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains("too large"), qPrintable(result.error));
        QVERIFY(timer.elapsed() < 2000); // rejected from the size, not by reading it
    }

    void failedSaveKeepsPreviousFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("keep.drawapp");
        const QImage first = randomPremultiplied(40, 30, 1);
        QCOMPARE(ProjectFile::save(first, path), QString());
        const QByteArray original = readAll(path);

        // The target cannot be replaced (read-only): the save fails and the old file survives.
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser));
        const QString error = ProjectFile::save(randomPremultiplied(40, 30, 2), path);
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(!error.isEmpty());
        QCOMPARE(readAll(path), original);
        QCOMPARE(ProjectFile::load(path).image, first);
        // No temporary files are left behind.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"keep.drawapp"});

        // Other failures: missing folder, and a folder in place of the file.
        QVERIFY(!ProjectFile::save(first, dir.filePath("no/such/folder/x.drawapp")).isEmpty());
        QVERIFY(QDir(dir.path()).mkdir("folder.drawapp"));
        QVERIFY(!ProjectFile::save(first, dir.filePath("folder.drawapp")).isEmpty());
    }

    void successfulSaveReplacesFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("replace.drawapp");
        QCOMPARE(ProjectFile::save(randomPremultiplied(10, 10, 1), path), QString());
        const QImage second = randomPremultiplied(20, 5, 2);
        QCOMPARE(ProjectFile::save(second, path), QString());
        QCOMPARE(ProjectFile::load(path).image, second);
    }

    // --- PNG export ---
    void pngExportMatchesDocumentPixels()
    {
        QTemporaryDir dir;
        const QImage image = randomPremultiplied(64, 48, 11);
        const QString path = dir.filePath("out.png");
        QCOMPARE(ProjectFile::exportPng(image, path), QString());
        QImage png(path);
        QVERIFY(!png.isNull());
        QCOMPARE(png.size(), image.size());
        QVERIFY(png.hasAlphaChannel());
        // PNG stores straight alpha: compare in straight ARGB32.
        QCOMPARE(png.convertToFormat(QImage::Format_ARGB32), image.convertToFormat(QImage::Format_ARGB32));
    }
};

QTEST_GUILESS_MAIN(TestDocument)
#include "tst_document.moc"
