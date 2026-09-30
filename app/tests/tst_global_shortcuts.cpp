#include "services/global_shortcuts.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using omnidict::services::GlobalShortcuts;

/// The Quick Lookup shortcut service. The desktop portal itself cannot run here, so
/// the tests pin the parts that do not need it; they point D-Bus at a bus that does not
/// exist first, so nothing ever reaches the real session's portal (and its dialogs).
class TestGlobalShortcuts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/omnidict-test-bus"); }

    void requestPathsFollowThePortalSpec()
    {
        // The Request object's path is known before the call, from the caller's unique name.
        QCOMPARE(GlobalShortcuts::requestPath(u":1.42"_s, u"omnidict7"_s),
                 u"/org/freedesktop/portal/desktop/request/1_42/omnidict7"_s);
        QCOMPARE(GlobalShortcuts::requestPath(u":1.1024"_s, u"t"_s),
                 u"/org/freedesktop/portal/desktop/request/1_1024/t"_s);
    }

    void withoutADesktopBusItIsUnavailable()
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
};

QTEST_GUILESS_MAIN(TestGlobalShortcuts)
#include "tst_global_shortcuts.moc"
