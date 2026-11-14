#pragma once

#include <QString>

#include <chrono>
#include <optional>

namespace omnidict::services {

/// The text selected in the app that has the keyboard focus, for quick lookup on Windows, where
/// no app can read another's selection the way X11's primary selection allows
/// (mocks/windows.html note 5, DOCS/windows-selection.md). Asks that app through UI Automation:
/// nothing is written to the clipboard and no keys are sent. Waits at most `timeout` for an app
/// that is slow to answer; call it before Omnidict takes the focus. Empty when the app gives no
/// selection, when Omnidict itself has the focus, and on every other system.
[[nodiscard]] std::optional<QString> focusedSelection(std::chrono::milliseconds timeout);

} // namespace omnidict::services
