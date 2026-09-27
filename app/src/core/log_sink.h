#pragma once

#include <QString>
#include <QStringList>

// Process-wide log sink (FEATURES E2). Installs a Qt message handler that
// formats every message once, forwards it to the previous handler (stderr /
// QTest), appends it to an optional log file and keeps the last N lines in
// memory for "Copy diagnostics".
namespace omnidict::core {

class LogSink
{
public:
    /// Idempotent. Safe to call before QCoreApplication exists.
    static void install();

    /// Starts (or switches) file logging. Empty path disables the file.
    /// Rotates `<path>` to `<path>.1` when it exceeds `maxBytes`.
    static void setLogFile(const QString& path, qint64 maxBytes = qint64{2} * 1024 * 1024);
    [[nodiscard]] static QString logFilePath();

    /// Most recent lines, oldest first (capacity 1000).
    [[nodiscard]] static QStringList recentLines();
};

} // namespace omnidict::core
