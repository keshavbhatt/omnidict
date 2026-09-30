#pragma once

#include "core/search_engine.h"
#include "services/lookup_service.h"

#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QTimer;

namespace omnidict::core {
class Settings;
struct Entry;
} // namespace omnidict::core

namespace omnidict::ui {

class EntryView;
class SearchField;

/// The Quick Lookup popup (DOCS/quick-lookup.md, mocks/quick-lookup.html): a small
/// window over the other apps with the best match for one word. Opened by the global
/// shortcut or `omnidict --popup`; Escape or clicking elsewhere closes it; "Open in
/// Omnidict" hands the word to the main window. It asks the same LookupService as the
/// main window, with request ids of its own.
class QuickLookup : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(QuickLookup)

public:
    QuickLookup(services::LookupService* lookup, core::Settings& settings, QWidget* parent = nullptr);
    ~QuickLookup() override = default;

    /// The dictionaries the library opened, for names and credits.
    void setDictionaries(const QList<services::DictionaryInfo>& dictionaries);

    /// Shows the popup with `word`; with no word, the selected text (or the clipboard,
    /// where the system gives no selection) once the popup is active. `activationToken`
    /// (Wayland) lets it come to the front when a shortcut or a launch opened it.
    void present(const QString& word, const QByteArray& activationToken = {});

    /// Looks `word` up in the popup.
    void lookUp(const QString& word);

    /// A word taken from a selection or the clipboard, cleaned for a lookup: one line,
    /// trimmed, at most a few words; empty when it does not look like something to look up.
    [[nodiscard]] static QString wordFrom(const QString& text);

Q_SIGNALS:
    /// "Open in Omnidict" or Enter: the main window should show `word` with all results.
    void openInMain(const QString& word);
    /// An entry is on screen (for tests and debug hooks).
    void entryShown(const QString& dictId, const QString& headword);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* buildHeader(QWidget* frame);
    QWidget* buildFooter(QWidget* frame);
    void pickInitialWord();
    void search();
    void onSearchFinished(quint64 requestId, const core::SearchResults& results,
                          const QStringList& favorites);
    void onEntryLoaded(quint64 requestId, const QString& dictId, const core::Entry& entry,
                       const QString& html, bool favorite);
    void showNothingFound(const QString& text, const QList<core::SuggestedWord>& suggestions);
    void showOtherDictionaries(const QString& headword, const core::SearchResults& results);
    void openLink(const QString& href);
    [[nodiscard]] const services::DictionaryInfo* dictionary(const QString& dictId) const;

    QPointer<services::LookupService> m_lookup;
    core::Settings& m_settings;
    QList<services::DictionaryInfo> m_dictionaries;
    SearchField* m_search = nullptr;
    QLabel* m_dictionaryName = nullptr;
    EntryView* m_entry = nullptr;
    QLabel* m_message = nullptr; ///< nothing found, with suggestions
    QLabel* m_also = nullptr;    ///< "Also in" the other dictionaries with the word
    QTimer* m_typingPause = nullptr;
    quint64 m_searchRequest = 0;
    quint64 m_entryRequest = 0;
    bool m_pickWordOnActivation = false;
    bool m_wasActive = false;
};

} // namespace omnidict::ui
