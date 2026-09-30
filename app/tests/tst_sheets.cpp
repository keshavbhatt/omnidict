#include "core/settings.h"
#include "services/lookup_service.h"
#include "ui/about_dialog.h"
#include "ui/bug_report_dialog.h"
#include "ui/diagnostics.h"
#include "ui/settings_dialog.h"
#include "ui/shortcuts_dialog.h"
#include "ui/style.h"
#include "ui/switch_button.h"
#include "ui/whats_new_dialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <algorithm>

using namespace Qt::StringLiterals;
using omnidict::core::Settings;
using omnidict::core::Theme;
using omnidict::services::DictionaryInfo;
using namespace omnidict::ui;

namespace {

QList<DictionaryInfo> sampleDictionaries()
{
    return {
        {.dictId = u"wikt-es-en"_s,
         .name = u"Spanish - English"_s,
         .attribution = u"Wiktionary, the free dictionary"_s,
         .license = u"CC BY-SA 4.0"_s,
         .licenseUrl = u"https://creativecommons.org/licenses/by-sa/4.0/"_s,
         .version = u"2026.09.27"_s,
         .entryCount = 110538},
    };
}

/// Renders `widget` to a PNG under OMNIDICT_GRAB_DIR; the caller checks that
/// variable is set before calling this.
void grab(QWidget* widget, const QString& name)
{
    const QString dir = QString::fromUtf8(qgetenv("OMNIDICT_GRAB_DIR"));
    QDir().mkpath(dir);
    widget->show();
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    widget->grab().save(dir + u'/' + name + u".png"_s);
    widget->hide();
}

// isVisible() reflects the whole ancestor chain (a row hidden by the filter
// hides its label too), so the dialog must actually be shown first.
bool anyLabelVisible(const QWidget* root, const QString& text)
{
    const auto labels = root->findChildren<QLabel*>();
    return std::ranges::any_of(
        labels, [&text](const QLabel* label) { return label->text() == text && label->isVisible(); });
}

} // namespace

/// GUI tests (offscreen) for the sheets: settings, shortcuts, what's new, bug
/// report, diagnostics and about.
class TestSheets : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    [[nodiscard]] QString iniPath(const QString& name) const { return m_dir.filePath(name + u".ini"_s); }

private Q_SLOTS:
    void windowTitlesEndWithTheAppNameSoQtAddsNoDash()
    {
        QGuiApplication::setApplicationDisplayName(u"Omnidict"_s);
        QCOMPARE(omnidict::ui::titleWithApp(u"Settings"_s), u"Settings - Omnidict"_s);
        QCOMPARE(omnidict::ui::titleWithApp(u"About Omnidict"_s), u"About Omnidict"_s);
    }

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(u"Omnidict"_s);
        QCoreApplication::setApplicationVersion(u"0.1.0"_s);
    }

    void settingsDialogTogglesWriteSettings()
    {
        Settings settings(iniPath(u"settings"_s));
        SettingsDialog dialog(settings, u"/tmp/omnidict-dicts"_s);

        auto* systemButton = dialog.findChild<QToolButton*>(u"themeSystem"_s);
        auto* lightButton = dialog.findChild<QToolButton*>(u"themeLight"_s);
        auto* darkButton = dialog.findChild<QToolButton*>(u"themeDark"_s);
        QVERIFY(systemButton && lightButton && darkButton);
        QVERIFY(systemButton->isChecked());
        darkButton->click();
        QCOMPARE(settings.theme(), Theme::Dark);
        lightButton->click();
        QCOMPARE(settings.theme(), Theme::Light);

        auto* bestMatch = dialog.findChild<SwitchButton*>(u"showBestMatchSwitch"_s);
        auto* searchDefinitions = dialog.findChild<SwitchButton*>(u"searchDefinitionsSwitch"_s);
        auto* suggestSpellings = dialog.findChild<SwitchButton*>(u"suggestSpellingsSwitch"_s);
        auto* remember = dialog.findChild<SwitchButton*>(u"rememberHistorySwitch"_s);
        QVERIFY(bestMatch && searchDefinitions && suggestSpellings && remember);
        QVERIFY(bestMatch->isChecked());
        QVERIFY(searchDefinitions->isChecked());
        QVERIFY(suggestSpellings->isChecked());
        QVERIFY(remember->isChecked());

        bestMatch->click();
        QVERIFY(!settings.showBestMatch());
        searchDefinitions->click();
        QVERIFY(!settings.searchDefinitions());
        suggestSpellings->click();
        QVERIFY(!settings.suggestSpellings());

        QSignalSpy clearSpy(&dialog, &SettingsDialog::clearHistoryRequested);
        auto* clearButton = dialog.findChild<QPushButton*>(u"clearHistoryButton"_s);
        QVERIFY(clearButton);
        const bool rememberedBefore = settings.rememberHistory();
        clearButton->click();
        QCOMPARE(clearSpy.size(), 1);
        QCOMPARE(settings.rememberHistory(), rememberedBefore); // asks, never clears anything itself

        remember->click();
        QVERIFY(!settings.rememberHistory());
    }

    void settingsExplainTheCustomShortcutWhereTheDesktopHasNoGlobalOnes()
    {
        omnidict::core::Settings settings(m_dir.filePath(u"quick.ini"_s));
        SettingsDialog dialog(settings, u"/tmp/omnidict-dicts"_s); // no shortcut service: unavailable
        dialog.show();
        auto* shortcutRow = dialog.findChild<QWidget*>(u"quickLookupShortcutRow"_s);
        auto* fallbackRow = dialog.findChild<QWidget*>(u"quickLookupFallbackRow"_s);
        auto* command = dialog.findChild<QLabel*>(u"quickLookupCommand"_s);
        QVERIFY(shortcutRow != nullptr && fallbackRow != nullptr && command != nullptr);
        QVERIFY(!shortcutRow->isVisible());
        QVERIFY(fallbackRow->isVisible());
        QVERIFY(command->text().contains(u"--popup"_s));

        // The command matches how this copy was installed.
        qputenv("FLATPAK_ID", "com.ktechpit.omnidict");
        QCOMPARE(SettingsDialog::popupCommand(), u"flatpak run com.ktechpit.omnidict --popup"_s);
        qunsetenv("FLATPAK_ID");
        qputenv("SNAP_NAME", "omnidict");
        QCOMPARE(SettingsDialog::popupCommand(), u"snap run omnidict --popup"_s);
        qunsetenv("SNAP_NAME");
        qputenv("APPIMAGE", "/home/me/Omnidict.AppImage");
        QCOMPARE(SettingsDialog::popupCommand(), u"\"/home/me/Omnidict.AppImage\" --popup"_s);
        qunsetenv("APPIMAGE");
        QCOMPARE(SettingsDialog::popupCommand(), u"omnidict --popup"_s);

        // Changing the key: KDE's Shortcuts page from outside a sandbox, nothing from inside.
        const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP");
        qputenv("XDG_CURRENT_DESKTOP", "KDE");
        QCOMPARE(SettingsDialog::shortcutSettingsCommand(),
                 (QStringList{u"systemsettings"_s, u"kcm_keys"_s}));
        QVERIFY(!SettingsDialog::shortcutSettingsUrl().isValid()); // native: the program itself
        qputenv("SNAP", "/snap/omnidict/5");
        QVERIFY(SettingsDialog::shortcutSettingsCommand().isEmpty()); // a sandbox cannot start it,
        QCOMPARE(SettingsDialog::shortcutSettingsUrl(), QUrl(u"systemsettings://kcm_keys"_s)); // but opens it
        qunsetenv("SNAP");
        qputenv("XDG_CURRENT_DESKTOP", "GNOME");
        QVERIFY(SettingsDialog::shortcutSettingsCommand().isEmpty());
        QVERIFY(!SettingsDialog::shortcutSettingsUrl().isValid());
        qputenv("XDG_CURRENT_DESKTOP", desktop);

        // System Settings starts with the system's libraries, not the ones Omnidict runs on.
        qputenv("LD_LIBRARY_PATH", "/snap/kf6-core24/current/usr/lib");
        qputenv("QT_PLUGIN_PATH", "/snap/kf6-core24/current/usr/lib/qt6/plugins");
        const QProcessEnvironment environment = SettingsDialog::systemEnvironment();
        QVERIFY(!environment.contains(u"LD_LIBRARY_PATH"_s));
        QVERIFY(!environment.contains(u"QT_PLUGIN_PATH"_s));
        QVERIFY(environment.contains(u"PATH"_s));
    }

    void shortcutsFilterHidesRowsAndEmptyGroups()
    {
        ShortcutsDialog dialog(defaultShortcuts());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto* filter = dialog.findChild<QLineEdit*>(u"shortcutsFilter"_s);
        QVERIFY(filter);

        QVERIFY(anyLabelVisible(&dialog, u"Back"_s));
        QVERIFY(anyLabelVisible(&dialog, u"Quit"_s));

        QTest::keyClicks(filter, u"quit"_s);
        QVERIFY(anyLabelVisible(&dialog, u"Quit"_s));
        QVERIFY(!anyLabelVisible(&dialog, u"Back"_s));
        // The Entry group has no row matching "quit": its heading hides too.
        QVERIFY(!anyLabelVisible(&dialog, u"Entry"_s));
        QVERIFY(anyLabelVisible(&dialog, u"App"_s));

        filter->clear();
        QVERIFY(anyLabelVisible(&dialog, u"Back"_s));
    }

    void whatsNewListsTheReleaseAndShowsOnlyAfterAnUpdate()
    {
        WhatsNewDialog dialog(u"0.1.0"_s);
        auto* picker = dialog.findChild<QComboBox*>(u"versionPicker"_s);
        QVERIFY(picker);
        bool foundLatest = false;
        for (int i = 0; i < picker->count(); ++i) {
            if (picker->itemText(i).contains(u"0.1.0"_s)) {
                foundLatest = true;
            }
        }
        QVERIFY(foundLatest);
        QCOMPARE(dialog.shownVersion(), u"0.1.0"_s);

        Settings settings(iniPath(u"whatsnew"_s));
        QVERIFY(!WhatsNewDialog::shouldShowWhatsNew(settings, u"0.1.0"_s)); // first run: nothing seen yet
        settings.setWhatsNewSeenVersion(u"0.1.0"_s);
        QVERIFY(!WhatsNewDialog::shouldShowWhatsNew(settings, u"0.1.0"_s)); // already seen this version
        settings.setWhatsNewSeenVersion(u"0.0.9"_s);
        QVERIFY(WhatsNewDialog::shouldShowWhatsNew(settings, u"0.1.0"_s)); // seen an older version
    }

    void diagnosticsTextHasVersionAndDictionaryButNoHomePath()
    {
        const QString text = diagnosticsText(sampleDictionaries());
        QVERIFY(text.contains(u"0.1.0"_s));
        QVERIFY(text.contains(u"Spanish - English"_s));
        QVERIFY(text.contains(u"110538"_s));
        QVERIFY(!text.contains(QDir::homePath()));
    }

    void aboutShowsEveryDictionaryAndOpenSourceNames()
    {
        AboutDialog dialog(sampleDictionaries());
        const auto labels = dialog.findChildren<QLabel*>();
        QStringList texts;
        for (const QLabel* label : labels) {
            texts << label->text();
        }
        QVERIFY(texts.contains(u"Spanish - English"_s));
        QVERIFY(texts.contains(u"Qt"_s));
        QVERIFY(texts.contains(u"SQLite"_s));
    }

    /// Grabs every sheet to a PNG in both themes for visual review; a no-op
    /// unless OMNIDICT_GRAB_DIR is set.
    void visualGrabs()
    {
        if (qEnvironmentVariableIsEmpty("OMNIDICT_GRAB_DIR")) {
            QSKIP("set OMNIDICT_GRAB_DIR to grab screenshots");
        }
        Settings settings(iniPath(u"grabs"_s));
        for (const bool dark : {false, true}) {
            Tokens::setCurrentScheme(dark);
            qApp->setPalette(paletteFor(Tokens::current()));
            qApp->setStyleSheet(styleSheetFor(Tokens::current()));
            const QString suffix = dark ? u"-dark"_s : u"-light"_s;

            SettingsDialog settingsDialog(settings, u"/tmp/omnidict-dicts"_s);
            grab(&settingsDialog, u"settings"_s + suffix);

            ShortcutsDialog shortcutsDialog(defaultShortcuts());
            grab(&shortcutsDialog, u"shortcuts"_s + suffix);

            WhatsNewDialog whatsNewDialog(u"0.1.0"_s);
            grab(&whatsNewDialog, u"whats-new"_s + suffix);

            BugReportDialog bugReportDialog(diagnosticsText(sampleDictionaries()));
            grab(&bugReportDialog, u"bug-report"_s + suffix);

            AboutDialog aboutDialog(sampleDictionaries());
            grab(&aboutDialog, u"about"_s + suffix);
        }
    }
};

QTEST_MAIN(TestSheets)
#include "tst_sheets.moc"
