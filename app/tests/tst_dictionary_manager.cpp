#include "core/bundle.h"
#include "services/dictionary_manager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <zstd.h>

using namespace Qt::StringLiterals;
using namespace omnidict::core;
using namespace omnidict::services;

namespace {

QString fixturePath()
{
    return QString::fromUtf8(OMNIDICT_FIXTURE_BUNDLE);
}

QByteArray zstdCompress(const QByteArray& data)
{
    const size_t bound = ZSTD_compressBound(static_cast<size_t>(data.size()));
    QByteArray out(static_cast<qsizetype>(bound), '\0');
    const size_t written =
        ZSTD_compress(out.data(), bound, data.constData(), static_cast<size_t>(data.size()), 3);
    if (ZSTD_isError(written) != 0U) {
        return {};
    }
    out.resize(static_cast<qsizetype>(written));
    return out;
}

/// A tiny single-threaded HTTP/1.1 server: enough to serve a `catalog.json`
/// and an `.odict`, honouring a `Range: bytes=N-` request on the routes that
/// opt in. No internet: it only ever listens on 127.0.0.1.
class TestHttpServer : public QObject
{
    Q_OBJECT

public:
    struct Route
    {
        QByteArray body;
        bool supportsRange = false;
        /// When > 0, the body is written in two pieces with a pause between
        /// them, to simulate a download still in flight.
        qsizetype splitAt = -1;
        int pauseMs = 0;
    };

    explicit TestHttpServer(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &TestHttpServer::acceptConnections);
        const bool listening = m_server.listen(QHostAddress::LocalHost);
        Q_ASSERT(listening);
    }

    void addRoute(const QString& path, const Route& route) { m_routes.insert(path, route); }
    [[nodiscard]] bool sawRangeRequest(const QString& path) const { return m_rangeSeen.value(path, false); }

    [[nodiscard]] QUrl urlFor(const QString& path) const
    {
        QUrl url;
        url.setScheme(u"http"_s);
        url.setHost(u"127.0.0.1"_s);
        url.setPort(m_server.serverPort());
        url.setPath(path);
        return url;
    }

private Q_SLOTS:
    void acceptConnections()
    {
        while (m_server.hasPendingConnections()) {
            QTcpSocket* socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleReadyRead(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

private:
    void handleReadyRead(QTcpSocket* socket)
    {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return; // headers not complete yet
        }
        const QByteArray header = buffer.left(headerEnd);
        m_buffers.remove(socket);

        const QList<QByteArray> lines = header.split('\n');
        QString path;
        if (!lines.isEmpty()) {
            const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
            if (requestLine.size() >= 2) {
                path = QString::fromLatin1(requestLine.at(1));
            }
        }
        QByteArray rangeValue;
        for (const QByteArray& line : lines) {
            if (line.startsWith("Range:") || line.startsWith("range:")) {
                rangeValue = line.mid(line.indexOf(':') + 1).trimmed();
            }
        }

        if (!m_routes.contains(path)) {
            writeStatusOnly(socket, 404);
            return;
        }
        const Route route = m_routes.value(path);
        qint64 start = 0;
        bool partial = false;
        if (route.supportsRange && rangeValue.startsWith("bytes=")) {
            m_rangeSeen[path] = true;
            const QByteArray spec = rangeValue.mid(6);
            start = spec.left(spec.indexOf('-')).toLongLong();
            partial = true;
        }
        const QByteArray content = route.body.mid(static_cast<qsizetype>(start));
        writeBody(socket, partial ? 206 : 200, content, partial, start, route.body.size(), route.splitAt,
                  route.pauseMs);
    }

    static void writeStatusOnly(QTcpSocket* socket, int status)
    {
        const QByteArray head = "HTTP/1.1 " + QByteArray::number(status) +
                                " Error\r\n"
                                "Content-Length: 0\r\n"
                                "Connection: close\r\n\r\n";
        socket->write(head);
        socket->flush();
        socket->disconnectFromHost();
    }

    static void writeBody(QTcpSocket* socket, int status, const QByteArray& content, bool partial,
                          qint64 rangeStart, qsizetype totalSize, qsizetype splitAt, int pauseMs)
    {
        QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n";
        head += "Content-Length: " + QByteArray::number(content.size()) + "\r\n";
        if (partial) {
            head += "Content-Range: bytes " + QByteArray::number(rangeStart) + "-" +
                    QByteArray::number(totalSize - 1) + "/" + QByteArray::number(totalSize) + "\r\n";
        }
        head += "Connection: close\r\n\r\n";
        socket->write(head);

        if (splitAt > 0 && splitAt < content.size() && pauseMs > 0) {
            socket->write(content.left(splitAt));
            socket->flush();
            const QByteArray rest = content.mid(splitAt);
            QPointer<QTcpSocket> guarded(socket);
            QTimer::singleShot(pauseMs, socket, [guarded, rest]() {
                QTcpSocket* raw = guarded.data();
                if (raw == nullptr) {
                    return;
                }
                raw->write(rest);
                raw->flush();
                raw->disconnectFromHost();
            });
        } else {
            socket->write(content);
            socket->flush();
            socket->disconnectFromHost();
        }
    }

    QTcpServer m_server;
    QHash<QString, Route> m_routes;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QHash<QString, bool> m_rangeSeen;
};

CatalogEntry makeEntry(const QString& dictId, const QString& version, const QUrl& url,
                       const QByteArray& compressedBody, qint64 sizeInstalled)
{
    CatalogEntry entry;
    entry.dictId = dictId;
    entry.name = dictId;
    entry.sourceLang = u"en"_s;
    entry.targetLang = u"en"_s;
    entry.kind = u"monolingual"_s;
    entry.version = version;
    entry.schemaVersion = Bundle::kSupportedSchemaVersion;
    entry.publisher = u"test"_s;
    entry.license = u"test"_s;
    entry.licenseUrl = u"http://example.invalid/"_s;
    entry.attribution = u"test"_s;
    entry.entryCount = 1;
    entry.sizeCompressed = compressedBody.size();
    entry.sizeInstalled = sizeInstalled;
    entry.sha256 =
        QString::fromLatin1(QCryptographicHash::hash(compressedBody, QCryptographicHash::Sha256).toHex());
    entry.url = url.toString();
    entry.builtAt = u"2026-09-27T00:00:00Z"_s;
    return entry;
}

DownloadStatus::State stateOf(const QList<DownloadStatus>& downloads, const QString& dictId)
{
    for (const DownloadStatus& status : downloads) {
        if (status.dictId == dictId) {
            return status.state;
        }
    }
    return DownloadStatus::Failed; // sentinel: "not found" never matches an expected state
}

} // namespace

class TestDictionaryManager : public QObject
{
    Q_OBJECT

private:
    QByteArray m_compressedFixture;
    qint64 m_installedSize = 0;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()),
                 "fixture bundle missing: run `make fixture` at the repository root first");
        QFile src(fixturePath());
        QVERIFY(src.open(QIODevice::ReadOnly));
        const QByteArray raw = src.readAll();
        m_installedSize = raw.size();
        m_compressedFixture = zstdCompress(raw);
        QVERIFY(!m_compressedFixture.isEmpty());
    }

    void catalogChangedFetchesAndCaches()
    {
        TestHttpServer server;
        const QByteArray catalogBody =
            QByteArrayLiteral("{\"catalog_version\":1,\"generated_at\":\"2026-09-27T00:00:00Z\","
                              "\"dictionaries\":[]}");
        server.addRoute(u"/catalog.json"_s,
                        {.body = catalogBody, .supportsRange = false, .splitAt = -1, .pauseMs = 0});

        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        QSignalSpy changed(&manager, &DictionaryManager::catalogChanged);

        manager.refreshCatalog(true);
        QVERIFY(changed.wait(5000));
        QCOMPARE(manager.catalog().catalogVersion, 1);
        QVERIFY(QFile::exists(QDir(cacheDir.path()).filePath(u"catalog.json"_s)));
    }

    void twoAtATimeLimitWithThreeInstalls()
    {
        TestHttpServer server;
        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));

        const CatalogEntry a = makeEntry(u"dict-a"_s, u"1"_s, server.urlFor(u"/a.odict"_s),
                                         m_compressedFixture, m_installedSize);
        const CatalogEntry b = makeEntry(u"dict-b"_s, u"1"_s, server.urlFor(u"/b.odict"_s),
                                         m_compressedFixture, m_installedSize);
        const CatalogEntry c = makeEntry(u"dict-c"_s, u"1"_s, server.urlFor(u"/c.odict"_s),
                                         m_compressedFixture, m_installedSize);

        manager.install(a);
        manager.install(b);
        manager.install(c);

        // No event loop has run yet: starting a download is synchronous, so
        // the queue's shape is already decided.
        const auto downloads = manager.downloads();
        QCOMPARE(downloads.size(), 3);
        QCOMPARE(stateOf(downloads, u"dict-a"_s), DownloadStatus::Downloading);
        QCOMPARE(stateOf(downloads, u"dict-b"_s), DownloadStatus::Downloading);
        QCOMPARE(stateOf(downloads, u"dict-c"_s), DownloadStatus::Waiting);

        // Freeing a slot lets the waiting one start.
        manager.cancel(u"dict-a"_s);
        QVERIFY(QTest::qWaitFor(
            [&] { return stateOf(manager.downloads(), u"dict-c"_s) == DownloadStatus::Downloading; }, 5000));
    }

    void cancelKeepsPartialFileForResume()
    {
        TestHttpServer server;
        TestHttpServer::Route route;
        route.body = m_compressedFixture;
        route.splitAt = m_compressedFixture.size() / 2;
        route.pauseMs = 2000; // long enough for the test to cancel first
        server.addRoute(u"/slow.odict"_s, route);

        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        const CatalogEntry entry = makeEntry(u"dict-slow"_s, u"1"_s, server.urlFor(u"/slow.odict"_s),
                                             m_compressedFixture, m_installedSize);
        manager.install(entry);

        QVERIFY(QTest::qWaitFor(
            [&] {
                const auto downloads = manager.downloads();
                return std::ranges::any_of(downloads, [](const DownloadStatus& status) {
                    return status.dictId == u"dict-slow"_s && status.received > 0;
                });
            },
            5000));

        QSignalSpy listChanged(&manager, &DictionaryManager::downloadsChanged);
        manager.cancel(u"dict-slow"_s);
        QCOMPARE(listChanged.size(), 1); // the window's download count follows
        QVERIFY(stateOf(manager.downloads(), u"dict-slow"_s) != DownloadStatus::Downloading);
        QCOMPARE(manager.downloads().size(), 0);

        const QString partPath = QDir(cacheDir.path()).filePath(u"downloads/dict-slow-1.odict.part"_s);
        QVERIFY(QFile::exists(partPath));
        QVERIFY(QFileInfo(partPath).size() > 0);
    }

    void notFoundReportsFailureMessage()
    {
        TestHttpServer server; // no route registered for /missing.odict
        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        const CatalogEntry entry = makeEntry(u"dict-404"_s, u"1"_s, server.urlFor(u"/missing.odict"_s),
                                             m_compressedFixture, m_installedSize);
        manager.install(entry);

        QVERIFY(QTest::qWaitFor(
            [&] { return stateOf(manager.downloads(), u"dict-404"_s) == DownloadStatus::Failed; }, 5000));
        const auto downloads = manager.downloads();
        const auto it = std::ranges::find_if(
            downloads, [](const DownloadStatus& s) { return s.dictId == u"dict-404"_s; });
        QVERIFY(it != downloads.end());
        QVERIFY(!it->error.isEmpty());
        QVERIFY(it->error.contains(u"404"_s));
    }

    void installedFiresAndFileInPlaceViaRangeResume()
    {
        TestHttpServer::Route route;
        route.body = m_compressedFixture;
        route.supportsRange = true;
        TestHttpServer server;
        server.addRoute(u"/sample-en.odict"_s, route);

        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;

        // Seed a partial download so the manager must resume with Range.
        const qsizetype splitAt = m_compressedFixture.size() / 3;
        const QString partPath =
            QDir(cacheDir.path()).filePath(u"downloads/sample-en-2026.09.1.odict.part"_s);
        QVERIFY(QDir().mkpath(QFileInfo(partPath).absolutePath()));
        {
            QFile part(partPath);
            QVERIFY(part.open(QIODevice::WriteOnly));
            part.write(m_compressedFixture.left(splitAt));
        }

        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        const CatalogEntry entry =
            makeEntry(u"sample-en"_s, u"2026.09.1"_s, server.urlFor(u"/sample-en.odict"_s),
                      m_compressedFixture, m_installedSize);

        QSignalSpy installedSpy(&manager, &DictionaryManager::installed);
        QSignalSpy listChanged(&manager, &DictionaryManager::downloadsChanged);
        manager.install(entry);
        QVERIFY(installedSpy.wait(10000));
        QCOMPARE(installedSpy.first().first().toString(), u"sample-en"_s);

        QVERIFY(server.sawRangeRequest(u"/sample-en.odict"_s));

        const QString installedPath = QDir(dictDir.path()).filePath(u"sample-en/2026.09.1/dict.sqlite"_s);
        QVERIFY(QFile::exists(installedPath));
        auto bundle = Bundle::open(installedPath);
        QVERIFY2(bundle.hasValue(), qPrintable(bundle.error()));
        QCOMPARE(bundle.value().meta().dictId, u"sample-en"_s);
        QCOMPARE(bundle.value().meta().version, u"2026.09.1"_s);

        QVERIFY(!QFile::exists(partPath));
        QCOMPARE(manager.downloads().size(), 0);
        QVERIFY(!listChanged.isEmpty()); // a finished download leaves the list with a signal
    }
};

QTEST_GUILESS_MAIN(TestDictionaryManager)
#include "tst_dictionary_manager.moc"
