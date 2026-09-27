#pragma once

#include "core/entry.h"

#include <QString>

namespace omnidict::core {

/// The entry as the HTML of DOCS/entry-html.md, the markup every client
/// renders identically (PLAN D12). Definitions and examples are inserted as
/// stored (restricted HTML); every other value is escaped. User-facing words
/// go through QCoreApplication::translate, so without a translator loaded the
/// output is byte for byte the golden files in tests/golden/entries. Pure, tested.
[[nodiscard]] QString renderEntry(const Entry& entry);

} // namespace omnidict::core
