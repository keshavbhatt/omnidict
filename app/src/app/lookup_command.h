#pragma once

#include <QString>

class QTextStream;

namespace omnidict::app {

/// Looks `word` up in the bundle at `bundlePath` and writes what it finds to
/// `out`, one entry per line; problems go to `err`. Headless check of the
/// bundle and lookup path, no window involved.
///
/// Returns the process exit code: 0 found, 1 nothing found, 2 bundle unusable.
[[nodiscard]] int runLookup(const QString& bundlePath, const QString& word, QTextStream& out,
                            QTextStream& err);

} // namespace omnidict::app
