#include "core/settings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace omnidict::core;

class TestSettings : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    [[nodiscard]] QString iniPath(const QString& name) const { return m_dir.filePath(name + u".ini"_s); }

private Q_SLOTS:
    void defaults()
    {
        const Settings settings(iniPath(u"defaults"_s));
        QCOMPARE(settings.theme(), Theme::System);
        QCOMPARE(settings.entryTextSize(), Settings::kDefaultEntryTextSize);
        QVERIFY(settings.showBestMatch());
        QVERIFY(settings.searchDefinitions());
        QVERIFY(settings.suggestSpellings());
        QVERIFY(settings.rememberHistory());
        QVERIFY(settings.dictionaryFilter().isEmpty());
        QVERIFY(settings.whatsNewSeenVersion().isEmpty());
        QVERIFY(settings.windowGeometry().isEmpty());
    }

    void valuesSurviveARestart()
    {
        {
            Settings settings(iniPath(u"restart"_s));
            settings.setTheme(Theme::Dark);
            settings.setEntryTextSize(18);
            settings.setSuggestSpellings(false);
            settings.setDictionaryFilter(u"wikt-es-en"_s);
            settings.setWhatsNewSeenVersion(u"0.1.0"_s);
        }
        const Settings settings(iniPath(u"restart"_s));
        QCOMPARE(settings.theme(), Theme::Dark);
        QCOMPARE(settings.entryTextSize(), 18);
        QVERIFY(!settings.suggestSpellings());
        QCOMPARE(settings.dictionaryFilter(), u"wikt-es-en"_s);
        QCOMPARE(settings.whatsNewSeenVersion(), u"0.1.0"_s);
    }

    void settersSignalOnlyRealChanges()
    {
        Settings settings(iniPath(u"signals"_s));
        QSignalSpy theme(&settings, &Settings::themeChanged);
        QSignalSpy search(&settings, &Settings::searchOptionsChanged);
        QSignalSpy filter(&settings, &Settings::dictionaryFilterChanged);
        QSignalSpy history(&settings, &Settings::rememberHistoryChanged);
        settings.setTheme(Theme::System); // the default already
        QCOMPARE(theme.size(), 0);
        settings.setTheme(Theme::Light);
        settings.setTheme(Theme::Light);
        QCOMPARE(theme.size(), 1);
        settings.setShowBestMatch(false);
        settings.setSearchDefinitions(false);
        settings.setSuggestSpellings(true);
        QCOMPARE(search.size(), 2);
        settings.setDictionaryFilter(u"wikt-en"_s);
        QCOMPARE(filter.size(), 1);
        settings.setRememberHistory(false);
        QCOMPARE(history.size(), 1);
    }

    void dictionaryPreferencesRoundTrip()
    {
        {
            Settings settings(iniPath(u"dicts"_s));
            QSignalSpy changed(&settings, &Settings::dictionaryPreferencesChanged);
            settings.setDictionaryOrder({u"wikt-hi-en"_s, u"wikt-en"_s});
            settings.setDisabledDictionaries({u"wikt-es-en"_s});
            settings.setDisabledDictionaries({u"wikt-es-en"_s});
            QCOMPARE(changed.size(), 2);
        }
        const Settings settings(iniPath(u"dicts"_s));
        QCOMPARE(settings.dictionaryOrder(), (QStringList{u"wikt-hi-en"_s, u"wikt-en"_s}));
        QCOMPARE(settings.disabledDictionaries(), QStringList{u"wikt-es-en"_s});
    }

    void textSizeIsClamped()
    {
        Settings settings(iniPath(u"size"_s));
        QSignalSpy size(&settings, &Settings::entryTextSizeChanged);
        settings.setEntryTextSize(1000);
        QCOMPARE(settings.entryTextSize(), Settings::kMaxEntryTextSize);
        settings.setEntryTextSize(1);
        QCOMPARE(settings.entryTextSize(), Settings::kMinEntryTextSize);
        QCOMPARE(size.size(), 2);
    }

    void aDamagedThemeFallsBackToSystem()
    {
        const QString path = iniPath(u"damaged"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[appearance]\ntheme=42\nentryTextSize=9999\n");
        }
        const Settings settings(path);
        QCOMPARE(settings.theme(), Theme::System);
        QCOMPARE(settings.entryTextSize(), Settings::kMaxEntryTextSize);
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
