#include "core/user_data.h"

#include "core/logging.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::core {

namespace {

// Version 1 of the user database. A later change adds a migration keyed on
// PRAGMA user_version rather than editing these statements.
const QString kSchema = uR"(
    PRAGMA journal_mode = WAL;
    CREATE TABLE IF NOT EXISTS history (
        dict_id   TEXT NOT NULL,
        headword  TEXT NOT NULL,
        preview   TEXT NOT NULL,
        viewed_at TEXT NOT NULL,
        seq       INTEGER NOT NULL,
        PRIMARY KEY (dict_id, headword)
    );
    CREATE INDEX IF NOT EXISTS idx_history_seq ON history(seq);
    CREATE TABLE IF NOT EXISTS favorites (
        dict_id  TEXT NOT NULL,
        headword TEXT NOT NULL,
        preview  TEXT NOT NULL,
        added_at TEXT NOT NULL,
        seq      INTEGER NOT NULL,
        PRIMARY KEY (dict_id, headword)
    );
    PRAGMA user_version = 1;
)"_s;

// Order comes from `seq`, one more than the largest so far: timestamps tie when
// views follow each other within a millisecond. The time is kept for display.
QString now()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

} // namespace

UserData::UserData(SqliteDb db)
    : m_db(std::move(db))
{}

Result<UserData> UserData::open(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    auto db = SqliteDb::openReadWrite(path);
    if (!db) {
        return Error{db.error()};
    }
    const auto created = db.value().exec(kSchema);
    if (!created) {
        return Error{u"cannot set up %1: %2"_s.arg(path, created.error())};
    }
    return UserData(db.take());
}

void UserData::run(const QString& sql, const QList<QString>& texts) const
{
    auto statement = m_db.prepare(sql);
    if (!statement) {
        qCWarning(lcCore) << "user data statement failed:" << statement.error();
        return;
    }
    int index = 1;
    for (const QString& text : texts) {
        statement.value().bind(index++, text);
    }
    (void)statement.value().next();
    if (statement.value().failed()) {
        qCWarning(lcCore) << "user data write failed:" << sql;
    }
}

QList<SavedEntry> UserData::savedEntries(const QString& sql, int limit) const
{
    QList<SavedEntry> entries;
    auto statement = m_db.prepare(sql);
    if (!statement) {
        qCWarning(lcCore) << "user data query failed:" << statement.error();
        return entries;
    }
    statement.value().bind(1, static_cast<qint64>(limit));
    while (statement.value().next()) {
        const SqliteStatement& row = statement.value();
        entries.append({.dictId = row.text(0), .headword = row.text(1), .preview = row.text(2)});
    }
    return entries;
}

void UserData::recordView(const SavedEntry& entry)
{
    run(u"INSERT INTO history (dict_id, headword, preview, viewed_at, seq)"
        " VALUES (?1, ?2, ?3, ?4, (SELECT IFNULL(MAX(seq), 0) + 1 FROM history))"
        " ON CONFLICT (dict_id, headword) DO UPDATE SET preview = excluded.preview,"
        " viewed_at = excluded.viewed_at, seq = excluded.seq"_s,
        {entry.dictId, entry.headword, entry.preview, now()});
    run(u"DELETE FROM history WHERE rowid NOT IN"
        " (SELECT rowid FROM history ORDER BY seq DESC LIMIT %1)"_s.arg(kHistoryLimit),
        {});
}

QList<SavedEntry> UserData::history(int limit) const
{
    return savedEntries(u"SELECT dict_id, headword, preview FROM history ORDER BY seq DESC LIMIT ?1"_s,
                        limit);
}

void UserData::clearHistory()
{
    run(u"DELETE FROM history"_s, {});
}

bool UserData::isFavorite(const QString& dictId, const QString& headword) const
{
    auto statement = m_db.prepare(u"SELECT 1 FROM favorites WHERE dict_id = ?1 AND headword = ?2"_s);
    if (!statement) {
        return false;
    }
    statement.value().bind(1, dictId);
    statement.value().bind(2, headword);
    return statement.value().next();
}

void UserData::setFavorite(const SavedEntry& entry, bool favorite)
{
    if (favorite) {
        run(u"INSERT OR REPLACE INTO favorites (dict_id, headword, preview, added_at, seq)"
            " VALUES (?1, ?2, ?3, ?4, (SELECT IFNULL(MAX(seq), 0) + 1 FROM favorites))"_s,
            {entry.dictId, entry.headword, entry.preview, now()});
    } else {
        run(u"DELETE FROM favorites WHERE dict_id = ?1 AND headword = ?2"_s, {entry.dictId, entry.headword});
    }
}

QList<SavedEntry> UserData::favorites() const
{
    return savedEntries(u"SELECT dict_id, headword, preview FROM favorites ORDER BY seq DESC LIMIT ?1"_s,
                        kHistoryLimit);
}

} // namespace omnidict::core
