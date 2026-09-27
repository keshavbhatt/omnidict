#pragma once

#include <QObject>

namespace omnidict::core {
class Settings;
}

namespace omnidict::ui {

/// Resolves the Theme setting (System, Light, Dark) into a scheme, following
/// the desktop live on System, and pushes the tokens' palette and style sheet
/// onto the application. Fusion everywhere, so the app looks the same on every
/// desktop (DOCS/DESIGN.md).
class ThemeApplier : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ThemeApplier)

public:
    explicit ThemeApplier(core::Settings& settings, QObject* parent = nullptr);
    ~ThemeApplier() override = default;

Q_SIGNALS:
    /// After the palette and sheet changed; widgets that paint their own
    /// colours (the results delegate, the entry view) refresh on it.
    void schemeChanged(bool dark);

private:
    void apply();

    core::Settings& m_settings;
};

} // namespace omnidict::ui
