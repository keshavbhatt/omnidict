#pragma once

namespace omnidict::ui {
class MainWindow;
}

namespace omnidict::app {

/// Headless checks for development (the rewrite kit's debug hooks), all off
/// unless their environment variable is set:
///   OMNIDICT_DEBUG_QUERY=word          type `word` once the dictionaries are open
///   OMNIDICT_DEBUG_GRAB=/path/shot.png save the window once an entry shows, then quit
///   OMNIDICT_DEBUG_OPEN=about          open the About dialog (grabbed instead of the window)
///   OMNIDICT_DEBUG_WINDOW_SIZE=1000x680
void installDebugHooks(ui::MainWindow& window);

} // namespace omnidict::app
