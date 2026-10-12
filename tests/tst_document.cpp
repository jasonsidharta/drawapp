// Layered document, undo history, project files and PNG export, without any UI.

#include "LayeredDocument.h"
#include "ProjectFile.h"
#include "RasterDocument.h"
#include "TileSnapshot.h"
#include "UndoHistory.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cstdlib>

namespace {

constexpr qsizetype TileBytes = qsizetype(TileSnapshot::TileSize) * TileSnapshot::TileSize * 4;

// Paints one dab on a layer and records it as an undo step, like the canvas does.
bool paintStep(LayeredDocument &doc, UndoHistory &history, quint64 layerId, QPointF center, qreal diameter,
               QColor color, BrushMode mode = BrushMode::Paint)
{
    const int index = doc.indexOf(layerId);
    TileSnapshot before;
    const QRect changed = RasterDocument::stampDabs(doc.pixels(index), {{center, diameter}}, color, mode, &before);
    doc.refreshComposite(changed);
    before.dropUnchanged(doc.layer(index).image);
    if (before.isEmpty())
        return false;
    history.push(UndoCommand::pixels(layerId, std::move(before), QStringLiteral("stroke")));
    return true;
}

void undo(LayeredDocument &doc, UndoHistory &history)
{
    const UndoCommand::Result r = history.undo(doc);
    if (r.activate)
        doc.setActiveLayer(r.activate);
    doc.refreshComposite(r.dirty);
}

void redo(LayeredDocument &doc, UndoHistory &history)
{
    const UndoCommand::Result r = history.redo(doc);
    if (r.activate)
        doc.setActiveLayer(r.activate);
    doc.refreshComposite(r.dirty);
}

quint64 addLayer(LayeredDocument &doc, UndoHistory &history, const QString &name)
{
    LayerData layer;
    layer.id = doc.allocateId();
    layer.name = name;
    layer.image = TiledImage(doc.size());
    const int index = doc.activeIndex() + 1;
    const quint64 id = layer.id;
    doc.insertLayer(index, layer);
    doc.setActiveLayer(id);
    history.push(UndoCommand::layerInserted(id, index, QStringLiteral("Add layer")));
    return id;
}

void deleteLayer(LayeredDocument &doc, UndoHistory &history, quint64 id)
{
    const int index = doc.indexOf(id);
    LayerData removed = doc.takeLayer(index);
    doc.refreshComposite();
    history.push(UndoCommand::layerRemoved(std::move(removed), index, QStringLiteral("Delete layer")));
}

void moveLayer(LayeredDocument &doc, UndoHistory &history, quint64 id, int to)
{
    const int from = doc.indexOf(id);
    doc.moveLayer(from, to);
    doc.refreshComposite();
    history.push(UndoCommand::moved(id, from, QStringLiteral("Move layer")));
}

TiledImage solid(QSize size, QColor color)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return TiledImage::fromImage(image);
}

// Random image with valid premultiplied pixels, including transparent and partly transparent ones.
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

bool near(QRgb a, QRgb b, int tolerance = 2)
{
    return std::abs(qRed(a) - qRed(b)) <= tolerance && std::abs(qGreen(a) - qGreen(b)) <= tolerance
           && std::abs(qBlue(a) - qBlue(b)) <= tolerance && std::abs(qAlpha(a) - qAlpha(b)) <= tolerance;
}

DocumentData threeLayerDocument(QSize size)
{
    DocumentData doc;
    doc.size = size;
    const char *names[] = {"Background", "Sketch ✏ 素描", "Line art"};
    for (int i = 0; i < 3; ++i) {
        LayerData layer;
        layer.id = quint64(10 + i * 7); // non-contiguous ids must survive
        layer.name = QString::fromUtf8(names[i]);
        layer.image = TiledImage::fromImage(randomPremultiplied(size.width(), size.height(), quint32(i + 1)));
        layer.visible = i != 1;
        layer.opacity = i == 2 ? 0.37 : 1.0;
        layer.locked = i == 0;
        doc.layers.push_back(layer);
    }
    doc.activeLayerId = 17;
    return doc;
}

// Version 1 file with explicit header fields (checksum recomputed unless told not to).
QByteArray makeV1(quint32 version, quint32 w, quint32 h, quint32 format, quint32 compression,
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

// Version 2 (or 3) file from a header, metadata JSON and layer blocks (checksum recomputed).
QByteArray makeV2(quint32 w, quint32 h, quint32 count, const QJsonObject &meta, const QList<QByteArray> &blocks,
                  quint32 version = 2)
{
    QByteArray payload = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    const quint32 metaSize = quint32(payload.size());
    for (const QByteArray &b : blocks)
        payload.append(b);
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("DRAWAPP\n", 8);
    s << version << w << h << count << metaSize << quint64(payload.size());
    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    s.writeRawData(digest.constData(), 32);
    out.append(payload);
    return out;
}

QJsonObject layerMeta(const QString &id, qint64 blockSize, double opacity = 1.0)
{
    QJsonObject o;
    o["id"] = id;
    o["name"] = "L";
    o["visible"] = true;
    o["opacity"] = opacity;
    o["locked"] = false;
    o["compressedSize"] = double(blockSize);
    return o;
}

QByteArray block(int w, int h)
{
    return qCompress(QByteArray(w * h * 4, '\0'));
}

// Version 3: one tile entry (column, row, data) and a layer's tile stream.
struct TileEntry
{
    quint32 column, row;
    QByteArray data;
};

QByteArray tileStream(const QList<TileEntry> &tiles, qint64 declaredCount = -1)
{
    QByteArray out(4, '\0');
    qToLittleEndian(quint32(declaredCount < 0 ? tiles.size() : declaredCount), out.data());
    for (const TileEntry &t : tiles) {
        char entry[12];
        qToLittleEndian(t.column, entry);
        qToLittleEndian(t.row, entry + 4);
        qToLittleEndian(quint32(t.data.size()), entry + 8);
        out.append(entry, 12);
        out.append(t.data);
    }
    return out;
}

QJsonObject tiledLayerMeta(const QString &id, qint64 blockSize, int tiles)
{
    QJsonObject o = layerMeta(id, blockSize);
    o["tiles"] = tiles;
    return o;
}

// An opaque grey tile of w x h pixels (B, G, R, A bytes).
QByteArray greyTile(int w, int h)
{
    QByteArray raw;
    for (int i = 0; i < w * h; ++i)
        raw.append(QByteArray::fromHex("808080ff"));
    return qCompress(raw);
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
    // --- layer model and compositing ---
    void newDocumentHasOneTransparentLayer()
    {
        LayeredDocument doc;
        QCOMPARE(doc.size(), QSize(2048, 1536));
        QCOMPARE(doc.count(), 1);
        QCOMPARE(doc.layer(0).name, QStringLiteral("Layer 1"));
        QCOMPARE(doc.activeLayerId(), doc.layer(0).id);
        QCOMPARE(qAlpha(doc.composite().pixel(100, 100)), 0);
        // Layers are sparse (milestone 10): the count is fixed; memory is checked when pixels are made.
        QCOMPARE(LayeredDocument::maxLayersFor(doc.size()), DocumentLimits::MaxLayers);
        QCOMPARE(LayeredDocument::maxLayersFor(QSize(16384, 16384)), DocumentLimits::MaxLayers);
        QCOMPARE(doc.layer(0).image.allocatedTileCount(), 0); // a blank layer holds no pixels
    }

    void compositeOrderOpacityAndVisibility()
    {
        DocumentData data;
        data.size = QSize(8, 8);
        LayerData bottom{1, "red", solid(data.size, QColor(255, 0, 0)), true, 1.0, false};
        LayerData top{2, "blue", solid(data.size, QColor(0, 0, 255)), true, 0.5, false};
        data.layers = {bottom, top};
        data.activeLayerId = 2;
        LayeredDocument doc(data);
        // Blue at 50 % over opaque red.
        QVERIFY(near(doc.composite().pixel(3, 3), qRgba(128, 0, 127, 255)));

        LayeredDocument swapped(data);
        swapped.moveLayer(1, 0); // blue now at the bottom, red on top at full opacity
        swapped.refreshComposite();
        QCOMPARE(swapped.composite().pixel(3, 3), qRgba(255, 0, 0, 255));

        swapped.layerRef(1).visible = false; // hide red: half-transparent blue over nothing
        swapped.refreshComposite();
        QVERIFY(near(swapped.composite().pixel(3, 3), qRgba(0, 0, 128, 128)));
    }

    void compositeKeepsTransparency()
    {
        DocumentData data;
        data.size = QSize(4, 4);
        data.layers = {{1, "a", solid(data.size, QColor(255, 0, 0, 128)), true, 1.0, false},
                       {2, "b", solid(data.size, QColor(0, 255, 0, 128)), true, 1.0, false}};
        data.activeLayerId = 1;
        const QRgb c = LayeredDocument(data).composite().pixel(1, 1);
        // alpha = a1 + a2 (1 - a1) ≈ 0.5 + 0.5 * 0.5 = 0.75
        QVERIFY2(std::abs(qAlpha(c) - 191) <= 2, qPrintable(QString::number(qAlpha(c))));
        QVERIFY(qGreen(c) > qRed(c)); // the upper (green) layer dominates
    }

    void erasingUpperLayerRevealsLowerLayer()
    {
        LayeredDocument doc(QSize(200, 200));
        UndoHistory history;
        const quint64 background = doc.layer(0).id;
        paintStep(doc, history, background, QPointF(100, 100), 150, QColor(0, 180, 0));
        const quint64 upper = addLayer(doc, history, "upper");
        paintStep(doc, history, upper, QPointF(100, 100), 150, QColor(0, 0, 200));
        QCOMPARE(doc.composite().pixel(100, 100), qRgb(0, 0, 200));
        paintStep(doc, history, upper, QPointF(100, 100), 30, Qt::black, BrushMode::Erase);
        QCOMPARE(doc.composite().pixel(100, 100), qRgb(0, 180, 0)); // lower layer shows through
        QCOMPARE(qAlpha(doc.layer(doc.indexOf(upper)).image.pixel(100, 100)), 0);
        QCOMPARE(doc.layer(doc.indexOf(background)).image.pixel(100, 100), qRgb(0, 180, 0)); // untouched
    }

    void partialRefreshMatchesFullComposite()
    {
        LayeredDocument doc(QSize(300, 200));
        UndoHistory history;
        const quint64 a = doc.layer(0).id;
        const quint64 b = addLayer(doc, history, "b");
        doc.layerRef(doc.indexOf(b)).opacity = 0.6;
        doc.refreshComposite();
        for (int i = 0; i < 20; ++i)
            paintStep(doc, history, i % 2 ? a : b, QPointF(20 + i * 13, 30 + i * 7), 25,
                      QColor::fromHsv(i * 17, 200, 220));
        QCOMPARE(doc.composite(), doc.snapshot().composite());
    }

    void layerIdsAreStableAndNeverReused()
    {
        LayeredDocument doc(QSize(16, 16));
        UndoHistory history;
        const quint64 first = doc.layer(0).id;
        const quint64 second = addLayer(doc, history, "2");
        deleteLayer(doc, history, second);
        const quint64 third = addLayer(doc, history, "3");
        QVERIFY(third != second && third != first);
        undo(doc, history); // remove "3"
        undo(doc, history); // restore "2" with its original id
        QVERIFY(doc.indexOf(second) >= 0);
        QCOMPARE(doc.activeLayerId(), second);
    }

    void deletingActiveLayerSelectsTheLayerBelow()
    {
        LayeredDocument doc(QSize(16, 16));
        UndoHistory history;
        const quint64 bottom = doc.layer(0).id;
        const quint64 middle = addLayer(doc, history, "middle");
        const quint64 top = addLayer(doc, history, "top");
        deleteLayer(doc, history, top);
        QCOMPARE(doc.activeLayerId(), middle);
        doc.setActiveLayer(bottom);
        deleteLayer(doc, history, bottom); // no layer below: the new bottom layer becomes active
        QCOMPARE(doc.activeLayerId(), middle);
        undo(doc, history); // restored layer becomes active again, at its old position
        QCOMPARE(doc.activeLayerId(), bottom);
        QCOMPARE(doc.indexOf(bottom), 0);
    }

    // --- undo / redo ---
    void strokeUndoRedoRestoresPixels()
    {
        LayeredDocument doc(QSize(300, 200));
        UndoHistory history;
        const QImage empty = doc.composite().toImage();
        const quint64 id = doc.layer(0).id;
        QVERIFY(paintStep(doc, history, id, QPointF(50, 50), 20, Qt::red));
        const QImage painted = doc.composite().toImage();
        undo(doc, history);
        QCOMPARE(doc.composite(), empty);
        QVERIFY(history.canRedo());
        redo(doc, history);
        QCOMPARE(doc.composite(), painted);
    }

    void eraserAndClearUndo()
    {
        LayeredDocument doc(QSize(500, 300));
        UndoHistory history;
        const quint64 id = doc.layer(0).id;
        paintStep(doc, history, id, QPointF(100, 100), 80, Qt::blue);
        const QImage painted = doc.composite().toImage();
        paintStep(doc, history, id, QPointF(100, 100), 30, Qt::black, BrushMode::Erase);
        QCOMPARE(qAlpha(doc.composite().pixel(100, 100)), 0);
        undo(doc, history);
        QCOMPARE(doc.composite(), painted);

        TileSnapshot before;
        RasterDocument::clear(doc.pixels(0), &before);
        doc.refreshComposite();
        history.push(UndoCommand::pixels(id, std::move(before), QStringLiteral("Clear layer")));
        QCOMPARE(qAlpha(doc.composite().pixel(100, 100)), 0);
        undo(doc, history);
        QCOMPARE(doc.composite(), painted);
    }

    void newEditInvalidatesRedo()
    {
        LayeredDocument doc(QSize(200, 200));
        UndoHistory history;
        const quint64 id = doc.layer(0).id;
        paintStep(doc, history, id, QPointF(30, 30), 20, Qt::red);
        const QImage afterFirst = doc.composite().toImage();
        paintStep(doc, history, id, QPointF(150, 150), 20, Qt::red);
        undo(doc, history);
        paintStep(doc, history, id, QPointF(150, 30), 20, Qt::blue);
        QVERIFY(!history.canRedo());
        undo(doc, history);
        QCOMPARE(doc.composite(), afterFirst);
    }

    void undoAfterReorderTargetsTheRightLayer()
    {
        LayeredDocument doc(QSize(200, 200));
        UndoHistory history;
        const quint64 a = doc.layer(0).id;
        const quint64 b = addLayer(doc, history, "b");
        paintStep(doc, history, a, QPointF(50, 50), 40, Qt::red);
        paintStep(doc, history, b, QPointF(150, 150), 40, Qt::blue);
        moveLayer(doc, history, a, 1); // a now on top
        QCOMPARE(doc.layer(1).id, a);
        undo(doc, history);            // move back
        QCOMPARE(doc.layer(0).id, a);
        undo(doc, history);            // b's stroke
        undo(doc, history);            // a's stroke
        QCOMPARE(qAlpha(doc.layer(doc.indexOf(a)).image.pixel(50, 50)), 0);
        QCOMPARE(qAlpha(doc.layer(doc.indexOf(b)).image.pixel(150, 150)), 0);

        // Redo after moving again: the strokes go back to their own layers, wherever they are.
        redo(doc, history);
        redo(doc, history);
        redo(doc, history);
        QCOMPARE(doc.layer(1).id, a);
        QCOMPARE(doc.layer(doc.indexOf(a)).image.pixel(50, 50), qRgb(255, 0, 0));
        QCOMPARE(qAlpha(doc.layer(doc.indexOf(a)).image.pixel(150, 150)), 0);
        QCOMPARE(doc.layer(doc.indexOf(b)).image.pixel(150, 150), qRgb(0, 0, 255));
    }

    void undoAfterDeletionRestoresLayerAndPixels()
    {
        LayeredDocument doc(QSize(200, 200));
        UndoHistory history;
        const quint64 a = doc.layer(0).id;
        const quint64 b = addLayer(doc, history, "b");
        paintStep(doc, history, b, QPointF(100, 100), 60, Qt::green);
        const QImage before = doc.composite().toImage();
        deleteLayer(doc, history, b);
        QCOMPARE(doc.count(), 1);
        QCOMPARE(qAlpha(doc.composite().pixel(100, 100)), 0);
        undo(doc, history);
        QCOMPARE(doc.count(), 2);
        QCOMPARE(doc.layer(1).id, b);
        QCOMPARE(doc.composite(), before);
        undo(doc, history); // b's stroke: still targets b
        QCOMPARE(qAlpha(doc.composite().pixel(100, 100)), 0);
        redo(doc, history);
        redo(doc, history); // delete again
        QCOMPARE(doc.count(), 1);
        QCOMPARE(doc.layer(0).id, a);
    }

    void propertyChangesAreUndoable()
    {
        LayeredDocument doc(QSize(8, 8));
        UndoHistory history;
        const quint64 id = doc.layer(0).id;
        LayerData &layer = doc.layerRef(0);
        const LayerProperties before{layer.name, layer.visible, layer.opacity, layer.locked};
        layer.name = "Ink";
        layer.visible = false;
        layer.opacity = 0.25;
        layer.locked = true;
        history.push(UndoCommand::properties(id, before, QStringLiteral("props")));
        undo(doc, history);
        QCOMPARE(doc.layer(0).name, QStringLiteral("Layer 1"));
        QVERIFY(doc.layer(0).visible);
        QCOMPARE(doc.layer(0).opacity, 1.0);
        QVERIFY(!doc.layer(0).locked);
        redo(doc, history);
        QCOMPARE(doc.layer(0).name, QStringLiteral("Ink"));
        QCOMPARE(doc.layer(0).opacity, 0.25);
        QVERIFY(doc.layer(0).locked && !doc.layer(0).visible);
    }

    void snapshotsStoreOnlyChangedTiles()
    {
        RasterDocument raster; // 2048x1536
        std::vector<Dab> diagonal;
        for (int i = 0; i <= 2000; i += 2)
            diagonal.push_back({QPointF(20 + i, 20 + i * 0.74), 12});
        TileSnapshot stroke;
        raster.stampDabs(diagonal, Qt::black, BrushMode::Paint, &stroke);
        QVERIFY(stroke.byteSize() < qsizetype(2048) * 1536 * 4 / 10);
        TileSnapshot small;
        raster.stampDabs({{QPointF(1000, 700), 10}}, Qt::black, BrushMode::Paint, &small);
        QVERIFY(small.tileCount() <= 4);
    }

    void unchangedTilesAreDropped()
    {
        RasterDocument raster(QSize(200, 200));
        TileSnapshot before;
        raster.stampDabs({{QPointF(100, 100), 50}}, Qt::black, BrushMode::Erase, &before);
        before.dropUnchanged(raster.image());
        QVERIFY(before.isEmpty());
    }

    void paintingOnBlankTilesCostsNoUndoMemory()
    {
        // The undo record of a stroke over unallocated tiles holds "was transparent" for them:
        // no pixels. It still undoes and redoes exactly.
        LayeredDocument doc(QSize(640, 64));
        UndoHistory history(64 * TileBytes);
        const quint64 id = doc.layer(0).id;
        for (int i = 0; i < 5; ++i)
            QVERIFY(paintStep(doc, history, id, QPointF(32 + 128 * i, 32), 10, Qt::red));
        QCOMPARE(history.undoCount(), 5);
        QCOMPARE(history.bytesUsed(), 0);
        QCOMPARE(doc.layer(0).image.allocatedTileCount(), 5);
        for (int i = 0; i < 5; ++i)
            undo(doc, history);
        QCOMPARE(doc.layer(0).image.allocatedTileCount(), 0);
        QCOMPARE(history.bytesUsed(), 5 * TileBytes); // the redo entries hold the painted tiles
        redo(doc, history);
        QCOMPARE(qAlpha(doc.composite().pixel(32, 32)), 255);
    }

    void budgetEvictsOldestUndoEntries()
    {
        LayeredDocument doc(QSize(640, 64));
        doc.pixels(0).fillRect(doc.pixels(0).rect(), 0xffffffff); // white paper (not history)
        doc.refreshComposite();
        UndoHistory history(3 * TileBytes);
        const quint64 id = doc.layer(0).id;
        for (int i = 0; i < 5; ++i)
            QVERIFY(paintStep(doc, history, id, QPointF(32 + 128 * i, 32), 10, Qt::red));
        QCOMPARE(history.undoCount(), 3);
        QCOMPARE(history.bytesUsed(), 3 * TileBytes);
        for (int i = 0; i < 3; ++i)
            undo(doc, history);
        QVERIFY(!history.canUndo());
        QCOMPARE(doc.composite().pixel(32, 32), qRgb(255, 0, 0)); // evicted edits stay
        QCOMPARE(doc.composite().pixel(544, 32), qRgb(255, 255, 255));
    }

    void budgetCountsRedoEntries()
    {
        LayeredDocument doc(QSize(640, 64));
        doc.pixels(0).fillRect(doc.pixels(0).rect(), 0xffffffff);
        doc.refreshComposite();
        UndoHistory history(3 * TileBytes);
        const quint64 id = doc.layer(0).id;
        for (int i = 0; i < 3; ++i)
            paintStep(doc, history, id, QPointF(32 + 128 * i, 32), 10, Qt::red);
        undo(doc, history);
        undo(doc, history);
        QCOMPARE(history.bytesUsed(), 3 * TileBytes);
        paintStep(doc, history, id, QPointF(32 + 128 * 4, 32), 10, Qt::blue);
        QCOMPARE(history.redoCount(), 0);
        QCOMPARE(history.bytesUsed(), 2 * TileBytes);
    }

    void layerCommandsCountRetainedImages()
    {
        const QSize size(512, 256); // 512 KiB per layer image
        const qsizetype layerBytes = qsizetype(size.width()) * size.height() * 4;
        LayeredDocument doc(size);
        UndoHistory history(2 * layerBytes + layerBytes / 2);
        const quint64 a = addLayer(doc, history, "a");
        const quint64 b = addLayer(doc, history, "b");
        const quint64 c = addLayer(doc, history, "c");
        for (const quint64 id : {a, b, c}) // painted all over: every tile allocated
            doc.pixels(doc.indexOf(id)).fillRect(QRect(QPoint(0, 0), size), 0xff102030);
        QCOMPARE(history.bytesUsed(), 0); // inserted layers live in the document
        deleteLayer(doc, history, a);
        deleteLayer(doc, history, b);
        QCOMPARE(history.bytesUsed(), 2 * layerBytes); // deleted layers are retained by history
        deleteLayer(doc, history, c);                  // over budget: oldest entries are evicted
        QVERIFY(history.bytesUsed() <= history.budgetBytes());
        QVERIFY(history.undoCount() < 6);

        // Undoing an insert moves the layer into the redo entry, which is counted too.
        LayeredDocument doc2(size);
        UndoHistory history2(10 * layerBytes);
        const quint64 x = addLayer(doc2, history2, "x");
        doc2.pixels(doc2.indexOf(x)).fillRect(QRect(QPoint(0, 0), size), 0xff102030);
        QCOMPARE(history2.bytesUsed(), 0);
        undo(doc2, history2);
        QCOMPARE(history2.bytesUsed(), layerBytes);
        redo(doc2, history2);
        QCOMPARE(history2.bytesUsed(), 0);
    }

    void editLargerThanBudgetIsNotUndoable()
    {
        LayeredDocument doc(QSize(256, 256));
        doc.pixels(0).fillRect(doc.pixels(0).rect(), 0xffffffff);
        doc.refreshComposite();
        UndoHistory history(TileBytes);
        const quint64 start = history.stateId();
        QVERIFY(paintStep(doc, history, doc.layer(0).id, QPointF(128, 128), 200, Qt::red));
        QVERIFY(!history.canUndo());
        QCOMPARE(history.bytesUsed(), 0);
        QVERIFY(history.stateId() != start);
    }

    void stateIdMatchesSavedStateAfterUndoAndEviction()
    {
        LayeredDocument doc(QSize(640, 64));
        UndoHistory history(2 * TileBytes);
        const quint64 id = doc.layer(0).id;
        paintStep(doc, history, id, QPointF(32, 32), 10, Qt::red);
        const quint64 saved = history.stateId();
        const QImage savedPixels = doc.composite().toImage();
        paintStep(doc, history, id, QPointF(160, 32), 10, Qt::red);
        paintStep(doc, history, id, QPointF(288, 32), 10, Qt::red);
        undo(doc, history);
        undo(doc, history);
        QCOMPARE(history.stateId(), saved);
        QCOMPARE(doc.composite(), savedPixels);
    }

    // --- project files ---
    void layeredRoundTripPreservesEverything()
    {
        const DocumentData original = threeLayerDocument(QSize(37, 23));
        const ProjectFile::LoadResult r = ProjectFile::decode(ProjectFile::encode(original));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.version, 3);
        QCOMPARE(r.document.size, original.size);
        QCOMPARE(r.document.activeLayerId, original.activeLayerId);
        QCOMPARE(r.document.layers.size(), original.layers.size());
        for (size_t i = 0; i < original.layers.size(); ++i) {
            const LayerData &a = original.layers[i], &b = r.document.layers[i];
            QCOMPARE(b.id, a.id);
            QCOMPARE(b.name, a.name);
            QCOMPARE(b.visible, a.visible);
            QCOMPARE(b.opacity, a.opacity);
            QCOMPARE(b.locked, a.locked);
            QCOMPARE(b.image, a.image);
        }
        QCOMPARE(LayeredDocument(r.document).composite(), original.composite());
    }

    void version1ProjectLoadsAsOneLayer()
    {
        const QImage image = randomPremultiplied(40, 30, 9);
        const ProjectFile::LoadResult r = ProjectFile::decode(ProjectFile::encodeVersion1(image));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.version, 1);
        QCOMPARE(r.document.layers.size(), size_t(1));
        QCOMPARE(r.document.layers[0].image, image);
        QVERIFY(r.document.layers[0].visible);
        QCOMPARE(r.document.layers[0].opacity, 1.0);
        QCOMPARE(r.document.activeLayerId, r.document.layers[0].id);
    }

    void roundTripThroughFile()
    {
        QTemporaryDir dir;
        const DocumentData original = threeLayerDocument(QSize(2048, 1536));
        const QString path = dir.filePath("art.drawapp");
        QCOMPARE(ProjectFile::save(original, path), QString());
        const ProjectFile::LoadResult r = ProjectFile::load(path);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.document.layers.size(), size_t(3));
        for (size_t i = 0; i < 3; ++i)
            QCOMPARE(r.document.layers[i].image, original.layers[i].image);
    }

    void rejectsDamagedVersion1Files_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("expected");
        const QImage image = randomPremultiplied(16, 8, 3);
        const QByteArray good = ProjectFile::encodeVersion1(image);
        const QByteArray payload = good.mid(ProjectFile::HeaderSize);
        QTest::newRow("empty") << QByteArray() << "not a DrawApp project";
        QTest::newRow("png file") << QByteArray("\x89PNG\r\n\x1a\n0000000000", 18) << "not a DrawApp project";
        QTest::newRow("header cut short") << good.left(40) << "damaged";
        QTest::newRow("payload truncated") << good.left(good.size() - 5) << "damaged";
        QTest::newRow("trailing bytes") << good + QByteArray("xx") << "damaged";
        QByteArray flipped = good;
        flipped[ProjectFile::HeaderSize + 10] = char(flipped[ProjectFile::HeaderSize + 10] ^ 0x55);
        QTest::newRow("payload byte flipped") << flipped << "checksum";
        QTest::newRow("newer version") << makeV1(ProjectFile::CurrentVersion + 1, 16, 8, 1, 1, payload) << "newer version";
        QTest::newRow("version zero") << makeV1(0, 16, 8, 1, 1, payload) << "damaged";
        QTest::newRow("zero width") << makeV1(1, 0, 8, 1, 1, payload) << "damaged";
        QTest::newRow("huge dimensions") << makeV1(1, 100000, 100000, 1, 1, payload) << "too large";
        QTest::newRow("too many pixels") << makeV1(1, 16384, 16384, 1, 1, payload) << "too large";
        QTest::newRow("unknown pixel format") << makeV1(1, 16, 8, 7, 1, payload) << "pixel format";
        QTest::newRow("bad checksum") << makeV1(1, 16, 8, 1, 1, payload, false) << "checksum";
        QTest::newRow("size mismatch") << makeV1(1, 32, 32, 1, 1, payload) << "does not match";
        QByteArray liar = payload;
        qToBigEndian<quint32>(0x7fffffff, liar.data());
        QTest::newRow("claims huge raw size") << makeV1(1, 16, 8, 1, 1, liar) << "does not match";
        QByteArray garbage(4, '\0');
        qToBigEndian<quint32>(16 * 8 * 4, garbage.data());
        garbage.append("not zlib data at all");
        QTest::newRow("corrupt zlib") << makeV1(1, 16, 8, 1, 1, garbage) << "decompressed";
        QByteArray invalidPixels;
        for (int i = 0; i < 16 * 8; ++i)
            invalidPixels.append(QByteArray::fromHex("c800000a"));
        QTest::newRow("invalid premultiplied pixels") << makeV1(1, 16, 8, 1, 1, qCompress(invalidPixels)) << "invalid pixel";
    }

    void rejectsDamagedVersion1Files()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, expected);
        const ProjectFile::LoadResult r = ProjectFile::decode(data);
        QVERIFY(!r.ok());
        QVERIFY(r.document.layers.empty());
        QVERIFY2(r.error.contains(expected, Qt::CaseInsensitive), qPrintable(r.error));
    }

    void rejectsDamagedLayeredFiles_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("expected");
        const QByteArray good = ProjectFile::encode(threeLayerDocument(QSize(8, 8)));
        const QByteArray b = block(8, 8);
        auto meta = [&](const QJsonArray &layers, const QString &active = "1") {
            QJsonObject o;
            o["activeLayerId"] = active;
            o["layers"] = layers;
            return o;
        };
        QTest::newRow("truncated") << good.left(good.size() - 7) << "damaged";
        QTest::newRow("version 2 truncated") << ProjectFile::encodeVersion2(threeLayerDocument(QSize(8, 8))).chopped(7)
                                             << "damaged";
        QByteArray flipped = good;
        flipped[flipped.size() - 20] = char(flipped[flipped.size() - 20] ^ 0x40);
        QTest::newRow("byte flipped") << flipped << "checksum";
        QTest::newRow("too many layers") << makeV2(8, 8, 100, meta({layerMeta("1", b.size())}), {b}) << "too large";
        // 2048x1536 x 50 layers = 600 MiB > 512 MiB: refused from the header alone.
        QTest::newRow("total pixels too large") << makeV2(2048, 1536, 50, meta({}), {}) << "too large";
        QTest::newRow("no layers") << makeV2(8, 8, 0, meta({}), {}) << "no layers";
        QTest::newRow("count mismatch") << makeV2(8, 8, 2, meta({layerMeta("1", b.size())}), {b}) << "count";
        QTest::newRow("duplicate ids")
            << makeV2(8, 8, 2, meta({layerMeta("1", b.size()), layerMeta("1", b.size())}), {b, b}) << "id";
        QTest::newRow("zero id") << makeV2(8, 8, 1, meta({layerMeta("0", b.size())}), {b}) << "id";
        QTest::newRow("opacity out of range") << makeV2(8, 8, 1, meta({layerMeta("1", b.size(), 1.5)}), {b}) << "opacity";
        QTest::newRow("block size too large") << makeV2(8, 8, 1, meta({layerMeta("1", b.size() + 50)}), {b}) << "size";
        QTest::newRow("extra data") << makeV2(8, 8, 1, meta({layerMeta("1", b.size())}), {b, "junk"}) << "after the last layer";
        QTest::newRow("wrong pixel size") << makeV2(8, 8, 1, meta({layerMeta("1", block(4, 4).size())}), {block(4, 4)})
                                          << "does not match";
        QJsonObject notArray;
        notArray["layers"] = "nope";
        QTest::newRow("bad metadata") << makeV2(8, 8, 1, notArray, {b}) << "count";
    }

    void rejectsDamagedLayeredFiles()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, expected);
        const ProjectFile::LoadResult r = ProjectFile::decode(data);
        QVERIFY(!r.ok());
        QVERIFY(r.document.layers.empty());
        QVERIFY2(r.error.contains(expected, Qt::CaseInsensitive), qPrintable(r.error));
    }

    void rejectsDamagedTiledFiles_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("expected");
        // 100 x 70 canvas, 64-pixel tiles: 2 x 2 tiles; the right and bottom ones are clipped.
        auto file = [](const QByteArray &stream, int tiles, QJsonValue tileSize = 64, quint32 w = 100, quint32 h = 70) {
            QJsonObject o;
            o["activeLayerId"] = "1";
            o["tileSize"] = tileSize;
            o["layers"] = QJsonArray{tiledLayerMeta("1", stream.size(), tiles)};
            return makeV2(w, h, 1, o, {stream}, 3);
        };
        const QByteArray full = greyTile(64, 64), clipped = greyTile(36, 64);
        const QByteArray good = tileStream({{0, 0, full}, {1, 0, clipped}});
        QTest::newRow("good file (control)") << file(good, 2) << QString();
        QTest::newRow("tile outside the canvas") << file(tileStream({{2, 0, full}}), 1) << "outside the canvas";
        QTest::newRow("tile row outside") << file(tileStream({{0, 5, full}}), 1) << "outside the canvas";
        QTest::newRow("tile repeated") << file(tileStream({{0, 0, full}, {0, 0, full}}), 2) << "repeated";
        QTest::newRow("tiles out of order") << file(tileStream({{1, 0, clipped}, {0, 0, full}}), 2) << "out of order";
        QTest::newRow("count differs from metadata") << file(good, 3) << "tile count";
        QTest::newRow("declared count too large") << file(tileStream({}, 1000000), 1000000) << "tile count";
        QTest::newRow("edge tile stored unclipped") << file(tileStream({{1, 0, full}}), 1) << "does not match";
        QTest::newRow("tile cut short") << file(good.chopped(10), 2) << "size";
        QTest::newRow("entry header cut short") << file(tileStream({{0, 0, full}}).append("abc"), 1) << "after the last tile";
        QByteArray liar = full;
        qToBigEndian<quint32>(0x7fffffff, liar.data());
        QTest::newRow("claims huge raw size") << file(tileStream({{0, 0, liar}}), 1) << "does not match";
        QByteArray notZlib(4, '\0');
        qToBigEndian<quint32>(64 * 64 * 4, notZlib.data());
        notZlib.append("not zlib data");
        QTest::newRow("corrupt zlib") << file(tileStream({{0, 0, notZlib}}), 1) << "decompressed";
        QByteArray bad;
        for (int i = 0; i < 64 * 64; ++i)
            bad.append(QByteArray::fromHex("c80000" "0a"));
        QTest::newRow("invalid premultiplied pixels") << file(tileStream({{0, 0, qCompress(bad)}}), 1) << "invalid pixel";
        QTest::newRow("tile size not a power of two") << file(good, 2, 48) << "tile size";
        QTest::newRow("tile size too small") << file(good, 2, 8) << "tile size";
        QTest::newRow("tile size missing") << file(good, 2, QJsonValue()) << "tile size";
        QTest::newRow("too many layers") << makeV2(8, 8, 100, QJsonObject(), {}, 3) << "too large";
        QTest::newRow("side too long") << makeV2(20000, 8, 1, QJsonObject(), {}, 3) << "too large";
    }

    void rejectsDamagedTiledFiles()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, expected);
        const ProjectFile::LoadResult r = ProjectFile::decode(data);
        if (expected.isEmpty()) {
            QVERIFY2(r.ok(), qPrintable(r.error));
            QCOMPARE(r.document.layers.front().image.allocatedTileCount(), 2);
            QCOMPARE(r.document.layers.front().image.pixel(99, 63), qRgba(128, 128, 128, 255));
            QCOMPARE(r.document.layers.front().image.pixel(10, 66), 0u);
            QCOMPARE(ProjectFile::verify(data), QString());
            return;
        }
        QVERIFY(!r.ok());
        QVERIFY(r.document.layers.empty());
        QVERIFY2(r.error.contains(expected, Qt::CaseInsensitive), qPrintable(r.error));
    }

    void version2FilesStillOpen()
    {
        // Projects saved by milestones 3 to 9 (dense layers) open into sparse layers with the same
        // pixels; fully transparent tiles are not allocated.
        DocumentData original = threeLayerDocument(QSize(200, 150));
        original.layers[1].image.fillRect(QRect(0, 0, 200, 150), 0);
        original.layers[1].image.fillRect(QRect(70, 70, 10, 10), 0xff00ff00);
        const QByteArray v2 = ProjectFile::encodeVersion2(original);
        QCOMPARE(qFromLittleEndian<quint32>(v2.constData() + 8), 2u);
        const ProjectFile::LoadResult r = ProjectFile::decode(v2);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.version, 2);
        for (size_t i = 0; i < original.layers.size(); ++i)
            QVERIFY(r.document.layers[i].image == original.layers[i].image);
        QCOMPARE(r.document.layers[1].image.allocatedTileCount(), 1);
        // Saved again, it is version 3, sparse, and still the same pixels.
        const QByteArray v3 = ProjectFile::encode(r.document);
        QCOMPARE(qFromLittleEndian<quint32>(v3.constData() + 8), 3u);
        const ProjectFile::LoadResult again = ProjectFile::decode(v3);
        QVERIFY2(again.ok(), qPrintable(again.error));
        for (size_t i = 0; i < original.layers.size(); ++i)
            QVERIFY(again.document.layers[i].image == original.layers[i].image);
    }

    void sparseFilesStaySmall()
    {
        // A 16384 x 16384 canvas with one small mark: the file stores one tile, and opening it
        // allocates one tile (the dense formats could not hold this canvas at all).
        DocumentData doc;
        doc.size = QSize(16384, 16384);
        LayerData layer;
        layer.id = 1;
        layer.name = "Mark";
        layer.image = TiledImage(doc.size);
        layer.image.fillRect(QRect(10000, 9000, 20, 20), 0xff000000);
        doc.layers.push_back(layer);
        doc.activeLayerId = 1;
        const QByteArray bytes = ProjectFile::encode(doc);
        QVERIFY2(bytes.size() < 4096, qPrintable(QString::number(bytes.size())));
        QCOMPARE(ProjectFile::verify(bytes), QString());
        const ProjectFile::LoadResult r = ProjectFile::decode(bytes);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.document.size, doc.size);
        QCOMPARE(r.document.layers.front().image.allocatedTileCount(), 1);
        QCOMPARE(r.document.layers.front().image.pixel(10010, 9010), 0xff000000u);
    }

    void openingRespectsTheMemoryBudget()
    {
        // Tiles are allocated against MemoryBudget while decoding: a project that does not fit is
        // refused with a message, and nothing stays allocated.
        DocumentData doc = threeLayerDocument(QSize(300, 300));
        const QByteArray bytes = ProjectFile::encode(doc);
        const qint64 before = MemoryBudget::used(MemoryBudget::Category::Document);
        MemoryBudget::failReservationsForTesting(5, 1000);
        const ProjectFile::LoadResult r = ProjectFile::decode(bytes);
        MemoryBudget::failReservationsForTesting(-1, 0);
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains("memory"), qPrintable(r.error));
        QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document), before);
        QVERIFY(ProjectFile::decode(bytes).ok());
    }

    void unknownActiveLayerFallsBackToTop()
    {
        DocumentData doc = threeLayerDocument(QSize(8, 8));
        doc.activeLayerId = 999;
        const ProjectFile::LoadResult r = ProjectFile::decode(ProjectFile::encode(doc));
        QVERIFY(r.ok());
        QCOMPARE(r.document.activeLayerId, doc.layers.back().id);
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
            QVERIFY(f.resize(ProjectFile::MaxFileBytes + 1));
        }
        QElapsedTimer timer;
        timer.start();
        const ProjectFile::LoadResult r = ProjectFile::load(big);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("too large"));
        QVERIFY(timer.elapsed() < 2000);
    }

    void failedSaveKeepsPreviousFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("keep.drawapp");
        const DocumentData first = threeLayerDocument(QSize(40, 30));
        QCOMPARE(ProjectFile::save(first, path), QString());
        const QByteArray original = readAll(path);
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser));
        const QString error = ProjectFile::save(DocumentData::fromImage(randomPremultiplied(40, 30, 2)), path);
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(!error.isEmpty());
        QCOMPARE(readAll(path), original);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"keep.drawapp"});
        QVERIFY(!ProjectFile::save(first, dir.filePath("no/such/folder/x.drawapp")).isEmpty());
    }

    // --- verification before writing (milestone 5) ---
    void verifyAcceptsEncodedProjectsAndRejectsDamage()
    {
        const QByteArray good = ProjectFile::encode(threeLayerDocument(QSize(50, 40)));
        QCOMPARE(ProjectFile::verify(good), QString());
        QByteArray flipped = good;
        flipped[flipped.size() - 20] = char(flipped[flipped.size() - 20] ^ 0x10);
        QVERIFY(!ProjectFile::verify(flipped).isEmpty());               // checksum
        QVERIFY(!ProjectFile::verify(good.left(good.size() - 1)).isEmpty()); // cut short
        QVERIFY(!ProjectFile::verify(good.left(30)).isEmpty());
        QVERIFY(!ProjectFile::verify(QByteArray("DRAWAPP\n")).isEmpty());

        // A layer block that does not decompress, behind a correct checksum.
        QByteArray badBlock = good;
        const quint32 metaSize = qFromLittleEndian<quint32>(badBlock.constData() + 24);
        const qsizetype blockStart = ProjectFile::HeaderSize + metaSize;
        badBlock[blockStart + 6] = char(badBlock[blockStart + 6] ^ 0x55);
        const QByteArray payload = badBlock.mid(ProjectFile::HeaderSize);
        const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
        badBlock.replace(36, 32, digest);
        QVERIFY(!ProjectFile::verify(badBlock).isEmpty());
        QVERIFY(!ProjectFile::decode(badBlock).ok()); // decode agrees
    }

    void localFilesAreNeverWrittenInPlace()
    {
        // ProjectFile's direct writer is for local files only: content URIs must go through SafeSave.
        QVERIFY(!ProjectFile::writeFileSafely(QStringLiteral("content://x/y"), "abc").isEmpty());
    }

    // --- PNG export ---
    void pngExportMatchesDocumentPixels()
    {
        QTemporaryDir dir;
        const QImage image = randomPremultiplied(64, 48, 11);
        const QString path = dir.filePath("out.png");
        QCOMPARE(ProjectFile::exportPng(image, path), QString());
        QImage png(path);
        QCOMPARE(png.size(), image.size());
        QCOMPARE(png.convertToFormat(QImage::Format_ARGB32), image.convertToFormat(QImage::Format_ARGB32));
    }

    void pngExportOfLayersMatchesVisibleComposite()
    {
        QTemporaryDir dir;
        const DocumentData doc = threeLayerDocument(QSize(50, 40)); // one hidden, one at 37 %
        const QImage composite = doc.composite();
        const QString path = dir.filePath("layers.png");
        QCOMPARE(ProjectFile::exportPng(composite, path), QString());
        QImage png(path);
        QCOMPARE(png.convertToFormat(QImage::Format_ARGB32), composite.convertToFormat(QImage::Format_ARGB32));
        // The hidden layer does not contribute.
        DocumentData withoutHidden = doc;
        withoutHidden.layers.erase(withoutHidden.layers.begin() + 1);
        QCOMPARE(withoutHidden.composite(), composite);
    }

    void readBoundedAllocatesWhatItReads()
    {
        // Milestone 9 regression: QIODevice::read(limit) reserved the whole project limit
        // (> 512 MiB of committed memory) to open a small file.
        QTemporaryDir dir;
        const QString path = dir.filePath("small.bin");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(1000, 'x'));
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray data = ProjectFile::readBounded(f, ProjectFile::MaxFileBytes);
        QCOMPARE(data.size(), 1000);
        QVERIFY(data.capacity() < 64 * 1024);
        // Oversized input: one byte past the limit, so it can be refused.
        QVERIFY(f.seek(0));
        QCOMPARE(ProjectFile::readBounded(f, 300).size(), 301);
        // A sequential device is read in bounded chunks.
        QBuffer seq;
        seq.setData(QByteArray(5000, 'y'));
        QVERIFY(seq.open(QIODevice::ReadOnly));
        QCOMPARE(ProjectFile::readBounded(seq, 1 << 20).size(), 5000);
        // And a small project still opens.
        const QString project = dir.filePath("p.drawapp");
        QCOMPARE(ProjectFile::save(threeLayerDocument(QSize(30, 20)), project), QString());
        QVERIFY(ProjectFile::load(project).ok());
    }
};

QTEST_GUILESS_MAIN(TestDocument)
#include "tst_document.moc"
