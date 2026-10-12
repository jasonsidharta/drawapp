// Recovery file format and RecoveryManager (worker writes, stale completions, orphans).

#include "LayeredDocument.h"
#include "ProjectFile.h"
#include "RasterDocument.h"
#include "RecoveryFile.h"
#include "RecoveryManager.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {

QImage randomPremultiplied(int w, int h, quint32 seed)
{
    QRandomGenerator rng(seed);
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = int(rng.bounded(256));
            line[x] = qRgba(int(rng.bounded(a + 1)), int(rng.bounded(a + 1)), int(rng.bounded(a + 1)), a);
        }
    }
    return image;
}

RecoveryFile::Metadata meta(quint64 doc, quint64 rev, const QString &path = QString())
{
    RecoveryFile::Metadata m;
    m.documentId = doc;
    m.revision = rev;
    m.savedAt = QDateTime::currentDateTimeUtc();
    m.originalPath = path;
    return m;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

// Rebuilds a recovery file with replaced metadata JSON (checksum recomputed).
QByteArray withMetadata(const QByteArray &good, const QByteArray &newMeta)
{
    const quint32 oldMeta = qFromLittleEndian<quint32>(good.constData() + 12);
    const QByteArray rest = good.mid(48 + oldMeta); // project length + project
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData(good.constData(), 8);
    s << quint32(1) << quint32(newMeta.size());
    const QByteArray digest = QCryptographicHash::hash(newMeta, QCryptographicHash::Sha256);
    s.writeRawData(digest.constData(), 32);
    s.writeRawData(newMeta.constData(), int(newMeta.size()));
    out.append(rest);
    return out;
}

bool waitForWrite(QSignalSpy &spy, int count = 1)
{
    return QTest::qWaitFor([&] { return spy.count() >= count; }, 10000);
}

} // namespace

class TestRecovery : public QObject
{
    Q_OBJECT

private slots:
    // --- file format ---
    void fileRoundTrip()
    {
        const QImage image = randomPremultiplied(33, 21, 5);
        RecoveryFile::Metadata m = meta(7, 42, QStringLiteral("C:/art/picture.drawapp"));
        m.originalUrl = QStringLiteral("file:///C:/art/picture.drawapp");
        const RecoveryFile::Contents c = RecoveryFile::decode(RecoveryFile::encode(DocumentData::fromImage(image), m));
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(c.document.layers[0].image, image);
        QCOMPARE(c.metadata.documentId, quint64(7));
        QCOMPARE(c.metadata.revision, quint64(42));
        QCOMPARE(c.metadata.originalPath, m.originalPath);
        QCOMPARE(c.metadata.originalUrl, m.originalUrl);
        QCOMPARE(c.metadata.savedAt.toMSecsSinceEpoch(), m.savedAt.toMSecsSinceEpoch());
    }

    void corruptFilesAreRejected_data()
    {
        QTest::addColumn<QByteArray>("data");
        const QByteArray good = RecoveryFile::encode(DocumentData::fromImage(randomPremultiplied(8, 8, 1)), meta(1, 1));
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("project file instead") << ProjectFile::encode(DocumentData::fromImage(randomPremultiplied(8, 8, 1)));
        QTest::newRow("header cut short") << good.left(30);
        QTest::newRow("truncated") << good.left(good.size() - 10);
        QTest::newRow("trailing bytes") << good + "x";
        QByteArray version = good;
        qToLittleEndian<quint32>(9, version.data() + 8);
        QTest::newRow("unknown version") << version;
        QByteArray metaLength = good;
        qToLittleEndian<quint32>(0x7fffffff, metaLength.data() + 12);
        QTest::newRow("huge metadata length") << metaLength;
        QByteArray metaFlip = good;
        metaFlip[50] = char(metaFlip[50] ^ 0x20);
        QTest::newRow("metadata changed") << metaFlip;
        QTest::newRow("metadata not json") << withMetadata(good, "not json {");
        QByteArray pixelFlip = good;
        pixelFlip[pixelFlip.size() - 3] = char(pixelFlip[pixelFlip.size() - 3] ^ 0x11);
        QTest::newRow("embedded project damaged") << pixelFlip;
    }

    void corruptFilesAreRejected()
    {
        QFETCH(QByteArray, data);
        const RecoveryFile::Contents c = RecoveryFile::decode(data);
        QVERIFY(!c.ok());
        QVERIFY(c.document.layers.empty());
    }

    void layeredRoundTrip()
    {
        DocumentData doc;
        doc.size = QSize(24, 16);
        for (int i = 0; i < 4; ++i) {
            LayerData layer;
            layer.id = quint64(100 + i);
            layer.name = QStringLiteral("L%1").arg(i);
            layer.image = randomPremultiplied(24, 16, quint32(i + 7));
            layer.visible = i != 2;
            layer.opacity = 0.25 * (i + 1);
            layer.locked = i == 3;
            doc.layers.push_back(layer);
        }
        doc.activeLayerId = 102;
        const RecoveryFile::Contents c = RecoveryFile::decode(RecoveryFile::encode(doc, meta(5, 6)));
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(c.recoveryVersion, quint32(2));
        QCOMPARE(c.projectVersion, 2);
        QCOMPARE(c.document.activeLayerId, quint64(102));
        QCOMPARE(c.document.layers.size(), size_t(4));
        for (size_t i = 0; i < 4; ++i) {
            QCOMPARE(c.document.layers[i].id, doc.layers[i].id);
            QCOMPARE(c.document.layers[i].name, doc.layers[i].name);
            QCOMPARE(c.document.layers[i].visible, doc.layers[i].visible);
            QCOMPARE(c.document.layers[i].opacity, doc.layers[i].opacity);
            QCOMPARE(c.document.layers[i].locked, doc.layers[i].locked);
            QCOMPARE(c.document.layers[i].image, doc.layers[i].image);
        }
    }

    void legacySingleLayerRecoveryIsReadAsOneLayer()
    {
        const QImage image = randomPremultiplied(30, 20, 3);
        const RecoveryFile::Contents c = RecoveryFile::decode(RecoveryFile::encodeVersion1(image, meta(1, 4, "C:/old.drawapp")));
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(c.recoveryVersion, quint32(1));
        QCOMPARE(c.projectVersion, 1);
        QCOMPARE(c.document.layers.size(), size_t(1));
        QCOMPARE(c.document.layers[0].image, image);
        QCOMPARE(c.metadata.originalPath, QStringLiteral("C:/old.drawapp"));
    }

    void snapshotIsIsolatedFromLaterEdits()
    {
        LayeredDocument live(QSize(64, 64));
        const DocumentData snapshot = live.snapshot(); // shares pixels until the live side writes
        const QImage before = snapshot.layers[0].image.copy();
        RasterDocument::stampDabs(live.pixels(0), {{QPointF(32, 32), 40}}, Qt::red, BrushMode::Paint);
        QVERIFY(live.layer(0).image != before);
        QCOMPARE(snapshot.layers[0].image, before); // the worker's copy did not change
        QVERIFY(snapshot.layers[0].image.constBits() != live.layer(0).image.constBits());
    }

    // --- manager writes ---
    void writeCommitsAtomically()
    {
        QTemporaryDir dir;
        RecoveryManager m(dir.filePath("recovery"));
        QCOMPARE(m.start(), QString());
        QVERIFY(QFileInfo(m.recoveryFilePath()).absolutePath() == QFileInfo(dir.filePath("recovery")).absoluteFilePath());
        QSignalSpy spy(&m, &RecoveryManager::writeFinished);
        const QImage image = randomPremultiplied(64, 40, 2);
        QVERIFY(m.requestWrite(DocumentData::fromImage(image), meta(1, 5)));
        QVERIFY(m.isWriting());
        QVERIFY(waitForWrite(spy));
        QCOMPARE(spy.first().at(0).toBool(), true);  // committed
        QCOMPARE(spy.first().at(1).toBool(), false); // not stale
        QVERIFY(!m.isWriting());
        QVERIFY(m.hasRecoveryFile());
        const RecoveryFile::Contents c = RecoveryFile::load(m.recoveryFilePath());
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(c.document.layers[0].image, image);
        QCOMPARE(c.metadata.revision, quint64(5));
        // Only the recovery file and the session lock: no temporary files left behind.
        QCOMPARE(QDir(dir.filePath("recovery")).entryList(QDir::Files).size(), 2);
    }

    void onlyOneWriteAtATime()
    {
        QTemporaryDir dir;
        RecoveryManager m(dir.path());
        QCOMPARE(m.start(), QString());
        QSemaphore gate;
        m.setBeforeCommitHookForTesting([&] { gate.acquire(); });
        QSignalSpy spy(&m, &RecoveryManager::writeFinished);
        QVERIFY(m.requestWrite(DocumentData::fromImage(randomPremultiplied(16, 16, 1)), meta(1, 1)));
        QVERIFY(!m.requestWrite(DocumentData::fromImage(randomPremultiplied(16, 16, 2)), meta(1, 2)));
        gate.release();
        QVERIFY(waitForWrite(spy));
        QVERIFY(m.requestWrite(DocumentData::fromImage(randomPremultiplied(16, 16, 3)), meta(1, 3)));
        gate.release();
        QVERIFY(waitForWrite(spy, 2));
        QCOMPARE(RecoveryFile::load(m.recoveryFilePath()).metadata.revision, quint64(3));
    }

    void staleWriteDoesNotRecreateDiscardedFile()
    {
        QTemporaryDir dir;
        RecoveryManager m(dir.path());
        QCOMPARE(m.start(), QString());
        QSignalSpy spy(&m, &RecoveryManager::writeFinished);
        QVERIFY(m.requestWrite(DocumentData::fromImage(randomPremultiplied(16, 16, 1)), meta(1, 1)));
        QVERIFY(waitForWrite(spy));
        QVERIFY(QFile::exists(m.recoveryFilePath()));

        QSemaphore gate;
        m.setBeforeCommitHookForTesting([&] { gate.acquire(); });
        QVERIFY(m.requestWrite(DocumentData::fromImage(randomPremultiplied(16, 16, 2)), meta(1, 2)));
        m.discard(); // for example the document was saved while the write was running
        QVERIFY(!QFile::exists(m.recoveryFilePath()));
        gate.release();
        QVERIFY(waitForWrite(spy, 2));
        QCOMPARE(spy.at(1).at(0).toBool(), false); // not committed
        QCOMPARE(spy.at(1).at(1).toBool(), true);  // stale
        QVERIFY(!QFile::exists(m.recoveryFilePath()));
        QVERIFY(!m.hasRecoveryFile());
    }

    void staleWriteCannotReplaceNewerDocument()
    {
        QTemporaryDir dir;
        RecoveryManager m(dir.path());
        QCOMPARE(m.start(), QString());
        QSignalSpy spy(&m, &RecoveryManager::writeFinished);
        QSemaphore gate;
        m.setBeforeCommitHookForTesting([&] { gate.acquire(); });
        QVERIFY(m.requestWrite(DocumentData::fromImage(randomPremultiplied(16, 16, 1)), meta(1, 9))); // old document
        m.discard();                                                          // New / Open
        gate.release();
        QVERIFY(waitForWrite(spy));
        QVERIFY(spy.at(0).at(1).toBool());
        const QImage newer = randomPremultiplied(16, 16, 2);
        QVERIFY(m.requestWrite(DocumentData::fromImage(newer), meta(2, 1))); // new document
        gate.release();
        QVERIFY(waitForWrite(spy, 2));
        const RecoveryFile::Contents c = RecoveryFile::load(m.recoveryFilePath());
        QCOMPARE(c.metadata.documentId, quint64(2));
        QCOMPARE(c.document.layers[0].image, newer);
    }

    void failedWriteKeepsPreviousRecovery()
    {
        QTemporaryDir dir;
        RecoveryManager m(dir.path());
        QCOMPARE(m.start(), QString());
        QSignalSpy spy(&m, &RecoveryManager::writeFinished);
        const QImage first = randomPremultiplied(16, 16, 1);
        QVERIFY(m.requestWrite(DocumentData::fromImage(first), meta(1, 1)));
        QVERIFY(waitForWrite(spy));
        const QByteArray before = readAll(m.recoveryFilePath());

        QVERIFY(QFile::setPermissions(m.recoveryFilePath(), QFileDevice::ReadOwner | QFileDevice::ReadUser));
        QVERIFY(m.requestWrite(DocumentData::fromImage(randomPremultiplied(16, 16, 2)), meta(1, 2)));
        QVERIFY(waitForWrite(spy, 2));
        QFile::setPermissions(m.recoveryFilePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(spy.at(1).at(0).toBool(), false);
        QVERIFY(!spy.at(1).at(2).toString().isEmpty()); // error message
        QCOMPARE(readAll(m.recoveryFilePath()), before);
        QCOMPARE(RecoveryFile::load(m.recoveryFilePath()).document.layers[0].image, first);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 2); // no temporary leftovers
    }

    void writeNowAndShutdown()
    {
        QTemporaryDir dir;
        {
            RecoveryManager m(dir.path());
            QCOMPARE(m.start(), QString());
            QCOMPARE(m.writeNow(DocumentData::fromImage(randomPremultiplied(8, 8, 1)), meta(1, 1)), QString());
            QVERIFY(QFile::exists(m.recoveryFilePath()));
            m.finishCleanly();
            QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty()); // file and lock removed
        }
        {
            RecoveryManager m(dir.path());
            QCOMPARE(m.start(), QString());
            QCOMPARE(m.writeNow(DocumentData::fromImage(randomPremultiplied(8, 8, 2)), meta(1, 2)), QString());
            m.releaseKeepingFile();
            QCOMPARE(QDir(dir.path()).entryList({"*.drawrecovery"}, QDir::Files).size(), 1);
            QVERIFY(QDir(dir.path()).entryList({"*.lock"}, QDir::Files).isEmpty());
        }
    }

    // --- orphans ---
    void orphansFromEndedSessionsAreFound()
    {
        QTemporaryDir dir;
        const QImage image = randomPremultiplied(20, 10, 4);
        {
            RecoveryManager ended(dir.path());
            QCOMPARE(ended.start(), QString());
            QCOMPARE(ended.writeNow(DocumentData::fromImage(image), meta(3, 8, QStringLiteral("C:/x.drawapp"))), QString());
            ended.releaseKeepingFile(); // like a crash: the recovery file remains, nobody owns it
        }
        // Leftover temporary file from a write interrupted by the crash.
        const QString orphanPath = QDir(dir.path()).entryList({"*.drawrecovery"}, QDir::Files).first();
        writeFile(dir.filePath(orphanPath + ".Ab12Cd"), "partial");

        RecoveryManager live(dir.path());
        QCOMPARE(live.start(), QString());
        const QList<RecoveryManager::Orphan> orphans = live.findOrphans();
        QCOMPARE(orphans.size(), 1);
        QVERIFY(orphans.first().contents.ok());
        QCOMPARE(orphans.first().contents.document.layers[0].image, image);
        QCOMPARE(orphans.first().contents.metadata.originalPath, QStringLiteral("C:/x.drawapp"));
        QVERIFY(!QFile::exists(dir.filePath(orphanPath + ".Ab12Cd")));

        // Adopting moves it to the live session's recovery file.
        QCOMPARE(live.adopt(orphans.first()), QString());
        QVERIFY(!QFile::exists(orphans.first().path));
        QVERIFY(live.hasRecoveryFile());
        QCOMPARE(RecoveryFile::load(live.recoveryFilePath()).document.layers[0].image, image);
        QVERIFY(live.findOrphans().isEmpty());
    }

    void runningSessionsAreNotOrphans()
    {
        QTemporaryDir dir;
        RecoveryManager first(dir.path());
        QCOMPARE(first.start(), QString());
        QCOMPARE(first.writeNow(DocumentData::fromImage(randomPremultiplied(8, 8, 1)), meta(1, 1)), QString());
        RecoveryManager second(dir.path());
        QCOMPARE(second.start(), QString());
        QVERIFY(second.findOrphans().isEmpty()); // first still holds its lock
    }

    void damagedOrphansAreSetAside()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("session-dead.drawrecovery"), "DRAWRCV\ngarbage that is not a recovery file");
        RecoveryManager m(dir.path());
        QCOMPARE(m.start(), QString());
        QList<RecoveryManager::Orphan> orphans = m.findOrphans();
        QCOMPARE(orphans.size(), 1);
        QVERIFY(!orphans.first().contents.ok());
        const QString aside = m.setAside(orphans.first());
        QVERIFY(QFile::exists(aside));
        QVERIFY(m.findOrphans().isEmpty());
        QVERIFY(QFile::exists(aside)); // set-aside files are not treated as leftovers
    }

    void discardedOrphanIsRemoved()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("session-dead.drawrecovery"), RecoveryFile::encode(DocumentData::fromImage(randomPremultiplied(8, 8, 1)), meta(1, 1)));
        RecoveryManager m(dir.path());
        QCOMPARE(m.start(), QString());
        const QList<RecoveryManager::Orphan> orphans = m.findOrphans();
        QCOMPARE(orphans.size(), 1);
        m.removeOrphan(orphans.first());
        QVERIFY(m.findOrphans().isEmpty());
        QVERIFY(QDir(dir.path()).entryList({"session-dead*"}, QDir::Files).isEmpty());
    }

    void unusableFolderDoesNotCrash()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("blocked"), "a file where the folder should be");
        RecoveryManager m(dir.filePath("blocked/recovery"));
        QVERIFY(!m.start().isEmpty());
        QVERIFY(!m.requestWrite(DocumentData::fromImage(randomPremultiplied(8, 8, 1)), meta(1, 1)));
        QVERIFY(m.findOrphans().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRecovery)
#include "tst_recovery.moc"
