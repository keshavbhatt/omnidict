#include "services/dictionary_manager.h"

#include "core/installer.h"
#include "services/logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThreadPool>

using namespace Qt::StringLiterals;

namespace omnidict::services {

namespace {

constexpr qint64 kCatalogTtlSeconds = qint64{24} * 60 * 60;
const QString kCatalogCacheFile = u"catalog.json"_s;
const QString kDownloadsSubdir = u"downloads"_s;

/// A freshly queued download's status: Waiting, nothing received yet.
DownloadStatus freshStatus(const QString& dictId, qint64 total)
{
    return DownloadStatus{
        .dictId = dictId, .state = DownloadStatus::Waiting, .received = 0, .total = total, .error = {}};
}

} // namespace

DictionaryManager::DictionaryManager(QString dictionariesRoot, QString cacheDir, QUrl catalogUrl,
                                     QObject* parent)
    : QObject(parent)
    , m_dictionariesRoot(std::move(dictionariesRoot))
    , m_cacheDir(std::move(cacheDir))
    , m_catalogUrl(std::move(catalogUrl))
    , m_network(new QNetworkAccessManager(this))
{
    loadCachedCatalog();
}

DictionaryManager::~DictionaryManager() = default;

QUrl DictionaryManager::defaultCatalogUrl()
{
    const QByteArray env = qgetenv("OMNIDICT_CATALOG_URL");
    if (!env.isEmpty()) {
        return QUrl::fromUserInput(QString::fromUtf8(env));
    }
    return {u"http://localhost:8000/catalog.json"_s};
}

QList<DownloadStatus> DictionaryManager::downloads() const
{
    QList<DownloadStatus> result;
    result.reserve(m_queueOrder.size());
    for (const QString& dictId : m_queueOrder) {
        const auto it = m_downloads.find(dictId);
        if (it != m_downloads.end()) {
            result << it->second->status;
        }
    }
    return result;
}

QString DictionaryManager::catalogCachePath() const
{
    return QDir(m_cacheDir).filePath(kCatalogCacheFile);
}

QString DictionaryManager::downloadPartPath(const core::CatalogEntry& entry) const
{
    return QDir(m_cacheDir)
        .filePath(kDownloadsSubdir + u'/' + entry.dictId + u'-' + entry.version + u".odict.part"_s);
}

void DictionaryManager::loadCachedCatalog()
{
    const QString path = catalogCachePath();
    const QFileInfo info(path);
    if (!info.isFile()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    auto parsed = core::parseCatalog(file.readAll());
    if (!parsed) {
        qCWarning(lcDownloads) << "cached catalog.json is invalid:" << parsed.error();
        return;
    }
    m_catalog = parsed.take();
    m_catalogFetchedAt = info.lastModified().toUTC();
}

void DictionaryManager::writeCatalogCache(const QByteArray& body) const
{
    QDir().mkpath(m_cacheDir);
    QFile file(catalogCachePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcDownloads) << "cannot cache catalog.json:" << file.errorString();
        return;
    }
    file.write(body);
}

void DictionaryManager::refreshCatalog(bool force)
{
    if (m_catalogReply != nullptr) {
        return; // already fetching
    }
    if (!force && m_catalogFetchedAt.isValid() &&
        m_catalogFetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < kCatalogTtlSeconds) {
        return;
    }
    m_catalogReply = m_network->get(QNetworkRequest(m_catalogUrl));
    connect(m_catalogReply, &QNetworkReply::finished, this, &DictionaryManager::onCatalogFinished);
}

void DictionaryManager::onCatalogFinished()
{
    QNetworkReply* reply = m_catalogReply;
    m_catalogReply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT catalogFailed(
            tr("The dictionary catalogue could not be reached: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray body = reply->readAll();
    auto parsed = core::parseCatalog(body);
    if (!parsed) {
        Q_EMIT catalogFailed(tr("The dictionary catalogue could not be read (%1)").arg(parsed.error()));
        return;
    }
    m_catalog = parsed.take();
    m_catalogFetchedAt = QDateTime::currentDateTimeUtc();
    writeCatalogCache(body);
    Q_EMIT catalogChanged();
}

DictionaryManager::Download* DictionaryManager::findDownload(const QString& dictId)
{
    const auto it = m_downloads.find(dictId);
    return it == m_downloads.end() ? nullptr : it->second.get();
}

int DictionaryManager::activeCount() const
{
    int count = 0;
    for (const auto& [dictId, download] : m_downloads) {
        if (download->status.state == DownloadStatus::Downloading ||
            download->status.state == DownloadStatus::Installing) {
            ++count;
        }
    }
    return count;
}

void DictionaryManager::install(const core::CatalogEntry& entry)
{
    Download* existing = findDownload(entry.dictId);
    if (existing != nullptr && (existing->status.state == DownloadStatus::Downloading ||
                                existing->status.state == DownloadStatus::Installing)) {
        return; // already in progress
    }

    if (existing != nullptr) {
        existing->entry = entry;
        existing->status = freshStatus(entry.dictId, entry.sizeCompressed);
        existing->headerChecked = false;
        existing->resumeOffset = 0;
    } else {
        auto download = std::make_unique<Download>();
        download->entry = entry;
        download->status = freshStatus(entry.dictId, entry.sizeCompressed);
        m_downloads.emplace(entry.dictId, std::move(download));
        m_queueOrder.append(entry.dictId);
    }
    Q_EMIT downloadChanged(findDownload(entry.dictId)->status);
    startQueuedDownloads();
}

void DictionaryManager::startQueuedDownloads()
{
    while (activeCount() < kMaxConcurrentDownloads) {
        QString nextId;
        for (const QString& id : std::as_const(m_queueOrder)) {
            const Download* download = findDownload(id);
            if (download != nullptr && download->status.state == DownloadStatus::Waiting) {
                nextId = id;
                break;
            }
        }
        if (nextId.isEmpty()) {
            return;
        }
        beginDownload(nextId);
    }
}

void DictionaryManager::beginDownload(const QString& dictId)
{
    Download* download = findDownload(dictId);
    if (download == nullptr) {
        return;
    }

    const QString partPath = downloadPartPath(download->entry);
    QDir().mkpath(QFileInfo(partPath).absolutePath());

    qint64 existingSize = 0;
    const QFileInfo partInfo(partPath);
    if (partInfo.exists()) {
        existingSize = partInfo.size();
    }

    auto file = std::make_unique<QFile>(partPath);
    const QIODevice::OpenMode mode =
        existingSize > 0 ? (QIODevice::WriteOnly | QIODevice::Append) : QIODevice::WriteOnly;
    if (!file->open(mode)) {
        failDownload(dictId, tr("The download could not be saved: %1").arg(file->errorString()),
                     /*keepPartial=*/true);
        return;
    }

    QNetworkRequest request(QUrl(download->entry.url));
    if (existingSize > 0) {
        request.setRawHeader(QByteArrayLiteral("Range"),
                             QByteArrayLiteral("bytes=") + QByteArray::number(existingSize) + '-');
    }

    QNetworkReply* reply = m_network->get(request);
    download->reply = reply;
    download->file = std::move(file);
    download->resumeOffset = existingSize;
    download->headerChecked = false;
    download->status.state = DownloadStatus::Downloading;
    download->status.received = existingSize;
    download->status.total = download->entry.sizeCompressed;
    download->status.error.clear();

    connect(reply, &QNetworkReply::readyRead, this, [this, dictId] { onReadyRead(dictId); });
    connect(reply, &QNetworkReply::finished, this, [this, dictId] { onDownloadFinished(dictId); });

    Q_EMIT downloadChanged(download->status);
}

void DictionaryManager::onReadyRead(const QString& dictId)
{
    Download* download = findDownload(dictId);
    if (download == nullptr || download->reply == nullptr) {
        return; // cancelled meanwhile
    }

    if (!download->headerChecked) {
        download->headerChecked = true;
        const int status = download->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A Range request answered with anything other than 206 means the
        // server ignored it and is sending the whole file again: restart the
        // part file from scratch instead of appending onto stale data.
        if (download->resumeOffset > 0 && status != 206) {
            download->file->close();
            if (!download->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QNetworkReply* reply = download->reply;
                download->reply = nullptr;
                reply->abort();
                reply->deleteLater();
                failDownload(dictId,
                             tr("The download could not be saved: %1").arg(download->file->errorString()),
                             /*keepPartial=*/true);
                return;
            }
            download->status.received = 0;
            download->resumeOffset = 0;
        }
    }

    const QByteArray chunk = download->reply->readAll();
    download->file->write(chunk);
    download->status.received += chunk.size();
    Q_EMIT downloadChanged(download->status);
}

void DictionaryManager::onDownloadFinished(const QString& dictId)
{
    Download* download = findDownload(dictId);
    if (download == nullptr || download->reply == nullptr) {
        return; // cancelled meanwhile
    }

    QNetworkReply* reply = download->reply;
    download->reply = nullptr;
    reply->deleteLater();
    if (download->file) {
        download->file->close();
    }

    // Checked before reply->error(): Qt maps common HTTP status codes (404,
    // 500, ...) to a QNetworkReply::NetworkError of their own, which would
    // otherwise take the branch below and report a vaguer message.
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400) {
        failDownload(dictId, tr("The server reported an error (%1)").arg(status),
                     /*keepPartial=*/true);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        failDownload(dictId, tr("The download stopped: %1").arg(reply->errorString()),
                     /*keepPartial=*/true);
        return;
    }

    download->status.state = DownloadStatus::Installing;
    Q_EMIT downloadChanged(download->status);

    const QString partPath = downloadPartPath(download->entry);
    const core::CatalogEntry entry = download->entry;
    const QString root = m_dictionariesRoot;
    DictionaryManager* self = this;
    QThreadPool::globalInstance()->start([self, dictId, partPath, entry, root]() {
        const core::Result<QString> result = core::installBundle(partPath, entry, root);
        QFile::remove(partPath);
        QMetaObject::invokeMethod(
            self, [self, dictId, result]() { self->onInstallFinished(dictId, result); },
            Qt::QueuedConnection);
    });
}

void DictionaryManager::onInstallFinished(const QString& dictId, const core::Result<QString>& result)
{
    const Download* download = findDownload(dictId);
    if (download == nullptr) {
        return; // cancelled meanwhile: the result is simply dropped
    }

    if (!result) {
        const QString& detail = result.error();
        QString message;
        if (detail.contains(u"checksum"_s)) {
            message = tr("The file did not match the catalogue (checksum)");
        } else if (detail.contains(u"size mismatch"_s)) {
            message = tr("The file did not match the catalogue (size)");
        } else if (detail.contains(u"dict_id mismatch"_s) || detail.contains(u"version mismatch"_s)) {
            message = tr("The downloaded file did not match the catalogue entry");
        } else {
            message = tr("The dictionary could not be installed (%1)").arg(detail);
        }
        failDownload(dictId, message, /*keepPartial=*/false);
        return;
    }

    m_downloads.erase(dictId);
    m_queueOrder.removeAll(dictId);
    Q_EMIT downloadsChanged();
    qCInfo(lcDownloads) << "installed" << dictId;
    Q_EMIT installed(dictId);
    startQueuedDownloads();
}

void DictionaryManager::failDownload(const QString& dictId, const QString& message, bool keepPartial)
{
    Download* download = findDownload(dictId);
    if (download == nullptr) {
        return;
    }
    if (!keepPartial) {
        QFile::remove(downloadPartPath(download->entry));
    }
    download->status.state = DownloadStatus::Failed;
    download->status.error = message;
    qCWarning(lcDownloads) << dictId << "failed:" << message;
    Q_EMIT downloadChanged(download->status);
    startQueuedDownloads();
}

void DictionaryManager::cancel(const QString& dictId)
{
    const auto it = m_downloads.find(dictId);
    if (it == m_downloads.end()) {
        return;
    }
    std::unique_ptr<Download> download = std::move(it->second);
    m_downloads.erase(it);
    m_queueOrder.removeAll(dictId);

    if (download->reply != nullptr) {
        download->reply->abort(); // triggers finished(); onDownloadFinished() finds nothing tracked
    }
    if (download->file) {
        download->file->close();
    }
    // The partial file on disk is kept: a later install() call resumes it.
    Q_EMIT downloadsChanged();
    startQueuedDownloads();
}

void DictionaryManager::remove(const QString& dictId)
{
    auto result = core::removeInstalled(dictId, m_dictionariesRoot);
    if (!result) {
        qCWarning(lcDownloads) << "cannot remove" << dictId << ":" << result.error();
        return;
    }
    if (result.value()) {
        Q_EMIT removed(dictId);
    }
}

} // namespace omnidict::services
