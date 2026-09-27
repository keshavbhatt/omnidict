#include "core/bundle.h"
#include "core/catalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

namespace {

/// One manifest object with every PLAN.md 5.1 field this project's
/// CatalogEntry carries, matching what `pipeline/omnipipe/package.py` writes.
QJsonObject validManifest(const QString& dictId, int schemaVersion = 2)
{
    return QJsonObject{
        {u"dict_id"_s, dictId},
        {u"name"_s, u"Spanish to English (Wiktionary)"_s},
        {u"source_lang"_s, u"es"_s},
        {u"target_lang"_s, u"en"_s},
        {u"kind"_s, u"bilingual"_s},
        {u"version"_s, u"2026.09.1"_s},
        {u"schema_version"_s, schemaVersion},
        {u"publisher"_s, u"Wiktionary contributors"_s},
        {u"license"_s, u"CC-BY-SA-4.0"_s},
        {u"license_url"_s, u"https://creativecommons.org/licenses/by-sa/4.0/"_s},
        {u"attribution"_s, u"Data from Wiktionary via kaikki.org, CC BY-SA 4.0"_s},
        {u"entry_count"_s, 48213},
        {u"size_compressed"_s, 4180000},
        {u"size_installed"_s, 21300000},
        {u"sha256"_s, u"abc123"_s},
        {u"url"_s, u"http://localhost:8000/dicts/wikt-es-en/2026.09.1/wikt-es-en.odict"_s},
        {u"built_at"_s, u"2026-09-27T00:00:00Z"_s},
        {u"source"_s, QJsonObject{{u"converter"_s, u"kaikki"_s}, {u"dump_date"_s, u"2026-09-20"_s}}},
    };
}

QByteArray catalogJson(const QJsonArray& dictionaries)
{
    const QJsonObject root{
        {u"catalog_version"_s, 1},
        {u"generated_at"_s, u"2026-09-27T00:00:00Z"_s},
        {u"dictionaries"_s, dictionaries},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

class TestCatalog : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesARealCatalog()
    {
        const QByteArray json = catalogJson(QJsonArray{validManifest(u"wikt-es-en"_s)});
        auto result = parseCatalog(json);
        QVERIFY2(result.hasValue(), qPrintable(result.error()));
        const Catalog catalog = result.take();
        QCOMPARE(catalog.catalogVersion, 1);
        QCOMPARE(catalog.generatedAt, u"2026-09-27T00:00:00Z"_s);
        QCOMPARE(catalog.dictionaries.size(), 1);

        const CatalogEntry& entry = catalog.dictionaries.first();
        QCOMPARE(entry.dictId, u"wikt-es-en"_s);
        QCOMPARE(entry.name, u"Spanish to English (Wiktionary)"_s);
        QCOMPARE(entry.sourceLang, u"es"_s);
        QCOMPARE(entry.targetLang, u"en"_s);
        QCOMPARE(entry.kind, u"bilingual"_s);
        QCOMPARE(entry.version, u"2026.09.1"_s);
        QCOMPARE(entry.schemaVersion, 2);
        QCOMPARE(entry.publisher, u"Wiktionary contributors"_s);
        QCOMPARE(entry.license, u"CC-BY-SA-4.0"_s);
        QCOMPARE(entry.licenseUrl, u"https://creativecommons.org/licenses/by-sa/4.0/"_s);
        QVERIFY(!entry.attribution.isEmpty());
        QCOMPARE(entry.entryCount, 48213);
        QCOMPARE(entry.sizeCompressed, 4180000);
        QCOMPARE(entry.sizeInstalled, 21300000);
        QCOMPARE(entry.sha256, u"abc123"_s);
        QCOMPARE(entry.url, u"http://localhost:8000/dicts/wikt-es-en/2026.09.1/wikt-es-en.odict"_s);
        QCOMPARE(entry.builtAt, u"2026-09-27T00:00:00Z"_s);
    }

    void skipsAnEntryWithANewerSchemaVersion()
    {
        const QJsonArray dictionaries{
            validManifest(u"wikt-es-en"_s, Bundle::kSupportedSchemaVersion),
            validManifest(u"wikt-future-en"_s, Bundle::kSupportedSchemaVersion + 1),
        };
        auto result = parseCatalog(catalogJson(dictionaries));
        QVERIFY2(result.hasValue(), qPrintable(result.error()));
        const Catalog catalog = result.take();
        QCOMPARE(catalog.dictionaries.size(), 1);
        QCOMPARE(catalog.dictionaries.first().dictId, u"wikt-es-en"_s);
    }

    void errorsOnAMissingField()
    {
        QJsonObject broken = validManifest(u"wikt-broken-en"_s);
        broken.remove(u"sha256"_s);
        auto result = parseCatalog(catalogJson(QJsonArray{broken}));
        QVERIFY(!result.hasValue());
        QVERIFY(result.error().contains(u"wikt-broken-en"_s));
        QVERIFY(result.error().contains(u"sha256"_s));
    }

    void errorsOnAWrongFieldType()
    {
        QJsonObject broken = validManifest(u"wikt-broken-en"_s);
        broken[u"entry_count"_s] = u"not a number"_s;
        auto result = parseCatalog(catalogJson(QJsonArray{broken}));
        QVERIFY(!result.hasValue());
        QVERIFY(result.error().contains(u"wikt-broken-en"_s));
    }

    void errorsOnInvalidJson()
    {
        auto result = parseCatalog(QByteArrayLiteral("not json"));
        QVERIFY(!result.hasValue());
    }

    void errorsOnAMissingTopLevelField()
    {
        const QJsonObject root{{u"catalog_version"_s, 1}, {u"generated_at"_s, u"x"_s}};
        auto result = parseCatalog(QJsonDocument(root).toJson());
        QVERIFY(!result.hasValue());
    }

    void compareVersionsCases_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expectedSign");

        QTest::newRow("equal") << u"2026.09.1"_s << u"2026.9.1"_s << 0;
        QTest::newRow("equal-identical") << u"1.0.0"_s << u"1.0.0"_s << 0;
        QTest::newRow("newer-minor") << u"2026.10.1"_s << u"2026.9.2"_s << 1;
        QTest::newRow("older-minor") << u"2026.9.2"_s << u"2026.10.1"_s << -1;
        QTest::newRow("missing-trailing-is-zero") << u"1.2"_s << u"1.2.0"_s << 0;
        QTest::newRow("missing-trailing-is-less") << u"1.2"_s << u"1.2.1"_s << -1;
        QTest::newRow("more-components-wins") << u"1.2.1"_s << u"1.2"_s << 1;
        QTest::newRow("single-component") << u"3"_s << u"2"_s << 1;
    }

    void compareVersionsCases()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expectedSign);

        const int result = compareVersions(a, b);
        if (expectedSign == 0) {
            QCOMPARE(result, 0);
        } else if (expectedSign > 0) {
            QVERIFY(result > 0);
        } else {
            QVERIFY(result < 0);
        }
        // Antisymmetric.
        const int reversed = compareVersions(b, a);
        QCOMPARE(reversed == 0, result == 0);
        QVERIFY((reversed > 0) == (result < 0) || result == 0);
    }
};

QTEST_GUILESS_MAIN(TestCatalog)
#include "tst_catalog.moc"
