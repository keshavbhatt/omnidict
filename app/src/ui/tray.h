#pragma once

#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>

class QAction;
class QMenu;

namespace omnidict::core {
class Settings;
} // namespace omnidict::core

namespace omnidict::services {
class Autostart;
class GlobalShortcuts;
} // namespace omnidict::services

namespace omnidict::ui {

class MainWindow;

/// The tray icon and its menu (mocks/tray.html, ADR-020): while it shows, closing the main
/// window keeps the app running, so the quick lookup key keeps working. Where the desktop
/// has no tray (GNOME without the AppIndicator extension, tests), nothing changes: closing
/// the window quits as before.
class Tray : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Tray)

public:
    /// `available`: whether the desktop shows tray icons (systemTrayAvailable() by default;
    /// tests pass true).
    Tray(MainWindow& window, core::Settings& settings, services::Autostart& autostart, bool available,
         QObject* parent = nullptr);
    ~Tray() override;

    [[nodiscard]] static bool systemTrayAvailable();

    /// Whether the desktop shows tray icons.
    [[nodiscard]] bool isAvailable() const { return m_available; }
    /// Whether the app keeps running when its window closes: a tray, and the setting on.
    [[nodiscard]] bool keepsRunning() const;

    /// The quick lookup shortcut, whose key the menu names (owned by the caller).
    void setGlobalShortcuts(services::GlobalShortcuts* shortcuts);
    /// After the window closed into the tray: the desktop says so, the first time only
    /// (mocks/tray.html note 3).
    void showFirstCloseNotice();
    /// Asks the desktop to start the app at login, or not to; startAtLoginSettled() follows.
    void setStartAtLogin(bool enabled);

    [[nodiscard]] QSystemTrayIcon* icon() const { return m_icon; }
    [[nodiscard]] QMenu* menu() const { return m_menu; }

Q_SIGNALS:
    /// The start-at-login request is answered; `enabled` is what is now in effect.
    void startAtLoginSettled(bool enabled);

private:
    void buildMenu();
    void update();
    void updateQuickLookupKey();
    void onActivated(QSystemTrayIcon::ActivationReason reason);

    MainWindow& m_window;
    core::Settings& m_settings;
    services::Autostart& m_autostart;
    bool m_available;
    QSystemTrayIcon* m_icon = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_quickLookup = nullptr;
    QPointer<services::GlobalShortcuts> m_shortcuts;
};

} // namespace omnidict::ui
