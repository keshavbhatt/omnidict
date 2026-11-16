// Omnidict Pro on the Microsoft Store build (ADR-024, mocks/pro.html, mocks/pro-prompts.html),
// with a stand-in for the licensing module.

#include "core/settings.h"
#include "fake_pro.h"
#include "ui/free_limit.h"
#include "ui/pro_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/style.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using omnidict::services::DictionaryInfo;
using omnidict::services::ProStatus;
using omnidict::tests::FakePro;
using omnidict::ui::ProDialog;
using Tier = ProStatus::Tier;

namespace {

DictionaryInfo dictionary(const QString& dictId)
{
    DictionaryInfo info;
    info.dictId = dictId;
    info.name = dictId.toUpper();
    return info;
}

/// Stands in for the browser.
class UrlCatcher : public QObject
{
    Q_OBJECT

public:
    QList<QUrl> urls;

public Q_SLOTS:
    void open(const QUrl& url) { urls.append(url); }
};

bool visible(const QWidget* dialog, const QString& name)
{
    const auto* widget = dialog->findChild<QWidget*>(name);
    return widget != nullptr && widget->isVisibleTo(dialog);
}

/// Renders `widget` under OMNIDICT_GRAB_DIR, for comparing with the mock.
void grab(QWidget* widget, const QString& name)
{
    if (qEnvironmentVariableIsEmpty("OMNIDICT_GRAB_DIR")) {
        return;
    }
    const QString dir = qEnvironmentVariable("OMNIDICT_GRAB_DIR");
    QDir().mkpath(dir);
    widget->show();
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    widget->grab().save(dir + u'/' + name + u".png"_s);
    widget->hide();
}

} // namespace

class TestPro : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        omnidict::ui::Tokens::setCurrentScheme(true);
        qApp->setStyleSheet(omnidict::ui::styleSheetFor(omnidict::ui::Tokens::current()));
    }

    void freeKeepsTheFirstDictionaryInTheUsersOrder()
    {
        // mocks/pro-prompts.html note 3.
        const QList<DictionaryInfo> installed{dictionary(u"a"_s), dictionary(u"b"_s), dictionary(u"c"_s),
                                              dictionary(u"d"_s)};
        const QStringList order{u"c"_s, u"a"_s, u"b"_s};
        QCOMPARE(omnidict::ui::free_limit::switchedOn(installed, order, {u"b"_s}),
                 (QStringList{u"c"_s, u"a"_s, u"d"_s}));
        const QStringList disabled = omnidict::ui::free_limit::disabledForFree(installed, order, {u"b"_s});
        QCOMPARE(omnidict::ui::free_limit::switchedOn(installed, order, disabled), QStringList{u"c"_s});
        QVERIFY(disabled.contains(u"b"_s)); // already off stays off
        // Nothing on, or one on: nothing changes.
        QCOMPARE(omnidict::ui::free_limit::disabledForFree(installed, order, {u"a"_s, u"b"_s, u"d"_s}),
                 (QStringList{u"a"_s, u"b"_s, u"d"_s}));
    }

    void theSheetNamesEveryState_data()
    {
        QTest::addColumn<int>("tier");
        QTest::addColumn<int>("daysPaid"); // 0: Forever or none
        QTest::addColumn<int>("state");
        QTest::addColumn<bool>("table");
        QTest::addColumn<bool>("choose");
        QTest::addColumn<bool>("moreTime");
        QTest::newRow("trial") << static_cast<int>(Tier::Trial) << 0
                               << static_cast<int>(ProDialog::State::Trial) << true << true << false;
        QTest::newRow("free") << static_cast<int>(Tier::Free) << 0 << static_cast<int>(ProDialog::State::Free)
                              << true << true << false;
        QTest::newRow("forever") << static_cast<int>(Tier::Pro) << 0
                                 << static_cast<int>(ProDialog::State::Forever) << false << false << false;
        QTest::newRow("year") << static_cast<int>(Tier::Pro) << 300
                              << static_cast<int>(ProDialog::State::Until) << false << false << true;
        QTest::newRow("soon") << static_cast<int>(Tier::Pro) << 5
                              << static_cast<int>(ProDialog::State::EndsSoon) << true << true << false;
        QTest::newRow("ended") << static_cast<int>(Tier::Free) << -3
                               << static_cast<int>(ProDialog::State::Ended) << true << true << false;
    }

    void theSheetNamesEveryState()
    {
        // mocks/pro.html: the status box, the table only without Pro, Choose a plan or Add more
        // time.
        QFETCH(int, tier);
        QFETCH(int, daysPaid);
        QFETCH(int, state);
        QFETCH(bool, table);
        QFETCH(bool, choose);
        QFETCH(bool, moreTime);
        FakePro pro;
        pro.set(static_cast<Tier>(tier),
                daysPaid != 0 ? QDateTime::currentDateTime().addDays(daysPaid).addSecs(3600) : QDateTime());
        QCOMPARE(static_cast<int>(ProDialog::stateOf(pro)), state);
        ProDialog dialog(pro);
        QCOMPARE(visible(&dialog, u"proTable"_s), table);
        QCOMPARE(visible(&dialog, u"proPass"_s), table);
        QCOMPARE(visible(&dialog, u"proChoose"_s), choose);
        QCOMPARE(visible(&dialog, u"proMoreTime"_s), moreTime);
        QCOMPARE(dialog.findChild<QLabel*>(u"proAccountId"_s)->text(), pro.accountId());
        const QString title = dialog.findChild<QLabel*>(u"proStatusTitle"_s)->text();
        QVERIFY2(!title.contains(u'('), qPrintable(title)); // no "day(s)"
        grab(&dialog, u"pro-"_s + QString::fromLatin1(QTest::currentDataTag()));
    }

    void chooseAPlanAndThePassOpenTheirPages()
    {
        FakePro pro(Tier::Free);
        ProDialog dialog(pro);
        UrlCatcher browser;
        QDesktopServices::setUrlHandler(u"https"_s, &browser, "open");
        dialog.findChild<QPushButton*>(u"proChoose"_s)->click();
        dialog.findChild<QPushButton*>(u"proPassGet"_s)->click();
        dialog.findChild<QPushButton*>(u"proManage"_s)->click();
        QDesktopServices::unsetUrlHandler(u"https"_s);
        QCOMPARE(browser.urls.size(), 3);
        QVERIFY(browser.urls.at(0).query().contains(u"source=sheet"_s));
        QCOMPARE(browser.urls.at(1), pro.passCheckoutUrl());
        QCOMPARE(browser.urls.at(2), pro.portalUrl());
        Q_EMIT dialog.findChild<QLabel*>(u"proChecked"_s)->linkActivated(u"check"_s);
        QCOMPARE(pro.refreshes, 1); // Check again
    }

    void settingsMarksQuickLookupAsProOnFree()
    {
        // mocks/pro-prompts.html note 2.
        QTemporaryDir dir;
        omnidict::core::Settings settings(dir.filePath(u"settings.ini"_s));
        FakePro pro(Tier::Free);
        omnidict::ui::SettingsDialog freeDialog(settings, dir.path());
        freeDialog.setProStatus(&pro);
        QVERIFY(freeDialog.findChild<QLabel*>(u"quickLookupProMark"_s) != nullptr);
        pro.set(Tier::Trial);
        omnidict::ui::SettingsDialog trialDialog(settings, dir.path());
        trialDialog.setProStatus(&pro);
        QVERIFY(trialDialog.findChild<QLabel*>(u"quickLookupProMark"_s) == nullptr);
        omnidict::ui::SettingsDialog linuxDialog(settings, dir.path());
        linuxDialog.setProStatus(nullptr);
        QVERIFY(linuxDialog.findChild<QLabel*>(u"quickLookupProMark"_s) == nullptr);
    }
};

QTEST_MAIN(TestPro)
#include "tst_pro.moc"
