#include "services/lookup_service.h"

#include "core/entry_renderer.h"
#include "core/library.h"
#include "services/logging.h"

#include <ranges>

namespace omnidict::services {

LookupService::LookupService(QObject* parent)
    : QObject(parent)
    , m_library(std::make_unique<core::Library>())
{}

LookupService::~LookupService() = default;

void LookupService::openLibrary(const QStringList& roots)
{
    m_library = std::make_unique<core::Library>(core::Library::discover(roots));
    QList<DictionaryInfo> dictionaries;
    for (const core::Bundle& bundle : m_library->bundles()) {
        const core::BundleMeta& meta = bundle.meta();
        dictionaries.append({.dictId = meta.dictId,
                             .name = meta.name,
                             .attribution = meta.attribution,
                             .license = meta.license,
                             .licenseUrl = meta.licenseUrl,
                             .version = meta.version,
                             .entryCount = meta.entryCount,
                             .publisher = meta.publisher,
                             .sourceLang = meta.sourceLang,
                             .targetLang = meta.targetLang,
                             .path = bundle.path(),
                             .sourceUrl = meta.sourceUrl});
    }
    qCInfo(lcLookup) << "opened" << dictionaries.size() << "dictionaries from" << roots;
    Q_EMIT libraryOpened(dictionaries, m_library->problems());
}

void LookupService::openUserData(const QString& path)
{
    auto data = core::UserData::open(path);
    if (!data) {
        qCWarning(lcLookup) << "history and favourites unavailable:" << data.error();
        return;
    }
    m_userData = data.take();
}

void LookupService::search(quint64 requestId, const core::SearchQuery& query)
{
    const core::SearchResults results = core::SearchEngine(*m_library).search(query);
    QStringList favorites;
    if (m_userData) {
        const auto collect = [&](const QString& dictId, const QString& headword) {
            if (m_userData->isFavorite(dictId, headword)) {
                favorites << core::favoriteKey(dictId, headword);
            }
        };
        for (const QList<core::ResultGroup>* groups : {&results.headwords, &results.definitions}) {
            for (const core::ResultGroup& group : *groups) {
                for (const core::EntryPreview& row : group.rows) {
                    collect(group.dictId, row.headword);
                }
            }
        }
        for (const core::SuggestedWord& word : results.suggestions) {
            collect(word.dictId, word.entry.headword);
        }
    }
    Q_EMIT searchFinished(requestId, results, favorites);
}

void LookupService::loadEntry(quint64 requestId, const QString& dictId, qint64 entryId)
{
    const core::Bundle* bundle = m_library->find(dictId);
    const std::optional<core::Entry> entry = bundle != nullptr ? bundle->entry(entryId) : std::nullopt;
    if (!entry) {
        Q_EMIT entryNotFound(requestId, dictId);
        return;
    }
    const bool favorite = m_userData && m_userData->isFavorite(dictId, entry->headword);
    Q_EMIT entryLoaded(requestId, dictId, *entry, core::renderEntry(*entry), favorite);
}

void LookupService::resolveHeadword(quint64 requestId, const QString& headword,
                                    const QString& preferredDictId)
{
    const core::Bundle* preferred = m_library->find(preferredDictId);
    if (preferred != nullptr) {
        const QList<core::EntryPreview> rows = preferred->lookupExact(headword);
        if (!rows.isEmpty()) {
            loadEntry(requestId, preferredDictId, rows.first().id);
            return;
        }
    }
    for (const core::Bundle& bundle : m_library->bundles()) {
        const QList<core::EntryPreview> rows = bundle.lookupExact(headword);
        if (!rows.isEmpty()) {
            loadEntry(requestId, bundle.meta().dictId, rows.first().id);
            return;
        }
    }
    Q_EMIT entryNotFound(requestId, headword);
}

/// The entry as history and favourites keep it, with its current preview.
core::SavedEntry LookupService::saved(const QString& dictId, const QString& headword) const
{
    core::SavedEntry entry{.dictId = dictId, .headword = headword, .preview = {}};
    if (const core::Bundle* bundle = m_library->find(dictId)) {
        const QList<core::EntryPreview> rows = bundle->lookupExact(headword);
        if (!rows.isEmpty()) {
            entry.preview = rows.first().preview;
        }
    }
    return entry;
}

void LookupService::recordView(const QString& dictId, const QString& headword)
{
    if (m_userData) {
        m_userData->recordView(saved(dictId, headword));
    }
}

void LookupService::setFavorite(const QString& dictId, const QString& headword, bool favorite)
{
    if (m_userData) {
        m_userData->setFavorite(saved(dictId, headword), favorite);
    }
}

void LookupService::requestSaved()
{
    constexpr int kRecentShown = 50;
    if (!m_userData) {
        Q_EMIT savedEntries({}, {});
        return;
    }
    Q_EMIT savedEntries(m_userData->history(kRecentShown), m_userData->favorites());
}

void LookupService::clearHistory()
{
    if (m_userData) {
        m_clearedHistory = m_userData->history(core::UserData::kHistoryLimit);
        m_userData->clearHistory();
    }
    requestSaved();
}

void LookupService::undoClearHistory()
{
    if (m_userData) {
        // Oldest first, so the most recent ends up on top again.
        for (const core::SavedEntry& entry : std::views::reverse(m_clearedHistory)) {
            m_userData->recordView(entry);
        }
    }
    m_clearedHistory.clear();
    requestSaved();
}

void registerLookupTypes()
{
    qRegisterMetaType<DictionaryInfo>();
    qRegisterMetaType<QList<DictionaryInfo>>();
    qRegisterMetaType<core::SearchQuery>();
    qRegisterMetaType<core::SearchResults>();
    qRegisterMetaType<core::Entry>();
    qRegisterMetaType<core::SavedEntry>();
    qRegisterMetaType<QList<core::SavedEntry>>();
}

} // namespace omnidict::services
