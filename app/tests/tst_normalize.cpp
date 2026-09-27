#include "core/normalize.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

/// Runs the contract shared with the pipeline (tests/normalize_vectors.json).
class TestNormalize : public QObject
{
    Q_OBJECT

private:
    static QJsonArray vectors()
    {
        QFile file(QString::fromUtf8(OMNIDICT_NORMALIZE_VECTORS));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(file.readAll()).object().value(u"vectors"_s).toArray();
    }

    static void addVectorRows()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        const QJsonArray all = vectors();
        for (const auto& value : all) {
            const QJsonObject vector = value.toObject();
            QTest::newRow(qPrintable(vector.value(u"name"_s).toString()))
                << vector.value(u"input"_s).toString() << vector.value(u"expected"_s).toString();
        }
    }

private Q_SLOTS:
    void vectorFileIsUsable()
    {
        const QJsonArray all = vectors();
        QVERIFY2(all.size() >= 30, "tests/normalize_vectors.json is missing or too small");
        QSet<QString> names;
        for (const auto& value : all) {
            const QJsonObject vector = value.toObject();
            QVERIFY(vector.value(u"input"_s).isString());
            QVERIFY(vector.value(u"expected"_s).isString());
            names.insert(vector.value(u"name"_s).toString());
        }
        QCOMPARE(names.size(), all.size());
    }

    void matchesSharedVectors_data() { addVectorRows(); }
    void matchesSharedVectors()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(normalizeHeadword(input), expected);
    }

    void normalizingTwiceChangesNothing_data() { addVectorRows(); }
    void normalizingTwiceChangesNothing()
    {
        QFETCH(QString, input);
        const QString once = normalizeHeadword(input);
        QCOMPARE(normalizeHeadword(once), once);
    }
};

QTEST_GUILESS_MAIN(TestNormalize)
#include "tst_normalize.moc"
