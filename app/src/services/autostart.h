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
///   - any other install writes `~/.config/autostart/<app id>.desktop` (XDG autostart).
/// Every way starts the app with `--background`: no window, only the tray icon.
class Autostart : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Autostart)

public:
    enum class Method
    {
        DesktopFile, ///< the app writes the autostart file itself
        Portal,      ///< the Background portal does (Flatpak)
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

    /// Whether the file is in place; for the portal, what it last answered.
    [[nodiscard]] bool isEnabled() const;
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

    QString m_appId;
    bool m_portalEnabled = false;
    bool m_portalRequested = false;
    QString m_requestPath;
    quint64 m_nextToken = 0;
};

} // namespace omnidict::services
