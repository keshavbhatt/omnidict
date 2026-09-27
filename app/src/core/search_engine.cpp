#include "core/search_engine.h"

#include <QSet>

namespace omnidict::core {

namespace {

bool isPattern(const QString& text)
{
    return text.contains(u'*') || text.contains(u'?');
}

/// Exact matches, then prefix matches not already listed, up to `limit`.
QList<EntryPreview> headwordRows(const Bundle& bundle, const QString& text, int limit)
{
    QList<EntryPreview> rows = bundle.lookupExact(text);
    if (rows.size() >= limit) {
        rows.resize(limit);
        return rows;
    }
    QSet<qint64> listed;
    for (const EntryPreview& row : std::as_const(rows)) {
        listed.insert(row.id);
    }
    const QList<EntryPreview> prefixed = bundle.searchPrefix(text, limit);
    for (const EntryPreview& row : prefixed) {
        if (rows.size() >= limit) {
            break;
        }
        if (!listed.contains(row.id)) {
            rows.append(row);
            listed.insert(row.id);
        }
    }
    return rows;
}

} // namespace

SearchResults SearchEngine::search(const SearchQuery& query) const
{
    SearchResults results;
    results.text = query.text;
    const QString text = query.text.trimmed();
    if (text.isEmpty()) {
        return results;
    }
    const bool pattern = isPattern(text);
    const bool fullText = !pattern && text.size() >= kMinFullTextLength && query.fullTextPerDictionary > 0;

    for (const Bundle& bundle : m_library.bundles()) {
        if (!query.dictId.isEmpty() && bundle.meta().dictId != query.dictId) {
            continue;
        }
        const QList<EntryPreview> rows = pattern ? bundle.searchPattern(text, query.perDictionary)
                                                 : headwordRows(bundle, text, query.perDictionary);
        if (!rows.isEmpty()) {
            results.headwords.append(
                {.dictId = bundle.meta().dictId, .dictName = bundle.meta().name, .rows = rows});
        }
        if (!fullText) {
            continue;
        }
        QSet<qint64> listed;
        for (const EntryPreview& row : rows) {
            listed.insert(row.id);
        }
        QList<EntryPreview> others;
        // Ask for more than shown: some hits are already among the headwords.
        const QList<EntryPreview> hits =
            bundle.searchFullText(text, query.fullTextPerDictionary + static_cast<int>(listed.size()));
        for (const EntryPreview& hit : hits) {
            if (!listed.contains(hit.id) && others.size() < query.fullTextPerDictionary) {
                others.append(hit);
            }
        }
        if (!others.isEmpty()) {
            results.definitions.append(
                {.dictId = bundle.meta().dictId, .dictName = bundle.meta().name, .rows = others});
        }
    }
    return results;
}

} // namespace omnidict::core
