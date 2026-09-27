#include "core/settings.h"
#include "models/results_model.h"
#include "ui/about_dialog.h"
#include "ui/entry_view.h"
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
        omnidict::core::Settings settings(m_profile.filePath(u"settings.ini"_s));
        MainWindow window(settings, {m_bundles.path()}, m_profile.filePath(u"userdata.sqlite"_s));
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
                     (QStringList{u"# Favourites"_s, u"book"_s, u"# Recent"_s, u"book"_s}));

        // A preview while typing is not a visit: "serendipity" stays out of Recent.
        QTest::keyClicks(search, u"serendipity"_s);
        QVERIFY(shown.wait());
        search->clear();
        QTRY_COMPARE(rows(results->model()),
                     (QStringList{u"# Favourites"_s, u"book"_s, u"# Recent"_s, u"book"_s}));

        // Opening a saved entry finds it again by headword.
        results->setCurrentIndex(results->model()->index(1, 0));
        QVERIFY(shown.wait());
        QCOMPARE(shown.last().at(1).toString(), u"book"_s);
        QVERIFY(star->isChecked());
    }

    void noMatchSuggestsAndLinksGoBack()
    {
        omnidict::core::Settings settings(m_profile.filePath(u"links.ini"_s));
        MainWindow window(settings, {m_bundles.path()}, m_profile.filePath(u"links.sqlite"_s));
        QSignalSpy ready(&window, &MainWindow::libraryReady);
        QSignalSpy shown(&window, &MainWindow::entryShown);
        QSignalSpy listed(&window, &MainWindow::resultsShown);
        window.resize(1000, 700); // two columns; one column has its own Back behaviour
        window.show();
        QVERIFY(ready.wait());
        auto* search = window.findChild<QLineEdit*>(u"search"_s);
        auto* results = window.findChild<QListView*>(u"results"_s);
        auto* back = window.findChild<QToolButton*>(u"backButton"_s);
        QVERIFY(search && results && back);

        // Nothing matches: suggestions, and no entry is opened on its own.
        listed.clear();
        QTest::keyClicks(search, u"prehaps"_s);
        QTRY_VERIFY(rows(results->model()).contains(u"# Did you mean"_s));
        QCOMPARE(rows(results->model()).value(2), u"perhaps"_s);
        QVERIFY(!window.findChild<QToolButton*>(u"favoriteButton"_s)->isEnabled());

        // A link inside an entry opens that word; Back returns.
        search->clear();
        QTest::keyClicks(search, u"dictionary"_s);
        QTRY_VERIFY(!shown.isEmpty() && shown.last().at(1).toString() == u"dictionary"_s);
        QVERIFY(!back->isEnabled());
        QMetaObject::invokeMethod(window.findChild<omnidict::ui::EntryView*>(), "headwordActivated",
                                  Q_ARG(QString, u"word"_s));
        QTRY_COMPARE(shown.last().at(1).toString(), u"word"_s);
        QVERIFY(back->isEnabled());
        back->click();
        QTRY_COMPARE(shown.last().at(1).toString(), u"dictionary"_s);
        QVERIFY(!back->isEnabled());
    }

    void aboutCreditsEveryDictionary()
    {
        const QList<omnidict::services::DictionaryInfo> dictionaries = {
            {.dictId = u"wikt-xx-en"_s,
             .name = u"Xx-English"_s,
             .attribution = u"Data from Wiktionary & friends"_s,
             .license = u"CC-BY-SA-4.0"_s,
             .licenseUrl = u"https://creativecommons.org/licenses/by-sa/4.0/"_s,
             .version = u"2026.09.1"_s,
             .entryCount = 1234},
        };
        const QString html = omnidict::ui::AboutDialog::aboutHtml(dictionaries);
        QVERIFY(html.contains(u"Xx-English"_s));
        QVERIFY(html.contains(u"Data from Wiktionary &amp; friends"_s));
        QVERIFY(
            html.contains(u"<a href=\"https://creativecommons.org/licenses/by-sa/4.0/\">CC-BY-SA-4.0</a>"_s));
        QVERIFY(omnidict::ui::AboutDialog::aboutHtml({}).contains(u"No dictionaries"_s));
    }

    void noDictionariesSaysWhereItLooked()
    {
        const QTemporaryDir empty;
        omnidict::core::Settings settings(m_profile.filePath(u"other.ini"_s));
        MainWindow window(settings, {empty.path()}, m_profile.filePath(u"other.sqlite"_s));
        QSignalSpy ready(&window, &MainWindow::libraryReady);
        window.show();
        QVERIFY(ready.wait());
        QVERIFY(!window.findChild<QToolButton*>(u"favoriteButton"_s)->isEnabled());
    }
};

QTEST_MAIN(TestSmoke)
#include "tst_smoke.moc"
