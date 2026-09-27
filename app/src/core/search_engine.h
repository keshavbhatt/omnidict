#pragma once

#include "core/entry.h"
#include "core/library.h"

#include <QList>
#include <QMetaType>
#include <QString>

namespace omnidict::core {

/// What the user asked for.
struct SearchQuery
{
    QString text{};
    QString dictId{};       ///< one dictionary; empty searches them all
    int perDictionary = 20; ///< headword results per dictionary
    int fullTextPerDictionary = 5;
};

/// One dictionary's share of the results.
struct ResultGroup
{
    QString dictId;
    QString dictName;
    QList<EntryPreview> rows;
};

struct SearchResults
{
    QString text{};
    QList<ResultGroup> headwords;   ///< exact matches first, then prefix matches
    QList<ResultGroup> definitions; ///< "also found in definitions", entries not in `headwords`

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
    const Library& m_library;
};

} // namespace omnidict::core

Q_DECLARE_METATYPE(omnidict::core::SearchResults)
