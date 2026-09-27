#pragma once

#include <QString>

namespace omnidict::core {

/// The lookup form of a headword or query (DOCS/schema.md, ADR-005):
/// NFKC, case fold, combining marks stripped where the base letter is Latin,
/// Cyrillic or Greek (never for scripts where the marks carry meaning),
/// whitespace collapsed and trimmed.
///
/// Must stay identical to `omnipipe.normalize.normalize_headword`; both are
/// tested against tests/normalize_vectors.json. Pure, tested.
[[nodiscard]] QString normalizeHeadword(const QString& text);

} // namespace omnidict::core
