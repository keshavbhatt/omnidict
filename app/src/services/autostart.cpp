#include "services/autostart.h"

#include "services/global_shortcuts.h"
#include "services/logging.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::services {

namespace {

const QString kService = u"org.freedesktop.portal.Desktop"_s;
const QString kPath = u"/org/freedesktop/portal/desktop"_s;
const QString kRequestInterface = u"org.freedesktop.portal.Request"_s;

/// One argument of a desktop file's Exec line, quoted when it needs to be (Desktop Entry
/// Specification, "The Exec key").
QString execArgument(const QString& argument)
{
    static const QString kReserved = uR"( "'\><~|&;$*?#()`)"_s;
    if (!std::ranges::any_of(argument, [](QChar c) { return kReserved.contains(c); })) {
        return argument;
    }
    QString quoted = argument;
    for (const QString& special : {u"\\"_s, u"\""_s, u"`"_s, u"$"_s}) {
        quoted.replace(special, u"\\"_s + special);
    }
    return u"\"%1\""_s.arg(quoted);
}

} // namespace

Autostart::Autostart(QString appId, QObject* parent)
    : QObject(parent)
    , m_appId(std::move(appId))
{}

Autostart::Method Autostart::method()
{
    return qEnvironmentVariableIsEmpty("FLATPAK_ID") ? Method::DesktopFile : Method::Portal;
}

QString Autostart::desktopFilePath() const
{
    if (method() == Method::Portal) {
        return {};
    }
    // Inside a snap, snapd reads the snap's own config directory, whatever XDG_CONFIG_HOME
    // the desktop extension sets.
    const QString snapData = qEnvironmentVariable("SNAP_USER_DATA");
    const QString config = !qEnvironmentVariableIsEmpty("SNAP") && !snapData.isEmpty()
                               ? snapData + u"/.config"_s
                               : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return u"%1/autostart/%2.desktop"_s.arg(config, m_appId);
}

QStringList Autostart::command()
{
    const QString background = QString::fromLatin1(kBackgroundOption);
    if (const QString snap = qEnvironmentVariable("SNAP_NAME"); !snap.isEmpty()) {
        return {snap, background}; // snapd runs the app the file is named for
    }
    if (const QString appImage = qEnvironmentVariable("APPIMAGE"); !appImage.isEmpty()) {
        return {appImage, background};
    }
    return {QCoreApplication::applicationFilePath(), background};
}

QString Autostart::desktopFileContents() const
{
    QStringList exec;
    for (const QString& argument : command()) {
        exec << execArgument(argument);
    }
    return u"[Desktop Entry]\n"
           "Type=Application\n"
           "Name=Omnidict\n"
           "Comment=%1\n"
           "Icon=%2\n"
           "Exec=%3\n"
           "Terminal=false\n"
           "NoDisplay=true\n"
           "X-GNOME-Autostart-enabled=true\n"
           "X-KDE-autostart-after=panel\n"_s.arg(tr("Starts Omnidict in the tray for quick lookup"), m_appId,
                                                 exec.join(u' '));
}

bool Autostart::isEnabled() const
{
    return method() == Method::Portal ? m_portalEnabled : QFile::exists(desktopFilePath());
}

void Autostart::setEnabled(bool enabled)
{
    if (method() == Method::Portal) {
        requestPortal(enabled);
        return;
    }
    writeDesktopFile(enabled);
}

void Autostart::writeDesktopFile(bool enabled)
{
    const QString path = desktopFilePath();
    if (!enabled) {
        const bool ok = !QFile::exists(path) || QFile::remove(path);
        if (!ok) {
            qCWarning(lcBackground) << "autostart: cannot remove" << path;
        }
        Q_EMIT finished(!ok, ok);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const bool ok = file.open(QIODevice::WriteOnly | QIODevice::Text) &&
                    file.write(desktopFileContents().toUtf8()) >= 0 && file.commit();
    if (!ok) {
        qCWarning(lcBackground) << "autostart: cannot write" << path << file.errorString();
    }
    Q_EMIT finished(ok, ok);
}

void Autostart::requestPortal(bool enabled)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = u"omnidictbg%1"_s.arg(++m_nextToken);
    if (!m_requestPath.isEmpty()) {
        bus.disconnect(kService, m_requestPath, kRequestInterface, u"Response"_s, this,
                       SLOT(onPortalResponse(QDBusMessage)));
    }
    // Listen before calling, so a quick answer cannot arrive unheard.
    m_requestPath = GlobalShortcuts::requestPath(bus.baseService(), token);
    m_portalRequested = enabled;
    bus.connect(kService, m_requestPath, kRequestInterface, u"Response"_s, this,
                SLOT(onPortalResponse(QDBusMessage)));

    const QVariantMap options{
        {u"handle_token"_s, token},
        {u"reason"_s, tr("Start Omnidict at login, in the tray, for quick lookup")},
        {u"autostart"_s, enabled},
        {u"commandline"_s, QStringList{u"omnidict"_s, QString::fromLatin1(kBackgroundOption)}},
        {u"dbus-activatable"_s, false},
    };
    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, kPath, u"org.freedesktop.portal.Background"_s, u"RequestBackground"_s);
    call << QString() << options;
    const auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* reply) {
        reply->deleteLater();
        if (reply->isError()) {
            qCWarning(lcBackground) << "autostart: no Background portal:" << reply->error().message();
            Q_EMIT finished(m_portalEnabled, false);
        }
    });
}

void Autostart::onPortalResponse(const QDBusMessage& message)
{
    // (u response, a{sv} results): results has "autostart", what the desktop granted.
    const QList<QVariant> arguments = message.arguments();
    const bool ok = arguments.size() >= 2 && arguments.at(0).toUInt() == 0;
    if (ok) {
        const auto results = qdbus_cast<QVariantMap>(arguments.at(1));
        m_portalEnabled = results.value(u"autostart"_s, m_portalRequested).toBool();
    } else {
        qCInfo(lcBackground) << "autostart: the desktop refused the background request";
    }
    Q_EMIT finished(m_portalEnabled, ok && m_portalEnabled == m_portalRequested);
}

} // namespace omnidict::services
