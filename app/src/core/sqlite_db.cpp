#include "core/sqlite_db.h"

#include "core/logging.h"

#include <sqlite3.h>

using namespace Qt::StringLiterals;

namespace omnidict::core {

namespace detail {

void SqliteCloser::operator()(sqlite3* db) const
{
    sqlite3_close(db);
}

void SqliteFinalizer::operator()(sqlite3_stmt* statement) const
{
    sqlite3_finalize(statement);
}

} // namespace detail

SqliteStatement::SqliteStatement(sqlite3_stmt* statement)
    : m_statement(statement)
{}

void SqliteStatement::bind(int index, const QString& value)
{
    const QByteArray& utf8 = m_boundText.emplace_back(value.toUtf8());
    // A null destructor is SQLITE_STATIC: the bytes stay ours, in m_boundText.
    const int rc =
        sqlite3_bind_text(m_statement.get(), index, utf8.constData(), static_cast<int>(utf8.size()), nullptr);
    if (rc != SQLITE_OK) {
        m_failed = true;
        qCWarning(lcSqlite) << "bind failed at parameter" << index << ":" << sqlite3_errstr(rc);
    }
}

void SqliteStatement::bind(int index, qint64 value)
{
    const int rc = sqlite3_bind_int64(m_statement.get(), index, value);
    if (rc != SQLITE_OK) {
        m_failed = true;
        qCWarning(lcSqlite) << "bind failed at parameter" << index << ":" << sqlite3_errstr(rc);
    }
}

bool SqliteStatement::next()
{
    if (m_failed) {
        return false;
    }
    const int rc = sqlite3_step(m_statement.get());
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc != SQLITE_DONE) {
        m_failed = true;
        qCWarning(lcSqlite) << "step failed:" << sqlite3_errmsg(sqlite3_db_handle(m_statement.get()));
    }
    return false;
}

QString SqliteStatement::text(int column) const
{
    const unsigned char* bytes = sqlite3_column_text(m_statement.get(), column);
    if (bytes == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(m_statement.get(), column);
    return QString::fromUtf8(reinterpret_cast<const char*>(bytes), size);
}

QByteArray SqliteStatement::blob(int column) const
{
    const void* bytes = sqlite3_column_blob(m_statement.get(), column);
    if (bytes == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(m_statement.get(), column);
    return {static_cast<const char*>(bytes), size};
}

qint64 SqliteStatement::integer(int column) const
{
    return sqlite3_column_int64(m_statement.get(), column);
}

bool SqliteStatement::isNull(int column) const
{
    return sqlite3_column_type(m_statement.get(), column) == SQLITE_NULL;
}

SqliteDb::SqliteDb(sqlite3* db)
    : m_db(db)
{}

Result<SqliteDb> SqliteDb::openReadOnly(const QString& path)
{
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(path.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY, nullptr);
    // SQLite hands back a handle even on failure; it still has to be closed.
    SqliteDb db(raw);
    if (rc != SQLITE_OK) {
        const QString reason =
            raw != nullptr ? QString::fromUtf8(sqlite3_errmsg(raw)) : QString::fromUtf8(sqlite3_errstr(rc));
        return Error{u"cannot open %1: %2"_s.arg(path, reason)};
    }
    return db;
}

Result<SqliteStatement> SqliteDb::prepare(const QString& sql) const
{
    const QByteArray utf8 = sql.toUtf8();
    sqlite3_stmt* raw = nullptr;
    const int rc =
        sqlite3_prepare_v2(m_db.get(), utf8.constData(), static_cast<int>(utf8.size()), &raw, nullptr);
    if (rc != SQLITE_OK) {
        return Error{QString::fromUtf8(sqlite3_errmsg(m_db.get()))};
    }
    return SqliteStatement(raw);
}

} // namespace omnidict::core
