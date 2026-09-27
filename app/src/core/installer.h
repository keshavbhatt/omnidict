#pragma once

#include "core/catalog.h"
#include "core/result.h"

#include <QString>

namespace omnidict::core {

/// Installs and removes downloaded dictionary bundles (PLAN.md 5.3). Pure
/// logic: no Qt Network, no threads of its own, so it is testable with a
/// plain file on disk. `DictionaryManager` runs `installBundle` off the GUI
/// thread (`QThreadPool`, ADR-004).
///
/// Layout on disk: `<dictionariesRoot>/<dictId>/<version>/dict.sqlite`, the
/// layout `core::Library::discover` already reads.

/// Verifies `odictPath` against `entry` (size, then a streamed sha256),
/// zstd-decompresses it into `<dictionariesRoot>/<entry.dictId>/<entry.version>/dict.sqlite`,
/// opens the result with `Bundle::open` and checks its `dict_id` and
/// `version` match `entry`, then removes every other installed version of
/// `entry.dictId`. On any failure the partial output is removed and whatever
/// was installed before this call (if anything) is left exactly as it was.
/// Returns the path to the installed `dict.sqlite`.
[[nodiscard]] Result<QString> installBundle(const QString& odictPath, const CatalogEntry& entry,
                                            const QString& dictionariesRoot);

/// Removes every installed version of `dictId` under `dictionariesRoot`.
/// The value is true when something was removed, false when the dictionary
/// was not installed; only an actual removal failure (for example a
/// permission error) is an Error.
[[nodiscard]] Result<bool> removeInstalled(const QString& dictId, const QString& dictionariesRoot);

/// Total bytes `dictId` occupies under `dictionariesRoot` (0 when not installed).
[[nodiscard]] qint64 installedSize(const QString& dictId, const QString& dictionariesRoot);

} // namespace omnidict::core
