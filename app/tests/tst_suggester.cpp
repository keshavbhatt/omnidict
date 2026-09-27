#include "core/bundle.h"
#include "core/suggester.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

class TestSuggester : public QObject
{
    Q_OBJECT

private:
    std::optional<Bundle> m_bundle;

    static QJsonArray cases()
    {
        QFile file(QString::fromUtf8(OMNIDICT_SUGGEST_CASES));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(file.readAll()).object().value(u"cases"_s).toArray();
    }

    static CodePoints points(const QString& text) { return text.toUcs4(); }

private Q_SLOTS:
    void initTestCase()
    {
        auto bundle = Bundle::open(QString::fromUtf8(OMNIDICT_FIXTURE_BUNDLE));
        QVERIFY2(bundle, "run `make fixture` first");
        m_bundle.emplace(bundle.take());
        QVERIFY(!cases().isEmpty());
    }

    // The same cases as pipeline/tests/test_suggest.py: the two must agree.
    void sharedCases_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        for (const auto& value : cases()) {
            const QJsonObject item = value.toObject();
            QStringList words;
            for (const auto& word : item.value(u"words"_s).toArray()) {
                words << word.toString();
            }
            const QString query = item.value(u"query"_s).toString();
            QTest::newRow(query.toUtf8().constData()) << query << words;
        }
    }

    void sharedCases()
    {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        QStringList words;
        for (const Suggestion& suggestion : m_bundle->suggest(query, 5)) {
            words << suggestion.word;
        }
        QCOMPARE(words, expected);
    }

    void theLimitCapsTheList() { QCOMPARE(m_bundle->suggest(u"bok"_s, 1).size(), 1); }

    void osaDistance_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");
        QTest::newRow("same") << u"perhaps"_s << u"perhaps"_s << 0;
        QTest::newRow("adjacent swap") << u"prehaps"_s << u"perhaps"_s << 1;
        QTest::newRow("substitution") << u"mouse"_s << u"moose"_s << 1;
        QTest::newRow("insertion") << u"book"_s << u"books"_s << 1;
        QTest::newRow("no edit inside a swap") << u"abc"_s << u"ca"_s << 3;
        QTest::newRow("missing virama") << u"नमसते"_s << u"नमस्ते"_s << 1;
        QTest::newRow("astral characters") << u"a\U0001F600b"_s << u"ab"_s << 1;
    }

    void osaDistance()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        QCOMPARE(omnidict::core::osaDistance(points(a), points(b), 5), expected);
    }

    void osaDistanceStopsPastTheCap()
    {
        QCOMPARE(omnidict::core::osaDistance(points(u"dictionary"_s), points(u"xylophone"_s), 2), 3);
        QCOMPARE(omnidict::core::osaDistance(points(u"a"_s), points(u"abcdef"_s), 2), 3);
    }

    void allowedDistanceGrowsWithLength()
    {
        QCOMPARE(maxSuggestionDistance(3), 1);
        QCOMPARE(maxSuggestionDistance(4), 1);
        QCOMPARE(maxSuggestionDistance(5), 2);
        QCOMPARE(maxSuggestionDistance(8), 2);
        QCOMPARE(maxSuggestionDistance(9), 3);
    }
};

QTEST_GUILESS_MAIN(TestSuggester)
#include "tst_suggester.moc"
