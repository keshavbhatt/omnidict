#include "core/log_sink.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using omnidict::core::LogSink;

namespace {
Q_LOGGING_CATEGORY(lcTest, "omnidict.test")
} // namespace

class TestLogSink : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

private Q_SLOTS:
    void initTestCase()
    {
        LogSink::install();
        LogSink::install(); // idempotent
    }

    void keepsRecentLinesWithCategoryAndLevel()
    {
        qCWarning(lcTest) << "first line";
        const QStringList lines = LogSink::recentLines();
        QVERIFY(!lines.isEmpty());
        QVERIFY(lines.last().contains(u"[omnidict.test] warning:"_s));
        QVERIFY(lines.last().endsWith(u"first line"_s));
    }

    void writesAndRotatesTheFile()
    {
        const QString path = m_dir.filePath(u"logs/omnidict.log"_s);
        constexpr qint64 kSmall = 200;
        LogSink::setLogFile(path, kSmall);
        QCOMPARE(LogSink::logFilePath(), path);
        for (int i = 0; i < 20; ++i) {
            qCWarning(lcTest) << "line" << i;
        }
        QVERIFY(QFile::exists(path));
        QVERIFY(QFile::exists(path + u".1"_s));
        QVERIFY(QFileInfo(path).size() < kSmall * 2);
        LogSink::setLogFile({});
        QVERIFY(LogSink::logFilePath().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLogSink)
#include "tst_log_sink.moc"
