#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>

class QDBusMessage;

namespace omnidict::services {

class HotkeyBackend;

/// One entry of the portal's `a(sa{sv})` shortcut lists: an id and its properties
/// (`description`, `preferred_trigger`, `trigger_description`).
struct PortalShortcut
{
    QString id;
    QVariantMap properties;
};

QDBusArgument& operator<<(QDBusArgument& argument, const PortalShortcut& shortcut);
const QDBusArgument& operator>>(const QDBusArgument& argument, PortalShortcut& shortcut);

/// The Quick Lookup shortcut, registered with the desktop through the
/// xdg-desktop-portal GlobalShortcuts portal (DOCS/quick-lookup.md, ADR-019): the
/// only way a sandboxed app (Flatpak, snap) and a Wayland one can have a global
/// shortcut. The desktop owns the key: it asks the user to accept it once, and its
/// own settings change it. Works while the app runs. Where there is no portal
/// (GNOME 46 and 47, wlroots desktops), the state says so and Settings explains the
/// custom-shortcut route instead.
///
/// Where the desktop has no such service (Windows, ADR-022), Omnidict registers the key
/// itself through a HotkeyBackend and Settings records a new one (mocks/windows.html).
class GlobalShortcuts : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(GlobalShortcuts)

public:
    enum class State
    {
        Checking,    ///< asking the portal
        Unavailable, ///< no GlobalShortcuts portal on this desktop
        Active,      ///< bound; trigger() says which key
        NoKey,       ///< accepted, but the desktop bound no key: the user has to choose one
        Declined,    ///< the user or the desktop refused the shortcut
        Taken,       ///< recorded key only: another app holds it; key() says which
    };

    static constexpr auto kQuickLookupId = "quick-lookup";
    /// Suggested to the desktop (owner, 2026-09-30); the desktop may choose another.
    static constexpr auto kPreferredTrigger = "CTRL+ALT+d";

    explicit GlobalShortcuts(QString appId, QObject* parent = nullptr);
    ~GlobalShortcuts() override;

    /// Registers the key itself through `backend` instead of the portal, starting with
    /// `key` (portable text, empty for none). Call before start().
    void useBackend(std::unique_ptr<HotkeyBackend> backend, const QString& key);
    /// Whether Omnidict records the key itself (a backend), rather than the desktop.
    [[nodiscard]] bool recordsKey() const { return m_backend != nullptr; }
    /// The recorded key in portable text ("Ctrl+Alt+D"), also when Taken; empty for none.
    [[nodiscard]] QString key() const { return m_key; }
    /// Records and registers a new key (empty turns it off); keyChanged() reports it for
    /// saving, stateChanged() the outcome.
    void setKey(const QString& key);
    /// Releases the recorded key while Settings listens for a new one, so pressing the
    /// current key reaches Settings; resume() registers it again.
    void pause();
    void resume();

    /// Registers the shortcut, asynchronously; stateChanged() reports the outcome.
    void start();

    [[nodiscard]] State state() const { return m_state; }
    /// The key as the desktop describes it ("Ctrl+Alt+D"); empty unless Active.
    [[nodiscard]] QString trigger() const { return m_trigger; }
    /// Whether the desktop can open its own dialog to change the key (portal version 2).
    [[nodiscard]] bool canConfigure() const
    {
        return (m_state == State::Active || m_state == State::NoKey) && m_version >= 2;
    }
    /// Opens the desktop's dialog to change the key; `parentWindow` per the portal
    /// spec ("x11:<xid>", "wayland:<handle>" or empty).
    void configure(const QString& parentWindow = {});

    /// The object path the portal answers a request on, known before the call so the
    /// answer cannot be missed: /org/freedesktop/portal/desktop/request/<sender>/<token>.
    [[nodiscard]] static QString requestPath(const QString& uniqueName, const QString& token);

Q_SIGNALS:
    /// The shortcut was pressed; `activationToken` lets the window come to the front
    /// on Wayland (empty when the desktop gives none).
    void activated(const QByteArray& activationToken);
    void stateChanged();
    /// The recorded key changed (setKey()), for Settings to keep.
    void keyChanged(const QString& key);

private Q_SLOTS:
    void onResponse(const QDBusMessage& message);
    void onActivated(const QDBusMessage& message);
    void onShortcutsChanged(const QDBusMessage& message);

private:
    using ResponseHandler = std::function<void(uint response, const QVariantMap& results)>;

    void registerHostApp();
    void readVersion();
    void createSession();
    void bindShortcut();
    void setState(State state);
    /// Takes the key from the portal's list and becomes Active, or NoKey without one.
    void takeTrigger(const QList<PortalShortcut>& shortcuts);
    [[nodiscard]] QDBusConnection bus() const;
    /// Calls a portal method that answers through a Request object.
    void request(const QString& method, QList<QVariant> arguments, QVariantMap options,
                 ResponseHandler onResponse);

    /// Registers the recorded key and sets the state from the answer.
    void applyKey();

    QString m_appId;
    std::unique_ptr<HotkeyBackend> m_backend;
    QString m_key;
    /// A connection of its own: the portal ties an app id to a connection on its first
    /// call, and Qt's own portal calls (colour scheme, file dialogs) come first on the
    /// shared one, so registering there fails and KDE files the shortcut under whatever
    /// started the app (issue #1).
    QString m_busName;
    State m_state = State::Checking;
    QString m_session;
    QString m_trigger;
    uint m_version = 0;
    quint64 m_nextToken = 0;
    QHash<QString, ResponseHandler> m_pending; ///< request path -> what to do with the answer
};

} // namespace omnidict::services

Q_DECLARE_METATYPE(omnidict::services::PortalShortcut)
