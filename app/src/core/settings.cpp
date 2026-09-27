#include "core/settings.h"

#include "core/logging.h"
#include "core/settings_keys.h"

#include <QSettings>

#include <algorithm>

namespace omnidict::core {

namespace {

// The defaults table: every setting's value before the user changes it.
constexpr Theme kDefaultTheme = Theme::System;
constexpr bool kDefaultShowBestMatch = true;
constexpr bool kDefaultSearchDefinitions = true;
constexpr bool kDefaultSuggestSpellings = true;
constexpr bool kDefaultRememberHistory = true;

} // namespace

Settings::Settings(const QString& iniFilePath, QObject* parent)
    : QObject(parent)
    , m_store(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
    qCDebug(lcCore) << "settings:" << m_store->fileName();
}

Settings::~Settings()
{
    m_store->sync();
}

bool Settings::boolValue(QLatin1StringView key, bool fallback) const
{
    return m_store->value(key, fallback).toBool();
}

bool Settings::store(QLatin1StringView key, const QVariant& value, const QVariant& fallback)
{
    if (m_store->value(key, fallback) == value) {
        return false;
    }
    if (value == fallback) {
        m_store->remove(key); // the file only holds what differs from the defaults
    } else {
        m_store->setValue(key, value);
    }
    return true;
}

QByteArray Settings::windowGeometry() const
{
    return m_store->value(keys::kWindowGeometry).toByteArray();
}

void Settings::setWindowGeometry(const QByteArray& geometry)
{
    store(keys::kWindowGeometry, geometry, QByteArray());
}

QString Settings::whatsNewSeenVersion() const
{
    return m_store->value(keys::kWhatsNewSeenVersion).toString();
}

void Settings::setWhatsNewSeenVersion(const QString& version)
{
    store(keys::kWhatsNewSeenVersion, version, QString());
}

Theme Settings::theme() const
{
    const int value = m_store->value(keys::kTheme, static_cast<int>(kDefaultTheme)).toInt();
    if (value < static_cast<int>(Theme::System) || value > static_cast<int>(Theme::Dark)) {
        return kDefaultTheme;
    }
    return static_cast<Theme>(value);
}

void Settings::setTheme(Theme theme)
{
    if (store(keys::kTheme, static_cast<int>(theme), static_cast<int>(kDefaultTheme))) {
        Q_EMIT themeChanged(theme);
    }
}

int Settings::entryTextSize() const
{
    const int value = m_store->value(keys::kEntryTextSize, kDefaultEntryTextSize).toInt();
    return std::clamp(value, kMinEntryTextSize, kMaxEntryTextSize);
}

void Settings::setEntryTextSize(int pixels)
{
    const int clamped = std::clamp(pixels, kMinEntryTextSize, kMaxEntryTextSize);
    if (store(keys::kEntryTextSize, clamped, kDefaultEntryTextSize)) {
        Q_EMIT entryTextSizeChanged(clamped);
    }
}

bool Settings::showBestMatch() const
{
    return boolValue(keys::kShowBestMatch, kDefaultShowBestMatch);
}

void Settings::setShowBestMatch(bool enabled)
{
    if (store(keys::kShowBestMatch, enabled, kDefaultShowBestMatch)) {
        Q_EMIT searchOptionsChanged();
    }
}

bool Settings::searchDefinitions() const
{
    return boolValue(keys::kSearchDefinitions, kDefaultSearchDefinitions);
}

void Settings::setSearchDefinitions(bool enabled)
{
    if (store(keys::kSearchDefinitions, enabled, kDefaultSearchDefinitions)) {
        Q_EMIT searchOptionsChanged();
    }
}

bool Settings::suggestSpellings() const
{
    return boolValue(keys::kSuggestSpellings, kDefaultSuggestSpellings);
}

void Settings::setSuggestSpellings(bool enabled)
{
    if (store(keys::kSuggestSpellings, enabled, kDefaultSuggestSpellings)) {
        Q_EMIT searchOptionsChanged();
    }
}

QString Settings::dictionaryFilter() const
{
    return m_store->value(keys::kDictionaryFilter).toString();
}

void Settings::setDictionaryFilter(const QString& dictId)
{
    if (store(keys::kDictionaryFilter, dictId, QString())) {
        Q_EMIT dictionaryFilterChanged(dictId);
    }
}

QStringList Settings::dictionaryOrder() const
{
    return m_store->value(keys::kDictionaryOrder).toStringList();
}

void Settings::setDictionaryOrder(const QStringList& dictIds)
{
    if (store(keys::kDictionaryOrder, dictIds, QStringList())) {
        Q_EMIT dictionaryPreferencesChanged();
    }
}

QStringList Settings::disabledDictionaries() const
{
    return m_store->value(keys::kDisabledDictionaries).toStringList();
}

void Settings::setDisabledDictionaries(const QStringList& dictIds)
{
    if (store(keys::kDisabledDictionaries, dictIds, QStringList())) {
        Q_EMIT dictionaryPreferencesChanged();
    }
}

bool Settings::rememberHistory() const
{
    return boolValue(keys::kRememberHistory, kDefaultRememberHistory);
}

void Settings::setRememberHistory(bool enabled)
{
    if (store(keys::kRememberHistory, enabled, kDefaultRememberHistory)) {
        Q_EMIT rememberHistoryChanged(enabled);
    }
}

} // namespace omnidict::core
