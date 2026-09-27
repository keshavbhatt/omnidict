#include "core/user_data.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

namespace {

SavedEntry saved(const QString& headword, const QString& dictId = u"sample-en"_s)
{
    return {.dictId = dictId, .headword = headword, .preview = headword + u" preview"_s};
}

QStringList headwords(const QList<SavedEntry>& entries)
{
    QStringList words;
    for (const SavedEntry& entry : entries) {
        words << entry.headword;
    }
    return words;
}

} // namespace

class TestUserData : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    [[nodiscard]] UserData openFresh(const QString& name) const
    {
        auto data = UserData::open(m_dir.filePath(name + u"/userdata.sqlite"_s));
        if (!data) {
            qFatal("cannot open user data: %s", qPrintable(data.error()));
        }
        return data.take();
    }

private Q_SLOTS:
    void historyIsMostRecentFirstWithoutDuplicates()
    {
        UserData data = openFresh(u"history"_s);
        data.recordView(saved(u"book"_s));
        data.recordView(saved(u"run"_s));
        data.recordView(saved(u"book"_s));
        data.recordView(saved(u"book"_s, u"other-dict"_s));
        QCOMPARE(headwords(data.history(10)), (QStringList{u"book"_s, u"book"_s, u"run"_s}));
        QCOMPARE(data.history(10).first().dictId, u"other-dict"_s);
        QCOMPARE(data.history(10).at(1).preview, u"book preview"_s);
        QCOMPARE(data.history(1).size(), 1);
    }

    void historyKeepsOnlyTheNewestItems()
    {
        UserData data = openFresh(u"limit"_s);
        for (int i = 0; i < UserData::kHistoryLimit + 5; ++i) {
            data.recordView(saved(u"word%1"_s.arg(i)));
        }
        const QList<SavedEntry> history = data.history(UserData::kHistoryLimit + 10);
        QCOMPARE(history.size(), UserData::kHistoryLimit);
        QCOMPARE(history.first().headword, u"word%1"_s.arg(UserData::kHistoryLimit + 4));
        data.clearHistory();
        QVERIFY(data.history(10).isEmpty());
    }

    void favoritesToggleAndPersist()
    {
        {
            UserData data = openFresh(u"favorites"_s);
            QVERIFY(!data.isFavorite(u"sample-en"_s, u"book"_s));
            data.setFavorite(saved(u"book"_s), true);
            data.setFavorite(saved(u"run"_s), true);
            data.setFavorite(saved(u"run"_s), true);
            QVERIFY(data.isFavorite(u"sample-en"_s, u"book"_s));
            QVERIFY(!data.isFavorite(u"other-dict"_s, u"book"_s));
            QCOMPARE(headwords(data.favorites()), (QStringList{u"run"_s, u"book"_s}));
            data.setFavorite(saved(u"run"_s), false);
        }
        // Reopened from disk.
        const UserData again = openFresh(u"favorites"_s);
        QCOMPARE(headwords(again.favorites()), QStringList{u"book"_s});
    }

    void anUnwritableLocationIsAnError()
    {
        QVERIFY(!UserData::open(u"/proc/omnidict-cannot-write-here/userdata.sqlite"_s));
    }
};

QTEST_GUILESS_MAIN(TestUserData)
#include "tst_user_data.moc"
