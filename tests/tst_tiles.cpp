// Milestone 10: sparse tiled storage (TiledImage), copy-on-write snapshots, concurrent readers,
// allocation failure and the memory budget. Headless, against the production classes.

#include "MemoryBudget.h"
#include "TiledImage.h"

#include <QPainter>
#include <QRandomGenerator>
#include <QtTest>
#include <atomic>
#include <thread>
#include <vector>

namespace {

QImage pattern(QSize size, quint32 seed)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QRandomGenerator rng(seed);
    for (int y = 0; y < size.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const int a = int(rng.bounded(256));
            row[x] = a == 0 ? 0 : qRgba(int(rng.bounded(a + 1)), int(rng.bounded(a + 1)), int(rng.bounded(a + 1)), a);
        }
    }
    return img;
}

quint64 checksum(const TiledImage &img)
{
    quint64 h = 1469598103934665603ull;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            h = (h ^ img.pixel(x, y)) * 1099511628211ull;
    return h;
}

struct BudgetGuard
{
    ~BudgetGuard()
    {
        MemoryBudget::setLimit(0);
        MemoryBudget::failReservationsForTesting(-1, 0);
    }
};

} // namespace

class TestTiles : public QObject
{
    Q_OBJECT

    static void tileSizes_data()
    {
        QTest::addColumn<int>("tile");
        QTest::newRow("64") << 64;
        QTest::newRow("128") << 128;
        QTest::newRow("256") << 256;
    }

private slots:
    void emptyGiantImageAllocatesNothing()
    {
        const qint64 before = MemoryBudget::usedTotal();
        TiledImage img(QSize(16384, 16384));
        QCOMPARE(img.allocatedTileCount(), 0);
        QCOMPARE(MemoryBudget::usedTotal(), before);
        QCOMPARE(img.pixel(8000, 9000), QRgb(0));
        QCOMPARE(img.pixel(16383, 16383), QRgb(0));
        QVERIFY(img.copy(QRect(100, 100, 64, 64)).pixel(5, 5) == 0);
        QVERIFY(img.contentBounds().isEmpty());
        QCOMPARE(img.columns(), 16384 / TiledImage::DefaultTileSize);
    }

    void writesAllocateOnlyTheTouchedTile_data() { tileSizes_data(); }
    void writesAllocateOnlyTheTouchedTile()
    {
        QFETCH(int, tile);
        const qint64 before = MemoryBudget::used(MemoryBudget::Category::Document);
        TiledImage img(QSize(4000, 3000), tile);
        QVERIFY(img.setPixel(1000, 1000, qRgba(10, 20, 30, 255)));
        QCOMPARE(img.allocatedTileCount(), 1);
        QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document) - before, qint64(tile) * tile * 4);
        QCOMPARE(img.pixel(1000, 1000), qRgba(10, 20, 30, 255));
        QCOMPARE(img.pixel(1001, 1000), QRgb(0));
        QCOMPARE(img.allocatedBounds(), img.tileRect(1000 / tile, 1000 / tile));
        QCOMPARE(img.contentBounds(), QRect(1000, 1000, 1, 1));
        // Writing transparency where there is no tile allocates nothing.
        QVERIFY(img.setPixel(10, 10, 0));
        QCOMPARE(img.allocatedTileCount(), 1);
        QImage clear(300, 300, QImage::Format_ARGB32_Premultiplied);
        clear.fill(Qt::transparent);
        QVERIFY(img.write(QPoint(2000, 2000), clear));
        QCOMPARE(img.allocatedTileCount(), 1);
    }

    void negativeAndOutOfBoundsCoordinates()
    {
        TiledImage img(QSize(100, 70));
        QCOMPARE(img.pixel(-1, 0), QRgb(0));
        QCOMPARE(img.pixel(0, -1), QRgb(0));
        QCOMPARE(img.pixel(100, 0), QRgb(0));
        QVERIFY(img.setPixel(-5, -5, 0xff000000));
        QVERIFY(img.setPixel(500, 5, 0xff000000));
        QCOMPARE(img.allocatedTileCount(), 0);
        // A write that starts left of and above the image is clipped.
        QImage src(40, 40, QImage::Format_ARGB32_Premultiplied);
        src.fill(qRgba(0, 0, 255, 255));
        QVERIFY(img.write(QPoint(-20, -30), src));
        QCOMPARE(img.pixel(0, 0), qRgba(0, 0, 255, 255));
        QCOMPARE(img.pixel(19, 9), qRgba(0, 0, 255, 255));
        QCOMPARE(img.pixel(20, 9), QRgb(0));
        QCOMPARE(img.pixel(19, 10), QRgb(0));
        // Pixels of an edge tile beyond the image stay zero and never read back.
        QCOMPARE(img.copy(QRect(90, 60, 20, 20)).pixel(15, 15), QRgb(0));
    }

    void roundTripAcrossTileBoundaries_data() { tileSizes_data(); }
    void roundTripAcrossTileBoundaries()
    {
        QFETCH(int, tile);
        const QImage src = pattern(QSize(1000, 700), 7); // not a multiple of any tile size
        const TiledImage img = TiledImage::fromImage(src, tile);
        QVERIFY(!img.isNull());
        QCOMPARE(img.toImage(), src);
        QVERIFY(img == src);
        // A region straddling four tiles, partly outside the image.
        const QRect r(tile - 13, tile - 7, 2 * tile, 2 * tile);
        QImage expect(r.size(), QImage::Format_ARGB32_Premultiplied);
        expect.fill(Qt::transparent);
        QPainter(&expect).drawImage(QPoint(0, 0), src, r & src.rect());
        QCOMPARE(img.copy(r), expect);
        // Writing back a region at an offset across boundaries.
        TiledImage copy(QSize(1000, 700), tile);
        QVERIFY(copy.write(QPoint(tile - 3, tile - 5), src.copy(0, 0, 300, 250)));
        for (int y = 0; y < 250; y += 7)
            for (int x = 0; x < 300; x += 7)
                QCOMPARE(copy.pixel(tile - 3 + x, tile - 5 + y), src.pixel(x, y));
    }

    void pruneReleasesTransparentTilesButNotSharedOnes()
    {
        TiledImage img(QSize(512, 512));
        QVERIFY(img.fillRect(QRect(0, 0, 512, 512), qRgba(1, 2, 3, 255)));
        QCOMPARE(img.allocatedTileCount(), 64);
        const TiledImage snapshot = img;
        const qint64 before = MemoryBudget::used(MemoryBudget::Category::Document);
        QVERIFY(img.fillRect(QRect(0, 0, 256, 512), 0)); // whole tiles: released at once
        QCOMPARE(img.allocatedTileCount(), 32);
        // The snapshot still owns them: nothing was freed.
        QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document), before);
        QCOMPARE(snapshot.pixel(10, 10), qRgba(1, 2, 3, 255));
        // Partly clearing a tile keeps it until pruned.
        QVERIFY(img.fillRect(QRect(256, 0, 63, 64), 0));
        QVERIFY(img.fillRect(QRect(319, 0, 1, 64), 0));
        QCOMPARE(img.allocatedTileCount(), 32);
        QCOMPARE(img.pruneTransparent(), 1);
        QCOMPARE(img.allocatedTileCount(), 31);
        QCOMPARE(img.pruneTransparent(), 0);
    }

    void snapshotsShareTilesAndCopyOnlyWhatChanges()
    {
        TiledImage img = TiledImage::fromImage(pattern(QSize(640, 640), 3));
        const int tiles = img.allocatedTileCount();
        const qint64 before = MemoryBudget::used(MemoryBudget::Category::Document);
        TiledImage a = img, b = img;
        QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document), before); // O(1), no pixels copied
        for (int r = 0; r < img.rows(); ++r)
            for (int c = 0; c < img.columns(); ++c)
                QVERIFY(img.sharesTileWith(a, c, r) || !img.hasTile(c, r));
        const quint64 sumA = checksum(a);
        QVERIFY(img.setPixel(70, 70, qRgba(9, 9, 9, 9)));
        QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document) - before, qint64(img.tileBytes())); // one tile copied
        QVERIFY(!img.sharesTileWith(a, 1, 1));
        QVERIFY(img.sharesTileWith(a, 0, 0));
        QCOMPARE(checksum(a), sumA); // snapshots unchanged
        QCOMPARE(checksum(b), sumA);
        QVERIFY(a == b);
        QVERIFY(img != a);
        QCOMPARE(img.allocatedTileCount(), tiles);
        // Destroying snapshots in any order frees exactly what only they held.
        a = TiledImage();
        QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document) - before, qint64(img.tileBytes()));
        b = TiledImage();
        QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document), before);
        img = TiledImage();
        QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document), before - qint64(tiles) * 64 * 64 * 4);
    }

    void concurrentReadersSeeTheirSnapshot()
    {
        // Several threads read snapshots while the owner keeps writing to the live image.
        TiledImage live = TiledImage::fromImage(pattern(QSize(512, 512), 11));
        for (int round = 0; round < 20; ++round) {
            const TiledImage snapshot = live;
            const quint64 expected = checksum(snapshot);
            std::atomic<int> mismatches{0};
            std::vector<std::thread> readers;
            for (int t = 0; t < 4; ++t)
                readers.emplace_back([&, copy = snapshot] {
                    for (int i = 0; i < 3; ++i)
                        if (checksum(copy) != expected)
                            ++mismatches;
                });
            QRandomGenerator rng{quint32(round)};
            for (int i = 0; i < 2000; ++i)
                live.setPixel(int(rng.bounded(512)), int(rng.bounded(512)), qRgba(255, 0, 0, 255));
            QVERIFY(live.fillRect(QRect(int(rng.bounded(400)), int(rng.bounded(400)), 100, 100), 0));
            for (std::thread &t : readers)
                t.join();
            QCOMPARE(mismatches.load(), 0);
            QCOMPARE(checksum(snapshot), expected);
        }
    }

    void writersOnSeparateCopiesAreIsolated()
    {
        const TiledImage base = TiledImage::fromImage(pattern(QSize(512, 512), 5));
        const quint64 baseSum = checksum(base);
        TiledImage mine = base;
        TiledImage result;
        std::thread worker([&, theirs = base]() mutable {
            for (int y = 0; y < 512; y += 3)
                theirs.setPixel(y, y, qRgba(0, 255, 0, 255));
            result = theirs;
        });
        for (int y = 0; y < 512; y += 5)
            mine.setPixel(511 - y, y, qRgba(0, 0, 255, 255));
        worker.join();
        QCOMPARE(checksum(base), baseSum);
        QCOMPARE(result.pixel(300, 300), qRgba(0, 255, 0, 255));
        QVERIFY(mine.pixel(300, 300) != qRgba(0, 255, 0, 255));
        QCOMPARE(mine.pixel(511, 0), qRgba(0, 0, 255, 255));
        QVERIFY(result.pixel(511, 0) != qRgba(0, 0, 255, 255));
    }

    void allocationFailureLeavesTheImageUnchanged()
    {
        BudgetGuard guard;
        TiledImage img = TiledImage::fromImage(pattern(QSize(300, 300), 9));
        const TiledImage before = img;
        const TiledImage shared = img; // so writes must copy tiles
        // A single tile.
        MemoryBudget::failReservationsForTesting(0, 1);
        QVERIFY(!img.writableTileData(0, 0));
        MemoryBudget::failReservationsForTesting(0, 1);
        QVERIFY(!img.setPixel(1, 1, 0xffffffff));
        QVERIFY(img == before);
        // A multi-tile write that fails part way: nothing changes. (Each case starts from a copy
        // that shares every tile, so every write has to allocate.)
        img = before;
        MemoryBudget::failReservationsForTesting(3, 1);
        QImage src(250, 250, QImage::Format_ARGB32_Premultiplied);
        src.fill(qRgba(0, 0, 0, 255));
        QVERIFY(!img.write(QPoint(10, 10), src));
        QVERIFY(img == before);
        img = before;
        MemoryBudget::failReservationsForTesting(2, 1);
        QVERIFY(!img.fillRect(QRect(0, 0, 300, 300), qRgba(5, 5, 5, 255)));
        QVERIFY(img == before);
        img = before;
        MemoryBudget::failReservationsForTesting(1, 1);
        QVERIFY(!img.fillRect(QRect(10, 10, 200, 30), 0));
        QVERIFY(img == before);
        MemoryBudget::failReservationsForTesting(0, 1);
        QVERIFY(TiledImage::fromImage(src).isNull());
        // With room again, the same write succeeds.
        QVERIFY(img.write(QPoint(10, 10), src));
        QCOMPARE(img.pixel(100, 100), qRgba(0, 0, 0, 255));
        QVERIFY(shared == before);
    }

    void budgetLimitAndEviction()
    {
        BudgetGuard guard;
        const qint64 tileBytes = 64 * 64 * 4;
        MemoryBudget::setLimit(MemoryBudget::usedTotal() + 4 * tileBytes);
        TiledImage img(QSize(1024, 1024));
        for (int i = 0; i < 4; ++i)
            QVERIFY(img.setPixel(i * 64, 0, 0xff000000));
        QVERIFY(!img.setPixel(300, 300, 0xff000000)); // over the limit
        QCOMPARE(img.pixel(300, 300), QRgb(0));
        // A cache registered on this thread is asked first.
        TiledImage cache(QSize(1024, 1024), 64, MemoryBudget::Category::Cache);
        MemoryBudget::setLimit(MemoryBudget::usedTotal() + 2 * tileBytes);
        QVERIFY(cache.setPixel(0, 0, 0xff000000));
        QVERIFY(cache.setPixel(64, 0, 0xff000000));
        int calls = 0;
        const int id = MemoryBudget::addEvictor([&](qint64) {
            ++calls;
            const qint64 freed = cache.allocatedBytes();
            cache.clear();
            return freed;
        });
        QVERIFY(img.setPixel(300, 300, 0xff000000)); // evicts the cache, then fits
        QCOMPARE(calls, 1);
        QCOMPARE(cache.allocatedTileCount(), 0);
        MemoryBudget::removeEvictor(id);
        // Evictors registered on another thread are never called from this one.
        std::thread([&] { QCOMPARE(MemoryBudget::evict(1), qint64(0)); }).join();
        QVERIFY(MemoryBudget::defaultLimit() >= 256ll * 1024 * 1024);
        QVERIFY(MemoryBudget::physicalMemory() > 0);
    }

    void randomOperationsMatchADenseReference_data() { tileSizes_data(); }
    void randomOperationsMatchADenseReference()
    {
        // Property-based: every operation applied to TiledImage and to a dense QImage reference
        // gives the same pixels, and snapshots keep what they had.
        QFETCH(int, tile);
        QRandomGenerator rng(quint32(1000 + tile));
        const QSize size(700, 500);
        TiledImage img(size, tile);
        QImage ref(size, QImage::Format_ARGB32_Premultiplied);
        ref.fill(Qt::transparent);
        std::vector<std::pair<TiledImage, QImage>> snapshots;
        for (int step = 0; step < 300; ++step) {
            const int op = int(rng.bounded(6));
            const QRect r(int(rng.bounded(800)) - 50, int(rng.bounded(600)) - 50, int(rng.bounded(300)), int(rng.bounded(300)));
            const QRgb colour = rng.bounded(4) == 0 ? 0 : qRgba(int(rng.bounded(100)), 50, 60, 200);
            switch (op) {
            case 0:
                QVERIFY(img.setPixel(r.x(), r.y(), colour));
                if (ref.rect().contains(r.topLeft()))
                    ref.setPixel(r.topLeft(), colour);
                break;
            case 1: {
                QVERIFY(img.fillRect(r, colour));
                QPainter p(&ref);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.fillRect(r, QColor::fromRgba(qUnpremultiply(colour)));
                break;
            }
            case 2: {
                const QImage src = pattern(r.size().expandedTo(QSize(1, 1)), quint32(step));
                QVERIFY(img.write(r.topLeft(), src));
                QPainter p(&ref);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.drawImage(r.topLeft(), src);
                break;
            }
            case 3:
                snapshots.emplace_back(img, ref.copy());
                break;
            case 4:
                img.pruneTransparent(r);
                break;
            case 5: {
                const int c = int(rng.bounded(img.columns())), row = int(rng.bounded(img.rows()));
                img.releaseTile(c, row);
                QPainter p(&ref);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.fillRect(img.tileRect(c, row), Qt::transparent);
                break;
            }
            }
            if (step % 10 == 0)
                QVERIFY2(img == ref, qPrintable(QString("step %1 op %2").arg(step).arg(op)));
        }
        QVERIFY(img == ref);
        for (const auto &[snap, dense] : snapshots)
            QVERIFY(snap == dense);
    }
};

QTEST_GUILESS_MAIN(TestTiles)
#include "tst_tiles.moc"
