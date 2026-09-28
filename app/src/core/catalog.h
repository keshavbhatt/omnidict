#pragma once

#include "core/result.h"

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

namespace omnidict::core {

/// One dictionary as listed in `catalog.json` (PLAN.md 5.1, 5.2): every field
/// of its `manifest.json`, except the optional `source` object, which nothing
/// in the client needs.
struct CatalogEntry
{
    QString dictId;
    QString name;
    QString sourceLang; ///< BCP-47
    QString targetLang; ///< equals sourceLang for a monolingual dictionary
    QString kind;       ///< "monolingual" | "bilingual"
    QString version;
    int schemaVersion = 0;
    QString publisher;
    QString license;
    QString licenseUrl;
    QString attribution;
    qint64 entryCount = 0;
    qint64 sizeCompressed = 0; ///< bytes, the `.odict` file
    qint64 sizeInstalled = 0;  ///< bytes, the decompressed `dict.sqlite`
    QString sha256;            ///< lowercase hex, of the `.odict` file
    QString url;
    QString builtAt; ///< UTC, ISO 8601
    /// `source.converter` ("kaikki", "freedict", ...); optional, empty when absent.
    QString sourceConverter;
    // Optional since schema_version 3 (DOCS/schema.md); empty when absent.
    QString sourceLangName; ///< English name, for codes a locale database may not know
    QString targetLangName;
    QString sourceUrl; ///< `source.url`: where the upstream source can be downloaded

    [[nodiscard]] bool operator==(const CatalogEntry&) const = default;
};

/// The parsed `catalog.json` (PLAN.md 5.2).
struct Catalog
{
    int catalogVersion = 0;
    QString generatedAt; ///< UTC, ISO 8601
    QList<CatalogEntry> dictionaries;
};

/// Parses `catalog.json` (PLAN.md 5.2), matching `pipeline/omnipipe/catalog.py`
/// exactly. Strict: a missing required field or one of the wrong JSON type is
/// an error naming the offending entry (by `dict_id` when it has one, else its
/// position). An entry whose `schema_version` is newer than
/// `Bundle::kSupportedSchemaVersion` is silently skipped, not an error: an
/// older client should simply not offer a dictionary it cannot read yet.
[[nodiscard]] Result<Catalog> parseCatalog(const QByteArray& json);

/// Compares two dotted, non-negative-integer version strings component by
/// component (`"2026.9.2"` == `"2026.09.2"`, `"2026.10.1"` > `"2026.9.2"`), a
/// missing trailing component reading as 0. Returns a value `< 0`, `== 0`, or
/// `> 0` as `a` compares less than, equal to, or greater than `b`. A component
/// that is not a non-negative integer reads as 0, the same as a missing one.
[[nodiscard]] int compareVersions(const QString& a, const QString& b);

/// Who provides a dictionary's content, by the pipeline converter that built it
/// ("Wiktionary" for `kaikki`); the publisher field when the converter is unknown.
[[nodiscard]] QString providerName(const CatalogEntry& entry);

} // namespace omnidict::core

Q_DECLARE_METATYPE(omnidict::core::CatalogEntry)
