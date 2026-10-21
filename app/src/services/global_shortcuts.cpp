#include "services/global_shortcuts.h"

#include "services/logging.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::services {

namespace {

const QString kService = u"org.freedesktop.portal.Desktop"_s;
const QString kPath = u"/org/freedesktop/portal/desktop"_s;
const QString kInterface = u"org.freedesktop.portal.GlobalShortcuts"_s;
const QString kRequestInterface = u"org.freedesktop.portal.Request"_s;

QVariantMap toMap(const QVariant& value)
{
    if (value.canConvert<QDBusArgument>()) {
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    }
    return value.toMap();
}

QList<PortalShortcut> toShortcuts(const QVariant& value)
{
    if (value.canConvert<QDBusArgument>()) {
        return qdbus_cast<QList<PortalShortcut>>(value.value<QDBusArgument>());
    }
    return value.value<QList<PortalShortcut>>();
}

} // namespace

QDBusArgument& operator<<(QDBusArgument& argument, const PortalShortcut& shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PortalShortcut& shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.properties;
    argument.endStructure();
    return argument; // NOLINT(bugprone-return-const-ref-from-parameter): the signature Qt DBus requires
}

GlobalShortcuts::GlobalShortcuts(QString appId, QObject* parent)
    : QObject(parent)
    , m_appId(std::move(appId))
    , m_busName(u"omnidict-shortcuts-%1"_s.arg(reinterpret_cast<quintptr>(this)))
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<QList<PortalShortcut>>();
}

GlobalShortcuts::~GlobalShortcuts()
{
    QDBusConnection::disconnectFromBus(m_busName);
}

QDBusConnection GlobalShortcuts::bus() const
{
    return QDBusConnection(m_busName);
}

QString GlobalShortcuts::requestPath(const QString& uniqueName, const QString& token)
{
    QString sender = uniqueName;
    if (sender.startsWith(u':')) {
        sender.remove(0, 1);
    }
    sender.replace(u'.', u'_');
    return u"/org/freedesktop/portal/desktop/request/%1/%2"_s.arg(sender, token);
}

void GlobalShortcuts::start()
{
    // The address as the environment has it now (libdbus remembers the first one it saw).
    const QString address = qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS");
    const QDBusConnection connection =
        address.isEmpty() ? QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_busName)
                          : QDBusConnection::connectToBus(address, m_busName);
    if (!connection.isConnected()) {
        setState(State::Unavailable);
        return;
    }
    registerHostApp();
}

void GlobalShortcuts::registerHostApp()
{
    // An app outside a sandbox tells the portal who it is (xdg-desktop-portal 1.19+), as
    // the first call on its connection; sandboxed apps are identified by their sandbox.
    if (!qEnvironmentVariableIsEmpty("FLATPAK_ID") || !qEnvironmentVariableIsEmpty("SNAP")) {
        readVersion();
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, kPath, u"org.freedesktop.host.portal.Registry"_s, u"Register"_s);
    call << m_appId << QVariantMap();
    const auto* watcher = new QDBusPendingCallWatcher(bus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* reply) {
        reply->deleteLater();
        if (reply->isError()) { // an older portal, or none: reading the version tells which
            qCInfo(lcShortcuts) << "global shortcuts: app id not registered:" << reply->error().message();
        }
        readVersion();
    });
}

void GlobalShortcuts::readVersion()
{
    QDBusMessage get =
        QDBusMessage::createMethodCall(kService, kPath, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    get << kInterface << u"version"_s;
    const auto* watcher = new QDBusPendingCallWatcher(bus().asyncCall(get), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *call;
        if (reply.isError()) {
            qCInfo(lcShortcuts) << "no global shortcuts portal:" << reply.error().message();
            setState(State::Unavailable);
            return;
        }
        m_version = reply.value().variant().toUInt();
        QDBusConnection session = bus();
        session.connect(kService, kPath, kInterface, u"Activated"_s, this, SLOT(onActivated(QDBusMessage)));
        session.connect(kService, kPath, kInterface, u"ShortcutsChanged"_s, this,
                        SLOT(onShortcutsChanged(QDBusMessage)));
        createSession();
    });
}

void GlobalShortcuts::createSession()
{
    const QVariantMap options{{u"session_handle_token"_s, u"omnidict"_s}};
    request(u"CreateSession"_s, {}, options, [this](uint response, const QVariantMap& results) {
        if (response != 0) {
            qCWarning(lcShortcuts) << "global shortcuts: no session, response" << response;
            setState(State::Declined);
            return;
        }
        m_session = results.value(u"session_handle"_s).toString();
        listShortcuts();
    });
}

void GlobalShortcuts::listShortcuts()
{
    request(u"ListShortcuts"_s, {QVariant::fromValue(QDBusObjectPath(m_session))}, {},
            [this](uint response, const QVariantMap& results) {
                const QList<PortalShortcut> shortcuts = toShortcuts(results.value(u"shortcuts"_s));
                const bool bound =
                    response == 0 && std::ranges::any_of(shortcuts, [](const PortalShortcut& shortcut) {
                        return shortcut.id == QLatin1StringView(kQuickLookupId);
                    });
                if (bound) {
                    takeTrigger(shortcuts);
                    return;
                }
                bindShortcut();
            });
}

void GlobalShortcuts::bindShortcut()
{
    const QList<PortalShortcut> shortcuts{
        {.id = QString::fromLatin1(kQuickLookupId),
         .properties = {{u"description"_s, tr("Quick lookup: look up the selected word")},
                        {u"preferred_trigger"_s, QString::fromLatin1(kPreferredTrigger)}}}};
    request(u"BindShortcuts"_s,
            {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(shortcuts), QString()}, {},
            [this](uint response, const QVariantMap& results) {
                if (response != 0) {
                    qCInfo(lcShortcuts)
                        << "global shortcuts: the shortcut was not accepted, response" << response;
                    setState(State::Declined);
                    return;
                }
                takeTrigger(toShortcuts(results.value(u"shortcuts"_s)));
            });
}

void GlobalShortcuts::configure(const QString& parentWindow)
{
    if (!canConfigure()) {
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, u"ConfigureShortcuts"_s);
    call << QVariant::fromValue(QDBusObjectPath(m_session)) << parentWindow << QVariantMap();
    bus().asyncCall(call);
}

void GlobalShortcuts::request(const QString& method, QList<QVariant> arguments, QVariantMap options,
                              ResponseHandler onResponse)
{
    QDBusConnection bus = this->bus();
    const QString token = u"omnidict%1"_s.arg(++m_nextToken);
    options.insert(u"handle_token"_s, token);
    // Listen before calling, so a quick answer cannot arrive unheard.
    const QString path = requestPath(bus.baseService(), token);
    m_pending.insert(path, std::move(onResponse));
    bus.connect(kService, path, kRequestInterface, u"Response"_s, this, SLOT(onResponse(QDBusMessage)));

    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    arguments.append(options);
    call.setArguments(arguments);
    const auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this, path, method](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            if (w->isError()) {
                qCWarning(lcShortcuts) << "global shortcuts:" << method << "failed:" << w->error().message();
                m_pending.remove(path);
                setState(State::Unavailable);
            }
        });
}

void GlobalShortcuts::onResponse(const QDBusMessage& message)
{
    const QString path = message.path();
    const ResponseHandler handler = m_pending.take(path);
    bus().disconnect(kService, path, kRequestInterface, u"Response"_s, this, SLOT(onResponse(QDBusMessage)));
    const QList<QVariant> arguments = message.arguments();
    if (!handler || arguments.size() < 2) {
        return;
    }
    handler(arguments.at(0).toUInt(), toMap(arguments.at(1)));
}

void GlobalShortcuts::onActivated(const QDBusMessage& message)
{
    // (o session_handle, s shortcut_id, t timestamp, a{sv} options)
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() < 4 || arguments.at(1).toString() != QLatin1StringView(kQuickLookupId)) {
        return;
    }
    const QVariantMap options = toMap(arguments.at(3));
    Q_EMIT activated(options.value(u"activation_token"_s).toString().toUtf8());
}

void GlobalShortcuts::onShortcutsChanged(const QDBusMessage& message)
{
    // (o session_handle, a(sa{sv}) shortcuts): the user changed the key in the desktop's settings.
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() >= 2) {
        takeTrigger(toShortcuts(arguments.at(1)));
        Q_EMIT stateChanged(); // the key may have changed while the state did not
    }
}

void GlobalShortcuts::takeTrigger(const QList<PortalShortcut>& shortcuts)
{
    for (const PortalShortcut& shortcut : shortcuts) {
        if (shortcut.id == QLatin1StringView(kQuickLookupId)) {
            m_trigger = shortcut.properties.value(u"trigger_description"_s).toString().trimmed();
        }
    }
    // KDE keeps the suggested key as an unticked default and binds nothing until the user
    // chooses one in System Settings (issue #1).
    setState(m_trigger.isEmpty() ? State::NoKey : State::Active);
}

void GlobalShortcuts::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged();
}

} // namespace omnidict::services
