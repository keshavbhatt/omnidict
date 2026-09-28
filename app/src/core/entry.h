#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

// Plain value types for what a dictionary bundle holds (DOCS/schema.md).
// Markup fields carry the restricted HTML subset exactly as stored.
namespace omnidict::core {

/// Bundle metadata, the `meta` table.
struct BundleMeta
{
    QString dictId;
    QString name;
    QString sourceLang; ///< BCP-47
    QString targetLang; ///< equals sourceLang for a monolingual dictionary
    QString version;
    int schemaVersion = 0;
    QString publisher;
    QString license;
    QString licenseUrl;
    QString attribution;
    qint64 entryCount = 0;
    QString builtAt; ///< UTC, ISO 8601
};

/// One row of a result list: enough to paint it without loading the entry.
struct EntryPreview
{
    qint64 id = 0;
    QString headword;
    QString preview;   ///< first sense, plain text
    int frequency = 0; ///< 1..5, 0 when the source has no frequency band
    /// When the query matched an inflected or variant form rather than the
    /// headword ("ran" for "run"), that form; empty otherwise.
    QString matchedForm{};

    [[nodiscard]] bool operator==(const EntryPreview&) const = default;
};

struct Pronunciation
{
    QString ipa;
    QString region; ///< "US", "UK", ... or empty
};

struct Example
{
    QString text;        ///< restricted HTML
    QString translation; ///< restricted HTML; empty for monolingual
};

struct Sense
{
    int ordinal = 0; ///< 1-based display order
    QString pos;     ///< normalized tag, may be empty
    QString pattern;
    QString label;
    QString definition; ///< restricted HTML
    QList<Example> examples;
};

struct Form
{
    QString form;
    QString tag; ///< "past", "plural", ... or empty
};

struct Relation
{
    QString type;         ///< "synonym" | "antonym" | "see" | "derived"
    QString target;       ///< headword text, may not exist in this dictionary
    int senseOrdinal = 0; ///< 0 when the relation belongs to the whole entry
};

/// A complete entry, everything the entry view renders.
struct Entry
{
    qint64 id = 0;
    QString headword;
    QString lang;
    int frequency = 0;
    QList<Pronunciation> pronunciations;
    QList<Sense> senses;
    QList<Form> forms;
    QList<Relation> relations;
};

} // namespace omnidict::core

Q_DECLARE_METATYPE(omnidict::core::EntryPreview)
