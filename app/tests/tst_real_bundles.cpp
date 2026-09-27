#include "core/bundle.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

/// The round-trip gate (PLAN.md 6.4): every real bundle the pipeline has
/// built answers the words in tests/known_headwords.json. Bundles that are not
/// built are skipped, so this passes on a machine that has none; set
/// OMNIDICT_BUNDLE_DIR to check bundles somewhere other than pipeline/out.
class TestRealBundles : public QObject
{
    Q_OBJECT

private:
    static QJsonObject dictionaries()
    {
        QFile file(QString::fromUtf8(OMNIDICT_KNOWN_HEADWORDS));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(file.readAll()).object().value(u"dictionaries"_s).toObject();
    }

    static QString bundlePath(const QString& dictId)
    {
        const QString dir =
            qEnvironmentVariable("OMNIDICT_BUNDLE_DIR", QString::fromUtf8(OMNIDICT_BUNDLE_DIR));
        return QDir(dir).filePath(dictId + u"/dict.sqlite"_s);
    }

    /// Opens the bundle for the current data row, or skips the row.
    static std::optional<Bundle> openOrSkip(const QString& dictId)
    {
        const QString path = bundlePath(dictId);
        if (!QFile::exists(path)) {
            return std::nullopt;
        }
        auto bundle = Bundle::open(path);
        if (!bundle) {
            qWarning("%s", qPrintable(bundle.error()));
            return std::nullopt;
        }
        return bundle.take();
    }

private Q_SLOTS:
    void listIsUsable()
    {
        const QJsonObject all = dictionaries();
        QVERIFY2(!all.isEmpty(), "tests/known_headwords.json is missing or empty");
        for (auto it = all.begin(); it != all.end(); ++it) {
            QCOMPARE(it.value().toObject().value(u"headwords"_s).toArray().size(), 20);
        }
    }

    void answersKnownWords_data()
    {
        QTest::addColumn<QString>("dictId");
        const QJsonObject all = dictionaries();
        for (auto it = all.begin(); it != all.end(); ++it) {
            QTest::newRow(qPrintable(it.key())) << it.key();
        }
    }
    void answersKnownWords()
    {
        QFETCH(QString, dictId);
        if (!QFile::exists(bundlePath(dictId))) {
            QSKIP("bundle not built");
        }
        const std::optional<Bundle> bundle = openOrSkip(dictId);
        QVERIFY2(bundle.has_value(), "bundle exists but cannot be opened");
        QCOMPARE(bundle->meta().dictId, dictId);
        QVERIFY(bundle->meta().entryCount > 0);
        QVERIFY(!bundle->meta().attribution.isEmpty());

        const QJsonObject words = dictionaries().value(dictId).toObject();
        const QJsonArray headwords = words.value(u"headwords"_s).toArray();
        for (const auto& value : headwords) {
            const QString word = value.toString();
            const QList<EntryPreview> rows = bundle->lookupExact(word);
            QVERIFY2(!rows.isEmpty(), qPrintable(u"no entry for "_s + word));
            QVERIFY2(rows.first().headword == word,
                     qPrintable(word + u" came back as "_s + rows.first().headword));
            QVERIFY2(!rows.first().preview.isEmpty(), qPrintable(u"empty preview for "_s + word));
            const std::optional<Entry> entry = bundle->entry(rows.first().id);
            QVERIFY2(entry.has_value() && !entry->senses.isEmpty(), qPrintable(word));
        }

        const QJsonObject forms = words.value(u"forms"_s).toObject();
        for (auto it = forms.begin(); it != forms.end(); ++it) {
            QStringList found;
            for (const EntryPreview& row : bundle->lookupExact(it.key())) {
                found << row.headword;
            }
            QVERIFY2(found.contains(it.value().toString()),
                     qPrintable(it.key() + u" should find "_s + it.value().toString() + u", found: "_s +
                                found.join(u", "_s)));
        }

        const QString prefix = headwords.first().toString().left(2);
        QVERIFY(!bundle->searchPrefix(prefix, 10).isEmpty());
        QVERIFY(!bundle->searchFullText(words.value(u"full_text"_s).toString(), 10).isEmpty());
    }

    void suggestsSpellingsInTime_data() { answersKnownWords_data(); }
    void suggestsSpellingsInTime()
    {
        QFETCH(QString, dictId);
        const std::optional<Bundle> bundle = openOrSkip(dictId);
        if (!bundle) {
            QSKIP("bundle not built");
        }
        if (bundle->meta().schemaVersion < 2) {
            QSKIP("bundle predates suggestions (schema version 1)");
        }
        // ADR-013: suggestions run on the lookup thread after a search that found
        // nothing, so they share its budget.
        constexpr qint64 kBudgetMs = 100;
        const QJsonObject cases = dictionaries().value(dictId).toObject().value(u"suggestions"_s).toObject();
        QVERIFY(!cases.isEmpty());
        qint64 slowest = 0;
        for (auto it = cases.begin(); it != cases.end(); ++it) {
            QElapsedTimer timer;
            timer.start();
            const QList<Suggestion> suggestions = bundle->suggest(it.key(), 5);
            slowest = std::max(slowest, timer.elapsed());
            QStringList words;
            for (const Suggestion& suggestion : suggestions) {
                words << suggestion.word;
            }
            QVERIFY2(words.contains(it.value().toString()),
                     qPrintable(it.key() + u" should suggest "_s + it.value().toString() + u", got: "_s +
                                words.join(u", "_s)));
        }
        qInfo("%s: slowest suggestion %lld ms", qPrintable(dictId), slowest);
        QVERIFY2(slowest < kBudgetMs, qPrintable(u"slowest suggestion took %1 ms"_s.arg(slowest)));
    }
};

QTEST_GUILESS_MAIN(TestRealBundles)
#include "tst_real_bundles.moc"
