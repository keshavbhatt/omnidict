#include "ui/main_window.h"

#include "core/settings.h"
#include "models/results_model.h"
#include "services/dictionary_manager.h"
#include "ui/about_dialog.h"
#include "ui/bug_report_dialog.h"
#include "ui/diagnostics.h"
#include "ui/dictionaries_dialog.h"
#include "ui/empty_state.h"
#include "ui/entry_view.h"
#include "ui/first_run_panel.h"
#include "ui/icons.h"
#include "ui/logging.h"
#include "ui/results_delegate.h"
#include "ui/search_field.h"
#include "ui/settings_dialog.h"
#include "ui/shortcuts_dialog.h"
#include "ui/style.h"
#include "ui/toast.h"
#include "ui/whats_new_dialog.h"

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
#include <QMouseEvent>
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
constexpr int kNarrowWidth = 720;

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
/// A credit line that already ends a sentence, without its full stop (we add our own).
QString withoutFinalStop(const QString& text)
{
    const QString trimmed = text.trimmed();
    return trimmed.endsWith(u'.') ? trimmed.chopped(1) : trimmed;
}

QString shortName(const QString& name)
{
    const qsizetype open = name.lastIndexOf(u" ("_s);
    return open > 0 && name.endsWith(u')') ? name.left(open) : name;
}

} // namespace

MainWindow::MainWindow(core::Settings& settings, services::DictionaryManager& manager, QStringList roots,
                       const QString& userDataPath, QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_manager(manager)
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
    m_entryPane = buildEntryPane();
    m_splitter->addWidget(m_entryPane);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({kResultsPaneWidth, kEntryPaneWidth});
    m_splitter->setChildrenCollapsible(false);
    m_firstRun = new FirstRunPanel(m_manager);
    m_bodyStack = new QStackedWidget;
    m_bodyStack->addWidget(m_splitter);
    m_bodyStack->addWidget(m_firstRun);
    layout->addWidget(m_bodyStack, 1);
    setCentralWidget(central);
    // The search field lines up with the list under it (mocks/main.html).
    connect(m_splitter, &QSplitter::splitterMoved, this, &MainWindow::applyLayoutMode);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kSearchDebounceMs);
    connect(m_search, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this, &MainWindow::requestSearch);
    connect(m_results->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onCurrentResultChanged);
    connect(m_results, &QListView::activated, this, [this] {
        recordShownEntry();
        showEntryColumn(true);
    });
    // In one column a click or Enter opens the entry; moving with the keys keeps the list.
    connect(m_results, &QListView::clicked, this, [this] { showEntryColumn(true); });
    connect(m_search, &QLineEdit::textEdited, this, [this] { showEntryColumn(false); });
    connect(m_entry, &EntryView::headwordActivated, this, &MainWindow::followHeadword);
    connect(m_star, &QToolButton::toggled, this, &MainWindow::onFavoriteToggled);

    setupActions();
    buildMainMenu();
    rebuildFilterMenu();
    refreshIcons();
    applyLayoutMode();
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

    m_dictionariesButton = flatButton(u"dictionariesButton"_s, tr("Dictionaries (Ctrl+Shift+D)"));
    m_menuButton = flatButton(u"menuButton"_s, tr("Main menu (F10)"));
    m_menuButton->setPopupMode(QToolButton::InstantPopup);

    m_headerLayout = new QHBoxLayout(header);
    m_headerLayout->setContentsMargins(kHeaderMargin, 0, kHeaderMargin, 0);
    m_headerLayout->setSpacing(8);
    m_headerLayout->addWidget(m_search);
    m_headerLayout->addWidget(m_filterButton);
    m_headerLayout->addStretch(1);
    m_headerLayout->addWidget(m_dictionariesButton);
    m_headerLayout->addWidget(m_menuButton);
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
    qApp->installEventFilter(this); // the mouse's Back and Forward buttons (handleMouseNavigation)
    m_results->setMinimumWidth(kResultsPaneWidth - 80);
    applyListTextSize(m_settings.entryTextSize());
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
    // Items that also have a key keep working with the menu closed: they are the window's actions too.
    const auto add = [this](const QString& text, const QString& glyph, const QList<QKeySequence>& keys,
                            auto slot) {
        QAction* action = m_mainMenu->addAction(text);
        action->setShortcuts(keys);
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
        m_menuIcons.append({action, glyph});
        return action;
    };
    add(tr("Dictionaries..."), u"books"_s, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D)},
        [this] { showDictionaries(); });
    add(tr("Settings..."), u"settings"_s, {QKeySequence(Qt::CTRL | Qt::Key_Comma)},
        &MainWindow::showSettings);
    m_mainMenu->addSeparator();
    add(tr("Keyboard shortcuts"), u"keyboard"_s,
        {QKeySequence(Qt::Key_F1), QKeySequence(Qt::CTRL | Qt::Key_Slash)}, &MainWindow::showShortcuts);
    add(tr("What's new"), u"sparkles"_s, {}, &MainWindow::showWhatsNew);
    add(tr("Report a bug..."), u"bug"_s, {}, &MainWindow::showBugReport);
    add(tr("About Omnidict"), u"info"_s, {}, &MainWindow::showAbout)->setObjectName(u"aboutAction"_s);
    m_mainMenu->addSeparator();
    add(tr("Quit"), u"power"_s, {QKeySequence(Qt::CTRL | Qt::Key_Q)}, [] {
        QApplication::quit();
    })->setObjectName(u"quitAction"_s);
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
    const QStringList disabled = m_settings.disabledDictionaries();
    const QStringList order = m_settings.dictionaryOrder();
    QList<const services::DictionaryInfo*> shown;
    for (const services::DictionaryInfo& info : std::as_const(m_dictionaries)) {
        if (!disabled.contains(info.dictId)) {
            shown.append(&info);
        }
    }
    std::ranges::stable_sort(shown, {}, [&order](const services::DictionaryInfo* info) {
        const qsizetype at = order.indexOf(info->dictId);
        return at < 0 ? order.size() : at;
    });
    for (const services::DictionaryInfo* dictionaryInfo : std::as_const(shown)) {
        const services::DictionaryInfo& info = *dictionaryInfo;
        // After a tab, the text goes in the menu's right-hand column.
        addChoice(shortName(info.name) + u'\t' + tr("%1 entries").arg(locale.toString(info.entryCount)),
                  info.dictId);
    }
    m_filterMenu->addSeparator();
    const QAction* manage =
        m_filterMenu->addAction(tr("Manage dictionaries...") + u'\t' + tr("Ctrl+Shift+D"));
    connect(manage, &QAction::triggered, this, [this] { showDictionaries(); });
    const services::DictionaryInfo* chosen = dictionary(selected);
    m_filterButton->setText(chosen != nullptr ? shortName(chosen->name) : tr("All dictionaries"));

    // The placeholder names what a search covers: switched-off dictionaries do not count.
    // Plurals by hand: without a translation loaded, %n forms render literally.
    if (!m_libraryOpen) {
        m_search->setPlaceholderText(tr("Search"));
    } else if (m_dictionaries.isEmpty()) {
        m_search->setPlaceholderText(tr("Add a dictionary to start"));
    } else if (chosen != nullptr && !disabled.contains(chosen->dictId)) {
        m_search->setPlaceholderText(tr("Search %1").arg(shortName(chosen->name)));
    } else if (shown.isEmpty()) {
        m_search->setPlaceholderText(tr("Switch on a dictionary to search"));
    } else {
        m_search->setPlaceholderText(shown.size() == 1 ? tr("Search 1 dictionary")
                                                       : tr("Search %1 dictionaries").arg(shown.size()));
    }
}

void MainWindow::refreshIcons()
{
    const Tokens& t = Tokens::current();
    m_filterButton->setIcon(icons::themed(u"books"_s, t.muted));
    m_filterChevron->setPixmap(icons::pixmap(u"down"_s, t.muted, kChevronSize, devicePixelRatioF()));
    m_menuButton->setIcon(icons::themed(u"menu"_s, t.text));
    onDownloadChanged(); // the Dictionaries button's glyph
    for (const auto& [action, glyph] : std::as_const(m_menuIcons)) {
        action->setIcon(icons::themed(glyph, t.muted));
    }
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

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    const bool narrow = width() < kNarrowWidth;
    if (narrow != m_narrow) {
        m_narrow = narrow;
        applyLayoutMode();
    }
}

void MainWindow::applyLayoutMode()
{
    m_results->setVisible(!m_narrow || !m_entryColumn);
    m_entryPane->setVisible(!m_narrow || m_entryColumn);
    // Header items: 0 search, 1 filter, 2 spacer, 3 dictionaries, 4 menu. One column: the search takes the
    // room.
    m_headerLayout->setStretch(0, m_narrow ? 1 : 0);
    m_headerLayout->setStretch(2, m_narrow ? 0 : 1);
    if (m_narrow) {
        m_search->setMinimumWidth(0);
        m_search->setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        // The search field lines up with the list under it (mocks/main.html).
        const int listWidth = m_splitter->sizes().constFirst();
        m_search->setFixedWidth((listWidth > 0 ? listWidth : kResultsPaneWidth) - (2 * kHeaderMargin));
    }
    updateNavigation();
}

void MainWindow::showEntryColumn(bool entry)
{
    if (entry != m_entryColumn) {
        m_entryColumn = entry;
        applyLayoutMode();
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
    const auto preferencesChanged = [this] {
        rebuildFilterMenu(); // also the search placeholder
        requestSearch();
    };
    connect(&m_settings, &core::Settings::dictionaryPreferencesChanged, this, preferencesChanged);
    connect(&m_settings, &core::Settings::dictionaryFilterChanged, this, preferencesChanged);
    connect(&m_manager, &services::DictionaryManager::installed, this, &MainWindow::reopenLibrary);
    connect(&m_manager, &services::DictionaryManager::removed, this, &MainWindow::reopenLibrary);
    connect(&m_manager, &services::DictionaryManager::downloadChanged, this, &MainWindow::onDownloadChanged);
    connect(&m_manager, &services::DictionaryManager::downloadsChanged, this, &MainWindow::onDownloadChanged);
    // While something downloads the button shows the download glyph, so it opens where the downloads are.
    connect(m_dictionariesButton, &QToolButton::clicked, this,
            [this] { showDictionaries(activeDownloads() > 0); });
    connect(m_firstRun, &FirstRunPanel::browseRequested, this, [this] { showDictionaries(true); });
    connect(&m_settings, &core::Settings::entryTextSizeChanged, m_entry, &EntryView::setTextSize);
    connect(&m_settings, &core::Settings::entryTextSizeChanged, this, &MainWindow::applyListTextSize);
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
    rebuildFilterMenu(); // also the search placeholder
    if (m_dictionariesDialog != nullptr) {
        m_dictionariesDialog->setInstalled(dictionaries);
    }
    // With nothing installed the window offers dictionaries instead of a search.
    const bool none = dictionaries.isEmpty();
    m_bodyStack->setCurrentWidget(none ? static_cast<QWidget*>(m_firstRun) : m_splitter);
    m_search->setEnabled(!none);
    m_filterButton->setEnabled(!none);
    if (none) {
        qCInfo(lcUi) << "no dictionaries in" << m_roots;
    }
    Q_EMIT libraryReady();
    requestSearch();
}

QString MainWindow::currentDictId() const
{
    const QString dictId = m_settings.dictionaryFilter();
    // A filter for a dictionary that is gone or switched off searches everything.
    const bool usable = dictionary(dictId) != nullptr && !m_settings.disabledDictionaries().contains(dictId);
    return m_libraryOpen && !usable ? QString() : dictId;
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
    m_settings.setDictionaryFilter(dictId); // dictionaryFilterChanged does the rest
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
                                  .suggestions = m_settings.suggestSpellings() ? kSuggestions : 0,
                                  .order = m_settings.dictionaryOrder(),
                                  .excluded = m_settings.disabledDictionaries()};
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::search, Qt::QueuedConnection,
                              m_searchRequest, query);
}

void MainWindow::onSearchFinished(quint64 requestId, const core::SearchResults& results,
                                  const QStringList& favorites)
{
    if (requestId != m_searchRequest) {
        return; // the user has typed on since
    }
    int searched = 1;
    if (currentDictId().isEmpty()) {
        const QStringList disabled = m_settings.disabledDictionaries();
        searched =
            static_cast<int>(std::ranges::count_if(m_dictionaries, [&](const services::DictionaryInfo& info) {
                return !disabled.contains(info.dictId);
            }));
    }
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
                                  m_entryRequest, current.data(models::ResultsModel::HeadwordRole).toString(),
                                  dictId);
        return;
    }
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::loadEntry, Qt::QueuedConnection,
                              m_entryRequest, dictId, entryId);
}

void MainWindow::onResultAction(const QModelIndex& index)
{
    switch (index.data(models::ResultsModel::ActionRole).value<models::ResultsModel::Action>()) {
    case models::ResultsModel::Action::ClearHistory:
        clearHistory();
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
    const QString credit =
        info != nullptr
            ? tr("%1. %2, version %3.").arg(withoutFinalStop(info->attribution), info->name, info->version)
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
    showEntryColumn(true);
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
        showEntryColumn(false); // one column: Back returns to the list
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

bool MainWindow::handleMouseNavigation(QObject* watched, QEvent* event)
{
    // The mouse's side buttons go Back and Forward anywhere in this window, as in a
    // browser. Filtered for the whole application because the list and the entry
    // accept every press themselves; other windows (sheets) are left alone.
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease &&
        event->type() != QEvent::MouseButtonDblClick) {
        return false;
    }
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::BackButton && mouse->button() != Qt::ForwardButton) {
        return false;
    }
    const auto* widget = qobject_cast<QWidget*>(watched);
    if (widget == nullptr || widget->window() != this) {
        return false;
    }
    // Act once, on release (as browsers do); swallow the press so nothing else reacts.
    if (event->type() == QEvent::MouseButtonRelease) {
        if (mouse->button() == Qt::BackButton && m_back->isEnabled()) {
            goBack();
        } else if (mouse->button() == Qt::ForwardButton && m_forward->isEnabled()) {
            goForward();
        }
    }
    return true;
}

void MainWindow::updateNavigation()
{
    m_back->setEnabled(!m_backStack.isEmpty() || (m_narrow && m_entryColumn));
    m_forward->setEnabled(!m_forwardStack.isEmpty());
}

void MainWindow::copyEntry()
{
    if (m_stack->currentWidget() == m_entry && !m_current.headword.isEmpty()) {
        QGuiApplication::clipboard()->setText(m_entry->plainText());
    }
}

void MainWindow::applyListTextSize(int entryPixels)
{
    // One size for the whole window: the list reads one pixel smaller than the entry.
    QFont font = m_results->font();
    font.setPixelSize(entryPixels - 1);
    m_results->setFont(font);
    m_results->doItemsLayout(); // row heights follow the new size
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

void MainWindow::reopenLibrary()
{
    m_libraryOpen = false;
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::openLibrary, Qt::QueuedConnection, m_roots);
}

int MainWindow::activeDownloads() const
{
    int active = 0;
    for (const services::DownloadStatus& status : m_manager.downloads()) {
        if (status.state != services::DownloadStatus::Failed) {
            ++active;
        }
    }
    return active;
}

void MainWindow::onDownloadChanged()
{
    // While anything downloads, the Dictionaries button says so (mocks/dictionaries-available.html).
    const int active = activeDownloads();
    const Tokens& t = Tokens::current();
    m_dictionariesButton->setIcon(active > 0 ? icons::themed(u"download"_s, t.accent)
                                             : icons::themed(u"books"_s, t.text));
    QString tip = tr("Dictionaries (Ctrl+Shift+D)");
    if (active == 1) {
        tip = tr("Dictionaries: 1 download in progress (Ctrl+Shift+D)");
    } else if (active > 1) {
        tip = tr("Dictionaries: %1 downloads in progress (Ctrl+Shift+D)").arg(active);
    }
    m_dictionariesButton->setToolTip(tip);
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
    // Sheets never stack (DOCS/DESIGN.md): About closes before the one it opens.
    enum class Next
    {
        None,
        WhatsNew,
        BugReport,
    };
    Next next = Next::None;
    AboutDialog dialog(m_dictionaries, this);
    connect(&dialog, &AboutDialog::whatsNewRequested, &dialog, [&] {
        next = Next::WhatsNew;
        dialog.accept();
    });
    connect(&dialog, &AboutDialog::bugReportRequested, &dialog, [&] {
        next = Next::BugReport;
        dialog.accept();
    });
    dialog.exec();
    if (next == Next::WhatsNew) {
        showWhatsNew();
    } else if (next == Next::BugReport) {
        showBugReport();
    }
}

void MainWindow::showDictionaries(bool available)
{
    // Downloads outlive the sheet: they belong to the manager, not to it.
    DictionariesDialog dialog(m_manager, m_settings, m_dictionaries, m_roots.constLast(), this);
    if (available) {
        dialog.showAvailable();
    }
    m_dictionariesDialog = &dialog;
    dialog.exec();
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(m_settings, m_roots.constLast(), this);
    connect(&dialog, &SettingsDialog::clearHistoryRequested, this, &MainWindow::clearHistory);
    dialog.exec();
}

void MainWindow::showShortcuts()
{
    ShortcutsDialog dialog(defaultShortcuts(), this);
    dialog.exec();
}

void MainWindow::showWhatsNew()
{
    WhatsNewDialog dialog(QCoreApplication::applicationVersion(), this);
    dialog.exec();
}

void MainWindow::showWhatsNewIfUpdated()
{
    const QString version = QCoreApplication::applicationVersion();
    const bool show = WhatsNewDialog::shouldShowWhatsNew(m_settings, version);
    m_settings.setWhatsNewSeenVersion(version);
    if (show) {
        QTimer::singleShot(0, this, &MainWindow::showWhatsNew);
    }
}

void MainWindow::showBugReport()
{
    BugReportDialog dialog(diagnosticsText(m_dictionaries), this);
    dialog.exec();
}

void MainWindow::clearHistory()
{
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::clearHistory, Qt::QueuedConnection);
    Toast::show(this, tr("History cleared"), tr("Undo"), [this] {
        QMetaObject::invokeMethod(m_lookup, &services::LookupService::undoClearHistory, Qt::QueuedConnection);
    });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (handleMouseNavigation(watched, event)) {
        return true;
    }
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
