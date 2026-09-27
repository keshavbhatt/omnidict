#pragma once

#include "core/result.h"
#include "core/sqlite_db.h"

#include <QList>
#include <QMetaType>
#include <QString>

namespace omnidict::core {

/// An entry the user looked at or starred. Entries are remembered by
/// dictionary and headword: entry ids change when a dictionary is updated.
struct SavedEntry
{
    QString dictId;
    QString headword;
    QString preview;

    [[nodiscard]] bool operator==(const SavedEntry&) const = default;
};

/// The user's history and favourites, in their own database, never inside a
/// dictionary bundle (PLAN D6). Move-only; belongs to one thread.
class UserData
{
public:
    /// How many history items are kept; older ones are dropped.
    static constexpr int kHistoryLimit = 500;

    /// Opens or creates the database at `path`.
    [[nodiscard]] static Result<UserData> open(const QString& path);

    /// Puts the entry at the top of the history.
    void recordView(const SavedEntry& entry);
    /// Most recent first.
    [[nodiscard]] QList<SavedEntry> history(int limit) const;
    void clearHistory();

    [[nodiscard]] bool isFavorite(const QString& dictId, const QString& headword) const;
    void setFavorite(const SavedEntry& entry, bool favorite);
    /// Most recently starred first.
    [[nodiscard]] QList<SavedEntry> favorites() const;

private:
    explicit UserData(SqliteDb db);

    [[nodiscard]] QList<SavedEntry> savedEntries(const QString& sql, int limit) const;
    void run(const QString& sql, const QList<QString>& texts) const;

    SqliteDb m_db;
};

} // namespace omnidict::core

Q_DECLARE_METATYPE(omnidict::core::SavedEntry)
