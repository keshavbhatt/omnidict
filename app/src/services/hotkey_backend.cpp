#include "services/hotkey_backend.h"

#ifdef Q_OS_WIN
#include "services/logging.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QHash>
#include <QStringList>
#include <qt_windows.h>
#endif

namespace omnidict::services {

#ifdef Q_OS_WIN

using namespace Qt::StringLiterals;

namespace {

/// The key part of a portable key text in Windows' terms: letters, digits, F1 to F24 and a
/// few named keys, which is all Settings records (mocks/windows.html note 2).
UINT virtualKey(const QString& name)
{
    if (name.size() == 1) {
        const QChar c = name.at(0).toUpper();
        if ((c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9')) {
            return c.unicode();
        }
        return 0;
    }
    if (name.startsWith(u'F')) {
        bool ok = false;
        const int number = name.mid(1).toInt(&ok);
        if (ok && number >= 1 && number <= 24) {
            return VK_F1 + static_cast<UINT>(number - 1);
        }
    }
    static const QHash<QString, UINT> kNamed{
        {u"Space"_s, VK_SPACE}, {u"Ins"_s, VK_INSERT}, {u"Del"_s, VK_DELETE},  {u"Home"_s, VK_HOME},
        {u"End"_s, VK_END},     {u"PgUp"_s, VK_PRIOR}, {u"PgDown"_s, VK_NEXT}, {u"Left"_s, VK_LEFT},
        {u"Right"_s, VK_RIGHT}, {u"Up"_s, VK_UP},      {u"Down"_s, VK_DOWN},   {u"Pause"_s, VK_PAUSE},
    };
    return kNamed.value(name, 0);
}

class WindowsHotkey final : public HotkeyBackend, public QAbstractNativeEventFilter
{
public:
    WindowsHotkey() { QCoreApplication::instance()->installNativeEventFilter(this); }
    ~WindowsHotkey() override
    {
        unregisterKey();
        if (QCoreApplication* app = QCoreApplication::instance()) {
            app->removeNativeEventFilter(this);
        }
    }
    Q_DISABLE_COPY_MOVE(WindowsHotkey)

    bool registerKey(const QString& key) override
    {
        unregisterKey();
        UINT modifiers = MOD_NOREPEAT;
        UINT vk = 0;
        for (const QString& part : key.split(u'+')) {
            if (part == u"Ctrl") {
                modifiers |= MOD_CONTROL;
            } else if (part == u"Alt") {
                modifiers |= MOD_ALT;
            } else if (part == u"Shift") {
                modifiers |= MOD_SHIFT;
            } else if (part == u"Meta") {
                modifiers |= MOD_WIN;
            } else {
                vk = virtualKey(part);
            }
        }
        if (vk == 0) {
            qCWarning(lcShortcuts) << "hotkey: no Windows key for" << key;
            return false;
        }
        // No window: WM_HOTKEY goes to this thread's queue, which Qt's dispatcher reads.
        if (RegisterHotKey(nullptr, kId, modifiers, vk) == 0) {
            qCInfo(lcShortcuts) << "hotkey: Windows refused" << key << "error" << GetLastError();
            return false;
        }
        m_registered = true;
        return true;
    }

    void unregisterKey() override
    {
        if (m_registered) {
            UnregisterHotKey(nullptr, kId);
            m_registered = false;
        }
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* /*result*/) override
    {
        if (eventType != "windows_generic_MSG") {
            return false;
        }
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == kId) {
            Q_EMIT pressed();
            return true;
        }
        return false;
    }

private:
    static constexpr int kId = 1;
    bool m_registered = false;
};

} // namespace

std::unique_ptr<HotkeyBackend> makeNativeHotkeyBackend()
{
    return std::make_unique<WindowsHotkey>();
}

#else

std::unique_ptr<HotkeyBackend> makeNativeHotkeyBackend()
{
    return nullptr; // the desktop's GlobalShortcuts portal (ADR-019)
}

#endif

} // namespace omnidict::services
