#pragma once

#include <QList>
#include <QString>

namespace omnidict::core {

class SqliteDb;

/// A headword close to a query that matched nothing.
struct Suggestion
{
    QString word{}; ///< the headword's normalized form, as stored in the `suggest` table
    int distance = 0;
};

/// A string as Unicode code points, the unit suggestions are measured in.
using CodePoints = QList<uint>;

/// Edits allowed between a query of `length` characters and a suggestion.
[[nodiscard]] int maxSuggestionDistance(qsizetype length);

/// Optimal string alignment distance (insertions, deletions, substitutions and
/// swaps of adjacent characters), or `cap + 1` as soon as it exceeds `cap`.
[[nodiscard]] int osaDistance(const CodePoints& a, const CodePoints& b, int cap);

/// Up to `limit` suggestions for `queryNorm` (already normalized) from the
/// `suggest` table of the bundle open on `db` (ADR-013). The steps are those of
/// the reference, `pipeline/omnipipe/suggest.py`; `tests/suggest_cases.json`
/// keeps the two in step. Empty on any SQLite error.
[[nodiscard]] QList<Suggestion> suggestSpellings(const SqliteDb& db, const QString& queryNorm, int limit);

} // namespace omnidict::core
