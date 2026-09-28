#include "core/catalog.h"

#include "core/bundle.h"
#include "core/logging.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace omnidict::core {

namespace {

/// Names an entry for an error message: its `dict_id` when present and a
/// string, else its position in the array.
QString entryLabel(const QJsonObject& object, qsizetype index)
{
    const QJsonValue dictId = object.value(u"dict_id"_s);
    if (dictId.isString() && !dictId.toString().isEmpty()) {
        return dictId.toString();
    }
    return u"entry %1"_s.arg(index);
}

/// A required string field. Empty is not accepted here: the schema has no
/// optional string among the ones this struct carries.
[[nodiscard]] Result<QString> requireString(const QJsonObject& object, const QString& key,
                                            const QString& label)
{
    if (!object.contains(key)) {
        return Error{u"%1: missing field %2"_s.arg(label, key)};
    }
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return Error{u"%1: field %2 must be a string"_s.arg(label, key)};
    }
    return value.toString();
}

/// A required whole number field, read via QJsonValue::toDouble(): JSON has
/// only a double, and every value this catalog carries (counts, byte sizes)
/// fits exactly in one (well under 2^53).
[[nodiscard]] Result<qint64> requireInteger(const QJsonObject& object, const QString& key,
                                            const QString& label)
{
    if (!object.contains(key)) {
        return Error{u"%1: missing field %2"_s.arg(label, key)};
    }
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return Error{u"%1: field %2 must be a number"_s.arg(label, key)};
    }
    const double number = value.toDouble();
    const auto whole = static_cast<qint64>(number);
    if (static_cast<double>(whole) != number) {
        return Error{u"%1: field %2 must be a whole number"_s.arg(label, key)};
    }
    return whole;
}

/// One required field: where it lives in the JSON object and where it lands
/// in the entry. Table-driven so `parseEntry` stays a simple loop instead of
/// the same six lines repeated for each of the 17 fields.
struct StringField
{
    QString key;
    QString CatalogEntry::* member;
};
struct IntegerField
{
    QString key;
    qint64 CatalogEntry::* member;
};

Result<CatalogEntry> parseEntry(const QJsonObject& object, const QString& label)
{
    static const std::array<StringField, 13> kStringFields{{
        {.key = u"dict_id"_s, .member = &CatalogEntry::dictId},
        {.key = u"name"_s, .member = &CatalogEntry::name},
        {.key = u"source_lang"_s, .member = &CatalogEntry::sourceLang},
        {.key = u"target_lang"_s, .member = &CatalogEntry::targetLang},
        {.key = u"kind"_s, .member = &CatalogEntry::kind},
        {.key = u"version"_s, .member = &CatalogEntry::version},
        {.key = u"publisher"_s, .member = &CatalogEntry::publisher},
        {.key = u"license"_s, .member = &CatalogEntry::license},
        {.key = u"license_url"_s, .member = &CatalogEntry::licenseUrl},
        {.key = u"attribution"_s, .member = &CatalogEntry::attribution},
        {.key = u"sha256"_s, .member = &CatalogEntry::sha256},
        {.key = u"url"_s, .member = &CatalogEntry::url},
        {.key = u"built_at"_s, .member = &CatalogEntry::builtAt},
    }};
    static const std::array<IntegerField, 3> kIntegerFields{{
        {.key = u"entry_count"_s, .member = &CatalogEntry::entryCount},
        {.key = u"size_compressed"_s, .member = &CatalogEntry::sizeCompressed},
        {.key = u"size_installed"_s, .member = &CatalogEntry::sizeInstalled},
    }};

    CatalogEntry entry;
    for (const StringField& field : kStringFields) {
        auto value = requireString(object, field.key, label);
        if (!value) {
            return Error{value.error()};
        }
        entry.*field.member = value.take();
    }
    for (const IntegerField& field : kIntegerFields) {
        auto value = requireInteger(object, field.key, label);
        if (!value) {
            return Error{value.error()};
        }
        entry.*field.member = value.take();
    }

    auto schemaVersion = requireInteger(object, u"schema_version"_s, label);
    if (!schemaVersion) {
        return Error{schemaVersion.error()};
    }
    entry.schemaVersion = static_cast<int>(schemaVersion.take());

    // Optional (PLAN.md 5.1 `source`): a missing or malformed object leaves it empty.
    const QJsonObject source = object.value(u"source"_s).toObject();
    entry.sourceConverter = source.value(u"converter"_s).toString();
    entry.sourceUrl = source.value(u"url"_s).toString();
    entry.sourceLangName = object.value(u"source_lang_name"_s).toString();
    entry.targetLangName = object.value(u"target_lang_name"_s).toString();

    return entry;
}

} // namespace

Result<Catalog> parseCatalog(const QByteArray& json)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return Error{u"catalog: invalid JSON: %1"_s.arg(parseError.errorString())};
    }
    if (!document.isObject()) {
        return Error{u"catalog: root is not a JSON object"_s};
    }
    const QJsonObject root = document.object();

    if (!root.contains(u"catalog_version"_s) || !root.value(u"catalog_version"_s).isDouble()) {
        return Error{u"catalog: missing or invalid field catalog_version"_s};
    }
    if (!root.contains(u"generated_at"_s) || !root.value(u"generated_at"_s).isString()) {
        return Error{u"catalog: missing or invalid field generated_at"_s};
    }
    if (!root.contains(u"dictionaries"_s) || !root.value(u"dictionaries"_s).isArray()) {
        return Error{u"catalog: missing or invalid field dictionaries"_s};
    }

    Catalog catalog;
    catalog.catalogVersion = static_cast<int>(root.value(u"catalog_version"_s).toDouble());
    catalog.generatedAt = root.value(u"generated_at"_s).toString();

    const QJsonArray dictionaries = root.value(u"dictionaries"_s).toArray();
    for (qsizetype i = 0; i < dictionaries.size(); ++i) {
        const QJsonValue value = dictionaries.at(i);
        if (!value.isObject()) {
            return Error{u"catalog: entry %1 is not a JSON object"_s.arg(i)};
        }
        const QJsonObject object = value.toObject();
        const QString label = entryLabel(object, i);

        auto entry = parseEntry(object, label);
        if (!entry) {
            return Error{entry.error()};
        }
        if (entry.value().schemaVersion > Bundle::kSupportedSchemaVersion) {
            qCInfo(lcCore) << "catalog: skipping" << entry.value().dictId << "schema_version"
                           << entry.value().schemaVersion << "newer than supported"
                           << Bundle::kSupportedSchemaVersion;
            continue;
        }
        catalog.dictionaries << entry.take();
    }

    return catalog;
}

int compareVersions(const QString& a, const QString& b)
{
    const QStringList left = a.split(u'.');
    const QStringList right = b.split(u'.');
    const qsizetype count = std::max(left.size(), right.size());
    for (qsizetype i = 0; i < count; ++i) {
        const int l = left.value(i).toInt();
        const int r = right.value(i).toInt();
        if (l != r) {
            return l < r ? -1 : 1;
        }
    }
    return 0;
}

QString providerName(const CatalogEntry& entry)
{
    // Brand names, the same in every language; one row per pipeline converter.
    static const QHash<QString, QString> kProviders{
        {u"kaikki"_s, u"Wiktionary"_s}, {u"freedict"_s, u"FreeDict"_s}, {u"oewn"_s, u"WordNet"_s},
        {u"jmdict"_s, u"JMdict"_s},     {u"cedict"_s, u"CC-CEDICT"_s},  {u"kengdic"_s, u"Kengdic"_s},
    };
    return kProviders.value(entry.sourceConverter, entry.publisher);
}

} // namespace omnidict::core
