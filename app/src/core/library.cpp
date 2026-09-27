#include "core/library.h"

#include "core/logging.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace omnidict::core {

namespace {

const QString kBundleFile = u"dict.sqlite"_s;

/// `2026.10.1` after `2026.9.2`: numeric per component.
bool versionLess(const QString& a, const QString& b)
{
    const QStringList left = a.split(u'.');
    const QStringList right = b.split(u'.');
    for (qsizetype i = 0; i < std::max(left.size(), right.size()); ++i) {
        const int l = left.value(i).toInt();
        const int r = right.value(i).toInt();
        if (l != r) {
            return l < r;
        }
    }
    return false;
}

/// The bundle file of one dictionary directory: its own, else its highest version's.
QString bundleFileIn(const QDir& dictDir)
{
    if (QFileInfo::exists(dictDir.filePath(kBundleFile))) {
        return dictDir.filePath(kBundleFile);
    }
    QStringList versions;
    const QStringList subdirs = dictDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& version : subdirs) {
        if (QFileInfo::exists(dictDir.filePath(version + u'/' + kBundleFile))) {
            versions << version;
        }
    }
    if (versions.isEmpty()) {
        return {};
    }
    std::ranges::sort(versions, versionLess);
    return dictDir.filePath(versions.last() + u'/' + kBundleFile);
}

} // namespace

Library Library::discover(const QStringList& roots)
{
    Library library;
    QSet<QString> seen;
    for (const QString& root : roots) {
        const QDir rootDir(root);
        const QStringList dictDirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& name : dictDirs) {
            const QString path = bundleFileIn(QDir(rootDir.filePath(name)));
            if (path.isEmpty()) {
                continue;
            }
            auto bundle = Bundle::open(path);
            if (!bundle) {
                library.m_problems << bundle.error();
                qCWarning(lcBundle) << "skipping" << path << ":" << bundle.error();
                continue;
            }
            const QString dictId = bundle.value().meta().dictId;
            if (seen.contains(dictId)) {
                continue;
            }
            seen.insert(dictId);
            library.m_bundles.push_back(bundle.take());
        }
    }
    std::ranges::sort(library.m_bundles, [](const Bundle& a, const Bundle& b) {
        return QString::localeAwareCompare(a.meta().name, b.meta().name) < 0;
    });
    qCInfo(lcBundle) << "library has" << library.m_bundles.size() << "dictionaries";
    return library;
}

const Bundle* Library::find(const QString& dictId) const
{
    const auto it =
        std::ranges::find_if(m_bundles, [&](const Bundle& bundle) { return bundle.meta().dictId == dictId; });
    return it != m_bundles.cend() ? &*it : nullptr;
}

} // namespace omnidict::core
