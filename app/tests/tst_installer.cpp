#include "core/catalog.h"
#include "core/installer.h"
#include "core/library.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sqlite3.h>
#include <zstd.h>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

namespace {

QString fixturePath()
{
    return QString::fromUtf8(OMNIDICT_FIXTURE_BUNDLE);
}

QByteArray zstdCompress(const QByteArray& data)
{
    const size_t bound = ZSTD_compressBound(static_cast<size_t>(data.size()));
    QByteArray out(static_cast<qsizetype>(bound), '\0');
    const size_t written =
        ZSTD_compress(out.data(), bound, data.constData(), static_cast<size_t>(data.size()), 19);
    if (ZSTD_isError(written) != 0U) {
        return {};
    }
    out.resize(static_cast<qsizetype>(written));
    return out;
}

QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

struct Odict
{
    QString path;
    qint64 sizeCompressed = 0;
    qint64 sizeInstalled = 0;
    QString sha256;
};

/// zstd-compresses `sqlitePath` (a real dict.sqlite) into `outPath`, the way
/// `pipeline/omnipipe/package.py` produces a real `.odict`.
Odict makeOdict(const QString& sqlitePath, const QString& outPath)
{
    QFile src(sqlitePath);
    if (!src.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray raw = src.readAll();
    const QByteArray compressed = zstdCompress(raw);

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    out.write(compressed);
    out.close();

    Odict odict;
    odict.path = outPath;
    odict.sizeCompressed = compressed.size();
    odict.sizeInstalled = raw.size();
    odict.sha256 = sha256Hex(compressed);
    return odict;
}

CatalogEntry entryFor(const Odict& odict, const QString& dictId, const QString& version)
{
    CatalogEntry entry;
    entry.dictId = dictId;
    entry.name = u"Sample English"_s;
    entry.sourceLang = u"en"_s;
    entry.targetLang = u"en"_s;
    entry.kind = u"monolingual"_s;
    entry.version = version;
    entry.schemaVersion = Bundle::kSupportedSchemaVersion;
    entry.publisher = u"Omnidict project"_s;
    entry.license = u"GPL-3.0-or-later"_s;
    entry.licenseUrl = u"https://www.gnu.org/licenses/gpl-3.0.html"_s;
    entry.attribution = u"Hand-written sample data for tests, Omnidict project"_s;
    entry.entryCount = 14;
    entry.sizeCompressed = odict.sizeCompressed;
    entry.sizeInstalled = odict.sizeInstalled;
    entry.sha256 = odict.sha256;
    entry.url = u"http://localhost:8000/dicts/sample-en/x/sample-en.odict"_s;
    entry.builtAt = u"2026-09-27T00:00:00Z"_s;
    return entry;
}

/// A writable copy of the fixture with its `meta.version` row changed, so a
/// second, distinct "version" can be installed without changing dict_id.
QString versionedCopy(const QTemporaryDir& dir, const QString& name, const QString& version)
{
    const QString path = dir.filePath(name);
    if (!QFile::copy(fixturePath(), path)) {
        return {};
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, "UPDATE meta SET value = ?1 WHERE key = 'version'", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            const QByteArray bytes = version.toUtf8();
            sqlite3_bind_text(stmt, 1, bytes.constData(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT);
            rc = sqlite3_step(stmt) == SQLITE_DONE ? SQLITE_OK : SQLITE_ERROR;
            sqlite3_finalize(stmt);
        }
    }
    sqlite3_close(db);
    return rc == SQLITE_OK ? path : QString();
}

} // namespace

class TestInstaller : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()),
                 "fixture bundle missing: run `make fixture` at the repository root first");
    }

    void installSucceedsAndLibraryFindsIt()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Odict odict = makeOdict(fixturePath(), dir.filePath(u"sample-en.odict"_s));
        QVERIFY(!odict.path.isEmpty());
        const CatalogEntry entry = entryFor(odict, u"sample-en"_s, u"2026.09.1"_s);
        const QString root = dir.filePath(u"installed"_s);

        auto result = installBundle(odict.path, entry, root);
        QVERIFY2(result.hasValue(), qPrintable(result.error()));
        const QString installedPath = result.take();
        QVERIFY(QFile::exists(installedPath));
        QCOMPARE(installedPath, QDir(root).filePath(u"sample-en/2026.09.1/dict.sqlite"_s));

        const Library library = Library::discover({root});
        QVERIFY(library.problems().isEmpty());
        const Bundle* found = library.find(u"sample-en"_s);
        QVERIFY(found != nullptr);
        QCOMPARE(found->meta().version, u"2026.09.1"_s);

        QVERIFY(installedSize(u"sample-en"_s, root) > 0);
    }

    void wrongChecksumFailsAndLeavesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Odict odict = makeOdict(fixturePath(), dir.filePath(u"bad.odict"_s));
        QVERIFY(!odict.path.isEmpty());
        CatalogEntry entry = entryFor(odict, u"sample-en"_s, u"2026.09.1"_s);
        entry.sha256 = u"0000000000000000000000000000000000000000000000000000000000000000"_s.left(64);
        const QString root = dir.filePath(u"installed"_s);

        auto result = installBundle(odict.path, entry, root);
        QVERIFY(!result.hasValue());
        QVERIFY(result.error().contains(u"checksum"_s));
        QVERIFY(!QDir(root).exists());
    }

    void installingNewerVersionRemovesOlder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(u"installed"_s);

        const Odict odictA = makeOdict(fixturePath(), dir.filePath(u"a.odict"_s));
        const CatalogEntry entryA = entryFor(odictA, u"sample-en"_s, u"2026.09.1"_s);
        auto resultA = installBundle(odictA.path, entryA, root);
        QVERIFY2(resultA.hasValue(), qPrintable(resultA.error()));
        QVERIFY(QDir(root).exists(u"sample-en/2026.09.1"_s));

        const QString sqliteB = versionedCopy(dir, u"b.sqlite"_s, u"2026.09.2"_s);
        QVERIFY(!sqliteB.isEmpty());
        const Odict odictB = makeOdict(sqliteB, dir.filePath(u"b.odict"_s));
        const CatalogEntry entryB = entryFor(odictB, u"sample-en"_s, u"2026.09.2"_s);
        auto resultB = installBundle(odictB.path, entryB, root);
        QVERIFY2(resultB.hasValue(), qPrintable(resultB.error()));

        QVERIFY(!QDir(root).exists(u"sample-en/2026.09.1"_s));
        QVERIFY(QDir(root).exists(u"sample-en/2026.09.2"_s));

        const Library library = Library::discover({root});
        const Bundle* found = library.find(u"sample-en"_s);
        QVERIFY(found != nullptr);
        QCOMPARE(found->meta().version, u"2026.09.2"_s);
    }

    void dictIdMismatchFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Odict odict = makeOdict(fixturePath(), dir.filePath(u"mismatch.odict"_s));
        QVERIFY(!odict.path.isEmpty());
        const CatalogEntry entry = entryFor(odict, u"not-sample-en"_s, u"2026.09.1"_s);
        const QString root = dir.filePath(u"installed"_s);

        auto result = installBundle(odict.path, entry, root);
        QVERIFY(!result.hasValue());
        QVERIFY(result.error().contains(u"dict_id mismatch"_s));
        QVERIFY(!QFile::exists(QDir(root).filePath(u"not-sample-en/2026.09.1/dict.sqlite"_s)));
    }

    void removeInstalledUninstallsAndIsIdempotent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Odict odict = makeOdict(fixturePath(), dir.filePath(u"sample-en.odict"_s));
        const CatalogEntry entry = entryFor(odict, u"sample-en"_s, u"2026.09.1"_s);
        const QString root = dir.filePath(u"installed"_s);
        QVERIFY(installBundle(odict.path, entry, root).hasValue());

        auto removed = removeInstalled(u"sample-en"_s, root);
        QVERIFY2(removed.hasValue(), qPrintable(removed.error()));
        QVERIFY(removed.value());
        QVERIFY(!QDir(root).exists(u"sample-en"_s));

        auto removedAgain = removeInstalled(u"sample-en"_s, root);
        QVERIFY2(removedAgain.hasValue(), qPrintable(removedAgain.error()));
        QVERIFY(!removedAgain.value());
    }
};

QTEST_GUILESS_MAIN(TestInstaller)
#include "tst_installer.moc"
