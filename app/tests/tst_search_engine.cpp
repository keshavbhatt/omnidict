#include "core/library.h"
#include "core/search_engine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

/// Copies the fixture to `path`, applying `sql` to the copy.
bool placeBundle(const QString& path, const char* sql = nullptr)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!QFile::copy(fixturePath(), path)) {
        return false;
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    if (sql == nullptr) {
        return true;
    }
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr);
    if (rc == SQLITE_OK) {
        rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }
    sqlite3_close(db);
    return rc == SQLITE_OK;
}

QStringList headwords(const QList<EntryPreview>& rows)
{
    QStringList words;
    for (const EntryPreview& row : rows) {
        words << row.headword;
    }
    return words;
}

QStringList dictIds(const QList<ResultGroup>& groups)
{
    QStringList ids;
    for (const ResultGroup& group : groups) {
        ids << group.dictId;
    }
    return ids;
}

} // namespace

class TestSearchEngine : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::optional<Library> m_library;

    [[nodiscard]] SearchEngine engine() const { return SearchEngine(*m_library); }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()), "run `make fixture` first");
        QVERIFY(m_dir.isValid());
        const QDir root(m_dir.path());
        // A pipeline build: <root>/<dict_id>/dict.sqlite
        QVERIFY(placeBundle(root.filePath(u"sample-en/dict.sqlite"_s)));
        // Installed versions: the newest wins, the older one is not even opened.
        // Its "book" is capitalised, so for "book" it answers less well than sample-en.
        const char* renamed = "UPDATE meta SET value = 'sample-two' WHERE key = 'dict_id';"
                              "UPDATE meta SET value = 'Another Sample' WHERE key = 'name';"
                              "UPDATE entries SET headword = 'Book' WHERE headword = 'book';";
        QVERIFY(placeBundle(root.filePath(u"sample-two/2026.10.1/dict.sqlite"_s), renamed));
        QDir().mkpath(root.filePath(u"sample-two/2026.9.2"_s));
        QFile broken(root.filePath(u"sample-two/2026.9.2/dict.sqlite"_s));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not a database");
        broken.close();
        // Not a dictionary at all.
        QDir().mkpath(root.filePath(u"junk"_s));
        QFile junk(root.filePath(u"junk/dict.sqlite"_s));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not a database either, and long enough to be read as one");
        junk.close();

        m_library = Library::discover({m_dir.path()});
    }

    void discoversBundlesInBothLayoutsSortedByName()
    {
        QCOMPARE(m_library->bundles().size(), std::size_t{2});
        QCOMPARE(m_library->bundles().at(0).meta().name, u"Another Sample"_s);
        QCOMPARE(m_library->bundles().at(1).meta().dictId, u"sample-en"_s);
        QCOMPARE(m_library->problems().size(), 1);
        QVERIFY(m_library->find(u"sample-two"_s) != nullptr);
        QVERIFY(m_library->find(u"absent"_s) == nullptr);
    }

    void aMissingRootIsAnEmptyLibrary()
    {
        const Library empty = Library::discover({m_dir.filePath(u"nowhere"_s)});
        QVERIFY(empty.bundles().empty());
    }

    void groupsHeadwordsByDictionaryExactFirst()
    {
        const SearchResults results = engine().search({.text = u"bookmark"_s});
        QCOMPARE(dictIds(results.headwords), (QStringList{u"sample-two"_s, u"sample-en"_s}));
        QCOMPARE(results.headwords.first().dictName, u"Another Sample"_s);
        QCOMPARE(headwords(results.headwords.last().rows), QStringList{u"bookmark"_s});
        QCOMPARE(headwords(engine().search({.text = u"book"_s}).headwords.first().rows),
                 (QStringList{u"book"_s, u"bookmark"_s, u"bookshelf"_s}));
    }

    void theDictionaryWithTheExactSpellingComesFirst()
    {
        // Library order puts sample-two first, but only sample-en has "book" as typed.
        const SearchResults results = engine().search({.text = u"book"_s});
        QCOMPARE(dictIds(results.headwords), (QStringList{u"sample-en"_s, u"sample-two"_s}));
        QCOMPARE(dictIds(engine().search({.text = u"Book"_s}).headwords),
                 (QStringList{u"sample-two"_s, u"sample-en"_s}));
    }

    void inflectedFormsFindTheirEntryFirst()
    {
        const SearchResults results = engine().search({.text = u"ran"_s});
        QCOMPARE(headwords(results.headwords.first().rows).first(), u"run"_s);
    }

    void aDictionaryFilterSearchesOnlyThatDictionary()
    {
        const SearchResults results = engine().search({.text = u"book"_s, .dictId = u"sample-en"_s});
        QCOMPARE(dictIds(results.headwords), QStringList{u"sample-en"_s});
    }

    void honoursThePerDictionaryLimit()
    {
        const SearchResults results = engine().search({.text = u"book"_s, .perDictionary = 2});
        QCOMPARE(headwords(results.headwords.first().rows), (QStringList{u"book"_s, u"bookmark"_s}));
    }

    void definitionsListOnlyEntriesNotAlreadyShown()
    {
        const SearchResults byMeaning = engine().search({.text = u"fortunate"_s});
        QVERIFY(byMeaning.headwords.isEmpty());
        QCOMPARE(dictIds(byMeaning.definitions), (QStringList{u"sample-two"_s, u"sample-en"_s}));
        QCOMPARE(headwords(byMeaning.definitions.first().rows), QStringList{u"serendipity"_s});

        const SearchResults byName = engine().search({.text = u"serendipity"_s});
        QCOMPARE(headwords(byName.headwords.first().rows), QStringList{u"serendipity"_s});
        for (const ResultGroup& group : byName.definitions) {
            QVERIFY(!headwords(group.rows).contains(u"serendipity"_s));
        }
    }

    void shortQueriesSkipDefinitions()
    {
        const SearchResults results = engine().search({.text = u"bo"_s});
        QVERIFY(!results.headwords.isEmpty());
        QVERIFY(results.definitions.isEmpty());
    }

    void wildcardsMatchHeadwordPatterns_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("one character") << u"b??k"_s << QStringList{u"book"_s};
        QTest::newRow("any run") << u"book*"_s << QStringList{u"book"_s, u"bookmark"_s, u"bookshelf"_s};
        QTest::newRow("leading run") << u"*shelf"_s << QStringList{u"bookshelf"_s};
        QTest::newRow("folded like a lookup") << u"CAF?"_s << QStringList{u"café"_s};
        QTest::newRow("LIKE syntax is text") << u"b_o*"_s << QStringList{};
        QTest::newRow("percent is text") << u"%*"_s << QStringList{};
    }
    void wildcardsMatchHeadwordPatterns()
    {
        QFETCH(QString, pattern);
        QFETCH(QStringList, expected);
        const SearchResults results = engine().search({.text = pattern, .dictId = u"sample-en"_s});
        QCOMPARE(headwords(results.headwords.value(0).rows), expected);
        QVERIFY(results.definitions.isEmpty());
    }

    void blankQueriesFindNothing()
    {
        QVERIFY(engine().search({.text = u"   "_s}).isEmpty());
        QVERIFY(engine().search({.text = QString()}).isEmpty());
        QVERIFY(engine().search({.text = u"zyzzyva"_s}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSearchEngine)
#include "tst_search_engine.moc"
