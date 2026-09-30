#pragma once

#include <QDBusArgument>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

class QDBusMessage;

namespace omnidict::services {

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
        Declined,    ///< the user or the desktop refused the shortcut
    };

    static constexpr auto kQuickLookupId = "quick-lookup";
    /// Suggested to the desktop (owner, 2026-09-30); the desktop may choose another.
    static constexpr auto kPreferredTrigger = "CTRL+ALT+d";

    explicit GlobalShortcuts(QString appId, QObject* parent = nullptr);
    ~GlobalShortcuts() override = default;

    /// Registers the shortcut, asynchronously; stateChanged() reports the outcome.
    void start();

    [[nodiscard]] State state() const { return m_state; }
    /// The key as the desktop describes it ("Ctrl+Alt+D"); empty until Active.
    [[nodiscard]] QString trigger() const { return m_trigger; }
    /// Whether the desktop can open its own dialog to change the key (portal version 2).
    [[nodiscard]] bool canConfigure() const { return m_state == State::Active && m_version >= 2; }
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

private Q_SLOTS:
    void onResponse(const QDBusMessage& message);
    void onActivated(const QDBusMessage& message);
    void onShortcutsChanged(const QDBusMessage& message);

private:
    using ResponseHandler = std::function<void(uint response, const QVariantMap& results)>;

    void registerHostApp();
    void createSession();
    void listShortcuts();
    void bindShortcut();
    void setState(State state);
    void takeTrigger(const QList<PortalShortcut>& shortcuts);
    /// Calls a portal method that answers through a Request object.
    void request(const QString& method, QList<QVariant> arguments, QVariantMap options,
                 ResponseHandler onResponse);

    QString m_appId;
    State m_state = State::Checking;
    QString m_session;
    QString m_trigger;
    uint m_version = 0;
    quint64 m_nextToken = 0;
    QHash<QString, ResponseHandler> m_pending; ///< request path -> what to do with the answer
};

} // namespace omnidict::services

Q_DECLARE_METATYPE(omnidict::services::PortalShortcut)
