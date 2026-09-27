#pragma once

#include <QLatin1StringView>

// Every persisted key lives here exactly once. Format: "section/camelCase".
// Adding a key = adding a typed accessor pair on core::Settings + a test.
namespace omnidict::core::keys {

// window/
inline constexpr QLatin1StringView kWindowGeometry{"window/geometry"};
inline constexpr QLatin1StringView kWhatsNewSeenVersion{"window/whatsNewSeenVersion"};

// appearance/
inline constexpr QLatin1StringView kTheme{"appearance/theme"};
inline constexpr QLatin1StringView kEntryTextSize{"appearance/entryTextSize"};

// search/
inline constexpr QLatin1StringView kShowBestMatch{"search/showBestMatch"};
inline constexpr QLatin1StringView kSearchDefinitions{"search/searchDefinitions"};
inline constexpr QLatin1StringView kSuggestSpellings{"search/suggestSpellings"};
inline constexpr QLatin1StringView kDictionaryFilter{"search/dictionaryFilter"};

// history/
inline constexpr QLatin1StringView kRememberHistory{"history/remember"};

} // namespace omnidict::core::keys
