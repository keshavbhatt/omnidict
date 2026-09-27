#pragma once

#include "core/entry.h"
#include "core/result.h"
#include "core/sqlite_db.h"

#include <QList>
#include <QString>

#include <optional>

namespace omnidict::core {

/// One installed dictionary: a read-only `dict.sqlite` in the canonical
/// schema (DOCS/schema.md). Queries are normalized here, so callers pass what
/// the user typed.
///
/// Move-only. An instance belongs to the thread that opened it.
class Bundle
{
public:
    /// The highest `meta.schema_version` this build reads.
    static constexpr int kSupportedSchemaVersion = 1;

    /// Opens the bundle and checks that it is one this build can read:
    /// required metadata present, schema version not newer than supported.
    [[nodiscard]] static Result<Bundle> open(const QString& path);

    [[nodiscard]] const BundleMeta& meta() const { return m_meta; }

    /// Entries whose headword, or one of whose inflected or variant forms,
    /// equals the query. Headword matches come first.
    [[nodiscard]] QList<EntryPreview> lookupExact(const QString& query) const;

    /// Entries whose headword or form starts with the query, an exact
    /// headword match first, then in the dictionary's own sort order.
    [[nodiscard]] QList<EntryPreview> searchPrefix(const QString& query, int limit) const;

    /// Entries whose definitions or examples contain every word of the
    /// query, best match first.
    [[nodiscard]] QList<EntryPreview> searchFullText(const QString& query, int limit) const;

    /// The complete entry, or nothing when the id is unknown.
    [[nodiscard]] std::optional<Entry> entry(qint64 id) const;

private:
    Bundle(SqliteDb db, BundleMeta meta);

    [[nodiscard]] QList<EntryPreview> previews(const QString& sql, const QList<QString>& texts,
                                               int limit) const;

    SqliteDb m_db;
    BundleMeta m_meta;
};

} // namespace omnidict::core
