#include "core/installer.h"

#include "core/bundle.h"
#include "core/logging.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <memory>
#include <utility>
#include <zstd.h>

using namespace Qt::StringLiterals;

namespace omnidict::core {

namespace {

constexpr qint64 kHashChunkSize = 1 << 20; // 1 MiB, matches the pipeline's own chunk size.
const QString kPartSuffix = u".part"_s;
const QString kBundleFile = u"dict.sqlite"_s;

struct ZstdDStreamCloser
{
    void operator()(ZSTD_DStream* stream) const { ZSTD_freeDStream(stream); }
};
using ZstdDStreamPtr = std::unique_ptr<ZSTD_DStream, ZstdDStreamCloser>;

/// Streams `path` once, returning its size and lowercase hex sha256.
[[nodiscard]] Result<std::pair<qint64, QString>> hashFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Error{u"cannot open %1: %2"_s.arg(path, file.errorString())};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(static_cast<qsizetype>(kHashChunkSize), '\0');
    qint64 size = 0;
    while (!file.atEnd()) {
        const qint64 read = file.read(buffer.data(), buffer.size());
        if (read < 0) {
            return Error{u"cannot read %1: %2"_s.arg(path, file.errorString())};
        }
        hash.addData(QByteArrayView(buffer.constData(), read));
        size += read;
    }
    return std::pair{size, QString::fromLatin1(hash.result().toHex())};
}

/// Streams `inputPath` (zstd-compressed) into `outputPath`, which is created
/// or truncated. Returns the decompressed byte count.
[[nodiscard]] Result<qint64> decompressFile(const QString& inputPath, const QString& outputPath)
{
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) {
        return Error{u"cannot open %1: %2"_s.arg(inputPath, input.errorString())};
    }
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Error{u"cannot create %1: %2"_s.arg(outputPath, output.errorString())};
    }

    const ZstdDStreamPtr stream(ZSTD_createDStream());
    if (!stream) {
        return Error{u"cannot create a zstd decompression stream"_s};
    }
    const size_t initResult = ZSTD_initDStream(stream.get());
    if (ZSTD_isError(initResult) != 0U) {
        return Error{u"zstd init failed: %1"_s.arg(QString::fromLatin1(ZSTD_getErrorName(initResult)))};
    }

    QByteArray inBuffer(static_cast<qsizetype>(ZSTD_DStreamInSize()), '\0');
    QByteArray outBuffer(static_cast<qsizetype>(ZSTD_DStreamOutSize()), '\0');
    qint64 totalOut = 0;
    size_t lastResult = 0;

    while (!input.atEnd()) {
        const qint64 read = input.read(inBuffer.data(), inBuffer.size());
        if (read < 0) {
            return Error{u"cannot read %1: %2"_s.arg(inputPath, input.errorString())};
        }
        ZSTD_inBuffer inDesc{.src = inBuffer.constData(), .size = static_cast<size_t>(read), .pos = 0};
        while (inDesc.pos < inDesc.size) {
            ZSTD_outBuffer outDesc{
                .dst = outBuffer.data(), .size = static_cast<size_t>(outBuffer.size()), .pos = 0};
            lastResult = ZSTD_decompressStream(stream.get(), &outDesc, &inDesc);
            if (ZSTD_isError(lastResult) != 0U) {
                return Error{u"zstd decompression failed: %1"_s.arg(
                    QString::fromLatin1(ZSTD_getErrorName(lastResult)))};
            }
            if (outDesc.pos > 0) {
                const auto toWrite = static_cast<qint64>(outDesc.pos);
                if (output.write(outBuffer.constData(), toWrite) != toWrite) {
                    return Error{u"cannot write %1: %2"_s.arg(outputPath, output.errorString())};
                }
                totalOut += toWrite;
            }
        }
    }
    if (lastResult != 0) {
        return Error{u"%1: truncated or incomplete zstd stream"_s.arg(inputPath)};
    }
    return totalOut;
}

/// Removes `path` if it exists and is now an empty directory, quietly.
void removeIfEmptyDir(const QString& path)
{
    QDir dir(path);
    if (dir.exists() && dir.isEmpty()) {
        dir.removeRecursively();
    }
}

/// Checks the plain, streamed size and sha256 of `odictPath` against `entry`,
/// before anything is written to `dictionariesRoot`.
Result<bool> verifyDownload(const QString& odictPath, const CatalogEntry& entry)
{
    const QFileInfo odictInfo(odictPath);
    if (!odictInfo.isFile()) {
        return Error{u"installBundle: %1: no such file"_s.arg(odictPath)};
    }
    if (odictInfo.size() != entry.sizeCompressed) {
        return Error{
            u"installBundle: %1: size mismatch: catalogue says %2 bytes, file is %3 bytes"_s.arg(entry.dictId)
                .arg(entry.sizeCompressed)
                .arg(odictInfo.size())};
    }
    auto hashed = hashFile(odictPath);
    if (!hashed) {
        return Error{hashed.error()};
    }
    if (hashed.value().second.compare(entry.sha256, Qt::CaseInsensitive) != 0) {
        return Error{u"installBundle: %1: checksum mismatch: catalogue says %2, file hashes to %3"_s.arg(
            entry.dictId, entry.sha256, hashed.value().second)};
    }
    return true;
}

/// Opens the freshly decompressed `partPath` and checks its meta against
/// `entry`, closing it again before returning: the caller renames or removes
/// the same path right after, and the connection must be gone by then.
Result<bool> verifyDecompressed(const QString& partPath, const CatalogEntry& entry)
{
    QString openedDictId;
    QString openedVersion;
    {
        auto opened = Bundle::open(partPath);
        if (!opened) {
            return Error{u"installBundle: %1: %2"_s.arg(entry.dictId, opened.error())};
        }
        openedDictId = opened.value().meta().dictId;
        openedVersion = opened.value().meta().version;
    }
    if (openedDictId != entry.dictId) {
        return Error{u"installBundle: dict_id mismatch: catalogue says %1, bundle says %2"_s.arg(
            entry.dictId, openedDictId)};
    }
    if (openedVersion != entry.version) {
        return Error{u"installBundle: version mismatch: catalogue says %1, bundle says %2"_s.arg(
            entry.version, openedVersion)};
    }
    return true;
}

/// Removes every version directory of `entry.dictId` under `root` except
/// `entry.version`, once it is the one just installed.
void pruneOtherVersions(const QDir& root, const CatalogEntry& entry)
{
    const QDir dictDir(root.filePath(entry.dictId));
    const QStringList versions = dictDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& version : versions) {
        if (version != entry.version) {
            QDir(dictDir.filePath(version)).removeRecursively();
        }
    }
}

} // namespace

Result<QString> installBundle(const QString& odictPath, const CatalogEntry& entry,
                              const QString& dictionariesRoot)
{
    auto verifiedDownload = verifyDownload(odictPath, entry);
    if (!verifiedDownload) {
        return Error{verifiedDownload.error()};
    }

    const QDir root(dictionariesRoot);
    const QString versionDirPath = root.filePath(entry.dictId + u'/' + entry.version);
    if (!QDir().mkpath(versionDirPath)) {
        return Error{u"installBundle: cannot create %1"_s.arg(versionDirPath)};
    }
    const QDir versionDir(versionDirPath);
    const QString partPath = versionDir.filePath(kBundleFile + kPartSuffix);
    const QString finalPath = versionDir.filePath(kBundleFile);
    QFile::remove(partPath); // a leftover from an earlier, failed attempt

    // A failure past this point removes the partial output and, if that was
    // the only thing in it, the version directory too, leaving any
    // previously installed version of this dictionary exactly as it was.
    const auto fail = [&](const QString& message) -> Result<QString> {
        QFile::remove(partPath);
        removeIfEmptyDir(versionDirPath);
        return Error{message};
    };

    auto decompressed = decompressFile(odictPath, partPath);
    if (!decompressed) {
        return fail(decompressed.error());
    }
    if (decompressed.value() != entry.sizeInstalled) {
        return fail(u"installBundle: %1: decompressed size mismatch: catalogue says %2 bytes, got %3 bytes"_s
                        .arg(entry.dictId)
                        .arg(entry.sizeInstalled)
                        .arg(decompressed.value()));
    }
    auto verified = verifyDecompressed(partPath, entry);
    if (!verified) {
        return fail(verified.error());
    }

    QFile::remove(finalPath);
    if (!QFile::rename(partPath, finalPath)) {
        return fail(u"installBundle: cannot rename %1 to %2"_s.arg(partPath, finalPath));
    }

    pruneOtherVersions(root, entry);
    qCInfo(lcInstaller) << "installed" << entry.dictId << entry.version << "at" << finalPath;
    return finalPath;
}

Result<bool> removeInstalled(const QString& dictId, const QString& dictionariesRoot)
{
    const QString dictDirPath = QDir(dictionariesRoot).filePath(dictId);
    QDir dictDir(dictDirPath);
    if (!dictDir.exists()) {
        return false;
    }
    if (!dictDir.removeRecursively()) {
        return Error{u"removeInstalled: cannot remove %1"_s.arg(dictDirPath)};
    }
    qCInfo(lcInstaller) << "removed" << dictId;
    return true;
}

qint64 installedSize(const QString& dictId, const QString& dictionariesRoot)
{
    const QString dictDirPath = QDir(dictionariesRoot).filePath(dictId);
    qint64 total = 0;
    QDirIterator it(dictDirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

} // namespace omnidict::core
