#include "ui/main_window.h"

#include "models/results_model.h"
#include "ui/about_dialog.h"
#include "ui/entry_view.h"
#include "ui/logging.h"
#include "ui/results_delegate.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
// PLAN 7.2: results follow typing after a short pause, not on every key.
constexpr int kSearchDebounceMs = 80;
constexpr int kResultsPaneWidth = 320;
constexpr int kEntryPaneWidth = 640;
} // namespace

MainWindow::MainWindow(QStringList roots, const QString& userDataPath, QWidget* parent)
    : QMainWindow(parent)
    , m_lookup(new services::LookupService) // parentless: moved to the lookup thread below
    , m_roots(std::move(roots))
{
    services::registerLookupTypes();
    m_lookup->moveToThread(&m_lookupThread);
    connect(&m_lookupThread, &QThread::finished, m_lookup, &QObject::deleteLater);
    m_lookupThread.setObjectName(u"lookup"_s);
    m_lookupThread.start();

    setupUi();
    connectLookup();
    m_entry->showMessage(tr("Opening dictionaries..."));
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::openUserData, Qt::QueuedConnection,
                              userDataPath);
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::openLibrary, Qt::QueuedConnection, m_roots);
}

MainWindow::~MainWindow()
{
    m_lookupThread.quit();
    m_lookupThread.wait();
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("Omnidict"));

    auto* splitter = new QSplitter;
    splitter->addWidget(buildSearchPane());
    splitter->addWidget(buildEntryPane());
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({kResultsPaneWidth, kEntryPaneWidth});
    splitter->setChildrenCollapsible(false);
    setCentralWidget(splitter);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kSearchDebounceMs);

    connect(m_search, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this, &MainWindow::requestSearch);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &MainWindow::requestSearch);
    connect(m_results->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onCurrentResultChanged);
    connect(m_entry, &EntryView::headwordActivated, this, &MainWindow::followHeadword);
    connect(m_star, &QToolButton::toggled, this, &MainWindow::onFavoriteToggled);

    m_search->setFocus();
}

QWidget* MainWindow::buildSearchPane()
{
    m_search = new QLineEdit;
    m_search->setObjectName(u"search"_s);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);

    m_filter = new QComboBox;
    m_filter->setObjectName(u"dictionaryFilter"_s);
    m_filter->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_filter->setToolTip(tr("Search one dictionary or all of them"));
    m_filter->addItem(tr("All dictionaries"), QString());

    m_model = new models::ResultsModel(this);
    m_results = new QListView;
    m_results->setObjectName(u"results"_s);
    m_results->setModel(m_model);
    m_results->setItemDelegate(new ResultsDelegate(m_results));
    m_results->setUniformItemSizes(false);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* menu = new QMenu(this);
    menu->addAction(tr("About Omnidict"), this, &MainWindow::showAbout);
    auto* menuButton = new QToolButton;
    menuButton->setObjectName(u"menuButton"_s);
    menuButton->setIcon(QIcon::fromTheme(u"open-menu-symbolic"_s, QIcon::fromTheme(u"application-menu"_s)));
    if (menuButton->icon().isNull()) {
        menuButton->setText(u"\u2630"_s); // no icon theme: a plain menu glyph
    }
    menuButton->setToolTip(tr("Menu"));
    menuButton->setAutoRaise(true);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setMenu(menu);

    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_filter);
    searchRow->addWidget(menuButton);
    auto* pane = new QWidget;
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(8, 8, 0, 8);
    layout->addLayout(searchRow);
    layout->addWidget(m_results, 1);
    return pane;
}

QWidget* MainWindow::buildEntryPane()
{
    m_entry = new EntryView;
    m_star = new QToolButton;
    m_star->setObjectName(u"favoriteButton"_s);
    m_star->setCheckable(true);
    m_star->setEnabled(false);
    m_star->setAutoRaise(true);
    showFavoriteState(false);
    auto* starShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
    connect(starShortcut, &QShortcut::activated, m_star, &QToolButton::click);

    auto* entryBar = new QHBoxLayout;
    entryBar->setContentsMargins(0, 4, 8, 0);
    entryBar->addStretch(1);
    entryBar->addWidget(m_star);
    auto* pane = new QWidget;
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(entryBar);
    layout->addWidget(m_entry, 1);
    return pane;
}

void MainWindow::connectLookup()
{
    connect(m_lookup, &services::LookupService::libraryOpened, this, &MainWindow::onLibraryOpened);
    connect(m_lookup, &services::LookupService::searchFinished, this, &MainWindow::onSearchFinished);
    connect(m_lookup, &services::LookupService::entryLoaded, this, &MainWindow::onEntryLoaded);
    connect(m_lookup, &services::LookupService::entryNotFound, this, &MainWindow::onEntryNotFound);
    connect(m_lookup, &services::LookupService::savedEntries, this, &MainWindow::onSavedEntries);
}

void MainWindow::setQuery(const QString& text)
{
    m_search->setText(text);
}

void MainWindow::onLibraryOpened(const QList<services::DictionaryInfo>& dictionaries,
                                 const QStringList& problems)
{
    m_dictionaries = dictionaries;
    while (m_filter->count() > 1) {
        m_filter->removeItem(1);
    }
    for (const services::DictionaryInfo& dictionary : dictionaries) {
        m_filter->addItem(dictionary.name, dictionary.dictId);
    }
    for (const QString& problem : problems) {
        qCWarning(lcUi) << "dictionary not usable:" << problem;
    }
    if (dictionaries.isEmpty()) {
        m_entry->showMessage(tr("No dictionaries found. Looked in: %1").arg(m_roots.join(u", "_s)));
    } else {
        // Plurals by hand: without a translation loaded, %n forms render literally.
        m_entry->showMessage(
            dictionaries.size() == 1
                ? tr("1 dictionary ready. Type a word to look it up.")
                : tr("%1 dictionaries ready. Type a word to look it up.").arg(dictionaries.size()));
    }
    Q_EMIT libraryReady();
    requestSearch();
}

QString MainWindow::currentDictId() const
{
    return m_filter->currentData().toString();
}

void MainWindow::requestSearch()
{
    m_debounce->stop();
    const QString text = m_search->text();
    m_searchRequest = m_nextRequest++;
    if (text.trimmed().isEmpty()) {
        // An empty search shows favourites and recent entries (PLAN 7.2).
        QMetaObject::invokeMethod(m_lookup, &services::LookupService::requestSaved, Qt::QueuedConnection);
        return;
    }
    const core::SearchQuery query{.text = text, .dictId = currentDictId()};
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::search, Qt::QueuedConnection,
                              m_searchRequest, query);
}

void MainWindow::onSearchFinished(quint64 requestId, const core::SearchResults& results)
{
    if (requestId != m_searchRequest) {
        return; // the user has typed on since
    }
    m_model->setResults(results);
    const int first = m_model->firstEntryRow();
    if (first >= 0) {
        // Show the best match straight away; the focus stays in the search field.
        // A preview, not a choice: it does not go into the history.
        m_autoSelecting = true;
        m_results->setCurrentIndex(m_model->index(first));
        m_autoSelecting = false;
        m_results->scrollToTop();
    } else {
        m_entry->showMessage(tr("No results for \"%1\".").arg(results.text.trimmed()));
    }
}

void MainWindow::onCurrentResultChanged(const QModelIndex& current)
{
    if (!current.isValid()) {
        return;
    }
    const QString dictId = current.data(models::ResultsModel::DictIdRole).toString();
    const qint64 entryId = current.data(models::ResultsModel::EntryIdRole).toLongLong();
    m_recordNext = !m_autoSelecting;
    m_entryRequest = m_nextRequest++;
    if (entryId == 0) {
        // A saved entry: found again by headword, since entry ids change between versions.
        QMetaObject::invokeMethod(m_lookup, &services::LookupService::resolveHeadword, Qt::QueuedConnection,
                                  m_entryRequest, current.data(Qt::DisplayRole).toString(), dictId);
        return;
    }
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::loadEntry, Qt::QueuedConnection,
                              m_entryRequest, dictId, entryId);
}

void MainWindow::onEntryLoaded(quint64 requestId, const QString& dictId, const core::Entry& entry,
                               const QString& html, bool favorite)
{
    if (requestId != m_entryRequest) {
        return;
    }
    m_entryDictId = dictId;
    m_entryHeadword = entry.headword;
    m_entry->showEntry(html);
    {
        const QSignalBlocker blocker(m_star);
        m_star->setEnabled(true);
        m_star->setChecked(favorite);
    }
    showFavoriteState(favorite);
    if (m_recordNext) {
        recordShownEntry();
    }
    Q_EMIT entryShown(dictId, entry.headword);
}

void MainWindow::showFavoriteState(bool favorite)
{
    m_star->setText(favorite ? u"\u2605"_s : u"\u2606"_s);
    m_star->setToolTip(favorite ? tr("Remove from favorites (Ctrl+D)") : tr("Add to favorites (Ctrl+D)"));
}

void MainWindow::recordShownEntry()
{
    m_recordNext = false;
    if (m_entryHeadword.isEmpty()) {
        return;
    }
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::recordView, Qt::QueuedConnection,
                              m_entryDictId, m_entryHeadword);
}

void MainWindow::onSavedEntries(const QList<core::SavedEntry>& recent,
                                const QList<core::SavedEntry>& favorites)
{
    if (!m_search->text().trimmed().isEmpty()) {
        return; // the user has started typing since
    }
    m_model->setSaved(recent, favorites);
}

void MainWindow::onFavoriteToggled(bool favorite)
{
    if (m_entryHeadword.isEmpty()) {
        return;
    }
    showFavoriteState(favorite);
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::setFavorite, Qt::QueuedConnection,
                              m_entryDictId, m_entryHeadword, favorite);
    if (m_search->text().trimmed().isEmpty()) {
        QMetaObject::invokeMethod(m_lookup, &services::LookupService::requestSaved, Qt::QueuedConnection);
    }
}

void MainWindow::onEntryNotFound(quint64 requestId, const QString& text)
{
    if (requestId == m_entryRequest) {
        m_entry->showMessage(tr("\"%1\" is not in your dictionaries.").arg(text));
    }
}

void MainWindow::followHeadword(const QString& headword)
{
    // The list follows the link too, so going on from there is one key away.
    const QSignalBlocker blocker(m_search);
    m_search->setText(headword);
    requestSearch();
    m_recordNext = true;
    m_entryRequest = m_nextRequest++;
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::resolveHeadword, Qt::QueuedConnection,
                              m_entryRequest, headword, m_entryDictId);
}

void MainWindow::showAbout()
{
    AboutDialog dialog(m_dictionaries, this);
    dialog.exec();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Down && m_model->firstEntryRow() >= 0) {
            m_results->setFocus();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !m_entryHeadword.isEmpty()) {
            recordShownEntry(); // Enter confirms the entry on screen as the one wanted
            return true;
        }
        if (key->key() == Qt::Key_Escape && !m_search->text().isEmpty()) {
            m_search->clear();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

} // namespace omnidict::ui
