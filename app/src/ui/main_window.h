#pragma once

#include "services/lookup_service.h"

#include <QMainWindow>
#include <QThread>

class QComboBox;
class QLineEdit;
class QListView;
class QTimer;

namespace omnidict::models {
class ResultsModel;
}

namespace omnidict::ui {

class EntryView;

/// The dictionary window: a search field and dictionary filter over a result
/// list, and the selected entry beside it. Every lookup runs on the lookup
/// thread; this class only sends requests and shows answers.
class MainWindow : public QMainWindow
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MainWindow)

public:
    /// `roots`: the directories dictionaries are found in (core::Library::discover).
    explicit MainWindow(QStringList roots, QWidget* parent = nullptr);
    ~MainWindow() override; // stops the lookup thread

    /// Types a query as if the user had.
    void setQuery(const QString& text);

Q_SIGNALS:
    /// The dictionaries are open (or found to be missing).
    void libraryReady();
    /// An entry is on screen.
    void entryShown(const QString& dictId, const QString& headword);

private:
    void setupUi();
    void connectLookup();

    void onLibraryOpened(const QList<services::DictionaryInfo>& dictionaries, const QStringList& problems);
    void requestSearch();
    void onSearchFinished(quint64 requestId, const core::SearchResults& results);
    void onCurrentResultChanged(const QModelIndex& current);
    void onEntryLoaded(quint64 requestId, const QString& dictId, const core::Entry& entry,
                       const QString& html);
    void onEntryNotFound(quint64 requestId, const QString& text);
    void followHeadword(const QString& headword);
    [[nodiscard]] QString currentDictId() const;

    bool eventFilter(QObject* watched, QEvent* event) override;

    QThread m_lookupThread;
    services::LookupService* m_lookup = nullptr; ///< lives on m_lookupThread, deleted when it finishes
    QStringList m_roots;
    QList<services::DictionaryInfo> m_dictionaries;

    QLineEdit* m_search = nullptr;
    QComboBox* m_filter = nullptr;
    QListView* m_results = nullptr;
    models::ResultsModel* m_model = nullptr;
    EntryView* m_entry = nullptr;
    QTimer* m_debounce = nullptr;

    quint64 m_nextRequest = 1;
    quint64 m_searchRequest = 0; ///< the search whose answer is wanted
    quint64 m_entryRequest = 0;  ///< the entry whose answer is wanted
    QString m_entryDictId;       ///< dictionary of the entry on screen
};

} // namespace omnidict::ui
