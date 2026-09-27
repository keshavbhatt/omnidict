#include "core/entry_renderer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

namespace {

QString goldenDir()
{
    return QString::fromUtf8(OMNIDICT_GOLDEN_ENTRIES);
}

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

/// A canonical JSONL record (DOCS/schema.md) as the entry a bundle would hold.
Entry entryFromJson(const QJsonObject& obj)
{
    Entry entry;
    entry.headword = obj.value(u"headword"_s).toString();
    entry.lang = obj.value(u"lang"_s).toString();
    entry.frequency = obj.value(u"frequency"_s).toInt(0);
    for (const auto& value : obj.value(u"pronunciations"_s).toArray()) {
        const QJsonObject pron = value.toObject();
        entry.pronunciations.append(
            {.ipa = pron.value(u"ipa"_s).toString(), .region = pron.value(u"region"_s).toString()});
    }
    int ordinal = 0;
    for (const auto& value : obj.value(u"senses"_s).toArray()) {
        const QJsonObject sense = value.toObject();
        QList<Example> examples;
        for (const auto& exampleValue : sense.value(u"examples"_s).toArray()) {
            const QJsonObject example = exampleValue.toObject();
            examples.append({.text = example.value(u"text"_s).toString(),
                             .translation = example.value(u"translation"_s).toString()});
        }
        entry.senses.append({.ordinal = ++ordinal,
                             .pos = sense.value(u"pos"_s).toString(),
                             .pattern = sense.value(u"pattern"_s).toString(),
                             .label = sense.value(u"label"_s).toString(),
                             .definition = sense.value(u"definition"_s).toString(),
                             .examples = examples});
    }
    for (const auto& value : obj.value(u"forms"_s).toArray()) {
        const QJsonObject form = value.toObject();
        entry.forms.append(
            {.form = form.value(u"form"_s).toString(), .tag = form.value(u"tag"_s).toString()});
    }
    for (const auto& value : obj.value(u"relations"_s).toArray()) {
        const QJsonObject relation = value.toObject();
        entry.relations.append({.type = relation.value(u"type"_s).toString(),
                                .target = relation.value(u"target"_s).toString(),
                                .senseOrdinal = relation.value(u"sense_ordinal"_s).toInt(0)});
    }
    return entry;
}

} // namespace

/// Holds the desktop renderer to the entry HTML contract (DOCS/entry-html.md)
/// through the golden files shared with the pipeline's reference renderer.
class TestEntryRenderer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void goldenFilesExist()
    {
        const QStringList inputs = QDir(goldenDir()).entryList({u"*.json"_s}, QDir::Files);
        QVERIFY2(inputs.size() >= 5, "tests/golden/entries has too few golden files");
        for (const QString& input : inputs) {
            const QString html = input.chopped(5) + u".html"_s;
            QVERIFY2(QFile::exists(QDir(goldenDir()).filePath(html)), qPrintable(html + u" is missing"_s));
        }
    }

    void matchesGoldenFile_data()
    {
        QTest::addColumn<QString>("name");
        const QStringList inputs = QDir(goldenDir()).entryList({u"*.json"_s}, QDir::Files, QDir::Name);
        for (const QString& input : inputs) {
            QTest::newRow(qPrintable(input.chopped(5))) << input.chopped(5);
        }
    }
    void matchesGoldenFile()
    {
        QFETCH(QString, name);
        const QDir dir(goldenDir());
        const QJsonObject input =
            QJsonDocument::fromJson(readText(dir.filePath(name + u".json"_s)).toUtf8()).object();
        QVERIFY(!input.isEmpty());
        const QString expected = readText(dir.filePath(name + u".html"_s));
        const QString actual = renderEntry(entryFromJson(input));
        if (actual != expected) {
            const QStringList want = expected.split(u'\n');
            const QStringList got = actual.split(u'\n');
            for (qsizetype i = 0; i < std::max(want.size(), got.size()); ++i) {
                const QString w = want.value(i);
                const QString g = got.value(i);
                if (w != g) {
                    QFAIL(qPrintable(u"line %1\n  want: %2\n  got:  %3"_s.arg(i + 1).arg(w, g)));
                }
            }
        }
        QCOMPARE(actual, expected);
    }
};

QTEST_GUILESS_MAIN(TestEntryRenderer)
#include "tst_entry_renderer.moc"
