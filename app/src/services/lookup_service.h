#pragma once

#include "core/entry.h"
#include "core/search_engine.h"
#include "core/user_data.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace omnidict::core {
class Library;
}

namespace omnidict::services {

/// What the UI needs to know about one available dictionary.
struct DictionaryInfo
{
    QString dictId;
    QString name;
    QString attribution;
    QString license;
    QString licenseUrl;
    QString version;
    qint64 entryCount = 0;
};

/// All dictionary access, on the thread the service is moved to (ADR-004):
/// SQLite connections belong to one thread, and a lookup must never block the
/// window. Requests arrive as queued slot calls and answers leave as signals
/// that carry the request id, so the caller can drop answers it no longer wants.
class LookupService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LookupService)

public:
    explicit LookupService(QObject* parent = nullptr);
    ~LookupService() override; // Library is incomplete in this header

public Q_SLOTS:
    /// Rebuilds the library from these directories (see core::Library::discover).
    void openLibrary(const QStringList& roots);
    /// Opens the history and favourites database; without it nothing is remembered.
    void openUserData(const QString& path);
    void search(quint64 requestId, const omnidict::core::SearchQuery& query);
    /// The entry and its HTML (DOCS/entry-html.md).
    void loadEntry(quint64 requestId, const QString& dictId, qint64 entryId);
    /// The best entry for a headword, as a `lex:` link asks for: an exact match
    /// in the preferred dictionary, else in any dictionary.
    void resolveHeadword(quint64 requestId, const QString& headword, const QString& preferredDictId);

    /// Puts an entry the user opened at the top of the history.
    void recordView(const QString& dictId, const QString& headword);
    void setFavorite(const QString& dictId, const QString& headword, bool favorite);
    /// Answers with savedEntries().
    void requestSaved();

Q_SIGNALS:
    void libraryOpened(const QList<omnidict::services::DictionaryInfo>& dictionaries,
                       const QStringList& problems);
    void searchFinished(quint64 requestId, const omnidict::core::SearchResults& results);
    void entryLoaded(quint64 requestId, const QString& dictId, const omnidict::core::Entry& entry,
                     const QString& html, bool favorite);
    void savedEntries(const QList<omnidict::core::SavedEntry>& recent,
                      const QList<omnidict::core::SavedEntry>& favorites);
    /// A `resolveHeadword` or `loadEntry` that found nothing.
    void entryNotFound(quint64 requestId, const QString& text);

private:
    [[nodiscard]] core::SavedEntry saved(const QString& dictId, const QString& headword) const;

    std::unique_ptr<core::Library> m_library;
    std::optional<core::UserData> m_userData;
};

/// Registers the types the service's queued signals carry. Call once before
/// connecting across threads.
void registerLookupTypes();

} // namespace omnidict::services

Q_DECLARE_METATYPE(omnidict::services::DictionaryInfo)
Q_DECLARE_METATYPE(omnidict::core::SearchQuery)
Q_DECLARE_METATYPE(omnidict::core::Entry)
