#include "core/bundle.h"

#include "core/logging.h"
#include "core/normalize.h"

#include <QHash>
#include <QStringList>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::core {

namespace {

constexpr int kNoLimit = -1;

const QString kPreviewColumns = u"e.id, e.headword, e.preview, e.frequency"_s;
// ?1 is the query as typed, ?2 its lookup form. An entry spelled exactly as
// typed ranks first ("día" before "dia"), then any other exact match, then the
// dictionary's own order.
const QString kPreviewOrder =
    u" ORDER BY (e.headword = ?1) DESC, (e.headword_norm = ?2) DESC, e.sort_key, e.id"_s;

/// The query as typed, in the form headwords are stored (NFC, trimmed).
QString asTyped(const QString& query)
{
    return query.trimmed().normalized(QString::NormalizationForm_C);
}

/// Sorts after every string that starts with `prefix`: SQLite compares text
/// bytewise, and no UTF-8 sequence is greater than the one for U+10FFFF.
QString prefixUpperBound(const QString& prefix)
{
    const char32_t last = 0x10FFFF;
    return prefix + QString::fromUcs4(&last, 1);
}

/// The query as an FTS5 expression: every word must match, none is syntax.
QString fullTextExpression(const QString& query)
{
    QStringList words;
    const QStringList typed = query.simplified().split(u' ', Qt::SkipEmptyParts);
    for (const QString& word : typed) {
        words << u'"' + QString(word).replace(u'"', u"\"\""_s) + u'"';
    }
    return words.join(u' ');
}

Result<QHash<QString, QString>> readMeta(const SqliteDb& db)
{
    auto statement = db.prepare(u"SELECT key, value FROM meta"_s);
    if (!statement) {
        return Error{statement.error()};
    }
    QHash<QString, QString> values;
    while (statement.value().next()) {
        values.insert(statement.value().text(0), statement.value().text(1));
    }
    if (statement.value().failed()) {
        return Error{u"cannot read the metadata"_s};
    }
    return values;
}

Result<BundleMeta> parseMeta(const QHash<QString, QString>& values)
{
    static const QStringList kRequired = {u"dict_id"_s,     u"name"_s,        u"source_lang"_s,
                                          u"target_lang"_s, u"version"_s,     u"schema_version"_s,
                                          u"publisher"_s,   u"license"_s,     u"license_url"_s,
                                          u"attribution"_s, u"entry_count"_s, u"built_at"_s};
    for (const QString& key : kRequired) {
        if (!values.contains(key)) {
            return Error{u"metadata key %1 is missing"_s.arg(key)};
        }
    }
    bool ok = false;
    const int schemaVersion = values.value(u"schema_version"_s).toInt(&ok);
    if (!ok || schemaVersion < 1) {
        return Error{u"schema version %1 is not valid"_s.arg(values.value(u"schema_version"_s))};
    }
    if (schemaVersion > Bundle::kSupportedSchemaVersion) {
        return Error{u"schema version %1 is newer than this build reads (%2)"_s.arg(schemaVersion)
                         .arg(Bundle::kSupportedSchemaVersion)};
    }

    BundleMeta meta;
    meta.dictId = values.value(u"dict_id"_s);
    meta.name = values.value(u"name"_s);
    meta.sourceLang = values.value(u"source_lang"_s);
    meta.targetLang = values.value(u"target_lang"_s);
    meta.version = values.value(u"version"_s);
    meta.schemaVersion = schemaVersion;
    meta.publisher = values.value(u"publisher"_s);
    meta.license = values.value(u"license"_s);
    meta.licenseUrl = values.value(u"license_url"_s);
    meta.attribution = values.value(u"attribution"_s);
    meta.entryCount = values.value(u"entry_count"_s).toLongLong();
    meta.builtAt = values.value(u"built_at"_s);
    return meta;
}

/// Runs `sql` with the entry id as its only parameter and hands each row to `row`.
template <typename Row>
void forEachRow(const SqliteDb& db, const QString& sql, qint64 entryId, Row row)
{
    auto statement = db.prepare(sql);
    if (!statement) {
        qCWarning(lcBundle) << "query failed:" << statement.error();
        return;
    }
    statement.value().bind(1, entryId);
    while (statement.value().next()) {
        row(statement.value());
    }
}

QList<Example> readExamples(const SqliteDb& db, qint64 senseId)
{
    QList<Example> examples;
    forEachRow(db, u"SELECT text, translation FROM examples WHERE sense_id = ?1 ORDER BY ordinal, id"_s,
               senseId, [&](const SqliteStatement& row) {
                   examples.append({.text = row.text(0), .translation = row.text(1)});
               });
    return examples;
}

QList<Sense> readSenses(const SqliteDb& db, qint64 entryId)
{
    QList<Sense> senses;
    QList<qint64> senseIds;
    forEachRow(db,
               u"SELECT id, ordinal, pos, pattern, label, definition FROM senses"
               " WHERE entry_id = ?1 ORDER BY ordinal, id"_s,
               entryId, [&](const SqliteStatement& row) {
                   senseIds.append(row.integer(0));
                   senses.append({.ordinal = static_cast<int>(row.integer(1)),
                                  .pos = row.text(2),
                                  .pattern = row.text(3),
                                  .label = row.text(4),
                                  .definition = row.text(5),
                                  .examples = {}});
               });
    for (qsizetype i = 0; i < senses.size(); ++i) {
        senses[i].examples = readExamples(db, senseIds.at(i));
    }
    return senses;
}

} // namespace

Bundle::Bundle(SqliteDb db, BundleMeta meta, QString path)
    : m_db(std::move(db))
    , m_meta(std::move(meta))
    , m_path(std::move(path))
{}

Result<Bundle> Bundle::open(const QString& path)
{
    auto db = SqliteDb::openReadOnly(path);
    if (!db) {
        return Error{db.error()};
    }
    const auto values = readMeta(db.value());
    if (!values) {
        return Error{u"%1 is not a dictionary: %2"_s.arg(path, values.error())};
    }
    auto meta = parseMeta(values.value());
    if (!meta) {
        return Error{u"%1 cannot be used: %2"_s.arg(path, meta.error())};
    }
    qCDebug(lcBundle) << "opened" << meta.value().dictId << meta.value().version << "from" << path;
    return Bundle(db.take(), meta.take(), path);
}

QList<EntryPreview> Bundle::previews(const QString& sql, const QList<QString>& texts, int limit) const
{
    auto statement = m_db.prepare(sql);
    if (!statement) {
        qCWarning(lcBundle) << m_meta.dictId << "query failed:" << statement.error();
        return {};
    }
    int index = 1;
    for (const QString& text : texts) {
        statement.value().bind(index++, text);
    }
    statement.value().bind(index, static_cast<qint64>(limit));

    QList<EntryPreview> rows;
    while (statement.value().next()) {
        const SqliteStatement& row = statement.value();
        rows.append({.id = row.integer(0),
                     .headword = row.text(1),
                     .preview = row.text(2),
                     .frequency = static_cast<int>(row.integer(3))});
    }
    return rows;
}

QList<Suggestion> Bundle::suggest(const QString& query, int limit) const
{
    constexpr int kFirstWithSuggestions = 2;
    if (m_meta.schemaVersion < kFirstWithSuggestions) {
        return {};
    }
    return suggestSpellings(m_db, normalizeHeadword(query), limit);
}

QList<EntryPreview> Bundle::lookupExact(const QString& query) const
{
    const QString key = normalizeHeadword(query);
    if (key.isEmpty()) {
        return {};
    }
    const QString sql = u"SELECT "_s + kPreviewColumns +
                        u" FROM entries e WHERE e.id IN ("
                        "SELECT id FROM entries WHERE headword_norm = ?2"
                        " UNION SELECT entry_id FROM forms WHERE form_norm = ?2)"_s +
                        kPreviewOrder + u" LIMIT ?3"_s;
    return previews(sql, {asTyped(query), key}, kNoLimit);
}

QList<EntryPreview> Bundle::searchPrefix(const QString& query, int limit) const
{
    const QString key = normalizeHeadword(query);
    if (key.isEmpty() || limit <= 0) {
        return {};
    }
    const QString sql = u"SELECT "_s + kPreviewColumns +
                        u" FROM entries e WHERE e.id IN ("
                        "SELECT id FROM entries WHERE headword_norm >= ?2 AND headword_norm < ?3"
                        " UNION SELECT entry_id FROM forms WHERE form_norm >= ?2 AND form_norm < ?3)"_s +
                        u" ORDER BY (e.headword = ?1) DESC, (e.headword_norm = ?2) DESC,"
                        " (e.headword_norm >= ?2 AND e.headword_norm < ?3) DESC, e.sort_key, e.id"
                        " LIMIT ?4"_s;
    return previews(sql, {asTyped(query), key, prefixUpperBound(key)}, limit);
}

QList<EntryPreview> Bundle::searchPattern(const QString& pattern, int limit) const
{
    const QString key = normalizeHeadword(pattern);
    if (key.isEmpty() || limit <= 0) {
        return {};
    }
    // `?` and `*` become LIKE's `_` and `%`; the characters LIKE would read as
    // syntax are escaped. The literal text before the first wildcard bounds the
    // range, so the index narrows the scan.
    QString like;
    for (const QChar c : key) {
        if (c == u'*') {
            like += u'%';
        } else if (c == u'?') {
            like += u'_';
        } else {
            if (c == u'%' || c == u'_' || c == u'\\') {
                like += u'\\';
            }
            like += c;
        }
    }
    const qsizetype firstWildcard = std::min(key.indexOf(u'*') < 0 ? key.size() : key.indexOf(u'*'),
                                             key.indexOf(u'?') < 0 ? key.size() : key.indexOf(u'?'));
    const QString literal = key.left(firstWildcard);
    const QString sql = u"SELECT "_s + kPreviewColumns +
                        u" FROM entries e WHERE e.headword_norm LIKE ?1 ESCAPE '\\'"
                        " AND e.headword_norm >= ?2 AND e.headword_norm < ?3"
                        " ORDER BY e.sort_key, e.id LIMIT ?4"_s;
    return previews(sql, {like, literal, prefixUpperBound(literal)}, limit);
}

QList<EntryPreview> Bundle::searchFullText(const QString& query, int limit) const
{
    const QString expression = fullTextExpression(query);
    if (expression.isEmpty() || limit <= 0) {
        return {};
    }
    // One fts row per sense; an entry ranks by its best sense.
    const QString sql = u"SELECT "_s + kPreviewColumns +
                        u" FROM (SELECT rowid AS sense_id, rank AS score FROM fts WHERE fts MATCH ?1) hit"
                        " JOIN fts_map m ON m.rowid = hit.sense_id"
                        " JOIN entries e ON e.id = m.entry_id"
                        " GROUP BY e.id ORDER BY MIN(hit.score), e.sort_key, e.id LIMIT ?2"_s;
    return previews(sql, {expression}, limit);
}

std::optional<Entry> Bundle::entry(qint64 id) const
{
    Entry entry;
    forEachRow(m_db, u"SELECT id, headword, lang, frequency FROM entries WHERE id = ?1"_s, id,
               [&](const SqliteStatement& row) {
                   entry.id = row.integer(0);
                   entry.headword = row.text(1);
                   entry.lang = row.text(2);
                   entry.frequency = static_cast<int>(row.integer(3));
               });
    if (entry.id == 0) {
        return std::nullopt;
    }
    forEachRow(m_db, u"SELECT ipa, region FROM pronunciations WHERE entry_id = ?1 ORDER BY id"_s, id,
               [&](const SqliteStatement& row) {
                   entry.pronunciations.append({.ipa = row.text(0), .region = row.text(1)});
               });
    entry.senses = readSenses(m_db, id);
    forEachRow(
        m_db, u"SELECT form, tag FROM forms WHERE entry_id = ?1 ORDER BY rowid"_s, id,
        [&](const SqliteStatement& row) { entry.forms.append({.form = row.text(0), .tag = row.text(1)}); });
    forEachRow(m_db,
               u"SELECT r.type, r.target, s.ordinal FROM relations r"
               " LEFT JOIN senses s ON s.id = r.sense_id WHERE r.entry_id = ?1 ORDER BY r.rowid"_s,
               id, [&](const SqliteStatement& row) {
                   entry.relations.append({.type = row.text(0),
                                           .target = row.text(1),
                                           .senseOrdinal = static_cast<int>(row.integer(2))});
               });
    return entry;
}

} // namespace omnidict::core
