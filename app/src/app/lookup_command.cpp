#include "app/lookup_command.h"

#include "core/bundle.h"

#include <QCoreApplication>
#include <QTextStream>

using namespace Qt::StringLiterals;

namespace omnidict::app {

namespace {
constexpr int kFound = 0;
constexpr int kNotFound = 1;
constexpr int kBundleUnusable = 2;
constexpr int kSuggestionLimit = 10;
} // namespace

int runLookup(const QString& bundlePath, const QString& word, QTextStream& out, QTextStream& err)
{
    const auto bundle = core::Bundle::open(bundlePath);
    if (!bundle) {
        err << bundle.error() << Qt::endl;
        return kBundleUnusable;
    }

    QList<core::EntryPreview> rows = bundle.value().lookupExact(word);
    if (rows.isEmpty()) {
        rows = bundle.value().searchPrefix(word, kSuggestionLimit);
    }
    if (rows.isEmpty()) {
        err << QCoreApplication::translate("lookup", "Nothing found for \"%1\" in %2.")
                   .arg(word, bundle.value().meta().name)
            << Qt::endl;
        return kNotFound;
    }
    for (const core::EntryPreview& row : std::as_const(rows)) {
        out << row.headword << u": "_s << row.preview << Qt::endl;
    }
    return kFound;
}

} // namespace omnidict::app
