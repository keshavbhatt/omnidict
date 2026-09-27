#pragma once

#include "core/entry.h"
#include "core/library.h"

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace omnidict::core {

/// What the user asked for.
struct SearchQuery
{
    QString text{};
    QString dictId{};       ///< one dictionary; empty searches them all
    int perDictionary = 20; ///< headword results per dictionary
    int fullTextPerDictionary = 5;
    int suggestions = 5; ///< "did you mean" words when nothing matches; 0 turns them off
    /// Dictionary ids in the user's order; groups of equal match quality follow
    /// it, and dictionaries not listed come after, in library order.
    // The braces keep GCC's -Wmissing-field-initializers quiet for designated
    // initializers that leave these out; clang-tidy calls them redundant.
    QStringList order{};    // NOLINT(readability-redundant-member-init)
    QStringList excluded{}; // NOLINT(readability-redundant-member-init): switched-off dictionaries
};

/// One dictionary's share of the results.
struct ResultGroup
{
    QString dictId;
    QString dictName;
    QList<EntryPreview> rows;
};

/// A headword close to a query that matched nothing, with the entry it opens.
struct SuggestedWord
{
    QString dictId;
    QString dictName;
    EntryPreview entry; ///< the suggested headword's first entry in that dictionary
};

struct SearchResults
{
    QString text{};
    /// Dictionaries whose first entry best matches the query first, then in
    /// library order; within one, exact matches first, then prefix matches.
    QList<ResultGroup> headwords;
    QList<ResultGroup> definitions; ///< "also found in definitions", entries not in `headwords`
    /// Only when nothing else was found and the query is not a pattern: the
    /// closest headwords across the dictionaries searched, best first.
    QList<SuggestedWord> suggestions;

    [[nodiscard]] bool isEmpty() const { return headwords.isEmpty() && definitions.isEmpty(); }
};

/// Searches every dictionary of a library at once and groups the results by
/// dictionary, the way the result list shows them (PLAN 7.2). Runs on the
/// thread that owns the library.
class SearchEngine
{
public:
    /// Words shorter than this are not looked for inside definitions: they
    /// match too much to be useful.
    static constexpr qsizetype kMinFullTextLength = 3;

    explicit SearchEngine(const Library& library)
        : m_library(library)
    {}

    /// A query with `?` or `*` is a headword pattern and skips the full-text
    /// part. A blank query finds nothing.
    [[nodiscard]] SearchResults search(const SearchQuery& query) const;

private:
    /// The dictionaries the query searches, in its order.
    [[nodiscard]] QList<const Bundle*> searched(const SearchQuery& query) const;
    [[nodiscard]] QList<SuggestedWord> suggest(const SearchQuery& query, const QString& text) const;

    const Library& m_library;
};

} // namespace omnidict::core

Q_DECLARE_METATYPE(omnidict::core::SearchResults)
