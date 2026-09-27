#pragma once

#include <QJsonObject>
#include <QObject>

#include <memory>

class QLocalServer;

namespace omnidict::app {

/// Single-instance guard and command channel (FEATURES E3), from the rewrite
/// kit. The first process to listen on the named local socket is the primary;
/// a later one finds it, forwards its command (the word to look up) and exits.
/// The key includes the profile directory, so scratch profiles run side by side.
class SingleInstance : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SingleInstance)

public:
    explicit SingleInstance(QString key, QObject* parent = nullptr);
    ~SingleInstance() override;

    [[nodiscard]] bool isPrimary() const { return m_primary; }

    /// Secondary only. Sends one command and waits briefly for delivery.
    bool sendToPrimary(const QJsonObject& command, int timeoutMs = 2000);

Q_SIGNALS:
    /// Primary only. Emitted for every command a secondary instance sent.
    void commandReceived(const QJsonObject& command);

private:
    [[nodiscard]] static bool primaryIsAlive(const QString& key, int timeoutMs);
    void startServer();
    void onNewConnection();

    QString m_key;
    bool m_primary = false;
    std::unique_ptr<QLocalServer> m_server;
};

/// Socket name for a profile directory: "omnidict-<hash>-<uid>" (the uid keeps
/// users apart on a shared /tmp).
[[nodiscard]] QString instanceKeyFor(const QString& profileDirectory);

} // namespace omnidict::app
