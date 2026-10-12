// Project and recovery files (milestone 10): compatibility with files written by earlier
// milestones, round trips through format 3, and malformed input.
#include "DocumentData.h"
#include "MemoryBudget.h"
#include "ProjectFile.h"
#include "RecoveryFile.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {

const QString Compat = QStringLiteral(DRAWAPP_SOURCE_DIR "/tests/golden/compat/");

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString imageHash(const QImage &img)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (int y = 0; y < img.height(); ++y)
        h.addData(QByteArrayView(reinterpret_cast<const char *>(img.constScanLine(y)), img.width() * 4));
    return QString::fromLatin1(h.result().toHex());
}

// Compares a decoded document with the description milestone 9 wrote (m9-fixtures.json).
QString differences(const DocumentData &d, const QJsonObject &expected)
{
    if (d.size != QSize(expected["width"].toInt(), expected["height"].toInt()))
        return QStringLiteral("size");
    if (QString::number(d.activeLayerId) != expected["activeLayerId"].toString())
        return QStringLiteral("active layer");
    const QJsonArray layers = expected["layers"].toArray();
    if (int(d.layers.size()) != layers.size())
        return QStringLiteral("layer count");
    for (int i = 0; i < layers.size(); ++i) {
        const QJsonObject e = layers[i].toObject();
        const LayerData &l = d.layers[size_t(i)];
        if (QString::number(l.id) != e["id"].toString() || l.name != e["name"].toString()
            || l.visible != e["visible"].toBool() || l.locked != e["locked"].toBool()
            || !qFuzzyCompare(l.opacity, e["opacity"].toDouble()))
            return QStringLiteral("layer %1 properties").arg(i);
        if (imageHash(l.image.toImage()) != e["sha256"].toString())
            return QStringLiteral("layer %1 pixels").arg(i);
    }
    if (imageHash(d.composite()) != expected["compositeSha256"].toString())
        return QStringLiteral("composite");
    return {};
}

QByteArray withChecksum(QByteArray bytes)
{
    if (bytes.size() >= ProjectFile::HeaderSize) {
        const QByteArray digest =
            QCryptographicHash::hash(QByteArrayView(bytes).sliced(ProjectFile::HeaderSize), QCryptographicHash::Sha256);
        std::memcpy(bytes.data() + 36, digest.constData(), 32);
        qToLittleEndian(quint64(bytes.size() - ProjectFile::HeaderSize), bytes.data() + 28);
    }
    return bytes;
}

// A decoded document must be usable: sizes agree, ids are unique, at least one layer.
bool valid(const DocumentData &d)
{
    if (d.layers.empty() || d.size.isEmpty())
        return false;
    QSet<quint64> ids;
    for (const LayerData &l : d.layers) {
        if (l.image.size() != d.size || l.id == 0 || ids.contains(l.id))
            return false;
        ids.insert(l.id);
    }
    return ids.contains(d.activeLayerId);
}

DocumentData sample()
{
    DocumentData d;
    d.size = QSize(200, 130);
    for (int i = 0; i < 3; ++i) {
        LayerData l;
        l.id = quint64(5 + i * 3);
        l.name = QStringLiteral("L%1").arg(i);
        l.image = TiledImage(d.size);
        l.image.fillRect(QRect(20 * i, 10 + 30 * i, 120, 70), qRgba(40 * i, 90, 120, 200));
        l.opacity = 0.3 + 0.3 * i;
        d.layers.push_back(l);
    }
    d.activeLayerId = 8;
    return d;
}

} // namespace

class TestFileFormat : public QObject
{
    Q_OBJECT

private slots:
    void filesFromEarlierMilestonesOpenUnchanged_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("milestone 1, format 1 (manual test file)") << "m1-manual.drawapp";
        QTest::newRow("milestone 9 Android emulator, format 2") << "m9-android-layered.drawapp";
        QTest::newRow("milestone 9, format 2, three layers") << "m9-layered.drawapp";
        QTest::newRow("milestone 9, format 2, sparse 2048 x 1536") << "m9-sparse.drawapp";
    }

    void filesFromEarlierMilestonesOpenUnchanged()
    {
        // The expected values were produced by the milestone 9 code itself (commit 7469a69):
        // milestone 10 must read every layer, property and pixel exactly as it did, and saving the
        // file again (format 3) must keep them.
        QFETCH(QString, file);
        const QJsonObject all = QJsonDocument::fromJson(readFile(Compat + "m9-fixtures.json")).object();
        QVERIFY2(all.contains(file), "fixture description missing");
        const QJsonObject expected = all[file].toObject();
        const ProjectFile::LoadResult r = ProjectFile::load(Compat + file);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(r.version == 1 || r.version == 2);
        QCOMPARE(differences(r.document, expected), QString());
        const QByteArray v3 = ProjectFile::encode(r.document);
        QCOMPARE(ProjectFile::verify(v3), QString());
        const ProjectFile::LoadResult again = ProjectFile::decode(v3);
        QVERIFY2(again.ok(), qPrintable(again.error));
        QCOMPARE(again.version, 3);
        QCOMPARE(differences(again.document, expected), QString());
        qInfo().noquote() << QStringLiteral("%1: %2 bytes as written, %3 bytes as format 3")
                                 .arg(file).arg(readFile(Compat + file).size()).arg(v3.size());
    }

    void recoveryFileFromMilestone9Restores()
    {
        const QJsonObject expected =
            QJsonDocument::fromJson(readFile(Compat + "m9-fixtures.json")).object()["m9-layered.drawrecovery"].toObject();
        const RecoveryFile::Contents c = RecoveryFile::load(Compat + "m9-layered.drawrecovery");
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(c.recoveryVersion, quint32(2));
        QCOMPARE(c.projectVersion, 2);
        QCOMPARE(c.metadata.originalPath, QStringLiteral("C:/Art/layered.drawapp"));
        QCOMPARE(c.metadata.displayName, QStringLiteral("layered.drawapp"));
        QCOMPARE(c.metadata.revision, quint64(19));
        QCOMPARE(differences(c.document, expected), QString());
        // Written again by this version: format 3 inside, same document.
        RecoveryFile::Metadata meta = c.metadata;
        const RecoveryFile::Contents again = RecoveryFile::decode(RecoveryFile::encode(c.document, meta));
        QVERIFY2(again.ok(), qPrintable(again.error));
        QCOMPARE(again.projectVersion, 3);
        QCOMPARE(differences(again.document, expected), QString());
    }

    void savingKeepsPreviousFileUntilTheNewOneIsComplete()
    {
        // An existing format 2 project is replaced by format 3 only through a complete, atomic
        // write; a failed write (here: the target is a directory) leaves it byte for byte intact.
        QTemporaryDir dir;
        const QString path = dir.filePath("old.drawapp");
        const QByteArray old = readFile(Compat + "m9-layered.drawapp");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(old);
        }
        const ProjectFile::LoadResult r = ProjectFile::load(path);
        QVERIFY(r.ok());
        QVERIFY(!ProjectFile::save(r.document, dir.path()).isEmpty()); // cannot write: refused
        QCOMPARE(readFile(path), old);
        QCOMPARE(ProjectFile::save(r.document, path), QString());
        QCOMPARE(ProjectFile::load(path).version, 3);
    }

    void malformedFilesNeverCrashOrLeak_data()
    {
        QTest::addColumn<int>("format");
        QTest::newRow("format 3") << 3;
        QTest::newRow("format 2") << 2;
        QTest::newRow("recovery") << 0;
    }

    void malformedFilesNeverCrashOrLeak()
    {
        // Deterministic fuzzing: bit flips, byte runs, truncation, insertion and header edits, half
        // of them with the checksum recomputed so the damage reaches the structure checks. Every
        // input either decodes to a valid document or is refused with a message; no tile stays
        // allocated afterwards.
        QFETCH(int, format);
        const DocumentData doc = sample();
        RecoveryFile::Metadata meta;
        meta.revision = 3;
        const QByteArray good = format == 3   ? ProjectFile::encode(doc)
                                : format == 2 ? ProjectFile::encodeVersion2(doc)
                                              : RecoveryFile::encode(doc, meta);
        const qint64 before = MemoryBudget::used(MemoryBudget::Category::Document);
        // zlib's complaints about corrupt input are expected here by the thousand.
        static QtMessageHandler previous = nullptr;
        previous = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &message) {
            if (!message.startsWith(QLatin1String("qUncompress")))
                previous(type, context, message);
        });
        const auto restoreHandler = qScopeGuard([] { qInstallMessageHandler(previous); });
        QRandomGenerator rng(quint32(1000 + format));
        int accepted = 0, refused = 0;
        constexpr int Rounds = 3000;
        for (int round = 0; round < Rounds; ++round) {
            QByteArray bytes = good;
            const int kind = int(rng.bounded(6));
            const int edits = 1 + int(rng.bounded(4));
            for (int e = 0; e < edits; ++e) {
                const int at = int(rng.bounded(quint32(bytes.size())));
                switch (kind) {
                case 0: bytes[at] = char(bytes[at] ^ (1 << rng.bounded(8))); break;
                case 1: for (int i = at; i < std::min<int>(bytes.size(), at + 16); ++i) bytes[i] = char(rng.bounded(256)); break;
                case 2: bytes.truncate(at); break;
                case 3: bytes.insert(at, QByteArray(1 + int(rng.bounded(64)), char(rng.bounded(256)))); break;
                case 4: if (bytes.size() >= 36) qToLittleEndian(quint32(rng.generate()), bytes.data() + 8 + 4 * rng.bounded(7)); break;
                default: if (bytes.size() >= 64) qToLittleEndian(quint32(rng.bounded(70000)), bytes.data() + 12 + 4 * rng.bounded(4)); break;
                }
                if (bytes.isEmpty())
                    break;
            }
            if (format != 0 && rng.bounded(2))
                bytes = withChecksum(bytes);
            if (format == 0) {
                const RecoveryFile::Contents c = RecoveryFile::decode(bytes);
                if (c.ok()) {
                    ++accepted;
                    QVERIFY(valid(c.document));
                } else {
                    ++refused;
                    QVERIFY(!c.error.isEmpty());
                }
            } else {
                const ProjectFile::LoadResult r = ProjectFile::decode(bytes);
                if (r.ok()) {
                    ++accepted;
                    QVERIFY(valid(r.document));
                } else {
                    ++refused;
                    QVERIFY(!r.error.isEmpty());
                    QVERIFY(r.document.layers.empty());
                }
                // verify() never accepts what decode() refuses.
                if (ProjectFile::verify(bytes).isEmpty())
                    QVERIFY(r.ok());
            }
            QCOMPARE(MemoryBudget::used(MemoryBudget::Category::Document), before);
        }
        QVERIFY(refused > Rounds / 2);
        qInfo().noquote() << QStringLiteral("%1 inputs: %2 refused, %3 accepted as valid documents").arg(Rounds).arg(refused).arg(accepted);
    }

    void hugeDeclaredSizesAreRefusedBeforeAllocating()
    {
        // A tiny file that declares the largest canvas and many tiles: refused or opened without
        // allocating more than its real tiles.
        QByteArray bytes = ProjectFile::encode(sample());
        qToLittleEndian(quint32(16384), bytes.data() + 12);
        qToLittleEndian(quint32(16384), bytes.data() + 16);
        bytes = withChecksum(bytes);
        const qint64 peakBefore = MemoryBudget::usedTotal();
        const ProjectFile::LoadResult r = ProjectFile::decode(bytes);
        if (r.ok())
            QVERIFY(valid(r.document));
        QVERIFY(MemoryBudget::usedTotal() - peakBefore < 16 * 1024 * 1024);
    }
};

QTEST_GUILESS_MAIN(TestFileFormat)
#include "tst_fileformat.moc"
