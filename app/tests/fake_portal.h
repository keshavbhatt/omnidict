#pragma once

// A stand-in for xdg-desktop-portal's GlobalShortcuts portal on a private D-Bus daemon,
// for tests: no real session, no desktop dialogs, no shortcuts left in the desktop's
// settings. Header only, shared by the tests that need a portal.

#include "services/global_shortcuts.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QVariantMap>

namespace omnidict::tests {

/// A private session bus (`dbus-daemon --session`) for the life of the object.
class PrivateBus
{
public:
    PrivateBus()
    {
        const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
        if (daemon.isEmpty() || !m_dir.isValid()) {
            return;
        }
        // No service directories: a name nobody owns stays unowned. With the stock session
        // config the daemon would start the real xdg-desktop-portal on this bus.
        QFile config(m_dir.filePath(QStringLiteral("bus.conf")));
        if (!config.open(QIODevice::WriteOnly)) {
            return;
        }
        config.write("<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                     " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                     "<busconfig><type>session</type><listen>unix:dir=");
        config.write(m_dir.path().toUtf8());
        config.write("</listen><auth>EXTERNAL</auth><policy context=\"default\">"
                     "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
                     "<allow own=\"*\"/></policy></busconfig>\n");
        config.close();
        // The host's daemon with the host's libraries, not the ones the tests run against.
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.remove(QStringLiteral("LD_LIBRARY_PATH"));
        m_process.setProcessEnvironment(environment);
        m_process.start(daemon, {QStringLiteral("--config-file=") + config.fileName(),
                                 QStringLiteral("--nofork"), QStringLiteral("--print-address")});
        if (m_process.waitForStarted() && m_process.waitForReadyRead(5000)) {
            m_address = QString::fromUtf8(m_process.readLine()).trimmed();
        }
    }
    ~PrivateBus()
    {
        m_process.kill();
        m_process.waitForFinished();
    }
    PrivateBus(const PrivateBus&) = delete;
    PrivateBus& operator=(const PrivateBus&) = delete;
    PrivateBus(PrivateBus&&) = delete;
    PrivateBus& operator=(PrivateBus&&) = delete;

    /// Empty when dbus-daemon is missing or did not start.
    [[nodiscard]] QString address() const { return m_address; }

private:
    QTemporaryDir m_dir;
    QProcess m_process;
    QString m_address;
};

/// org.freedesktop.portal.Desktop with the GlobalShortcuts interface (version 2) and the
/// host app registry. Like the real portal, it refuses Register from a connection that
/// already made another portal call. BindShortcuts answers with `trigger` as the key the
/// desktop chose; an empty one is what KDE does when it binds no key.
class FakePortal : public QDBusVirtualObject
{
public:
    explicit FakePortal(const QString& address)
        : m_bus(QDBusConnection::connectToBus(address, QStringLiteral("omnidict-fake-portal")))
    {
        qDBusRegisterMetaType<services::PortalShortcut>();
        qDBusRegisterMetaType<QList<services::PortalShortcut>>();
        m_bus.registerVirtualObject(QStringLiteral("/org/freedesktop/portal/desktop"), this);
        m_registered = m_bus.registerService(QStringLiteral("org.freedesktop.portal.Desktop"));
    }
    ~FakePortal() override
    {
        m_bus.unregisterObject(QStringLiteral("/org/freedesktop/portal/desktop"));
        QDBusConnection::disconnectFromBus(QStringLiteral("omnidict-fake-portal"));
    }
    FakePortal(const FakePortal&) = delete;
    FakePortal& operator=(const FakePortal&) = delete;
    FakePortal(FakePortal&&) = delete;
    FakePortal& operator=(FakePortal&&) = delete;

    [[nodiscard]] bool isRegistered() const { return m_registered; }
    void setTrigger(const QString& trigger) { m_trigger = trigger; }
    /// App ids registered, by connection.
    [[nodiscard]] QHash<QString, QString> registeredAppIds() const { return m_appIds; }
    /// Register calls refused because the connection had already called the portal.
    [[nodiscard]] int refusedRegistrations() const { return m_refused; }

    /// The desktop's settings changed the key (ShortcutsChanged).
    void changeTrigger(const QString& trigger)
    {
        m_trigger = trigger;
        QDBusMessage signal = QDBusMessage::createSignal(
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QStringLiteral("ShortcutsChanged"));
        signal << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariant::fromValue(shortcuts());
        m_bus.send(signal);
    }

    QString introspect(const QString& /*path*/) const override { return {}; }

    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        const QString sender = message.service();
        const QString interface = message.interface();
        const QString member = message.member();
        if (interface == QStringLiteral("org.freedesktop.host.portal.Registry")) {
            if (m_identified.contains(sender)) {
                ++m_refused;
                connection.send(message.createErrorReply(
                    QStringLiteral("org.freedesktop.portal.Error.Failed"),
                    QStringLiteral("Could not register app ID: Connection already associated with an "
                                   "application ID")));
                return true;
            }
            m_identified.insert(sender);
            m_appIds.insert(sender, message.arguments().value(0).toString());
            connection.send(message.createReply());
            return true;
        }
        m_identified.insert(sender);
        if (interface == QStringLiteral("org.freedesktop.DBus.Properties") &&
            member == QStringLiteral("Get")) {
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(2U))));
            return true;
        }
        if (interface != QStringLiteral("org.freedesktop.portal.GlobalShortcuts")) {
            return false;
        }
        const QList<QVariant> arguments = message.arguments();
        const QVariantMap options = qdbus_cast<QVariantMap>(arguments.constLast());
        const QString token = options.value(QStringLiteral("handle_token")).toString();
        const QString path = services::GlobalShortcuts::requestPath(sender, token);
        connection.send(message.createReply(QVariant::fromValue(QDBusObjectPath(path))));
        QVariantMap results;
        if (member == QStringLiteral("CreateSession")) {
            m_session = QStringLiteral("/org/freedesktop/portal/desktop/session/test/omnidict");
            results.insert(QStringLiteral("session_handle"), m_session);
        } else if (member == QStringLiteral("ListShortcuts")) {
            results.insert(QStringLiteral("shortcuts"),
                           QVariant::fromValue(QList<services::PortalShortcut>()));
        } else if (member == QStringLiteral("BindShortcuts")) {
            results.insert(QStringLiteral("shortcuts"), QVariant::fromValue(shortcuts()));
        } else {
            return true; // ConfigureShortcuts: nothing to answer
        }
        QDBusMessage response = QDBusMessage::createTargetedSignal(
            sender, path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"));
        response << 0U << results;
        connection.send(response);
        return true;
    }

private:
    [[nodiscard]] QList<services::PortalShortcut> shortcuts() const
    {
        services::PortalShortcut shortcut{
            .id = QString::fromLatin1(services::GlobalShortcuts::kQuickLookupId), .properties = {}};
        shortcut.properties.insert(QStringLiteral("trigger_description"), m_trigger);
        return {shortcut};
    }

    QDBusConnection m_bus;
    bool m_registered = false;
    QString m_trigger;
    QString m_session;
    QSet<QString> m_identified;
    QHash<QString, QString> m_appIds;
    int m_refused = 0;
};

} // namespace omnidict::tests
