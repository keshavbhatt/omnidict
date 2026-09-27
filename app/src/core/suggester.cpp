#include "core/suggester.h"

#include "core/logging.h"
#include "core/sqlite_db.h"

#include <QHash>

#include <algorithm>
#include <cstdlib>
#include <tuple>

using namespace Qt::StringLiterals;

namespace omnidict::core {

namespace {

// Keep these equal to the constants in pipeline/omnipipe/suggest.py.
constexpr qsizetype kMinQueryLength = 3;
constexpr qsizetype kMaxTrigrams = 8;
constexpr qint64 kMaxPostings = 100'000;
constexpr qsizetype kMaxCandidates = 300;
constexpr uint kStart = 0x02;
constexpr uint kEnd = 0x03;
constexpr qsizetype kTrigram = 3;

QString toString(const CodePoints& points)
{
    return QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()), points.size());
}

/// The distinct trigrams of `points`, sorted by code point as Python sorts strings.
QList<CodePoints> trigrams(const CodePoints& points)
{
    QList<CodePoints> grams;
    for (qsizetype i = 0; i + kTrigram <= points.size(); ++i) {
        grams.append(points.mid(i, kTrigram));
    }
    std::ranges::sort(grams);
    const auto duplicates = std::ranges::unique(grams);
    grams.erase(duplicates.begin(), duplicates.end());
    return grams;
}

QString phrase(const CodePoints& trigram)
{
    QString quoted = toString(trigram);
    quoted.replace(u'"', u"\"\""_s);
    return u'"' + quoted + u'"';
}

struct Counted
{
    qint64 docs = 0;
    CodePoints gram;
};

/// The trigrams of `padded` that occur in the table, rarest first.
QList<Counted> countTrigrams(const SqliteDb& db, const CodePoints& padded)
{
    auto statement = db.prepare(u"SELECT doc FROM temp.suggest_vocab WHERE term = ?1"_s);
    if (!statement) {
        qCWarning(lcBundle) << "suggestions:" << statement.error();
        return {};
    }
    QList<Counted> counted;
    for (const CodePoints& gram : trigrams(padded)) {
        statement.value().reset();
        statement.value().bind(1, toString(gram));
        if (statement.value().next() && statement.value().integer(0) > 0) {
            counted.append({.docs = statement.value().integer(0), .gram = gram});
        }
    }
    std::ranges::sort(counted, [](const Counted& a, const Counted& b) {
        return std::tie(a.docs, a.gram) < std::tie(b.docs, b.gram);
    });
    return counted;
}

/// One hit per taken trigram for every word containing it; the most hits first
/// (ties by rowid), at most kMaxCandidates.
QList<std::pair<qint64, int>> candidates(const SqliteDb& db, const QList<Counted>& counted)
{
    auto statement = db.prepare(u"SELECT rowid FROM suggest WHERE suggest MATCH ?1"_s);
    if (!statement) {
        qCWarning(lcBundle) << "suggestions:" << statement.error();
        return {};
    }
    QHash<qint64, int> hits;
    qint64 postings = 0;
    for (qsizetype taken = 0; taken < counted.size(); ++taken) {
        const Counted& trigram = counted.at(taken);
        if (taken == kMaxTrigrams || (taken > 0 && postings + trigram.docs > kMaxPostings)) {
            break;
        }
        postings += trigram.docs;
        statement.value().reset();
        statement.value().bind(1, phrase(trigram.gram));
        while (statement.value().next()) {
            ++hits[statement.value().integer(0)];
        }
    }
    QList<std::pair<qint64, int>> ranked(hits.keyValueBegin(), hits.keyValueEnd());
    std::ranges::sort(ranked, [](const auto& a, const auto& b) {
        return std::tie(b.second, a.first) < std::tie(a.second, b.first);
    });
    if (ranked.size() > kMaxCandidates) {
        ranked.resize(kMaxCandidates);
    }
    return ranked;
}

struct Ranked
{
    int distance = 0;
    qsizetype lengthDifference = 0;
    int negativeHits = 0;
    CodePoints word;
};

bool rankedBefore(const Ranked& a, const Ranked& b)
{
    return std::tie(a.distance, a.lengthDifference, a.negativeHits, a.word) <
           std::tie(b.distance, b.lengthDifference, b.negativeHits, b.word);
}

/// The candidates within the allowed distance of `query`, best first.
QList<Ranked> rank(const SqliteDb& db, const CodePoints& query, const QList<std::pair<qint64, int>>& found)
{
    auto statement = db.prepare(u"SELECT word FROM suggest WHERE rowid = ?1"_s);
    if (!statement) {
        qCWarning(lcBundle) << "suggestions:" << statement.error();
        return {};
    }
    const int cap = maxSuggestionDistance(query.size());
    QList<Ranked> ranked;
    for (const auto& [rowid, hitCount] : found) {
        statement.value().reset();
        statement.value().bind(1, rowid);
        if (!statement.value().next()) {
            continue;
        }
        const CodePoints stored = statement.value().text(0).toUcs4();
        if (stored.size() < 2) {
            continue;
        }
        CodePoints word = stored.mid(1, stored.size() - 2); // without the markers
        if (word == query) {
            continue;
        }
        const int distance = osaDistance(query, word, cap);
        if (distance <= cap) {
            ranked.append({.distance = distance,
                           .lengthDifference = std::abs(word.size() - query.size()),
                           .negativeHits = -hitCount,
                           .word = std::move(word)});
        }
    }
    std::ranges::sort(ranked, rankedBefore);
    return ranked;
}

} // namespace

int maxSuggestionDistance(qsizetype length)
{
    constexpr qsizetype kShort = 4;
    constexpr qsizetype kMedium = 8;
    if (length <= kShort) {
        return 1;
    }
    return length <= kMedium ? 2 : 3;
}

int osaDistance(const CodePoints& a, const CodePoints& b, int cap)
{
    if (std::abs(a.size() - b.size()) > cap) {
        return cap + 1;
    }
    const qsizetype width = b.size() + 1;
    QList<int> before(width, 0);
    QList<int> previous(width, 0);
    QList<int> current(width, 0);
    for (qsizetype j = 0; j < width; ++j) {
        previous[j] = static_cast<int>(j);
    }
    for (qsizetype i = 1; i <= a.size(); ++i) {
        current[0] = static_cast<int>(i);
        int rowMin = current.at(0);
        for (qsizetype j = 1; j < width; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            int value = std::min({previous.at(j) + 1, current.at(j - 1) + 1, previous.at(j - 1) + cost});
            if (i > 1 && j > 1 && a.at(i - 1) == b.at(j - 2) && a.at(i - 2) == b.at(j - 1)) {
                value = std::min(value, before.at(j - 2) + 1);
            }
            current[j] = value;
            rowMin = std::min(rowMin, value);
        }
        if (rowMin > cap) {
            return cap + 1;
        }
        std::swap(before, previous);
        std::swap(previous, current);
    }
    return std::min(previous.at(width - 1), cap + 1);
}

QList<Suggestion> suggestSpellings(const SqliteDb& db, const QString& queryNorm, int limit)
{
    const CodePoints query = queryNorm.toUcs4();
    if (query.size() < kMinQueryLength || limit <= 0) {
        return {};
    }
    // The vocabulary is a view over the bundle's index; temp keeps the bundle read-only.
    if (const auto created = db.exec(
            u"CREATE VIRTUAL TABLE IF NOT EXISTS temp.suggest_vocab USING fts5vocab(main, suggest, row)"_s);
        !created) {
        qCWarning(lcBundle) << "suggestions:" << created.error();
        return {};
    }
    CodePoints padded;
    padded.reserve(query.size() + 2);
    padded.append(kStart);
    padded.append(query);
    padded.append(kEnd);

    const QList<Ranked> ranked = rank(db, query, candidates(db, countTrigrams(db, padded)));
    QList<Suggestion> suggestions;
    for (const Ranked& item : ranked) {
        if (suggestions.size() == limit) {
            break;
        }
        suggestions.append({.word = toString(item.word), .distance = item.distance});
    }
    return suggestions;
}

} // namespace omnidict::core
