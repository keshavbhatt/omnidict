#include "app/single_instance.h"

#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <future>

using namespace Qt::StringLiterals;
using omnidict::app::instanceKeyFor;
using omnidict::app::SingleInstance;

class TestSingleInstance : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keysDifferPerProfileAndStayStable()
    {
        QCOMPARE(instanceKeyFor(u"/tmp/a"_s), instanceKeyFor(u"/tmp/a"_s));
        QVERIFY(instanceKeyFor(u"/tmp/a"_s) != instanceKeyFor(u"/tmp/b"_s));
        QVERIFY(instanceKeyFor(u"/tmp/a"_s).startsWith(u"omnidict-"_s));
    }

    void theSecondInstanceHandsItsWordToTheFirst()
    {
        const QString key = instanceKeyFor(QUuid::createUuid().toString());
        const SingleInstance first(key);
        QVERIFY(first.isPrimary());
        QSignalSpy received(&first, &SingleInstance::commandReceived);

        SingleInstance second(key);
        QVERIFY(!second.isPrimary());
        // Sent from another thread, as a second process would: on Windows a local socket is a
        // named pipe, and a blocking write from the primary's own thread waits for an accept
        // that thread cannot run (as PixelFx found).
        auto sent = std::async(std::launch::async,
                               [&second] { return second.sendToPrimary({{u"query"_s, u"perro"_s}}); });
        QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 5000);
        QVERIFY(sent.get());
        QCOMPARE(received.first().first().toJsonObject().value(u"query"_s).toString(), u"perro"_s);
    }

    void aFreedKeyCanBeTakenAgain()
    {
        const QString key = instanceKeyFor(QUuid::createUuid().toString());
        {
            const SingleInstance first(key);
            QVERIFY(first.isPrimary());
        }
        const SingleInstance next(key);
        QVERIFY(next.isPrimary());
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_single_instance.moc"
