#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QDBusMessage;

namespace omnidict::services {

/// Starting Omnidict at login, in the tray (mocks/tray.html note 5). The desktop holds
/// the entry:
///   - a Flatpak asks the Background portal, which writes the entry for it;
///   - a snap writes `$SNAP_USER_DATA/.config/autostart/<app id>.desktop`, which snapd
///     starts at login because snapcraft.yaml names it in the app's `autostart`;
///   - any other install writes `~/.config/autostart/<app id>.desktop` (XDG autostart);
///   - on Windows, the Store package's startup task (ADR-022, mocks/windows.html note 4),
///     which starts the app without arguments: launchedAtLogin() tells it apart.
/// Every other way starts the app with `--background`: no window, only the tray icon.
class Autostart : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Autostart)

public:
    enum class Method
    {
        DesktopFile, ///< the app writes the autostart file itself
        Portal,      ///< the Background portal does (Flatpak)
        StartupTask, ///< the MSIX package's startup task (Windows)
    };

    static constexpr auto kBackgroundOption = "--background";

    /// `appId` is the desktop file id, "com.ktechpit.omnidict".
    explicit Autostart(QString appId, QObject* parent = nullptr);
    ~Autostart() override = default;

    [[nodiscard]] static Method method();
    /// The autostart file this install writes; empty for the portal.
    [[nodiscard]] QString desktopFilePath() const;
    /// The file's contents: an entry that runs command().
    [[nodiscard]] QString desktopFileContents() const;
    /// What the desktop runs at login: this install's program and `--background`.
    [[nodiscard]] static QStringList command();

    /// Whether the file is in place; for the portal and the startup task, what it last answered.
    [[nodiscard]] bool isEnabled() const;
    /// The user (or a policy) switched the startup task off in Windows, which an app cannot
    /// undo: only Windows Settings can turn it on again.
    [[nodiscard]] bool isBlocked() const { return m_blocked; }
    /// Reads the startup task's state; finished() reports it. Nothing for the other methods,
    /// whose state is known at once.
    void refresh();
    /// Windows started this process through the startup task, at login.
    [[nodiscard]] static bool launchedAtLogin();
    /// Writes or removes the entry; finished() reports the outcome (at once for a file,
    /// once the portal answers otherwise).
    void setEnabled(bool enabled);

Q_SIGNALS:
    /// `enabled` is the state now in effect; `ok` is false when the change failed.
    void finished(bool enabled, bool ok);

private Q_SLOTS:
    void onPortalResponse(const QDBusMessage& message);

private:
    void writeDesktopFile(bool enabled);
    void requestPortal(bool enabled);
    /// Asks Windows to switch the startup task on or off (refresh() when `request` is false).
    void requestStartupTask(bool request, bool enabled);
    /// What the startup task's state (Windows.ApplicationModel.StartupTaskState, -1 when it
    /// could not be read) means here.
    void takeTaskState(int state, bool requested, bool wanted);

    QString m_appId;
    bool m_portalEnabled = false;
    bool m_portalRequested = false;
    bool m_taskEnabled = false;
    bool m_blocked = false;
    QString m_requestPath;
    quint64 m_nextToken = 0;
};

} // namespace omnidict::services
