#pragma once

#include "core/result.h"

#include <QByteArray>
#include <QString>

#include <memory>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace omnidict::core {

namespace detail {
struct SqliteCloser
{
    void operator()(sqlite3* db) const;
};
struct SqliteFinalizer
{
    void operator()(sqlite3_stmt* statement) const;
};
} // namespace detail

/// One prepared statement. Bind, then step through rows with next().
/// Move-only; finalized on destruction.
class SqliteStatement
{
public:
    /// Takes ownership of a statement prepared by SqliteDb::prepare().
    explicit SqliteStatement(sqlite3_stmt* statement);

    /// Parameter indexes are 1-based, as in SQL (`?1`).
    void bind(int index, const QString& value);
    void bind(int index, qint64 value);

    /// Advances to the next row. False when the rows are exhausted or the step
    /// failed; failed() tells the two apart.
    [[nodiscard]] bool next();
    [[nodiscard]] bool failed() const { return m_failed; }

    /// Column indexes are 0-based. NULL reads as an empty string or 0.
    [[nodiscard]] QString text(int column) const;
    [[nodiscard]] QByteArray blob(int column) const;
    [[nodiscard]] qint64 integer(int column) const;
    [[nodiscard]] bool isNull(int column) const;

private:
    std::unique_ptr<sqlite3_stmt, detail::SqliteFinalizer> m_statement;
    // SQLite reads bound text when the statement runs, not when it is bound,
    // so the UTF-8 bytes have to outlive the bind call.
    std::vector<QByteArray> m_boundText;
    bool m_failed = false;
};

/// A SQLite connection (ADR-003): read-only for dictionary bundles, writable
/// for the user's own data. Move-only; closed on destruction. Like the
/// connection it wraps, an instance belongs to one thread.
class SqliteDb
{
public:
    [[nodiscard]] static Result<SqliteDb> openReadOnly(const QString& path);
    /// Creates the file when it does not exist.
    [[nodiscard]] static Result<SqliteDb> openReadWrite(const QString& path);

    /// Runs one or more statements that return no rows.
    [[nodiscard]] Result<bool> exec(const QString& sql) const;

    [[nodiscard]] Result<SqliteStatement> prepare(const QString& sql) const;

private:
    explicit SqliteDb(sqlite3* db);
    [[nodiscard]] static Result<SqliteDb> open(const QString& path, int flags);

    std::unique_ptr<sqlite3, detail::SqliteCloser> m_db;
};

} // namespace omnidict::core
