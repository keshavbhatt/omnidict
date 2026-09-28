#include "core/bundle.h"
#include "core/settings.h"
#include "services/dictionary_manager.h"
#include "services/lookup_service.h"
#include "ui/dictionaries_dialog.h"
#include "ui/style.h"
#include "ui/switch_button.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <zstd.h>

using namespace Qt::StringLiterals;
using namespace omnidict::core;
using namespace omnidict::services;
using namespace omnidict::ui;

namespace {

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

/// A tiny single-threaded HTTP/1.1 server, trimmed from tst_dictionary_manager.cpp's:
/// enough to serve a `catalog.json` and a plain `.odict` body. 127.0.0.1 only.
class TestHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit TestHttpServer(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &TestHttpServer::acceptConnections);
        const bool listening = m_server.listen(QHostAddress::LocalHost);
        Q_ASSERT(listening);
    }

    void addRoute(const QString& path, const QByteArray& body) { m_routes.insert(path, body); }

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
            return;
        }
        const QByteArray header = buffer.left(headerEnd);
        m_buffers.remove(socket);

        QString path;
        const QList<QByteArray> lines = header.split('\n');
        if (!lines.isEmpty()) {
            const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
            if (requestLine.size() >= 2) {
                path = QString::fromLatin1(requestLine.at(1));
            }
        }
        if (!m_routes.contains(path)) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->flush();
            socket->disconnectFromHost();
            return;
        }
        const QByteArray body = m_routes.value(path);
        QByteArray head = "HTTP/1.1 200 OK\r\n";
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        head += "Connection: close\r\n\r\n";
        socket->write(head);
        socket->write(body);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QString, QByteArray> m_routes;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

QJsonObject manifest(const QString& dictId, const QString& name, const QString& version,
                     const QString& sourceLang, const QString& targetLang, const QString& publisher,
                     const QString& url, const QByteArray& compressedBody, qint64 sizeInstalled = 0)
{
    return QJsonObject{
        {u"dict_id"_s, dictId},
        {u"name"_s, name},
        {u"source_lang"_s, sourceLang},
        {u"target_lang"_s, targetLang},
        {u"kind"_s, u"bilingual"_s},
        {u"version"_s, version},
        {u"schema_version"_s, Bundle::kSupportedSchemaVersion},
        {u"publisher"_s, publisher},
        {u"license"_s, u"CC-BY-SA-4.0"_s},
        {u"license_url"_s, u"https://creativecommons.org/licenses/by-sa/4.0/"_s},
        {u"attribution"_s, u"test fixture"_s},
        {u"entry_count"_s, 1},
        {u"size_compressed"_s, static_cast<qint64>(compressedBody.size())},
        {u"size_installed"_s, sizeInstalled},
        {u"sha256"_s,
         QString::fromLatin1(QCryptographicHash::hash(compressedBody, QCryptographicHash::Sha256).toHex())},
        {u"url"_s, url},
        {u"built_at"_s, u"2026-09-27T00:00:00Z"_s},
    };
}

QByteArray catalogJson(const QJsonArray& dictionaries)
{
    const QJsonObject root{
        {u"catalog_version"_s, 1},
        {u"generated_at"_s, u"2026-09-27T00:00:00Z"_s},
        {u"dictionaries"_s, dictionaries},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

DictionaryInfo makeInstalled(const QString& dictId, const QString& name, const QString& version,
                             const QString& path, const QString& publisher = u"Wiktionary"_s)
{
    DictionaryInfo info;
    info.dictId = dictId;
    info.name = name;
    info.attribution = u"test fixture"_s;
    info.license = u"CC-BY-SA-4.0"_s;
    info.version = version;
    info.entryCount = 1000;
    info.publisher = publisher;
    info.sourceLang = u"es"_s;
    info.targetLang = u"en"_s;
    info.path = path;
    return info;
}

/// Writes `bytes` bytes of filler so QFileInfo(path).size() reports something.
void writeDummyFile(const QString& path, qint64 bytes = 4096)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(static_cast<qsizetype>(bytes), 'x'));
}

/// Renders `widget` to a PNG under OMNIDICT_GRAB_DIR; the caller checks that
/// variable is set before calling this (tst_sheets.cpp does the same).
void grab(QWidget* widget, const QString& name)
{
    const QString dir = QString::fromUtf8(qgetenv("OMNIDICT_GRAB_DIR"));
    QDir().mkpath(dir);
    widget->show();
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    widget->grab().save(dir + u'/' + name + u".png"_s);
    widget->hide();
}

} // namespace

/// GUI tests (offscreen) for the Dictionaries sheet.
class TestDictionariesDialog : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    [[nodiscard]] QString iniPath(const QString& name) const { return m_dir.filePath(name + u".ini"_s); }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(u"Omnidict"_s);
        QCoreApplication::setApplicationVersion(u"0.1.0"_s);
    }

    void installedTabListsInSettingsOrder()
    {
        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        const QString pathA = dictDir.filePath(u"a/1/dict.sqlite"_s);
        const QString pathB = dictDir.filePath(u"b/1/dict.sqlite"_s);
        const QString pathC = dictDir.filePath(u"c/1/dict.sqlite"_s);
        writeDummyFile(pathA);
        writeDummyFile(pathB);
        writeDummyFile(pathC);

        Settings settings(iniPath(u"order"_s));
        settings.setDictionaryOrder({u"b"_s, u"a"_s});

        DictionaryManager manager(dictDir.path(), cacheDir.path(),
                                  QUrl(u"http://127.0.0.1:1/catalog.json"_s));
        const QList<DictionaryInfo> installed{
            makeInstalled(u"a"_s, u"A Dictionary"_s, u"1"_s, pathA),
            makeInstalled(u"b"_s, u"B Dictionary"_s, u"1"_s, pathB),
            makeInstalled(u"c"_s, u"C Dictionary"_s, u"1"_s, pathC),
        };
        DictionariesDialog dialog(manager, settings, installed, dictDir.path());

        auto* list = dialog.findChild<QListWidget*>(u"installedList"_s);
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), u"b"_s);
        QCOMPARE(list->item(1)->data(Qt::UserRole).toString(), u"a"_s);
        QCOMPARE(list->item(2)->data(Qt::UserRole).toString(), u"c"_s);
    }

    void togglingSwitchWritesDisabledDictionaries()
    {
        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        const QString path = dictDir.filePath(u"a/1/dict.sqlite"_s);
        writeDummyFile(path);

        Settings settings(iniPath(u"toggle"_s));
        DictionaryManager manager(dictDir.path(), cacheDir.path(),
                                  QUrl(u"http://127.0.0.1:1/catalog.json"_s));
        const QList<DictionaryInfo> installed{makeInstalled(u"a"_s, u"A Dictionary"_s, u"1"_s, path)};
        DictionariesDialog dialog(manager, settings, installed, dictDir.path());

        auto* toggle = dialog.findChild<SwitchButton*>(u"dictSwitch_a"_s);
        QVERIFY(toggle != nullptr);
        QVERIFY(toggle->isChecked());
        toggle->click();
        QVERIFY(settings.disabledDictionaries().contains(u"a"_s));
        toggle->click();
        QVERIFY(!settings.disabledDictionaries().contains(u"a"_s));
    }

    void movingRowWithAltDownWritesDictionaryOrder()
    {
        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        const QString pathA = dictDir.filePath(u"a/1/dict.sqlite"_s);
        const QString pathB = dictDir.filePath(u"b/1/dict.sqlite"_s);
        writeDummyFile(pathA);
        writeDummyFile(pathB);

        Settings settings(iniPath(u"move"_s));
        DictionaryManager manager(dictDir.path(), cacheDir.path(),
                                  QUrl(u"http://127.0.0.1:1/catalog.json"_s));
        const QList<DictionaryInfo> installed{
            makeInstalled(u"a"_s, u"A Dictionary"_s, u"1"_s, pathA),
            makeInstalled(u"b"_s, u"B Dictionary"_s, u"1"_s, pathB),
        };
        DictionariesDialog dialog(manager, settings, installed, dictDir.path());

        auto* list = dialog.findChild<QListWidget*>(u"installedList"_s);
        QVERIFY(list != nullptr);
        list->setCurrentRow(0); // "a", the first row by name
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), u"a"_s);

        QTest::keyClick(list, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(settings.dictionaryOrder(), (QStringList{u"b"_s, u"a"_s}));
    }

    void newerCatalogVersionShowsUpdateOnInstalledRow()
    {
        TestHttpServer server;
        const QByteArray body = QByteArrayLiteral("odict body");
        server.addRoute(
            u"/catalog.json"_s,
            catalogJson(QJsonArray{manifest(u"a"_s, u"A Dictionary"_s, u"2026.10.1"_s, u"es"_s, u"en"_s,
                                            u"Wiktionary"_s, u"http://example.invalid/a"_s, body)}));

        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        const QString path = dictDir.filePath(u"a/2026.09.1/dict.sqlite"_s);
        writeDummyFile(path);

        Settings settings(iniPath(u"update"_s));
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        QSignalSpy catalogChanged(&manager, &DictionaryManager::catalogChanged);
        const QList<DictionaryInfo> installed{makeInstalled(u"a"_s, u"A Dictionary"_s, u"2026.09.1"_s, path)};
        DictionariesDialog dialog(manager, settings, installed, dictDir.path());

        QVERIFY(catalogChanged.wait(5000));
        auto* update = dialog.findChild<QPushButton*>(u"installedUpdateButton_a"_s);
        QVERIFY(update != nullptr);
    }

    void refreshReadsTheCatalogueAgain()
    {
        TestHttpServer server;
        const QByteArray body = QByteArrayLiteral("odict body");
        server.addRoute(
            u"/catalog.json"_s,
            catalogJson(QJsonArray{manifest(u"a"_s, u"A Dictionary"_s, u"2026.10.1"_s, u"es"_s, u"en"_s,
                                            u"Wiktionary"_s, u"http://example.invalid/a"_s, body)}));
        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        Settings settings(iniPath(u"refresh"_s));
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        QSignalSpy catalogChanged(&manager, &DictionaryManager::catalogChanged);
        DictionariesDialog dialog(manager, settings, {}, dictDir.path());
        QVERIFY(catalogChanged.wait(5000)); // the read on opening

        auto* refresh = dialog.findChild<QPushButton*>(u"refreshCatalogButton"_s);
        auto* footer = dialog.findChild<QLabel*>(u"availableFooter"_s);
        QVERIFY(refresh != nullptr && footer != nullptr);
        QVERIFY(footer->text().contains(u"just now"_s));
        refresh->click();
        QVERIFY(!refresh->isEnabled()); // Refreshing... until the answer comes
        QVERIFY(catalogChanged.wait(5000));
        QVERIFY(refresh->isEnabled());
        QCOMPARE(refresh->text(), u"Refresh"_s);
        QCOMPARE(catalogChanged.size(), 2);
    }

    void availableTabLanguageFilterHidesRows()
    {
        TestHttpServer server;
        const QByteArray body = QByteArrayLiteral("odict body");
        server.addRoute(u"/catalog.json"_s,
                        catalogJson(QJsonArray{
                            manifest(u"fr-en"_s, u"French - English"_s, u"1"_s, u"fr"_s, u"en"_s,
                                     u"Wiktionary"_s, u"http://example.invalid/fr"_s, body),
                            manifest(u"de-en"_s, u"German - English"_s, u"1"_s, u"de"_s, u"en"_s,
                                     u"Wiktionary"_s, u"http://example.invalid/de"_s, body),
                        }));

        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        Settings settings(iniPath(u"filter"_s));
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        QSignalSpy catalogChanged(&manager, &DictionaryManager::catalogChanged);
        DictionariesDialog dialog(manager, settings, {}, dictDir.path());
        dialog.showAvailable();

        QVERIFY(catalogChanged.wait(5000));
        QVERIFY(dialog.findChild<QWidget*>(u"availableRow_fr-en"_s) != nullptr);
        QVERIFY(dialog.findChild<QWidget*>(u"availableRow_de-en"_s) != nullptr);

        auto* fromCombo = dialog.findChild<QComboBox*>(u"fromLanguage"_s);
        QVERIFY(fromCombo != nullptr);
        const int frIndex = fromCombo->findData(u"fr"_s);
        QVERIFY(frIndex >= 0);
        fromCombo->setCurrentIndex(frIndex);

        QVERIFY(dialog.findChild<QWidget*>(u"availableRow_fr-en"_s) != nullptr);
        QVERIFY(dialog.findChild<QWidget*>(u"availableRow_de-en"_s) == nullptr);

        // English is "English", not the "American English" Qt gives the bare code.
        auto* toCombo = dialog.findChild<QComboBox*>(u"toLanguage"_s);
        QVERIFY(toCombo != nullptr);
        QCOMPARE(toCombo->itemText(toCombo->findData(u"en"_s)), u"English"_s);
        QCOMPARE(toCombo->maxVisibleItems(), 12);
    }

    void downloadingKeepsTheListInPlace()
    {
        // Forty rows; the reader scrolls to the last and downloads it. The list must
        // stay where it is and the other rows must not be rebuilt.
        QFile fixture(QString::fromUtf8(OMNIDICT_FIXTURE_BUNDLE));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray raw = fixture.readAll();
        const QByteArray compressed = zstdCompress(raw);
        TestHttpServer server;
        server.addRoute(u"/last.odict"_s, compressed);
        QJsonArray entries;
        for (int i = 0; i < 40; ++i) {
            const QString id = u"d%1"_s.arg(i, 2, 10, QLatin1Char('0'));
            entries.append(manifest(id, u"Dictionary %1"_s.arg(id), u"1"_s, u"en"_s, u"en"_s, u"Test"_s,
                                    server.urlFor(u"/last.odict"_s).toString(), compressed, raw.size()));
        }
        server.addRoute(u"/catalog.json"_s, catalogJson(entries));
        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        Settings settings(iniPath(u"inplace"_s));
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        QSignalSpy catalogChanged(&manager, &DictionaryManager::catalogChanged);
        DictionariesDialog dialog(manager, settings, {}, dictDir.path());
        dialog.resize(760, 560);
        dialog.show();
        dialog.showAvailable();
        QVERIFY(catalogChanged.wait(5000));
        QCoreApplication::processEvents();

        auto* scroll = dialog.findChild<QScrollArea*>(u"availableScroll"_s);
        QVERIFY(scroll != nullptr);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0); // rows laid out
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        const int position = scroll->verticalScrollBar()->value();
        QVERIFY(position > 0);
        auto* firstRow = dialog.findChild<QWidget*>(u"availableRow_d00"_s);
        QVERIFY(firstRow != nullptr);

        QSignalSpy downloadChanged(&manager, &DictionaryManager::downloadChanged);
        dialog.findChild<QPushButton*>(u"downloadButton_d39"_s)->click();
        QVERIFY(downloadChanged.wait(5000));
        QTest::qWait(50); // a rebuild would restore the position on the next pass
        QCOMPARE(scroll->verticalScrollBar()->value(), position);
        QCOMPARE(dialog.findChild<QWidget*>(u"availableRow_d00"_s), firstRow); // not rebuilt
    }

    void clickingDownloadEndsWithInstalledState()
    {
        QFile fixture(QString::fromUtf8(OMNIDICT_FIXTURE_BUNDLE));
        QVERIFY2(fixture.exists(), "fixture bundle missing: run `make fixture` at the repository root first");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray raw = fixture.readAll();
        const QByteArray compressed = zstdCompress(raw);
        QVERIFY(!compressed.isEmpty());

        TestHttpServer server;
        server.addRoute(u"/sample-en.odict"_s, compressed);
        server.addRoute(
            u"/catalog.json"_s,
            catalogJson(QJsonArray{
                manifest(u"sample-en"_s, u"Sample English"_s, u"2026.09.1"_s, u"en"_s, u"en"_s, u"Sample"_s,
                         server.urlFor(u"/sample-en.odict"_s).toString(), compressed, raw.size())}));

        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        Settings settings(iniPath(u"download"_s));
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        QSignalSpy catalogChanged(&manager, &DictionaryManager::catalogChanged);
        DictionariesDialog dialog(manager, settings, {}, dictDir.path());
        dialog.showAvailable();
        QVERIFY(catalogChanged.wait(5000));

        auto* download = dialog.findChild<QPushButton*>(u"downloadButton_sample-en"_s);
        QVERIFY(download != nullptr);
        QSignalSpy downloadChanged(&manager, &DictionaryManager::downloadChanged);
        QSignalSpy installedSpy(&manager, &DictionaryManager::installed);
        download->click();
        QVERIFY(!downloadChanged.isEmpty() || downloadChanged.wait(5000));
        QVERIFY(installedSpy.wait(10000));

        QVERIFY(dialog.findChild<QPushButton*>(u"downloadButton_sample-en"_s) == nullptr);
        bool foundInstalledLabel = false;
        for (const QLabel* label : dialog.findChildren<QLabel*>()) {
            if (label->text() == u"Installed"_s) {
                foundInstalledLabel = true;
            }
        }
        QVERIFY(foundInstalledLabel);
    }

    void removeAbsentForDictionaryOutsideRoot()
    {
        QTemporaryDir dictDir;   // dictionariesRoot
        QTemporaryDir elsewhere; // simulates a --bundles directory
        const QString path = elsewhere.filePath(u"a/1/dict.sqlite"_s);
        writeDummyFile(path);

        Settings settings(iniPath(u"outside"_s));
        DictionaryManager manager(dictDir.path(), dictDir.path(), QUrl(u"http://127.0.0.1:1/catalog.json"_s));
        const QList<DictionaryInfo> installed{makeInstalled(u"a"_s, u"A Dictionary"_s, u"1"_s, path)};
        DictionariesDialog dialog(manager, settings, installed, dictDir.path());

        auto* menu = dialog.findChild<QMenu*>(u"moreMenu_a"_s);
        QVERIFY(menu != nullptr);
        QVERIFY(menu->findChild<QAction*>(u"removeAction_a"_s) == nullptr);
        QVERIFY(!menu->actions().isEmpty()); // "Show in folder..." is still offered
    }

    /// Grabs both tabs to a PNG in both themes for visual review; a no-op
    /// unless OMNIDICT_GRAB_DIR is set.
    void visualGrabs()
    {
        if (qEnvironmentVariableIsEmpty("OMNIDICT_GRAB_DIR")) {
            QSKIP("set OMNIDICT_GRAB_DIR to grab screenshots");
        }
        QTemporaryDir dictDir;
        QTemporaryDir cacheDir;
        const QString pathEs = dictDir.filePath(u"wikt-es-en/1/dict.sqlite"_s);
        const QString pathEn = dictDir.filePath(u"wikt-en/1/dict.sqlite"_s);
        const QString pathHi = dictDir.filePath(u"wikt-hi-en/1/dict.sqlite"_s);
        // Small filler files: the machine this runs on is short on disk, and
        // the exact on-disk size shown does not matter for a visual review.
        writeDummyFile(pathEs);
        writeDummyFile(pathEn);
        writeDummyFile(pathHi);

        TestHttpServer server;
        const QByteArray body = QByteArrayLiteral("odict body");
        server.addRoute(u"/catalog.json"_s,
                        catalogJson(QJsonArray{
                            manifest(u"wikt-en"_s, u"English"_s, u"2026.10.04"_s, u"en"_s, u"en"_s,
                                     u"Wiktionary"_s, u"http://example.invalid/en"_s, body),
                            manifest(u"wikt-fr-en"_s, u"French - English"_s, u"1"_s, u"fr"_s, u"en"_s,
                                     u"Wiktionary"_s, u"http://example.invalid/fr"_s, body),
                        }));

        Settings settings(iniPath(u"grabs"_s));
        settings.setDisabledDictionaries({u"wikt-hi-en"_s});
        DictionaryManager manager(dictDir.path(), cacheDir.path(), server.urlFor(u"/catalog.json"_s));
        QSignalSpy catalogChanged(&manager, &DictionaryManager::catalogChanged);
        manager.refreshCatalog(true);
        QVERIFY(catalogChanged.wait(5000)); // fetched once; each dialog below reads the cached result
        const QList<DictionaryInfo> installed{
            makeInstalled(u"wikt-es-en"_s, u"Spanish - English"_s, u"2026.09.27"_s, pathEs),
            makeInstalled(u"wikt-en"_s, u"English"_s, u"2026.09.20"_s, pathEn),
            makeInstalled(u"wikt-hi-en"_s, u"Hindi - English"_s, u"2026.08.01"_s, pathHi),
        };

        for (const bool dark : {false, true}) {
            Tokens::setCurrentScheme(dark);
            qApp->setPalette(paletteFor(Tokens::current()));
            qApp->setStyleSheet(styleSheetFor(Tokens::current()));
            const QString suffix = dark ? u"-dark"_s : u"-light"_s;

            DictionariesDialog dialog(manager, settings, installed, dictDir.path());
            grab(&dialog, u"dictionaries"_s + suffix);
            dialog.showAvailable();
            grab(&dialog, u"dictionaries-available"_s + suffix);
        }
    }
};

QTEST_MAIN(TestDictionariesDialog)
#include "tst_dictionaries_dialog.moc"
