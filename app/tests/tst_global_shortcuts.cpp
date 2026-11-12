#include "fake_hotkey.h"
#include "fake_portal.h"
#include "services/global_shortcuts.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using omnidict::services::GlobalShortcuts;

/// The Quick Lookup shortcut service, against a fake portal on a private D-Bus daemon
/// (fake_portal.h), so nothing ever reaches the real session's portal and its dialogs.
class TestGlobalShortcuts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Outside a sandbox: the service registers its app id with the portal.
        qunsetenv("SNAP");
        qunsetenv("FLATPAK_ID");
        m_bus = std::make_unique<omnidict::tests::PrivateBus>();
        // Without dbus-daemon, a bus that does not exist: the portal-free tests still run.
        const QString address =
            m_bus->address().isEmpty() ? u"unix:path=/nonexistent/omnidict-test-bus"_s : m_bus->address();
        qputenv("DBUS_SESSION_BUS_ADDRESS", address.toUtf8());
    }

    void requestPathsFollowThePortalSpec()
    {
        // The Request object's path is known before the call, from the caller's unique name.
        QCOMPARE(GlobalShortcuts::requestPath(u":1.42"_s, u"omnidict7"_s),
                 u"/org/freedesktop/portal/desktop/request/1_42/omnidict7"_s);
        QCOMPARE(GlobalShortcuts::requestPath(u":1.1024"_s, u"t"_s),
                 u"/org/freedesktop/portal/desktop/request/1_1024/t"_s);
    }

    void withoutAPortalItIsUnavailable()
    {
        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        QCOMPARE(shortcuts.state(), GlobalShortcuts::State::Checking);
        QSignalSpy changed(&shortcuts, &GlobalShortcuts::stateChanged);
        shortcuts.start();
        QTRY_COMPARE(shortcuts.state(), GlobalShortcuts::State::Unavailable);
        QVERIFY(!changed.isEmpty());
        QVERIFY(shortcuts.trigger().isEmpty());
        QVERIFY(!shortcuts.canConfigure());
        shortcuts.configure(); // harmless without a session
    }

    void aKeyFromTheDesktopMakesItActive()
    {
        auto portal = makePortal();
        if (!portal) {
            QSKIP("no private D-Bus daemon for the fake portal");
        }
        portal->setTrigger(u"Ctrl+Alt+D"_s);
        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        shortcuts.start();
        QTRY_COMPARE(shortcuts.state(), GlobalShortcuts::State::Active);
        QCOMPARE(shortcuts.trigger(), u"Ctrl+Alt+D"_s);
        QVERIFY(shortcuts.canConfigure());
    }

    void theAppIdIsRegisteredBeforeAnyOtherPortalCall()
    {
        // Issue #1: registered after Qt's own portal calls, the portal refused the id and KDE
        // filed the shortcut under the program that started Omnidict (Konsole, Dolphin).
        auto portal = makePortal();
        if (!portal) {
            QSKIP("no private D-Bus daemon for the fake portal");
        }
        portal->setTrigger(u"Ctrl+Alt+D"_s);
        // What Qt does at startup on the app's shared connection: read the colour scheme.
        QDBusMessage settings = QDBusMessage::createMethodCall(
            u"org.freedesktop.portal.Desktop"_s, u"/org/freedesktop/portal/desktop"_s,
            u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
        settings << u"org.freedesktop.portal.Settings"_s << u"version"_s;
        QVERIFY(QDBusConnection::sessionBus().call(settings, QDBus::BlockWithGui).type() ==
                QDBusMessage::ReplyMessage);

        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        shortcuts.start();
        QTRY_COMPARE(shortcuts.state(), GlobalShortcuts::State::Active);
        QCOMPARE(portal->refusedRegistrations(), 0);
        QCOMPARE(portal->registeredAppIds().values(), QStringList{u"com.ktechpit.omnidict"_s});
    }

    void aSavedShortcutIsBoundAgain()
    {
        // KDE lists a shortcut saved by an earlier run but fires only keys bound in this
        // session: skipping BindShortcuts left the key dead after a restart.
        auto portal = makePortal();
        if (!portal) {
            QSKIP("no private D-Bus daemon for the fake portal");
        }
        portal->setTrigger(u"Ctrl+Alt+D"_s);
        portal->setSaved(true); // an earlier run bound it
        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        const QSignalSpy activated(&shortcuts, &GlobalShortcuts::activated);
        shortcuts.start();
        QTRY_COMPARE(shortcuts.state(), GlobalShortcuts::State::Active);
        QVERIFY(portal->press());
        QTRY_COMPARE(activated.count(), 1);
    }

    void noKeyFromTheDesktopIsItsOwnState()
    {
        // KDE binds the shortcut but keeps the suggested key unticked (issue #1).
        auto portal = makePortal();
        if (!portal) {
            QSKIP("no private D-Bus daemon for the fake portal");
        }
        portal->setTrigger({});
        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        shortcuts.start();
        QTRY_COMPARE(shortcuts.state(), GlobalShortcuts::State::NoKey);
        QVERIFY(shortcuts.trigger().isEmpty());
        QVERIFY(shortcuts.canConfigure()); // the way to choose one

        // The user chooses a key in the desktop's settings.
        QSignalSpy changed(&shortcuts, &GlobalShortcuts::stateChanged);
        portal->changeTrigger(u"Meta+Shift+O"_s);
        QTRY_COMPARE(shortcuts.state(), GlobalShortcuts::State::Active);
        QCOMPARE(shortcuts.trigger(), u"Meta+Shift+O"_s);
        QVERIFY(!changed.isEmpty());
    }

    // Windows (ADR-022, mocks/windows.html): Omnidict registers the recorded key itself.
    void aRecordedKeyIsRegisteredAndFires()
    {
        auto backend = std::make_unique<omnidict::tests::FakeHotkey>();
        omnidict::tests::FakeHotkey* fake = backend.get();
        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        shortcuts.useBackend(std::move(backend), u"Ctrl+Alt+D"_s);
        QVERIFY(shortcuts.recordsKey());
        QVERIFY(!shortcuts.canConfigure()); // no desktop dialog: Settings records the key
        shortcuts.start();
        QCOMPARE(shortcuts.state(), GlobalShortcuts::State::Active);
        QCOMPARE(shortcuts.trigger(), u"Ctrl+Alt+D"_s);
        QCOMPARE(fake->registered(), u"Ctrl+Alt+D"_s);

        QSignalSpy activated(&shortcuts, &GlobalShortcuts::activated);
        fake->press();
        QCOMPARE(activated.size(), 1);
    }

    void aNewKeyIsKeptAndAnEmptyOneTurnsItOff()
    {
        auto backend = std::make_unique<omnidict::tests::FakeHotkey>();
        omnidict::tests::FakeHotkey* fake = backend.get();
        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        shortcuts.useBackend(std::move(backend), u"Ctrl+Alt+D"_s);
        shortcuts.start();
        QSignalSpy keyChanged(&shortcuts, &GlobalShortcuts::keyChanged);

        shortcuts.setKey(u"Ctrl+Shift+K"_s);
        QCOMPARE(shortcuts.state(), GlobalShortcuts::State::Active);
        QCOMPARE(fake->registered(), u"Ctrl+Shift+K"_s);
        QCOMPARE(keyChanged.size(), 1);
        QCOMPARE(keyChanged.at(0).at(0).toString(), u"Ctrl+Shift+K"_s);

        shortcuts.setKey({}); // Backspace in Settings
        QCOMPARE(shortcuts.state(), GlobalShortcuts::State::NoKey);
        QVERIFY(fake->registered().isEmpty());
        QCOMPARE(keyChanged.size(), 2);
        QVERIFY(keyChanged.at(1).at(0).toString().isEmpty());
    }

    void aKeyAnotherAppHoldsIsTaken()
    {
        auto backend = std::make_unique<omnidict::tests::FakeHotkey>(QStringList{u"Ctrl+Alt+D"_s});
        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        shortcuts.useBackend(std::move(backend), u"Ctrl+Alt+D"_s);
        shortcuts.start();
        QCOMPARE(shortcuts.state(), GlobalShortcuts::State::Taken);
        QCOMPARE(shortcuts.key(), u"Ctrl+Alt+D"_s); // kept, so Settings shows which key
        QVERIFY(shortcuts.trigger().isEmpty());     // and the tray shows none

        shortcuts.setKey(u"Ctrl+Shift+D"_s);
        QCOMPARE(shortcuts.state(), GlobalShortcuts::State::Active);
    }

    void pausingReleasesTheKeyWhileSettingsListens()
    {
        auto backend = std::make_unique<omnidict::tests::FakeHotkey>();
        omnidict::tests::FakeHotkey* fake = backend.get();
        GlobalShortcuts shortcuts(u"com.ktechpit.omnidict"_s);
        shortcuts.useBackend(std::move(backend), u"Ctrl+Alt+D"_s);
        shortcuts.start();
        shortcuts.pause();
        QVERIFY(fake->registered().isEmpty());
        shortcuts.resume();
        QCOMPARE(fake->registered(), u"Ctrl+Alt+D"_s);
        QCOMPARE(shortcuts.state(), GlobalShortcuts::State::Active);
    }

private:
    /// A fake portal on the private bus; null without dbus-daemon.
    [[nodiscard]] std::unique_ptr<omnidict::tests::FakePortal> makePortal() const
    {
        if (m_bus->address().isEmpty()) {
            return {};
        }
        auto portal = std::make_unique<omnidict::tests::FakePortal>(m_bus->address());
        return portal->isRegistered() ? std::move(portal) : nullptr;
    }

    std::unique_ptr<omnidict::tests::PrivateBus> m_bus;
};

QTEST_GUILESS_MAIN(TestGlobalShortcuts)
#include "tst_global_shortcuts.moc"
