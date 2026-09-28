#include "core/bundle.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sqlite3.h>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

namespace {

QString fixturePath()
{
    return QString::fromUtf8(OMNIDICT_FIXTURE_BUNDLE);
}

QStringList headwords(const QList<EntryPreview>& rows)
{
    QStringList words;
    for (const EntryPreview& row : rows) {
        words << row.headword;
    }
    return words;
}

/// A writable copy of the fixture with `sql` applied to it.
QString alteredCopy(const QTemporaryDir& dir, const QString& name, const char* sql)
{
    const QString path = dir.filePath(name);
    if (!QFile::copy(fixturePath(), path)) {
        return {};
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr);
    if (rc == SQLITE_OK) {
        rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }
    sqlite3_close(db);
    return rc == SQLITE_OK ? path : QString();
}

} // namespace

/// Reads the bundle the pipeline builds from tests/fixtures/sample-en.jsonl,
/// so this is also the round trip between the two halves of the project.
class TestBundle : public QObject
{
    Q_OBJECT

private:
    static Bundle openFixture()
    {
        auto bundle = Bundle::open(fixturePath());
        if (!bundle) {
            qFatal("fixture bundle unusable: %s", qPrintable(bundle.error()));
        }
        return bundle.take();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()),
                 "fixture bundle missing: run `make fixture` at the repository root first");
    }

    void opensAndReadsMetadata()
    {
        const Bundle bundle = openFixture();
        QCOMPARE(bundle.meta().dictId, u"sample-en"_s);
        QCOMPARE(bundle.meta().sourceLang, u"en"_s);
        QCOMPARE(bundle.meta().schemaVersion, 2);
        QCOMPARE(bundle.meta().entryCount, 14);
        QVERIFY(!bundle.meta().attribution.isEmpty());
        QVERIFY(!bundle.meta().license.isEmpty());
    }

    void looksUpHeadwords_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("headword");
        QTest::newRow("plain") << u"perhaps"_s << u"perhaps"_s;
        QTest::newRow("as typed, upper case") << u"PERHAPS"_s << u"perhaps"_s;
        QTest::newRow("capitalised headword") << u"monday"_s << u"Monday"_s;
        QTest::newRow("without the diacritic") << u"cafe"_s << u"café"_s;
        QTest::newRow("with the diacritic") << u"Café"_s << u"café"_s;
        QTest::newRow("padded with spaces") << u"  dictionary  "_s << u"dictionary"_s;
    }
    void looksUpHeadwords()
    {
        QFETCH(QString, query);
        QFETCH(QString, headword);
        const Bundle bundle = openFixture();
        const QList<EntryPreview> rows = bundle.lookupExact(query);
        QCOMPARE(headwords(rows), QStringList{headword});
        QVERIFY(!rows.first().preview.isEmpty());
        QVERIFY(rows.first().id > 0);
    }

    void anEntrySpelledAsTypedComesFirst()
    {
        const Bundle bundle = openFixture();
        QCOMPARE(headwords(bundle.lookupExact(u"résumé"_s)), (QStringList{u"résumé"_s, u"resume"_s}));
        QCOMPARE(headwords(bundle.lookupExact(u"resume"_s)), (QStringList{u"resume"_s, u"résumé"_s}));
        QCOMPARE(headwords(bundle.lookupExact(u"RESUME"_s)).size(), 2);
        // Decomposed input is the same spelling.
        QCOMPARE(headwords(bundle.lookupExact(u"re\u0301sume\u0301"_s)).first(), u"résumé"_s);
        QCOMPARE(headwords(bundle.searchPrefix(u"résu"_s, 10)).first(), u"resume"_s);
    }

    void findsAnEntryByItsInflectedForm()
    {
        const Bundle bundle = openFixture();
        QCOMPARE(headwords(bundle.lookupExact(u"ran"_s)), QStringList{u"run"_s});
        QCOMPARE(headwords(bundle.lookupExact(u"mice"_s)), QStringList{u"mouse"_s});
    }

    void aMatchThroughAFormSaysWhichForm()
    {
        const Bundle bundle = openFixture();
        QCOMPARE(bundle.lookupExact(u"ran"_s).value(0).matchedForm, u"ran"_s);
        QCOMPARE(bundle.lookupExact(u"Mice"_s).value(0).matchedForm,
                 u"mice"_s); // as the dictionary spells it
        QVERIFY(bundle.lookupExact(u"run"_s).value(0).matchedForm.isEmpty());
        // café's alternative spelling is also its own normalized headword: no note.
        QVERIFY(bundle.lookupExact(u"cafe"_s).value(0).matchedForm.isEmpty());

        const QList<EntryPreview> rows = bundle.searchPrefix(u"runn"_s, 10);
        QCOMPARE(headwords(rows), QStringList{u"run"_s});
        QCOMPARE(rows.first().matchedForm, u"running"_s);
        for (const EntryPreview& row : bundle.searchPrefix(u"book"_s, 10)) {
            QVERIFY(row.matchedForm.isEmpty());
        }
    }

    void unknownWordFindsNothing()
    {
        const Bundle bundle = openFixture();
        QVERIFY(bundle.lookupExact(u"zyzzyva"_s).isEmpty());
        QVERIFY(bundle.lookupExact(QString()).isEmpty());
        QVERIFY(bundle.lookupExact(u"   "_s).isEmpty());
        QVERIFY(bundle.searchPrefix(u"zyz"_s, 10).isEmpty());
    }

    void prefixSearchPutsTheExactMatchFirstThenSortOrder()
    {
        const Bundle bundle = openFixture();
        QCOMPARE(headwords(bundle.searchPrefix(u"book"_s, 10)),
                 (QStringList{u"book"_s, u"bookmark"_s, u"bookshelf"_s}));
        QCOMPARE(headwords(bundle.searchPrefix(u"Books"_s, 10)), QStringList{u"bookshelf"_s});
    }

    void prefixSearchListsHeadwordMatchesBeforeFormMatches()
    {
        const QTemporaryDir dir;
        const QString path =
            alteredCopy(dir, u"forms.sqlite"_s,
                        "INSERT INTO forms (form, form_norm, entry_id, tag)"
                        " SELECT 'mocha', 'mocha', id, NULL FROM entries WHERE headword = 'café'");
        QVERIFY(!path.isEmpty());
        const auto bundle = Bundle::open(path);
        QVERIFY(bundle);
        // café sorts before the m words, but only its form starts with "m".
        QCOMPARE(headwords(bundle.value().searchPrefix(u"m"_s, 10)),
                 (QStringList{u"maybe"_s, u"Monday"_s, u"mouse"_s, u"café"_s}));
    }

    void prefixSearchHonoursTheLimit()
    {
        const Bundle bundle = openFixture();
        QCOMPARE(bundle.searchPrefix(u"book"_s, 2).size(), 2);
        QVERIFY(bundle.searchPrefix(u"book"_s, 0).isEmpty());
    }

    void prefixSearchTreatsWildcardCharactersAsText()
    {
        const Bundle bundle = openFixture();
        QVERIFY(bundle.searchPrefix(u"%"_s, 10).isEmpty());
        QVERIFY(bundle.searchPrefix(u"b_ok"_s, 10).isEmpty());
    }

    void fullTextSearchReadsDefinitions()
    {
        const Bundle bundle = openFixture();
        QCOMPARE(headwords(bundle.searchFullText(u"fortunate"_s, 10)), QStringList{u"serendipity"_s});
        QCOMPARE(headwords(bundle.searchFullText(u"FORTUNATE"_s, 10)), QStringList{u"serendipity"_s});
        QVERIFY(bundle.searchFullText(u"zyzzyva"_s, 10).isEmpty());
    }

    void fullTextSearchTreatsQuerySyntaxAsText()
    {
        const Bundle bundle = openFixture();
        QCOMPARE(headwords(bundle.searchFullText(u"(fortunate*)"_s, 10)), QStringList{u"serendipity"_s});
        QCOMPARE(headwords(bundle.searchFullText(u"fortunate:"_s, 10)), QStringList{u"serendipity"_s});
        QVERIFY(bundle.searchFullText(u"\"unbalanced"_s, 10).isEmpty());
        QVERIFY(bundle.searchFullText(u"   "_s, 10).isEmpty());
    }

    void loadsACompleteEntry()
    {
        const Bundle bundle = openFixture();
        const QList<EntryPreview> rows = bundle.lookupExact(u"run"_s);
        QCOMPARE(rows.size(), 1);

        const std::optional<Entry> entry = bundle.entry(rows.first().id);
        QVERIFY(entry.has_value());
        QCOMPARE(entry->headword, u"run"_s);
        QCOMPARE(entry->lang, u"en"_s);
        QVERIFY(entry->senses.size() >= 3);
        for (qsizetype i = 0; i < entry->senses.size(); ++i) {
            QCOMPARE(entry->senses.at(i).ordinal, static_cast<int>(i) + 1);
            QVERIFY(!entry->senses.at(i).definition.isEmpty());
        }
        QVERIFY(!entry->senses.first().examples.isEmpty());

        QStringList forms;
        for (const Form& form : entry->forms) {
            forms << form.form;
        }
        QVERIFY(forms.contains(u"ran"_s));
        QVERIFY(forms.contains(u"running"_s));
    }

    void entryKeepsItsMarkupAndRelations()
    {
        const Bundle bundle = openFixture();
        const std::optional<Entry> perhaps = bundle.entry(bundle.lookupExact(u"perhaps"_s).first().id);
        QVERIFY(perhaps.has_value());
        QCOMPARE(perhaps->frequency, 3);
        QVERIFY(!perhaps->pronunciations.isEmpty());
        QVERIFY(perhaps->senses.first().definition.contains(u"<b>perhaps</b>"_s));
        QCOMPARE(perhaps->senses.first().pos, u"adv"_s);
        QCOMPARE(perhaps->relations.size(), 1);
        QCOMPARE(perhaps->relations.first().type, u"synonym"_s);
        QCOMPARE(perhaps->relations.first().target, u"maybe"_s);

        const std::optional<Entry> dictionary = bundle.entry(bundle.lookupExact(u"dictionary"_s).first().id);
        QVERIFY(dictionary.has_value());
        QVERIFY(dictionary->senses.first().definition.contains(u"<a href=\"lex:word\">"_s));
    }

    void unknownEntryIdIsNothing()
    {
        const Bundle bundle = openFixture();
        QVERIFY(!bundle.entry(0).has_value());
        QVERIFY(!bundle.entry(999999).has_value());
    }

    void refusesABundleFromANewerSchema()
    {
        const QTemporaryDir dir;
        const QString path =
            alteredCopy(dir, u"newer.sqlite"_s, "UPDATE meta SET value = '3' WHERE key = 'schema_version'");
        QVERIFY(!path.isEmpty());
        const auto bundle = Bundle::open(path);
        QVERIFY(!bundle);
        QVERIFY2(bundle.error().contains(u"newer"_s), qPrintable(bundle.error()));
    }

    void refusesABundleWithMetadataMissing()
    {
        const QTemporaryDir dir;
        const QString path = alteredCopy(dir, u"bare.sqlite"_s, "DELETE FROM meta WHERE key = 'attribution'");
        QVERIFY(!path.isEmpty());
        const auto bundle = Bundle::open(path);
        QVERIFY(!bundle);
        QVERIFY2(bundle.error().contains(u"attribution"_s), qPrintable(bundle.error()));
    }

    void refusesWhatIsNotABundle()
    {
        const QTemporaryDir dir;
        QVERIFY(!Bundle::open(dir.filePath(u"absent.sqlite"_s)));

        QFile notADatabase(dir.filePath(u"text.sqlite"_s));
        QVERIFY(notADatabase.open(QIODevice::WriteOnly));
        notADatabase.write("this is not a database, only a text file of some length\n");
        notADatabase.close();
        QVERIFY(!Bundle::open(notADatabase.fileName()));
    }
};

QTEST_GUILESS_MAIN(TestBundle)
#include "tst_bundle.moc"
