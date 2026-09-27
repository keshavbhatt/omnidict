#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>

class QSettings;

namespace omnidict::core {

enum class Theme
{
    System,
    Light,
    Dark,
};

/// Typed facade over QSettings (DOCS/CODING_STANDARDS.md): the only place that
/// constructs a QSettings. Defaults live in settings.cpp; every setter that
/// changes a value emits a signal, so the rest of the app reacts instead of
/// polling.
class Settings : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Settings)

public:
    /// Entry text size in pixels, and its bounds (Ctrl+Plus, Ctrl+Minus, Ctrl+0).
    static constexpr int kDefaultEntryTextSize = 15;
    static constexpr int kMinEntryTextSize = 11;
    static constexpr int kMaxEntryTextSize = 28;

    /// Settings kept in an INI file at `iniFilePath` (in the profile directory).
    explicit Settings(const QString& iniFilePath, QObject* parent = nullptr);
    ~Settings() override;

    // window/
    [[nodiscard]] QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);
    /// The app version whose What's new sheet was last shown; empty on a first run.
    [[nodiscard]] QString whatsNewSeenVersion() const;
    void setWhatsNewSeenVersion(const QString& version);

    // appearance/
    [[nodiscard]] Theme theme() const;
    void setTheme(Theme theme);
    [[nodiscard]] int entryTextSize() const;
    void setEntryTextSize(int pixels); ///< clamped to the bounds above

    // search/
    /// Show the best match while typing (the entry follows the first result).
    [[nodiscard]] bool showBestMatch() const;
    void setShowBestMatch(bool enabled);
    [[nodiscard]] bool searchDefinitions() const;
    void setSearchDefinitions(bool enabled);
    [[nodiscard]] bool suggestSpellings() const;
    void setSuggestSpellings(bool enabled);
    /// The dictionary the search is restricted to; empty searches all of them.
    [[nodiscard]] QString dictionaryFilter() const;
    void setDictionaryFilter(const QString& dictId);

    // dictionaries/
    /// Dictionary ids in the order the user arranged them (result groups follow
    /// it); dictionaries not listed come after, by name.
    [[nodiscard]] QStringList dictionaryOrder() const;
    void setDictionaryOrder(const QStringList& dictIds);
    /// Installed dictionaries the user switched off: not searched, not in the filter.
    [[nodiscard]] QStringList disabledDictionaries() const;
    void setDisabledDictionaries(const QStringList& dictIds);

    // history/
    [[nodiscard]] bool rememberHistory() const;
    void setRememberHistory(bool enabled);

Q_SIGNALS:
    void themeChanged(omnidict::core::Theme theme);
    void entryTextSizeChanged(int pixels);
    /// Any of showBestMatch, searchDefinitions or suggestSpellings changed.
    void searchOptionsChanged();
    void dictionaryFilterChanged(const QString& dictId);
    void rememberHistoryChanged(bool enabled);
    /// The order or the switched-off set changed.
    void dictionaryPreferencesChanged();

private:
    [[nodiscard]] bool boolValue(QLatin1StringView key, bool fallback) const;
    /// Stores the value; true when it changed.
    bool store(QLatin1StringView key, const QVariant& value, const QVariant& fallback);

    std::unique_ptr<QSettings> m_store;
};

} // namespace omnidict::core
