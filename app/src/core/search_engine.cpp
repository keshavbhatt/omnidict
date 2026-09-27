#include "core/search_engine.h"

#include "core/normalize.h"

#include <QSet>

#include <algorithm>

namespace omnidict::core {

namespace {

/// How well a dictionary's results answer the query: 0 when its first entry is
/// spelled exactly as typed, 1 when it is the same word once normalized, 2 for
/// prefix matches only. The best dictionaries come first, so the best entry is
/// the first one in the list.
int matchRank(const ResultGroup& group, const QString& typed, const QString& key)
{
    const EntryPreview& top = group.rows.first();
    if (top.headword == typed) {
        return 0;
    }
    return normalizeHeadword(top.headword) == key ? 1 : 2;
}

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
    if (!pattern && results.isEmpty()) {
        results.suggestions = suggest(query, text);
    }
    if (!pattern) {
        const QString typed = text.normalized(QString::NormalizationForm_C);
        const QString key = normalizeHeadword(text);
        std::ranges::stable_sort(results.headwords, [&](const ResultGroup& a, const ResultGroup& b) {
            return matchRank(a, typed, key) < matchRank(b, typed, key);
        });
    }
    return results;
}

QList<SuggestedWord> SearchEngine::suggest(const SearchQuery& query, const QString& text) const
{
    if (query.suggestions <= 0) {
        return {};
    }
    struct Candidate
    {
        int distance = 0;
        QString word{};
        const Bundle* bundle = nullptr;
    };
    QList<Candidate> found;
    for (const Bundle& bundle : m_library.bundles()) {
        if (!query.dictId.isEmpty() && bundle.meta().dictId != query.dictId) {
            continue;
        }
        for (const Suggestion& suggestion : bundle.suggest(text, query.suggestions)) {
            found.append({.distance = suggestion.distance, .word = suggestion.word, .bundle = &bundle});
        }
    }
    // Closest first; among equals the library order stands.
    std::ranges::stable_sort(found, {}, &Candidate::distance);

    QList<SuggestedWord> words;
    QSet<QString> seen;
    for (const Candidate& candidate : std::as_const(found)) {
        if (words.size() >= query.suggestions) {
            break;
        }
        if (seen.contains(candidate.word)) {
            continue;
        }
        const QList<EntryPreview> entries = candidate.bundle->lookupExact(candidate.word);
        if (entries.isEmpty()) {
            continue;
        }
        seen.insert(candidate.word);
        words.append({.dictId = candidate.bundle->meta().dictId,
                      .dictName = candidate.bundle->meta().name,
                      .entry = entries.first()});
    }
    return words;
}

} // namespace omnidict::core
