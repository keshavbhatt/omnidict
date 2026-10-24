#include "services/autostart.h"

#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using omnidict::services::Autostart;

/// Start at login (mocks/tray.html note 5): the autostart entry each kind of install writes.
class TestAutostart : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        for (const char* name : {"FLATPAK_ID", "SNAP", "SNAP_NAME", "SNAP_USER_DATA", "APPIMAGE"}) {
            qunsetenv(name);
        }
    }

    void aPlainInstallWritesAnXdgAutostartFile()
    {
        QCOMPARE(Autostart::method(), Autostart::Method::DesktopFile);
        Autostart autostart(u"com.ktechpit.omnidict"_s);
        const QString path = autostart.desktopFilePath();
        QCOMPARE(path, QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                           u"/autostart/com.ktechpit.omnidict.desktop"_s);
        QFile::remove(path);
        QVERIFY(!autostart.isEnabled());

        QSignalSpy finished(&autostart, &Autostart::finished);
        autostart.setEnabled(true);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), true);
        QCOMPARE(finished.at(0).at(1).toBool(), true);
        QVERIFY(autostart.isEnabled());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString contents = QString::fromUtf8(file.readAll());
        QVERIFY(contents.startsWith(u"[Desktop Entry]\n"_s));
        QVERIFY(
            contents.contains(u"\nExec=%1 --background\n"_s.arg(QCoreApplication::applicationFilePath())));
        QVERIFY(contents.contains(u"\nIcon=com.ktechpit.omnidict\n"_s));
        file.close();

        autostart.setEnabled(false);
        QCOMPARE(finished.count(), 2);
        QCOMPARE(finished.at(1).at(0).toBool(), false);
        QVERIFY(!QFile::exists(path));
        QVERIFY(!autostart.isEnabled());
    }

    void anAppImageRunsItselfQuotedWhenItHasToBe()
    {
        qputenv("APPIMAGE", "/home/a user/Apps/Omnidict-1.0.1-x86_64.AppImage");
        QCOMPARE(Autostart::command(),
                 (QStringList{u"/home/a user/Apps/Omnidict-1.0.1-x86_64.AppImage"_s, u"--background"_s}));
        const Autostart autostart(u"com.ktechpit.omnidict"_s);
        QVERIFY(autostart.desktopFileContents().contains(
            u"\nExec=\"/home/a user/Apps/Omnidict-1.0.1-x86_64.AppImage\" --background\n"_s));
    }

    void aSnapWritesIntoItsOwnConfigForSnapd()
    {
        const QTemporaryDir snapData;
        qputenv("SNAP", "/snap/omnidict/16");
        qputenv("SNAP_NAME", "omnidict");
        qputenv("SNAP_USER_DATA", snapData.path().toUtf8());
        const Autostart autostart(u"com.ktechpit.omnidict"_s);
        QCOMPARE(autostart.desktopFilePath(),
                 snapData.path() + u"/.config/autostart/com.ktechpit.omnidict.desktop"_s);
        QVERIFY(autostart.desktopFileContents().contains(u"\nExec=omnidict --background\n"_s));
    }

    void aFlatpakAsksThePortal()
    {
        qputenv("FLATPAK_ID", "com.ktechpit.omnidict");
        QCOMPARE(Autostart::method(), Autostart::Method::Portal);
        const Autostart autostart(u"com.ktechpit.omnidict"_s);
        QVERIFY(autostart.desktopFilePath().isEmpty());
        QVERIFY(!autostart.isEnabled()); // until the portal answers
    }
};

QTEST_GUILESS_MAIN(TestAutostart)
#include "tst_autostart.moc"
