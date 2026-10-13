// Milestone 10: sparse tiled storage (TiledImage), copy-on-write snapshots, concurrent readers,
// allocation failure and the memory budget. Headless, against the production classes.

#include "DocumentData.h"
#include "TransformSession.h"
#include "TransformPreview.h"
#include "UndoHistory.h"
#include "MemoryBudget.h"
#include "TiledImage.h"
#include "ViewPyramid.h"
#include "ViewRenderer.h"

#include <QPainter>
#include <QRandomGenerator>
#include <QtTest>
#include <atomic>
#include <climits>
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
        MemoryBudget::setPhysicalFloorForTesting(0);
    }
};

// The pyramid's definition, computed densely: each pixel the rounded average of the 2 x 2
// pixels below it that lie inside the image.
QImage reduce(const QImage &src)
{
    QImage out((src.width() + 1) / 2, (src.height() + 1) / 2, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < out.height(); ++y)
        for (int x = 0; x < out.width(); ++x) {
            quint32 s[4] = {0, 0, 0, 0};
            quint32 n = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = 2 * x + dx, sy = 2 * y + dy;
                    if (sx >= src.width() || sy >= src.height())
                        continue;
                    const QRgb p = src.pixel(sx, sy);
                    s[0] += qRed(p);
                    s[1] += qGreen(p);
                    s[2] += qBlue(p);
                    s[3] += qAlpha(p);
                    ++n;
                }
            out.setPixel(x, y, qRgba(int((s[0] + n / 2) / n), int((s[1] + n / 2) / n), int((s[2] + n / 2) / n),
                                     int((s[3] + n / 2) / n)));
        }
    return out;
}

ViewTransform viewAt(qreal zoom, QPointF pan)
{
    ViewTransform v;
    v.setZoomAt(zoom, QPointF(0, 0));
    v.setPan(pan);
    return v;
}

int maxDifference(const QImage &a, const QImage &b)
{
    int worst = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            worst = std::max({worst, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                              std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
        }
    return worst;
}

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
    void viewportPreviewMatchesIndependentLayers()
    {
        const QSize size(96, 80);
        std::vector<LayerData> layers;
        for (int i = 0; i < 3; ++i) {
            LayerData layer;
            layer.id = i + 1;
            layer.image = TiledImage::fromImage(pattern(size, 70 + i));
            layer.opacity = 0.35 + i * 0.2;
            layers.push_back(layer);
        }
        QImage mask(size, QImage::Format_Grayscale8);
        mask.fill(0);
        for (int y = 13; y < 57; ++y)
            for (int x = 17; x < 65; ++x)
                mask.scanLine(y)[x] = uchar(80 + x * 2);
        TransformSession session;
        QVERIFY(session.begin(std::vector<TiledImage>{layers[0].image, layers[2].image}, Selection::fromMask(mask)));
        session.moveBy({8, 4});
        session.setRotation(13);
        const QRect crop(11, 7, 63, 51);
        TransformPreview preview;
        QVERIFY(preview.prepare(session, layers, {1, 3}, crop, 0));
        const QImage actual = preview.render(session, TransformSession::Quality::Final);
        QVERIFY(!actual.isNull());
        auto applied = layers;
        QVERIFY(session.render(0, applied[0].image, applied[0].image.rect(), TransformSession::Quality::Final));
        QVERIFY(session.render(1, applied[2].image, applied[2].image.rect(), TransformSession::Quality::Final));
        TiledImage reference(size);
        QVERIFY(compositeLayers(reference, reference.rect(), applied));
        QVERIFY(maxDifference(actual, reference.copy(crop)) <= 1);
        QCOMPARE(layers[1].image, applied[1].image); // interleaved unselected layer stays put
        session.reset();
        TiledImage original(size);
        QVERIFY(compositeLayers(original, original.rect(), layers));
        QCOMPARE(preview.render(session, TransformSession::Quality::Final), original.copy(crop));
    }

    void reducedPreviewKeepsThinStrokes()
    {
        LayerData layer;
        layer.id = 1;
        layer.image = TiledImage(QSize(512, 512));
        QVERIFY(layer.image.fillRect({101, 50, 1, 400}, qRgb(255, 0, 0)));
        TransformSession session;
        QVERIFY(session.begin(std::vector<TiledImage>{layer.image}, Selection()));
        session.moveBy({3, 0});
        TransformPreview preview;
        QVERIFY(preview.prepare(session, {layer}, {1}, QRect(0, 0, 64, 64), 3));
        const QImage shown = preview.render(session, TransformSession::Quality::Fast);
        QVERIFY(!shown.isNull());
        for (int y = 10; y < 50; ++y) {
            int coverage = 0;
            for (int x = 0; x < shown.width(); ++x)
                coverage += qAlpha(shown.pixel(x, y));
            QVERIFY2(coverage >= 25, "The one-pixel stroke vanished in the reduced preview");
        }
        QCOMPARE(shown.size(), QSize(64, 64));
    }

    void denseCopyKeepsItsReservation()
    {
        BudgetGuard guard;
        TiledImage image = TiledImage::fromImage(pattern({128, 128}, 23));
        const qint64 before = MemoryBudget::usedTotal();
        QImage first = image.toImage();
        QVERIFY(!first.isNull());
        QCOMPARE(MemoryBudget::usedTotal(), before + first.sizeInBytes());
        QImage shared = first;
        first = {};
        QCOMPARE(MemoryBudget::usedTotal(), before + shared.sizeInBytes());
        MemoryBudget::setLimit(MemoryBudget::usedTotal());
        QVERIFY(image.toImage().isNull());
        shared = {};
        QCOMPARE(MemoryBudget::usedTotal(), before);
    }

    void transformCacheLifetimeAndFailures()
    {
        BudgetGuard guard;
        TiledImage image = TiledImage::fromImage(pattern({127, 65}, 24));
        const qint64 before = MemoryBudget::usedTotal();
        for (int attempt = 0; attempt < 20; ++attempt) {
            {
                TransformSession session;
                MemoryBudget::failReservationsForTesting(attempt, 1);
                session.begin(std::vector<TiledImage>{image}, Selection());
                MemoryBudget::failReservationsForTesting(-1, 0);
            }
            QCOMPARE(MemoryBudget::usedTotal(), before);
        }
        TransformSession worker;
        {
            TransformSession session;
            QVERIFY(session.begin(std::vector<TiledImage>{image}, Selection()));
            worker = session.workerCopy();
        }
        QVERIFY(MemoryBudget::usedTotal() > before);
        QImage output = worker.renderRegion(0, image.rect(), TransformSession::Quality::Final);
        QCOMPARE(output, image.toImage());
        output = {};
        worker.end();
        QCOMPARE(MemoryBudget::usedTotal(), before);
    }

    void selectionAllocationFailureIsExplicit()
    {
        BudgetGuard guard;
        TransformSession session;
        QVERIFY(session.begin(std::vector<TiledImage>{}, Selection::all({64, 64})));
        MemoryBudget::failReservationsForTesting(0, 1);
        QVERIFY_EXCEPTION_THROWN(session.transformedSelection(), std::bad_alloc);
        QVERIFY(session.hasSelection());
    }

    void pyramidGrowthDuringEvictionBalancesBudget()
    {
        BudgetGuard guard;
        TiledImage image = TiledImage::fromImage(pattern({512, 512}, 25));
        const qint64 before = MemoryBudget::usedTotal();
        {
            ViewPyramid pyramid;
            QVERIFY(!pyramid.pixels(image, 0, {0, 0, 64, 64}).isNull());
            MemoryBudget::setLimit(before + 512 * 512 * 4);
            QVERIFY(!pyramid.pixels(image, 0, image.rect()).isNull());
            QCOMPARE(MemoryBudget::usedTotal(), before + 512 * 512 * 4);
        }
        QCOMPARE(MemoryBudget::usedTotal(), before);
    }

    void sharedUndoCannotFundReservations()
    {
        TiledImage image = TiledImage::fromImage(pattern({64, 64}, 26));
        TileSnapshot before;
        before.capture(image, image.rect());
        UndoHistory history;
        history.push(UndoCommand::pixels(1, std::move(before), QStringLiteral("shared")));
        int dropped = -1;
        QCOMPARE(history.shed(1, &dropped), qsizetype(0));
        QCOMPARE(dropped, 0);
        QCOMPARE(history.undoCount(), 1);
        image = {};
        QCOMPARE(history.shed(1, &dropped), qsizetype(64 * 64 * 4));
        QCOMPARE(dropped, 1);
    }

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

    // --- view pyramid (milestone 10 stage 6) ---

    void pyramidLevelChoice()
    {
        const QSize big(16384, 16384);
        QCOMPARE(ViewPyramid::levelFor(4.0, big), 0);
        QCOMPARE(ViewPyramid::levelFor(1.0, big), 0);
        QCOMPARE(ViewPyramid::levelFor(0.75, big), 0);
        QCOMPARE(ViewPyramid::levelFor(0.5, big), 1);  // exactly half: level 1 drawn 1:1
        QCOMPARE(ViewPyramid::levelFor(0.3, big), 1);
        QCOMPARE(ViewPyramid::levelFor(0.25, big), 2);
        QCOMPARE(ViewPyramid::levelFor(0.05, big), 4);  // the smallest zoom
        QCOMPARE(ViewPyramid::levelFor(0.001, QSize(8, 8)), 3); // never below one pixel
        QCOMPARE(ViewPyramid::levelSize(QSize(101, 7), 1), QSize(51, 4));
        QCOMPARE(ViewPyramid::levelSize(QSize(101, 7), 3), QSize(13, 1));
    }

    void pyramidLevelsAreExactAverages()
    {
        // Odd sizes: edge pixels average only the pixels inside.
        const QImage dense = pattern(QSize(301, 197), 11);
        const TiledImage image = TiledImage::fromImage(dense);
        ViewPyramid pyramid;
        QImage expected = dense;
        for (int k = 1; k <= 4; ++k) {
            expected = reduce(expected);
            const TiledImage *level = pyramid.level(image, k, QRect(QPoint(0, 0), ViewPyramid::levelSize(image.size(), k)));
            QVERIFY(level);
            QCOMPARE(level->size(), expected.size());
            QVERIFY2(*level == expected, qPrintable(QString::number(k)));
        }
    }

    void pyramidRebuildsOnlyWhatChanged()
    {
        TiledImage image(QSize(1024, 1024));
        QVERIFY(image.fillRect(QRect(100, 100, 600, 500), qRgba(10, 20, 30, 255)));
        ViewPyramid pyramid;
        const QRect all3(QPoint(0, 0), ViewPyramid::levelSize(image.size(), 3));
        QVERIFY(pyramid.level(image, 3, all3));
        const qint64 first = pyramid.tilesBuilt();
        QVERIFY(first > 0);
        QVERIFY(pyramid.level(image, 3, all3));
        QCOMPARE(pyramid.tilesBuilt(), first); // nothing changed: nothing rebuilt
        // One dab-sized change: one tile per level is rebuilt (levels 1, 2 and 3).
        QVERIFY(image.setPixel(700, 650, qRgba(200, 0, 0, 255)));
        const TiledImage *level = pyramid.level(image, 3, all3);
        QVERIFY(level);
        QCOMPARE(pyramid.tilesBuilt(), first + 3);
        QVERIFY(*level == reduce(reduce(reduce(image.toImage()))));
        // A copy that shares the image's tiles (as the transform preview does) reuses the levels.
        TiledImage copy = image;
        QVERIFY(pyramid.level(copy, 3, all3));
        QCOMPARE(pyramid.tilesBuilt(), first + 3);
        // Undo-style tile exchange is seen too.
        TiledImage other = image;
        QVERIFY(other.fillRect(QRect(0, 0, 64, 64), qRgba(0, 0, 255, 255)));
        image.setTileRef(0, 0, other.tileRef(0, 0));
        QVERIFY(*pyramid.level(image, 3, all3) == reduce(reduce(reduce(image.toImage()))));
    }

    void pyramidOfAnEmptyImageAllocatesNothing()
    {
        const TiledImage image(QSize(16384, 16384));
        ViewPyramid pyramid;
        const QSize s4 = ViewPyramid::levelSize(image.size(), 4);
        QVERIFY(pyramid.level(image, 4, QRect(QPoint(0, 0), s4)));
        QCOMPARE(pyramid.byteSize(), 0);
    }

    void pyramidIsEvictedAndRefusedCleanly()
    {
        BudgetGuard guard;
        const TiledImage image = TiledImage::fromImage(pattern(QSize(512, 512), 3));
        ViewPyramid pyramid;
        const QRect all1(QPoint(0, 0), ViewPyramid::levelSize(image.size(), 1));
        QVERIFY(pyramid.level(image, 1, all1));
        QVERIFY(pyramid.byteSize() > 0);
        const qint64 cache = MemoryBudget::used(MemoryBudget::Category::Cache);
        QVERIFY(MemoryBudget::evict(qint64(1) << 40) >= pyramid.byteSize());
        QCOMPARE(pyramid.byteSize(), 0);
        QVERIFY(MemoryBudget::used(MemoryBudget::Category::Cache) < cache);
        // Refused: no level, no crash; then it rebuilds the same pixels.
        MemoryBudget::failReservationsForTesting(0, 1000);
        QVERIFY(!pyramid.level(image, 1, all1));
        QVERIFY(pyramid.pixels(image, 1, all1).isNull());
        MemoryBudget::failReservationsForTesting(-1, 0);
        const TiledImage *level = pyramid.level(image, 1, all1);
        QVERIFY(level && *level == reduce(image.toImage()));
    }

    void viewMatchesTheDenseRendererAboveHalfZoom()
    {
        // The canvas's view (tiled, with the pyramid) draws exactly the pixels of the milestone 9
        // renderer from 50 % zoom up, at fractional zooms and pans (no seams at tile edges).
        const QImage dense = pattern(QSize(700, 500), 5);
        const TiledImage tiled = TiledImage::fromImage(dense);
        ViewPyramid pyramid;
        for (const qreal zoom : {0.51, 0.77, 1.0, 1.7, 3.3, 8.0}) {
            for (const QPointF pan : {QPointF(0, 0), QPointF(13.37, -21.6), QPointF(-200.25, 7.5)}) {
                const ViewTransform view = viewAt(zoom, pan);
                QImage a(640, 480, QImage::Format_ARGB32_Premultiplied), b = a;
                {
                    QPainter p(&a);
                    ViewRenderer::render(p, dense, view, a.rect());
                }
                {
                    QPainter p(&b);
                    ViewRenderer::render(p, tiled, pyramid, view, b.rect());
                }
                QVERIFY2(a == b, qPrintable(QString("zoom %1 pan %2,%3").arg(zoom).arg(pan.x()).arg(pan.y())));
            }
        }
    }

    void zoomedOutViewDrawsThePyramidLevel()
    {
        // Below 50 %: the level image drawn with the same geometry (sizes divisible by 2^k so the
        // level covers the document exactly). Equal to drawing the dense level as a document at
        // zoom × 2^k, up to rounding in the transform.
        const QImage dense = pattern(QSize(1024, 768), 9);
        const TiledImage tiled = TiledImage::fromImage(dense);
        ViewPyramid pyramid;
        for (const qreal zoom : {0.4, 0.26, 0.12, 0.05}) {
            const int k = ViewPyramid::levelFor(zoom, tiled.size());
            QVERIFY(k > 0);
            QImage level = dense;
            for (int i = 0; i < k; ++i)
                level = reduce(level);
            const QPointF pan(17.25, 9.5);
            QImage a(400, 300, QImage::Format_ARGB32_Premultiplied), b = a;
            {
                QPainter p(&a);
                ViewRenderer::render(p, level, viewAt(zoom * (1 << k), pan), a.rect());
            }
            {
                QPainter p(&b);
                ViewRenderer::render(p, tiled, pyramid, viewAt(zoom, pan), b.rect());
            }
            const int worst = maxDifference(a, b);
            QVERIFY2(worst <= 2, qPrintable(QString("zoom %1: %2").arg(zoom).arg(worst)));
        }
    }

    void zoomedOutViewOfAHugeSparseCanvasIsCheap()
    {
        // 16384 x 16384 with a few marks, seen whole at the smallest zoom: the levels and the
        // drawing buffer stay small (the dense milestone 9 path would need 1 GiB).
        TiledImage image(QSize(16384, 16384));
        for (int i = 0; i < 20; ++i)
            QVERIFY(image.fillRect(QRect(800 * i, 700 * i, 40, 40), qRgba(0, 0, 0, 255)));
        ViewPyramid pyramid;
        const ViewTransform view = viewAt(0.05, QPointF(10, 10));
        QImage out(840, 840, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&out);
            ViewRenderer::render(p, image, pyramid, view, out.rect());
        }
        QVERIFY(pyramid.byteSize() < 8 * 1024 * 1024);
        // The marks are visible (darker than the checkerboard).
        const QPointF mark = view.docToView(QPointF(800 * 10 + 20, 700 * 10 + 20));
        QVERIFY(qGray(out.pixel(mark.toPoint())) < 150);
    }

    void tiledCompositingMatchesDenseCompositing()
    {
        // Every way the tiled composite is built (whole canvas tile by tile, brush-sized areas
        // through one painter, areas at tile corners) gives exactly the dense milestone 9 result.
        const QSize size(333, 217);
        std::vector<LayerData> layers;
        for (int i = 0; i < 5; ++i) {
            LayerData l;
            l.id = quint64(i + 1);
            QImage img = pattern(size, quint32(40 + i));
            // Leave holes, so some tiles are missing.
            QPainter p(&img);
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillRect(QRect(64 * (i % 3), 64, 140, 70), Qt::transparent);
            p.end();
            l.image = TiledImage::fromImage(img);
            l.image.pruneTransparent();
            l.opacity = i == 2 ? 0.37 : (i == 4 ? 0.8 : 1.0);
            l.visible = i != 3;
            layers.push_back(l);
        }
        QImage dense(size, QImage::Format_ARGB32_Premultiplied);
        compositeLayers(dense, dense.rect(), layers);
        TiledImage whole(size);
        QVERIFY(compositeLayers(whole, whole.rect(), layers));
        QVERIFY(whole == dense);
        // Brush-sized refreshes, many places, into a composite that starts out stale.
        TiledImage pieces(size);
        QVERIFY(pieces.fillRect(pieces.rect(), qRgba(9, 9, 9, 9)));
        QRandomGenerator rng(5);
        for (int i = 0; i < 400; ++i) {
            const QRect r(int(rng.bounded(size.width())) - 20, int(rng.bounded(size.height())) - 20,
                          1 + int(rng.bounded(150)), 1 + int(rng.bounded(150)));
            QVERIFY(compositeLayers(pieces, r, layers));
        }
        QVERIFY(compositeLayers(pieces, QRect(0, 0, 333, 217), layers)); // finish everything
        QVERIFY(pieces == dense);
        // Each small refresh alone equals the dense result inside it.
        for (int i = 0; i < 200; ++i) {
            TiledImage target(size);
            QVERIFY(target.fillRect(target.rect(), qRgba(9, 9, 9, 9)));
            const QRect r(int(rng.bounded(size.width() - 10)), int(rng.bounded(size.height() - 10)),
                          2 + int(rng.bounded(180)), 2 + int(rng.bounded(180)));
            QVERIFY(compositeLayers(target, r, layers));
            const QRect inside = r & target.rect();
            QCOMPARE(target.copy(inside), dense.copy(inside));
            // Outside the area nothing changed.
            QCOMPARE(target.pixel(inside.left() > 0 ? 0 : size.width() - 1, inside.top() > 0 ? 0 : size.height() - 1),
                     qRgba(9, 9, 9, 9));
        }
    }

    void contentBoundsAreExact()
    {
        // The bounds skip tiles already inside them; checked against a scan of every pixel.
        QRandomGenerator rng(17);
        for (int round = 0; round < 300; ++round) {
            const QSize size(1 + int(rng.bounded(700)), 1 + int(rng.bounded(500)));
            TiledImage img(size);
            const int marks = int(rng.bounded(6));
            for (int m = 0; m < marks; ++m) {
                const QRect r(int(rng.bounded(size.width())), int(rng.bounded(size.height())), 1 + int(rng.bounded(90)),
                              1 + int(rng.bounded(90)));
                QVERIFY(img.fillRect(r, qRgba(1, 2, 3, 200)));
            }
            if (rng.bounded(2)) // a painted-then-erased (allocated but transparent) tile
                QVERIFY(img.fillRect(QRect(int(rng.bounded(size.width())), int(rng.bounded(size.height())), 5, 5), 0));
            int left = INT_MAX, top = INT_MAX, right = -1, bottom = -1;
            for (int y = 0; y < size.height(); ++y)
                for (int x = 0; x < size.width(); ++x)
                    if (img.pixel(x, y)) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
            const QRect expected = right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
            QCOMPARE(img.contentBounds(), expected);
        }
    }

    void lowDeviceMemoryRefusesCleanly()
    {
        // Other programs can leave the device short of memory long before DrawApp's own limit;
        // then reservations are refused (after the caches are given up) and messages say why.
        BudgetGuard guard;
        QVERIFY(MemoryBudget::availablePhysicalMemory() > 0);
        int evicted = 0;
        const int evictor = MemoryBudget::addEvictor([&](qint64) {
            ++evicted;
            return qint64(0);
        });
        const auto removeEvictor = qScopeGuard([&] { MemoryBudget::removeEvictor(evictor); });
        MemoryBudget::setPhysicalFloorForTesting(qint64(1) << 50); // more than any device has
        TiledImage img(QSize(256, 256));
        QVERIFY(!img.fillRect(QRect(0, 0, 100, 100), 0xff000000));
        QVERIFY(img.allocatedTileCount() == 0);
        QVERIFY(evicted > 0);
        QVERIFY2(MemoryBudget::shortageText().contains("device"), qPrintable(MemoryBudget::shortageText()));
        MemoryBudget::setPhysicalFloorForTesting(0);
        QVERIFY(img.fillRect(QRect(0, 0, 100, 100), 0xff000000));
        // A refusal by the limit says so.
        MemoryBudget::setLimit(MemoryBudget::usedTotal() + 1);
        QVERIFY(!img.fillRect(QRect(150, 150, 100, 100), 0xff000000));
        QVERIFY2(MemoryBudget::shortageText().contains("limit"), qPrintable(MemoryBudget::shortageText()));
    }
};

QTEST_GUILESS_MAIN(TestTiles)
#include "tst_tiles.moc"
