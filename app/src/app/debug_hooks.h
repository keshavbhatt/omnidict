#pragma once

namespace omnidict::services {
class DictionaryManager;
}

namespace omnidict::ui {
class MainWindow;
}

namespace omnidict::app {

/// Headless checks for development (the rewrite kit's debug hooks), all off
/// unless their environment variable is set:
///   OMNIDICT_DEBUG_QUERY=word          type `word` once the dictionaries are open
///   OMNIDICT_DEBUG_GRAB=/path/shot.png save the window once it settles after a search, then quit
///   OMNIDICT_DEBUG_OPEN=sheet          open a sheet and grab it instead of the window: about,
///                                      settings, shortcuts, whatsnew, bugreport, dictionaries, available
///   OMNIDICT_DEBUG_INSTALL=dict_id     install that dictionary from the catalogue, then quit
///                                      (exit 0 once the window has it, 3 on failure)
///   OMNIDICT_DEBUG_WINDOW_SIZE=1000x680
void installDebugHooks(ui::MainWindow& window, services::DictionaryManager& manager);

} // namespace omnidict::app
