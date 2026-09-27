#pragma once

#include "core/catalog.h"
#include "core/result.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

#include <map>
#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace omnidict::services {

/// One dictionary's place in the download queue (PLAN.md 7.2): Waiting for a
/// slot, actively Downloading, off-thread Installing (hash + decompress,
/// ADR-004), or Failed with a short user-facing reason.
struct DownloadStatus
{
    enum State
    {
        Waiting,
        Downloading,
        Installing,
        Failed
    };

    QString dictId;
    State state = Waiting;
    qint64 received = 0;
    qint64 total = 0;
    QString error; ///< set only when state == Failed
};

/// Downloads, verifies and installs dictionaries from the catalogue
/// (PLAN.md 5.3, 7.2, 7.4). Lives on the GUI thread and owns the
/// QNetworkAccessManager; at most kMaxConcurrentDownloads run at once, the
/// rest wait. `core::installBundle` does the actual verify+decompress work,
/// off the GUI thread on QThreadPool::globalInstance() (ADR-004), with the
/// result posted back through QMetaObject::invokeMethod.
class DictionaryManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DictionaryManager)

public:
    static constexpr int kMaxConcurrentDownloads = 2;

    /// `dictionariesRoot` is where bundles are installed (core::Library reads
    /// it); `cacheDir` holds the cached catalog.json and in-progress
    /// downloads (`downloads/<dictId>-<version>.odict.part`).
    explicit DictionaryManager(QString dictionariesRoot, QString cacheDir, QUrl catalogUrl,
                               QObject* parent = nullptr);
    ~DictionaryManager() override; // QFile is incomplete in this header (Download::file)

    /// http://localhost:8000/catalog.json, overridable with the
    /// OMNIDICT_CATALOG_URL environment variable.
    [[nodiscard]] static QUrl defaultCatalogUrl();

    [[nodiscard]] const core::Catalog& catalog() const { return m_catalog; }
    /// Invalid when the catalogue has never been fetched or loaded from cache.
    [[nodiscard]] QDateTime catalogFetchedAt() const { return m_catalogFetchedAt; }
    [[nodiscard]] QUrl catalogUrl() const { return m_catalogUrl; }

    [[nodiscard]] QList<DownloadStatus> downloads() const;

public Q_SLOTS:
    /// Fetches the catalogue when it has never been fetched, is more than 24h
    /// old, or `force` is true; caches the result in cacheDir/catalog.json.
    /// Emits catalogChanged() or catalogFailed().
    void refreshCatalog(bool force = false);
    /// Queues a download and install of `entry`; retries a Failed one. At
    /// most kMaxConcurrentDownloads run at once (PLAN.md 7.2).
    void install(const core::CatalogEntry& entry);
    /// Stops `dictId`'s download (or drops it from the wait queue) and stops
    /// tracking it; the partial file is kept on disk so a later install()
    /// call can resume it.
    void cancel(const QString& dictId);
    /// Uninstalls `dictId`. A no-op, quietly, when it is not installed.
    void remove(const QString& dictId);

Q_SIGNALS:
    void catalogChanged();
    void catalogFailed(const QString& reason);
    void downloadChanged(const omnidict::services::DownloadStatus& status);
    /// downloads() changed as a whole: one started, finished or was cancelled.
    void downloadsChanged();
    void installed(const QString& dictId);
    void removed(const QString& dictId);

private:
    struct Download
    {
        core::CatalogEntry entry;
        DownloadStatus status;
        QNetworkReply* reply = nullptr; ///< non-owning; null while Waiting or Installing
        std::unique_ptr<QFile> file;
        qint64 resumeOffset = 0;    ///< bytes already on disk when this attempt began
        bool headerChecked = false; ///< whether the reply's status code has been inspected yet
    };

    [[nodiscard]] QString catalogCachePath() const;
    [[nodiscard]] QString downloadPartPath(const core::CatalogEntry& entry) const;
    void loadCachedCatalog();
    void writeCatalogCache(const QByteArray& body) const;
    void startQueuedDownloads();
    void beginDownload(const QString& dictId);
    /// Bytes of free space still needed to download and unpack `entry`; 0 when it fits.
    [[nodiscard]] qint64 missingSpace(const core::CatalogEntry& entry, qint64 alreadyDownloaded) const;
    void failDownload(const QString& dictId, const QString& message, bool keepPartial);
    [[nodiscard]] int activeCount() const;
    /// Nothing when `dictId` is not tracked (already cancelled or finished).
    [[nodiscard]] Download* findDownload(const QString& dictId);

    void onCatalogFinished();
    void onReadyRead(const QString& dictId);
    /// Appends what the reply has buffered to the part file; false when the write fails.
    [[nodiscard]] static bool writeAvailable(Download& download);
    void onDownloadFinished(const QString& dictId);
    void onInstallFinished(const QString& dictId, const core::Result<QString>& result);

    QString m_dictionariesRoot;
    QString m_cacheDir;
    QUrl m_catalogUrl;
    QNetworkAccessManager* m_network; ///< child of `this`

    core::Catalog m_catalog;
    QDateTime m_catalogFetchedAt;
    QNetworkReply* m_catalogReply = nullptr; ///< non-owning; null when idle

    // std::map, not QHash/QMap: the value is move-only (owns a QFile), and
    // Qt's implicitly-shared containers need it copyable (CODING_STANDARDS.md
    // section on containers: std:: when it never crosses a Qt API boundary).
    std::map<QString, std::unique_ptr<Download>> m_downloads;
    QList<QString> m_queueOrder; ///< insertion order, for a stable downloads() and FIFO waiting
};

} // namespace omnidict::services

Q_DECLARE_METATYPE(omnidict::services::DownloadStatus)
