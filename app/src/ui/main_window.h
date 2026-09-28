#pragma once

#include "services/lookup_service.h"

#include <QMainWindow>
#include <QPointer>
#include <QThread>

class QAction;
class QBoxLayout;
class QActionGroup;
class QLabel;
class QListView;
class QMenu;
class QSplitter;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace omnidict::core {
class Settings;
}

namespace omnidict::services {
class DictionaryManager;
struct DownloadStatus;
} // namespace omnidict::services

namespace omnidict::models {
class ResultsModel;
}

namespace omnidict::ui {

class DictionariesDialog;
class EmptyState;
class FirstRunPanel;
class EntryView;
class SearchField;

/// The dictionary window (mocks/main.html): a header with the search field,
/// the dictionary filter and the main menu; the result list; the entry with
/// its bar (Back, Forward, Copy, star) or an empty state in its place. Every
/// lookup runs on the lookup thread; this class only sends requests and shows
/// answers.
class MainWindow : public QMainWindow
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MainWindow)

public:
    /// `roots`: the directories dictionaries are found in (core::Library::discover);
    /// `userDataPath`: the history and favourites database.
    /// `manager` downloads and installs into the last of `roots`.
    MainWindow(core::Settings& settings, services::DictionaryManager& manager, QStringList roots,
               const QString& userDataPath, QWidget* parent = nullptr);
    ~MainWindow() override; // stops the lookup thread

    /// Types a query as if the user had.
    void setQuery(const QString& text);
    /// Opens the About sheet (modal).
    void showAbout();
    /// The other sheets, as the main menu opens them (modal).
    /// `available` opens the Dictionaries sheet on its Available tab.
    void showDictionaries(bool available = false);
    void showSettings();
    void showShortcuts();
    void showWhatsNew();
    void showBugReport();
    /// After an update, shows What's new once; always remembers the running version.
    void showWhatsNewIfUpdated();

Q_SIGNALS:
    /// The dictionaries are open (or found to be missing).
    void libraryReady();
    /// An entry is on screen.
    void entryShown(const QString& dictId, const QString& headword);
    /// The result list shows the answer to a search, or the saved entries.
    void resultsShown();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// An entry by dictionary and headword: what Back and Forward walk.
    struct Visit
    {
        QString dictId;
        QString headword;
    };

    void setupUi();
    [[nodiscard]] QWidget* buildHeader();
    [[nodiscard]] QWidget* buildResults();
    [[nodiscard]] QWidget* buildEntryPane();
    void setupActions();
    void buildMainMenu();
    void rebuildFilterMenu();
    void refreshIcons();
    void connectLookup();
    void connectSettings();

    void onLibraryOpened(const QList<services::DictionaryInfo>& dictionaries, const QStringList& problems);
    void requestSearch();
    void onSearchFinished(quint64 requestId, const core::SearchResults& results,
                          const QStringList& favorites);
    void onCurrentResultChanged(const QModelIndex& current);
    void onResultAction(const QModelIndex& index);
    void onEntryLoaded(quint64 requestId, const QString& dictId, const core::Entry& entry,
                       const QString& html, bool favorite);
    void onSavedEntries(const QList<core::SavedEntry>& recent, const QList<core::SavedEntry>& favorites);
    void onFavoriteToggled(bool favorite);
    void onEntryNotFound(quint64 requestId, const QString& text);
    void recordShownEntry();
    void showFavoriteState(bool favorite);
    void followHeadword(const QString& headword);
    void openVisit(const Visit& visit);
    void goBack();
    void goForward();
    /// The mouse's Back and Forward buttons: true when `event` was one of them.
    bool handleMouseNavigation(QObject* watched, QEvent* event);
    void updateNavigation();
    void copyEntry();
    void changeTextSize(int step);
    void applyListTextSize(int entryPixels);
    void setFilter(const QString& dictId);
    void showWelcome();
    void reopenLibrary();
    void onDownloadChanged();
    /// Downloads waiting, running or installing (failed ones do not count).
    [[nodiscard]] int activeDownloads() const;
    void showNoMatch(const QString& text, bool hasSuggestions);
    void clearEntry();
    void clearHistory();
    /// Below kNarrowWidth the list and the entry share one column (DOCS/DESIGN.md).
    void applyLayoutMode();
    void showEntryColumn(bool entry);

    [[nodiscard]] QString currentDictId() const;
    [[nodiscard]] const services::DictionaryInfo* dictionary(const QString& dictId) const;

    core::Settings& m_settings;
    services::DictionaryManager& m_manager;
    QThread m_lookupThread;
    services::LookupService* m_lookup = nullptr; ///< lives on m_lookupThread, deleted when it finishes
    QStringList m_roots;
    QList<services::DictionaryInfo> m_dictionaries;
    bool m_libraryOpen = false;

    QBoxLayout* m_headerLayout = nullptr;
    SearchField* m_search = nullptr;
    QToolButton* m_filterButton = nullptr;
    QLabel* m_filterChevron = nullptr;
    QMenu* m_filterMenu = nullptr;
    QToolButton* m_menuButton = nullptr;
    QMenu* m_mainMenu = nullptr;
    QList<std::pair<QAction*, QString>> m_menuIcons; ///< main menu actions and their glyphs
    QStackedWidget* m_bodyStack = nullptr;           ///< the list and entry, or the first-run panel
    FirstRunPanel* m_firstRun = nullptr;
    QPointer<DictionariesDialog> m_dictionariesDialog; ///< while open: follows installs and removals
    QToolButton* m_dictionariesButton = nullptr;
    QSplitter* m_splitter = nullptr;
    QListView* m_results = nullptr;
    models::ResultsModel* m_model = nullptr;
    QToolButton* m_back = nullptr;
    QToolButton* m_forward = nullptr;
    QLabel* m_crumb = nullptr;
    QToolButton* m_copy = nullptr;
    QToolButton* m_star = nullptr;
    QWidget* m_entryPane = nullptr;
    QStackedWidget* m_stack = nullptr;
    EntryView* m_entry = nullptr;
    EmptyState* m_empty = nullptr;
    QTimer* m_debounce = nullptr;

    quint64 m_nextRequest = 1;
    quint64 m_searchRequest = 0;     ///< the search whose answer is wanted
    quint64 m_entryRequest = 0;      ///< the entry whose answer is wanted
    Visit m_current;                 ///< the entry on screen; empty when none
    QList<Visit> m_backStack;        ///< entries left by following links, most recent last
    QList<Visit> m_forwardStack;     ///< entries left by going back, most recent last
    bool m_autoSelecting = false;    ///< selecting the best match as a preview, not a user choice
    bool m_recordNext = false;       ///< the entry being loaded goes into the history
    bool m_forceDefinitions = false; ///< the next search looks inside definitions whatever the setting
    bool m_narrow = false;           ///< one column: the list or the entry
    bool m_entryColumn = false;      ///< in one column, the entry is the one shown
};

} // namespace omnidict::ui
