#include "core/settings.h"
#include "models/results_model.h"
#include "services/dictionary_manager.h"
#include "ui/about_dialog.h"
#include "ui/entry_view.h"
#include "ui/main_window.h"
#include "ui/quick_lookup.h"
#include "ui/style.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>

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
        omnidict::services::DictionaryManager manager(m_profile.filePath(u"settings-dicts"_s),
                                                      m_profile.filePath(u"settings-cache"_s),
                                                      QUrl(u"http://127.0.0.1:9/catalog.json"_s));
        MainWindow window(settings, manager, {m_bundles.path()}, m_profile.filePath(u"userdata.sqlite"_s));
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

    void quickLookupShowsTheBestMatchAndOpensItInTheMainWindow()
    {
        omnidict::core::Settings settings(m_profile.filePath(u"quick.ini"_s));
        omnidict::services::DictionaryManager manager(m_profile.filePath(u"quick-dicts"_s),
                                                      m_profile.filePath(u"quick-cache"_s),
                                                      QUrl(u"http://127.0.0.1:9/catalog.json"_s));
        MainWindow window(settings, manager, {m_bundles.path()}, m_profile.filePath(u"quick.sqlite"_s));
        QSignalSpy ready(&window, &MainWindow::libraryReady);
        QSignalSpy mainShown(&window, &MainWindow::entryShown);
        QVERIFY(ready.wait()); // the main window is not shown: `omnidict --popup` starts like this

        omnidict::ui::QuickLookup* quick = window.quickLookup();
        QSignalSpy quickShown(quick, &omnidict::ui::QuickLookup::entryShown);
        window.showQuickLookup(u"perhaps"_s);
        QVERIFY(quick->isVisible());
        QVERIFY(!window.isVisible());
        QTRY_VERIFY(!quickShown.isEmpty());
        QCOMPARE(quickShown.last().at(1).toString(), u"perhaps"_s);
        QVERIFY(mainShown.isEmpty()); // its own requests: the main window saw none of them
        if (const QString dir = qEnvironmentVariable("OMNIDICT_GRAB_DIR"); !dir.isEmpty()) {
            QDir().mkpath(dir);
            qApp->setPalette(omnidict::ui::paletteFor(omnidict::ui::Tokens::current()));
            qApp->setStyleSheet(omnidict::ui::styleSheetFor(omnidict::ui::Tokens::current()));
            QTest::qWait(100);
            quick->grab().save(dir + u"/quick-lookup.png"_s); // for review against mocks/quick-lookup.html
        }

        // Nothing found: the "did you mean" words are offered.
        auto* field = quick->findChild<QLineEdit*>(u"quickSearch"_s);
        auto* message = quick->findChild<QLabel*>(u"quickMessage"_s);
        quick->lookUp(u"prehaps"_s);
        QTRY_VERIFY(message->isVisible());
        QVERIFY(message->text().contains(u"perhaps"_s));

        // Enter hands the word to the main window, which comes up with it; the popup closes.
        quick->lookUp(u"serendipity"_s);
        QTest::keyClick(field, Qt::Key_Return);
        QTRY_VERIFY(window.isVisible());
        QVERIFY(!quick->isVisible());
        QTRY_VERIFY(!mainShown.isEmpty() && mainShown.last().at(1).toString() == u"serendipity"_s);
    }

    void quickLookupStartsWithTheClipboardAndEscapeClosesIt()
    {
        omnidict::core::Settings settings(m_profile.filePath(u"quick2.ini"_s));
        omnidict::services::DictionaryManager manager(m_profile.filePath(u"quick2-dicts"_s),
                                                      m_profile.filePath(u"quick2-cache"_s),
                                                      QUrl(u"http://127.0.0.1:9/catalog.json"_s));
        MainWindow window(settings, manager, {m_bundles.path()}, m_profile.filePath(u"quick2.sqlite"_s));
        QSignalSpy ready(&window, &MainWindow::libraryReady);
        window.show();
        QVERIFY(ready.wait());

        // No word given: the selection where the system has one, else the clipboard
        // (GNOME on Wayland: copy, then press the shortcut).
        QGuiApplication::clipboard()->setText(u"  bookshelf \nsecond line"_s);
        if (QGuiApplication::clipboard()->supportsSelection()) {
            QGuiApplication::clipboard()->setText(QString(), QClipboard::Selection);
        }
        omnidict::ui::QuickLookup* quick = window.quickLookup();
        QSignalSpy quickShown(quick, &omnidict::ui::QuickLookup::entryShown);
        window.showQuickLookup({});
        QTRY_VERIFY(!quickShown.isEmpty());
        QCOMPARE(quickShown.last().at(1).toString(), u"bookshelf"_s);

        QTest::keyClick(quick, Qt::Key_Escape);
        QVERIFY(!quick->isVisible());
        QVERIFY(window.isVisible()); // closing the popup leaves the main window alone
    }

    void quickLookupTakesOnlySomethingThatLooksLikeAWord()
    {
        using omnidict::ui::QuickLookup;
        QCOMPARE(QuickLookup::wordFrom(u"  run away  "_s), u"run away"_s);
        QCOMPARE(QuickLookup::wordFrom(u"first line\nsecond"_s), u"first line"_s);
        QCOMPARE(QuickLookup::wordFrom(u"one two three four five six"_s), QString()); // a sentence
        QCOMPARE(QuickLookup::wordFrom(QString(70, u'a')), QString());                // not a word
        QCOMPARE(QuickLookup::wordFrom(u"\n"_s), QString());
    }

    void noMatchSuggestsAndLinksGoBack()
    {
        omnidict::core::Settings settings(m_profile.filePath(u"links.ini"_s));
        omnidict::services::DictionaryManager manager(m_profile.filePath(u"links-dicts"_s),
                                                      m_profile.filePath(u"links-cache"_s),
                                                      QUrl(u"http://127.0.0.1:9/catalog.json"_s));
        MainWindow window(settings, manager, {m_bundles.path()}, m_profile.filePath(u"links.sqlite"_s));
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

        // The mouse's side buttons over the entry do the same, as in a browser.
        auto* forward = window.findChild<QToolButton*>(u"forwardButton"_s);
        QWidget* entryArea = window.findChild<omnidict::ui::EntryView*>()->viewport();
        QVERIFY(forward->isEnabled());
        QTest::mouseClick(entryArea, Qt::ForwardButton);
        QTRY_COMPARE(shown.last().at(1).toString(), u"word"_s);
        QTest::mouseClick(entryArea, Qt::BackButton);
        QTRY_COMPARE(shown.last().at(1).toString(), u"dictionary"_s);
        const qsizetype visits = shown.size();
        QTest::mouseClick(entryArea, Qt::BackButton); // nothing further back: no change
        QTest::qWait(50);
        QCOMPARE(shown.size(), visits);
    }

    void copiedEntriesKeepLabelsThatAreDrawnAsImages()
    {
        omnidict::ui::EntryView view;
        view.showEntry(u"<p class=\"def\"><span class=\"label\">derogatory</span> scoundrel</p>"_s,
                       u"From a test dictionary."_s);
        QVERIFY(view.toHtml().contains(u"omnidict-label:0"_s)); // shown as a boxed image
        const QString text = view.plainText();
        QVERIFY(text.contains(u"derogatory scoundrel"_s));
        QVERIFY(text.contains(u"From a test dictionary."_s));
    }

    void thePlaceholderCountsOnlySwitchedOnDictionaries()
    {
        omnidict::core::Settings settings(m_profile.filePath(u"placeholder.ini"_s));
        omnidict::services::DictionaryManager manager(m_profile.filePath(u"placeholder-dicts"_s),
                                                      m_profile.filePath(u"placeholder-cache"_s),
                                                      QUrl(u"http://127.0.0.1:9/catalog.json"_s));
        MainWindow window(settings, manager, {m_bundles.path()}, m_profile.filePath(u"placeholder.sqlite"_s));
        QSignalSpy ready(&window, &MainWindow::libraryReady);
        window.show();
        QVERIFY(ready.wait());
        auto* search = window.findChild<QLineEdit*>(u"search"_s);
        QCOMPARE(search->placeholderText(), u"Search 1 dictionary"_s);
        settings.setDisabledDictionaries({u"sample-en"_s});
        QCOMPARE(search->placeholderText(), u"Switch on a dictionary to search"_s);
        settings.setDisabledDictionaries({});
        settings.setDictionaryFilter(u"sample-en"_s);
        QCOMPARE(search->placeholderText(), u"Search Sample English"_s);
    }

    void controlWheelDoesNotZoomTheEntry()
    {
        omnidict::ui::EntryView view;
        view.showEntry(u"<p class=\"def\">text</p>"_s, {});
        const qreal before = view.document()->defaultFont().pointSizeF();
        const int pixelsBefore = view.document()->defaultFont().pixelSize();
        QWheelEvent wheel(QPointF(5, 5), view.mapToGlobal(QPointF(5, 5)), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &wheel);
        QCOMPARE(view.document()->defaultFont().pointSizeF(), before);
        QCOMPARE(view.document()->defaultFont().pixelSize(), pixelsBefore);
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
        QVERIFY(!html.contains(u"Source"_s)); // a bundle from before schema 3 records none

        // The upstream source, named by its site (mocks/about.html note 4).
        QList<omnidict::services::DictionaryInfo> withSource = dictionaries;
        withSource[0].sourceUrl = u"https://download.freedict.org/dictionaries/deu-eng/1.9-fd1/src.tar.xz"_s;
        QVERIFY(omnidict::ui::AboutDialog::aboutHtml(withSource)
                    .contains(u"Source: <a href=\"https://download.freedict.org/dictionaries/deu-eng/1.9-fd1/"
                              u"src.tar.xz\">download.freedict.org</a>"_s));
    }

    void noDictionariesSaysWhereItLooked()
    {
        const QTemporaryDir empty;
        omnidict::core::Settings settings(m_profile.filePath(u"other.ini"_s));
        omnidict::services::DictionaryManager manager(m_profile.filePath(u"other-dicts"_s),
                                                      m_profile.filePath(u"other-cache"_s),
                                                      QUrl(u"http://127.0.0.1:9/catalog.json"_s));
        MainWindow window(settings, manager, {empty.path()}, m_profile.filePath(u"other.sqlite"_s));
        QSignalSpy ready(&window, &MainWindow::libraryReady);
        window.show();
        QVERIFY(ready.wait());
        QVERIFY(!window.findChild<QToolButton*>(u"favoriteButton"_s)->isEnabled());
    }
};

QTEST_MAIN(TestSmoke)
#include "tst_smoke.moc"
