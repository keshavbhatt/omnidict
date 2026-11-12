#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace omnidict::services {

/// A system-wide key the app registers itself, where the desktop has no service that lets
/// the user pick it (Windows: RegisterHotKey, ADR-022). GlobalShortcuts drives it; tests use
/// a fake one.
class HotkeyBackend : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(HotkeyBackend)

public:
    using QObject::QObject;
    ~HotkeyBackend() override = default;

    /// Registers `key`, in QKeySequence's portable text ("Ctrl+Alt+D"), replacing the key
    /// registered before; false when the system refuses it (another app holds it).
    virtual bool registerKey(const QString& key) = 0;
    /// Releases the registered key, if any.
    virtual void unregisterKey() = 0;

Q_SIGNALS:
    void pressed();
};

/// This platform's own backend; null where the desktop's portal is used instead (Linux).
[[nodiscard]] std::unique_ptr<HotkeyBackend> makeNativeHotkeyBackend();

} // namespace omnidict::services
