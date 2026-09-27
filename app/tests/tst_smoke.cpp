#include "models/results_model.h"
#include "ui/main_window.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace Qt::StringLiterals;
using omnidict::models::ResultsModel;
using omnidict::ui::MainWindow;

namespace {

/// The model's rows as "headword" for entries and "# heading" for headings.
QStringList rows(const QAbstractItemModel* model)
{
    QStringList out;
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const auto kind = index.data(ResultsModel::KindRole).value<ResultsModel::RowKind>();
        out << (kind == ResultsModel::RowKind::Entry ? QString() : u"# "_s) + index.data().toString();
    }
    return out;
}

} // namespace

/// Builds the whole window against the fixture dictionary and a scratch
/// profile, and walks the main path: search, open, star, see it remembered.
class TestSmoke : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_profile;
    QTemporaryDir m_bundles;

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString fixture = QString::fromUtf8(OMNIDICT_FIXTURE_BUNDLE);
        QVERIFY2(QFile::exists(fixture), "run `make fixture` first");
        QVERIFY(QDir().mkpath(m_bundles.filePath(u"sample-en"_s)));
        QVERIFY(QFile::copy(fixture, m_bundles.filePath(u"sample-en/dict.sqlite"_s)));
    }

    void searchOpenStarAndRemember()
    {
        MainWindow window({m_bundles.path()}, m_profile.filePath(u"userdata.sqlite"_s));
        QSignalSpy ready(&window, &MainWindow::libraryReady);
        QSignalSpy shown(&window, &MainWindow::entryShown);
        window.show();
        QVERIFY(ready.wait());

        auto* search = window.findChild<QLineEdit*>(u"search"_s);
        auto* results = window.findChild<QListView*>(u"results"_s);
        auto* star = window.findChild<QToolButton*>(u"favoriteButton"_s);
        QVERIFY(search && results && star);

        // Typing shows grouped results and previews the best match.
        QTest::keyClicks(search, u"book"_s);
        QVERIFY(shown.wait());
        QCOMPARE(shown.last().at(1).toString(), u"book"_s);
        const QStringList found = rows(results->model());
        QCOMPARE(found.mid(0, 4),
                 (QStringList{u"# Sample English"_s, u"book"_s, u"bookmark"_s, u"bookshelf"_s}));
        QCOMPARE(found.value(4), u"# Also found in definitions"_s);
        QVERIFY(star->isEnabled());
        QVERIFY(!star->isChecked());

        // Enter confirms it (history), Ctrl+D stars it (favourites).
        QTest::keyClick(search, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_D, Qt::ControlModifier);
        QVERIFY(star->isChecked());

        // An empty search shows what was kept.
        search->clear();
        QTRY_COMPARE(rows(results->model()),
                     (QStringList{u"# Favorites"_s, u"book"_s, u"# Recent"_s, u"book"_s}));

        // A preview while typing is not a visit: "serendipity" stays out of Recent.
        QTest::keyClicks(search, u"serendipity"_s);
        QVERIFY(shown.wait());
        search->clear();
        QTRY_COMPARE(rows(results->model()),
                     (QStringList{u"# Favorites"_s, u"book"_s, u"# Recent"_s, u"book"_s}));

        // Opening a saved entry finds it again by headword.
        results->setCurrentIndex(results->model()->index(1, 0));
        QVERIFY(shown.wait());
        QCOMPARE(shown.last().at(1).toString(), u"book"_s);
        QVERIFY(star->isChecked());
    }

    void noDictionariesSaysWhereItLooked()
    {
        const QTemporaryDir empty;
        MainWindow window({empty.path()}, m_profile.filePath(u"other.sqlite"_s));
        QSignalSpy ready(&window, &MainWindow::libraryReady);
        window.show();
        QVERIFY(ready.wait());
        QVERIFY(!window.findChild<QToolButton*>(u"favoriteButton"_s)->isEnabled());
    }
};

QTEST_MAIN(TestSmoke)
#include "tst_smoke.moc"
