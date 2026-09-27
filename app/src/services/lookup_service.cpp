#include "services/lookup_service.h"

#include "core/entry_renderer.h"
#include "core/library.h"
#include "services/logging.h"

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
                             .entryCount = meta.entryCount});
    }
    qCInfo(lcLookup) << "opened" << dictionaries.size() << "dictionaries from" << roots;
    Q_EMIT libraryOpened(dictionaries, m_library->problems());
}

void LookupService::search(quint64 requestId, const core::SearchQuery& query)
{
    Q_EMIT searchFinished(requestId, core::SearchEngine(*m_library).search(query));
}

void LookupService::loadEntry(quint64 requestId, const QString& dictId, qint64 entryId)
{
    const core::Bundle* bundle = m_library->find(dictId);
    const std::optional<core::Entry> entry = bundle != nullptr ? bundle->entry(entryId) : std::nullopt;
    if (!entry) {
        Q_EMIT entryNotFound(requestId, dictId);
        return;
    }
    Q_EMIT entryLoaded(requestId, dictId, *entry, core::renderEntry(*entry));
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

void registerLookupTypes()
{
    qRegisterMetaType<DictionaryInfo>();
    qRegisterMetaType<QList<DictionaryInfo>>();
    qRegisterMetaType<core::SearchQuery>();
    qRegisterMetaType<core::SearchResults>();
    qRegisterMetaType<core::Entry>();
}

} // namespace omnidict::services
