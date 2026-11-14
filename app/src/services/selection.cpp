#include "services/selection.h"

namespace omnidict::services {

#ifndef Q_OS_WIN
// Elsewhere the popup reads the selection itself (QClipboard::Selection, ui::QuickLookup).
std::optional<QString> focusedSelection(std::chrono::milliseconds /*timeout*/)
{
    return std::nullopt;
}
#endif

} // namespace omnidict::services
