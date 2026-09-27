#include "ui/main_window.h"

#include "core/settings.h"
#include "models/results_model.h"
#include "ui/about_dialog.h"
#include "ui/empty_state.h"
#include "ui/entry_view.h"
#include "ui/icons.h"
#include "ui/logging.h"
#include "ui/results_delegate.h"
#include "ui/search_field.h"
#include "ui/style.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
// PLAN 7.2: results follow typing after a short pause, not on every key.
constexpr int kSearchDebounceMs = 80;
constexpr int kResultsPaneWidth = 340; // mocks/mock.css --list-w
constexpr int kEntryPaneWidth = 660;
constexpr int kHeaderHeight = 56;
constexpr int kEntryBarHeight = 44;
constexpr int kHeaderMargin = 12;
constexpr int kIconSize = 18;
constexpr int kChevronSize = 14;
constexpr int kMaxVisits = 50; // mocks/main-link.html
constexpr int kPerDictionary = 20;
constexpr int kFullTextPerDictionary = 5;
constexpr int kSuggestions = 5;

QToolButton* flatButton(const QString& objectName, const QString& toolTip)
{
    auto* button = new QToolButton;
    button->setObjectName(objectName);
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    button->setAutoRaise(true);
    button->setIconSize(QSize(kIconSize, kIconSize));
    return button;
}

/// "Spanish-English (Wiktionary)" without the source in brackets, for the chip.
QString shortName(const QString& name)
{
    const qsizetype open = name.lastIndexOf(u" ("_s);
    return open > 0 && name.endsWith(u')') ? name.left(open) : name;
}

} // namespace

MainWindow::MainWindow(core::Settings& settings, QStringList roots, const QString& userDataPath,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
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
    connectSettings();
    m_empty->setContent(u"book"_s, tr("Opening dictionaries..."), {});
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
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildHeader());

    m_splitter = new QSplitter;
    m_splitter->setHandleWidth(1);
    m_splitter->addWidget(buildResults());
    m_splitter->addWidget(buildEntryPane());
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({kResultsPaneWidth, kEntryPaneWidth});
    m_splitter->setChildrenCollapsible(false);
    layout->addWidget(m_splitter, 1);
    setCentralWidget(central);
    // The search field lines up with the list under it (mocks/main.html).
    connect(m_splitter, &QSplitter::splitterMoved, this,
            [this] { m_search->setFixedWidth(m_splitter->sizes().constFirst() - (2 * kHeaderMargin)); });
    m_search->setFixedWidth(kResultsPaneWidth - (2 * kHeaderMargin));

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kSearchDebounceMs);
    connect(m_search, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this, &MainWindow::requestSearch);
    connect(m_results->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onCurrentResultChanged);
    connect(m_results, &QListView::activated, this, [this] { recordShownEntry(); });
    connect(m_entry, &EntryView::headwordActivated, this, &MainWindow::followHeadword);
    connect(m_star, &QToolButton::toggled, this, &MainWindow::onFavoriteToggled);

    setupActions();
    buildMainMenu();
    rebuildFilterMenu();
    refreshIcons();
    const QByteArray geometry = m_settings.windowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    m_search->setFocus();
}

QWidget* MainWindow::buildHeader()
{
    auto* header = new QFrame;
    header->setObjectName(u"header"_s);
    header->setProperty("bar", true);
    header->setFixedHeight(kHeaderHeight);

    m_search = new SearchField;
    m_search->setObjectName(u"search"_s);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setAccessibleName(tr("Search"));
    m_search->installEventFilter(this);

    m_filterMenu = new QMenu(this);
    m_filterButton = new QToolButton;
    m_filterButton->setObjectName(u"dictionaryFilter"_s);
    m_filterButton->setProperty("chip", true);
    m_filterButton->setToolTip(tr("Search in (Ctrl+L)"));
    m_filterButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_filterButton->setIconSize(QSize(kChevronSize + 1, kChevronSize + 1));
    m_filterButton->setPopupMode(QToolButton::InstantPopup);
    m_filterButton->setMenu(m_filterMenu);
    // The chevron sits inside the chip, after the text.
    m_filterChevron = new QLabel(m_filterButton);
    auto* chipLayout = new QHBoxLayout(m_filterButton);
    chipLayout->setContentsMargins(0, 0, 10, 0);
    chipLayout->addStretch(1);
    chipLayout->addWidget(m_filterChevron);
    m_filterButton->setStyleSheet(u"QToolButton { padding-right: 30px; }"_s);

    m_menuButton = flatButton(u"menuButton"_s, tr("Main menu (F10)"));
    m_menuButton->setPopupMode(QToolButton::InstantPopup);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(kHeaderMargin, 0, kHeaderMargin, 0);
    layout->setSpacing(8);
    layout->addWidget(m_search);
    layout->addWidget(m_filterButton);
    layout->addStretch(1);
    layout->addWidget(m_menuButton);
    return header;
}

QWidget* MainWindow::buildResults()
{
    m_model = new models::ResultsModel(this);
    m_results = new QListView;
    m_results->setObjectName(u"results"_s);
    m_results->setModel(m_model);
    auto* delegate = new ResultsDelegate(m_results);
    m_results->setItemDelegate(delegate);
    m_results->setUniformItemSizes(false);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_results->setMouseTracking(true); // hover rows
    m_results->installEventFilter(this);
    m_results->setMinimumWidth(kResultsPaneWidth - 80);
    connect(delegate, &ResultsDelegate::actionActivated, this, &MainWindow::onResultAction);
    return m_results;
}

QWidget* MainWindow::buildEntryPane()
{
    auto* bar = new QFrame;
    bar->setProperty("bar", true);
    bar->setFixedHeight(kEntryBarHeight);
    m_back = flatButton(u"backButton"_s, tr("Back (Alt+Left)"));
    m_forward = flatButton(u"forwardButton"_s, tr("Forward (Alt+Right)"));
    m_crumb = new QLabel;
    m_crumb->setProperty("muted", true);
    m_crumb->setProperty("small", true);
    m_copy = flatButton(u"copyButton"_s, tr("Copy entry (Ctrl+Shift+C)"));
    m_star = flatButton(u"favoriteButton"_s, tr("Add to favourites (Ctrl+D)"));
    m_star->setProperty("favorite", true);
    m_star->setCheckable(true);
    connect(m_back, &QToolButton::clicked, this, &MainWindow::goBack);
    connect(m_forward, &QToolButton::clicked, this, &MainWindow::goForward);
    connect(m_copy, &QToolButton::clicked, this, &MainWindow::copyEntry);

    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(kHeaderMargin, 0, kHeaderMargin, 0);
    barLayout->setSpacing(4);
    barLayout->addWidget(m_back);
    barLayout->addWidget(m_forward);
    barLayout->addWidget(m_crumb, 1);
    barLayout->addWidget(m_copy);
    barLayout->addWidget(m_star);

    m_entry = new EntryView;
    m_entry->setTextSize(m_settings.entryTextSize());
    m_empty = new EmptyState;
    connect(m_empty, &EmptyState::buttonClicked, this, [this] {
        m_forceDefinitions = true;
        requestSearch();
    });
    m_stack = new QStackedWidget;
    m_stack->addWidget(m_entry);
    m_stack->addWidget(m_empty);

    auto* pane = new QWidget;
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(m_stack, 1);
    clearEntry();
    return pane;
}

void MainWindow::setupActions()
{
    const auto add = [this](const QList<QKeySequence>& keys, auto slot) {
        auto* action = new QAction(this);
        action->setShortcuts(keys);
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
        return action;
    };
    add({QKeySequence(Qt::CTRL | Qt::Key_F), QKeySequence(Qt::CTRL | Qt::Key_K)}, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    add({QKeySequence(Qt::CTRL | Qt::Key_L)}, [this] { m_filterButton->showMenu(); });
    add({QKeySequence(Qt::Key_F10)}, [this] { m_menuButton->showMenu(); });
    add({QKeySequence(Qt::ALT | Qt::Key_Left)}, &MainWindow::goBack);
    add({QKeySequence(Qt::ALT | Qt::Key_Right)}, &MainWindow::goForward);
    add({QKeySequence(Qt::CTRL | Qt::Key_D)}, [this] {
        if (m_star->isEnabled()) {
            m_star->click();
        }
    });
    add({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)}, &MainWindow::copyEntry);
    add({QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)},
        [this] { changeTextSize(1); });
    add({QKeySequence(Qt::CTRL | Qt::Key_Minus)}, [this] { changeTextSize(-1); });
    add({QKeySequence(Qt::CTRL | Qt::Key_0)},
        [this] { m_settings.setEntryTextSize(core::Settings::kDefaultEntryTextSize); });
}

void MainWindow::buildMainMenu()
{
    m_mainMenu = new QMenu(this);
    auto* about = m_mainMenu->addAction(tr("About Omnidict"), this, &MainWindow::showAbout);
    about->setObjectName(u"aboutAction"_s);
    m_mainMenu->addSeparator();
    auto* quit = m_mainMenu->addAction(tr("Quit"), qApp, &QApplication::quit);
    quit->setObjectName(u"quitAction"_s);
    quit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    quit->setShortcutContext(Qt::WindowShortcut);
    addAction(quit);
    m_menuButton->setMenu(m_mainMenu);
}

void MainWindow::rebuildFilterMenu()
{
    m_filterMenu->clear();
    auto* group = new QActionGroup(m_filterMenu);
    const QString selected = currentDictId();
    const auto addChoice = [&](const QString& text, const QString& dictId) {
        QAction* action = m_filterMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(dictId == selected);
        action->setData(dictId);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, dictId] { setFilter(dictId); });
    };
    addChoice(tr("All dictionaries"), QString());
    if (!m_dictionaries.isEmpty()) {
        m_filterMenu->addSeparator();
    }
    const QLocale locale;
    for (const services::DictionaryInfo& info : std::as_const(m_dictionaries)) {
        // After a tab, the text goes in the menu's right-hand column.
        addChoice(shortName(info.name) + u'\t' + tr("%1 entries").arg(locale.toString(info.entryCount)),
                  info.dictId);
    }
    const services::DictionaryInfo* chosen = dictionary(selected);
    m_filterButton->setText(chosen != nullptr ? shortName(chosen->name) : tr("All dictionaries"));
}

void MainWindow::refreshIcons()
{
    const Tokens& t = Tokens::current();
    m_filterButton->setIcon(icons::themed(u"books"_s, t.muted));
    m_filterChevron->setPixmap(icons::pixmap(u"down"_s, t.muted, kChevronSize, devicePixelRatioF()));
    m_menuButton->setIcon(icons::themed(u"menu"_s, t.text));
    m_back->setIcon(icons::themed(u"back"_s, t.text));
    m_forward->setIcon(icons::themed(u"forward"_s, t.text));
    m_copy->setIcon(icons::themed(u"copy"_s, t.text));
    showFavoriteState(m_star->isChecked());
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && m_search != nullptr) {
        refreshIcons();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_settings.setWindowGeometry(saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::connectLookup()
{
    connect(m_lookup, &services::LookupService::libraryOpened, this, &MainWindow::onLibraryOpened);
    connect(m_lookup, &services::LookupService::searchFinished, this, &MainWindow::onSearchFinished);
    connect(m_lookup, &services::LookupService::entryLoaded, this, &MainWindow::onEntryLoaded);
    connect(m_lookup, &services::LookupService::entryNotFound, this, &MainWindow::onEntryNotFound);
    connect(m_lookup, &services::LookupService::savedEntries, this, &MainWindow::onSavedEntries);
}

void MainWindow::connectSettings()
{
    connect(&m_settings, &core::Settings::entryTextSizeChanged, m_entry, &EntryView::setTextSize);
    connect(&m_settings, &core::Settings::searchOptionsChanged, this, &MainWindow::requestSearch);
}

void MainWindow::setQuery(const QString& text)
{
    m_search->setText(text);
}

void MainWindow::onLibraryOpened(const QList<services::DictionaryInfo>& dictionaries,
                                 const QStringList& problems)
{
    m_dictionaries = dictionaries;
    m_libraryOpen = true;
    QHash<QString, QString> names;
    for (const services::DictionaryInfo& info : dictionaries) {
        names.insert(info.dictId, info.name);
    }
    m_model->setDictionaryNames(names);
    for (const QString& problem : problems) {
        qCWarning(lcUi) << "dictionary not usable:" << problem;
    }
    // Plurals by hand: without a translation loaded, %n forms render literally.
    m_search->setPlaceholderText(dictionaries.size() == 1
                                     ? tr("Search 1 dictionary")
                                     : tr("Search %1 dictionaries").arg(dictionaries.size()));
    rebuildFilterMenu();
    if (dictionaries.isEmpty()) {
        m_empty->setContent(u"books"_s, tr("No dictionaries yet"),
                            tr("Looked in: %1").arg(m_roots.join(u", "_s)));
        m_stack->setCurrentWidget(m_empty);
    }
    Q_EMIT libraryReady();
    requestSearch();
}

QString MainWindow::currentDictId() const
{
    const QString dictId = m_settings.dictionaryFilter();
    // A filter for a dictionary that is gone searches everything.
    return m_libraryOpen && dictionary(dictId) == nullptr ? QString() : dictId;
}

const services::DictionaryInfo* MainWindow::dictionary(const QString& dictId) const
{
    for (const services::DictionaryInfo& info : m_dictionaries) {
        if (info.dictId == dictId) {
            return &info;
        }
    }
    return nullptr;
}

void MainWindow::setFilter(const QString& dictId)
{
    m_settings.setDictionaryFilter(dictId);
    rebuildFilterMenu();
    requestSearch();
}

void MainWindow::requestSearch()
{
    m_debounce->stop();
    const QString text = m_search->text();
    m_searchRequest = m_nextRequest++;
    if (text.trimmed().isEmpty()) {
        // An empty search shows favourites and recent entries (PLAN 7.2).
        m_forceDefinitions = false;
        QMetaObject::invokeMethod(m_lookup, &services::LookupService::requestSaved, Qt::QueuedConnection);
        if (!m_dictionaries.isEmpty()) {
            showWelcome();
        }
        return;
    }
    const bool definitions = m_forceDefinitions || m_settings.searchDefinitions();
    m_forceDefinitions = false;
    const core::SearchQuery query{.text = text,
                                  .dictId = currentDictId(),
                                  .perDictionary = kPerDictionary,
                                  .fullTextPerDictionary = definitions ? kFullTextPerDictionary : 0,
                                  .suggestions = m_settings.suggestSpellings() ? kSuggestions : 0};
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::search, Qt::QueuedConnection,
                              m_searchRequest, query);
}

void MainWindow::onSearchFinished(quint64 requestId, const core::SearchResults& results,
                                  const QStringList& favorites)
{
    if (requestId != m_searchRequest) {
        return; // the user has typed on since
    }
    const int searched = currentDictId().isEmpty() ? static_cast<int>(m_dictionaries.size()) : 1;
    m_model->setResults(results, favorites, searched);
    m_results->scrollToTop();
    Q_EMIT resultsShown();
    if (results.headwords.isEmpty()) {
        showNoMatch(results.text.trimmed(), !results.suggestions.isEmpty());
        return;
    }
    if (m_settings.showBestMatch()) {
        // Show the best match straight away; the focus stays in the search field.
        // A preview, not a choice: it does not go into the history.
        m_autoSelecting = true;
        m_results->setCurrentIndex(m_model->index(m_model->firstEntryRow()));
        m_autoSelecting = false;
    }
}

void MainWindow::onCurrentResultChanged(const QModelIndex& current)
{
    if (!current.isValid() ||
        current.data(models::ResultsModel::KindRole).value<models::ResultsModel::RowKind>() !=
            models::ResultsModel::RowKind::Entry) {
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

void MainWindow::onResultAction(const QModelIndex& index)
{
    switch (index.data(models::ResultsModel::ActionRole).value<models::ResultsModel::Action>()) {
    case models::ResultsModel::Action::ClearHistory:
        QMetaObject::invokeMethod(m_lookup, &services::LookupService::clearHistory, Qt::QueuedConnection);
        break;
    case models::ResultsModel::Action::ShowAllDefinitions:
        m_model->showAllDefinitions();
        break;
    case models::ResultsModel::Action::None:
        break;
    }
}

void MainWindow::onEntryLoaded(quint64 requestId, const QString& dictId, const core::Entry& entry,
                               const QString& html, bool favorite)
{
    if (requestId != m_entryRequest) {
        return;
    }
    m_current = {.dictId = dictId, .headword = entry.headword};
    const services::DictionaryInfo* info = dictionary(dictId);
    const QString credit = info != nullptr
                               ? tr("%1. %2, version %3.").arg(info->attribution, info->name, info->version)
                               : QString();
    m_entry->showEntry(html, credit);
    m_stack->setCurrentWidget(m_entry);
    m_crumb->setText(info != nullptr ? info->name : QString());
    m_copy->setEnabled(true);
    {
        const QSignalBlocker blocker(m_star);
        m_star->setEnabled(true);
        m_star->setChecked(favorite);
    }
    showFavoriteState(favorite);
    updateNavigation();
    if (m_recordNext) {
        recordShownEntry();
    }
    Q_EMIT entryShown(dictId, entry.headword);
}

void MainWindow::showFavoriteState(bool favorite)
{
    const Tokens& t = Tokens::current();
    m_star->setIcon(favorite ? icons::themed(u"star-filled"_s, t.warm) : icons::themed(u"star"_s, t.text));
    const QString tip = favorite ? tr("Remove from favourites (Ctrl+D)") : tr("Add to favourites (Ctrl+D)");
    m_star->setToolTip(tip);
    m_star->setAccessibleName(tip);
}

void MainWindow::recordShownEntry()
{
    m_recordNext = false;
    if (m_current.headword.isEmpty() || !m_settings.rememberHistory()) {
        return;
    }
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::recordView, Qt::QueuedConnection,
                              m_current.dictId, m_current.headword);
}

void MainWindow::onSavedEntries(const QList<core::SavedEntry>& recent,
                                const QList<core::SavedEntry>& favorites)
{
    if (!m_search->text().trimmed().isEmpty()) {
        return; // the user has started typing since
    }
    m_model->setSaved(recent, favorites);
    Q_EMIT resultsShown();
}

void MainWindow::onFavoriteToggled(bool favorite)
{
    if (m_current.headword.isEmpty()) {
        return;
    }
    showFavoriteState(favorite);
    m_model->setFavorite(m_current.dictId, m_current.headword, favorite);
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::setFavorite, Qt::QueuedConnection,
                              m_current.dictId, m_current.headword, favorite);
    if (m_search->text().trimmed().isEmpty()) {
        QMetaObject::invokeMethod(m_lookup, &services::LookupService::requestSaved, Qt::QueuedConnection);
    }
}

void MainWindow::onEntryNotFound(quint64 requestId, const QString& text)
{
    if (requestId == m_entryRequest) {
        clearEntry();
        m_empty->setContent(u"search"_s, tr("\"%1\" is not in your dictionaries").arg(text), {});
        m_stack->setCurrentWidget(m_empty);
    }
}

void MainWindow::followHeadword(const QString& headword)
{
    if (!m_current.headword.isEmpty()) {
        m_backStack.append(m_current);
        if (m_backStack.size() > kMaxVisits) {
            m_backStack.removeFirst();
        }
    }
    m_forwardStack.clear();
    openVisit({.dictId = m_current.dictId, .headword = headword});
    m_recordNext = true;
}

void MainWindow::openVisit(const Visit& visit)
{
    m_recordNext = false;
    m_results->clearSelection();
    m_entryRequest = m_nextRequest++;
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::resolveHeadword, Qt::QueuedConnection,
                              m_entryRequest, visit.headword, visit.dictId);
}

void MainWindow::goBack()
{
    if (m_backStack.isEmpty()) {
        return;
    }
    m_forwardStack.append(m_current);
    openVisit(m_backStack.takeLast());
}

void MainWindow::goForward()
{
    if (m_forwardStack.isEmpty()) {
        return;
    }
    m_backStack.append(m_current);
    openVisit(m_forwardStack.takeLast());
}

void MainWindow::updateNavigation()
{
    m_back->setEnabled(!m_backStack.isEmpty());
    m_forward->setEnabled(!m_forwardStack.isEmpty());
}

void MainWindow::copyEntry()
{
    if (m_stack->currentWidget() == m_entry && !m_current.headword.isEmpty()) {
        QGuiApplication::clipboard()->setText(m_entry->plainText());
    }
}

void MainWindow::changeTextSize(int step)
{
    m_settings.setEntryTextSize(m_settings.entryTextSize() + step);
}

void MainWindow::clearEntry()
{
    m_current = {};
    m_crumb->clear();
    m_copy->setEnabled(false);
    {
        const QSignalBlocker blocker(m_star);
        m_star->setChecked(false);
        m_star->setEnabled(false);
    }
    showFavoriteState(false);
    updateNavigation();
}

void MainWindow::showWelcome()
{
    clearEntry();
    m_empty->setContent(u"book"_s, tr("Type a word to look it up"), {},
                        {tr("Use ? for one letter and * for any run, as in c?t or bio*"),
                         tr("Ctrl+L picks one dictionary"), tr("F1 lists every shortcut")});
    m_stack->setCurrentWidget(m_empty);
}

void MainWindow::showNoMatch(const QString& text, bool hasSuggestions)
{
    clearEntry();
    const bool canSearchDefinitions = !m_settings.searchDefinitions();
    QString hint;
    if (hasSuggestions) {
        hint = canSearchDefinitions ? tr("Pick a suggestion on the left, or search inside definitions.")
                                    : tr("Pick a suggestion on the left.");
    } else {
        hint = tr("Check the spelling, or use a wildcard such as bio*.");
    }
    m_empty->setContent(u"search"_s, tr("No entry for %1").arg(text), hint, {},
                        canSearchDefinitions ? tr("Search definitions") : QString());
    m_stack->setCurrentWidget(m_empty);
}

void MainWindow::showAbout()
{
    AboutDialog dialog(m_dictionaries, this);
    dialog.exec();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress) {
        return QMainWindow::eventFilter(watched, event);
    }
    const auto* key = static_cast<QKeyEvent*>(event);
    if (watched == m_search) {
        if (key->key() == Qt::Key_Down && m_model->firstEntryRow() >= 0) {
            m_results->setFocus();
            if (!m_results->currentIndex().isValid()) {
                m_results->setCurrentIndex(m_model->index(m_model->firstEntryRow()));
            }
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !m_current.headword.isEmpty()) {
            recordShownEntry(); // Enter confirms the entry on screen as the one wanted
            return true;
        }
        if (key->key() == Qt::Key_Escape && !m_search->text().isEmpty()) {
            m_search->clear();
            return true;
        }
    } else if (watched == m_results) {
        const bool atTop = m_results->currentIndex().row() <= m_model->firstEntryRow();
        if (key->key() == Qt::Key_Escape || (key->key() == Qt::Key_Up && atTop)) {
            m_search->setFocus();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

} // namespace omnidict::ui
