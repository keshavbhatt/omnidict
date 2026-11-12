#pragma once

#include "services/hotkey_backend.h"

#include <QString>
#include <QStringList>

namespace omnidict::tests {

/// Stands in for Windows' RegisterHotKey: refuses the keys in `taken`, as when another app
/// holds them, and records what is registered.
class FakeHotkey final : public services::HotkeyBackend
{
public:
    explicit FakeHotkey(QStringList taken = {})
        : m_taken(std::move(taken))
    {}

    bool registerKey(const QString& key) override
    {
        m_registered.clear();
        if (m_taken.contains(key)) {
            return false;
        }
        m_registered = key;
        return true;
    }

    void unregisterKey() override { m_registered.clear(); }

    /// The key registered now; empty for none.
    [[nodiscard]] QString registered() const { return m_registered; }
    void press() { Q_EMIT pressed(); }

private:
    QStringList m_taken;
    QString m_registered;
};

} // namespace omnidict::tests
