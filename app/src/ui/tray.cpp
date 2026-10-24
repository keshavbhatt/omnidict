#include "ui/tray.h"

#include "core/settings.h"
#include "services/autostart.h"
#include "services/global_shortcuts.h"
#include "ui/icons.h"
#include "ui/main_window.h"
#include "ui/style.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QKeySequence>
#include <QMenu>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

constexpr int kNoticeMs = 8000;

/// The colour app icon, sent to the panel as pixels: a theme icon goes as a name, which the
/// desktop cannot find for a snap or a Flatpak (their icons are inside the sandbox), and the
/// panel then shows nothing.
QIcon trayIcon()
{
    return icons::brand();
}

} // namespace

Tray::Tray(MainWindow& window, core::Settings& settings, services::Autostart& autostart, bool available,
           QObject* parent)
    : QObject(parent)
    , m_window(window)
    , m_settings(settings)
    , m_autostart(autostart)
    , m_available(available)
    , m_icon(new QSystemTrayIcon(trayIcon(), this))
    , m_menu(new QMenu)
{
    m_icon->setToolTip(u"Omnidict"_s);
    buildMenu();
    m_icon->setContextMenu(m_menu);
    refreshIcons();
    qApp->installEventFilter(this); // the theme changed: the menu's glyphs follow
    connect(m_icon, &QSystemTrayIcon::activated, this, &Tray::onActivated);
    connect(&m_settings, &core::Settings::keepInTrayChanged, this, [this] {
        update();
        if (!keepsRunning() && !m_window.isVisible()) {
            m_window.bringToFront(); // the tray went away with the window hidden in it
        }
    });
    connect(&m_autostart, &services::Autostart::finished, this, [this](bool enabled, bool /*ok*/) {
        m_settings.setStartAtLogin(enabled);
        Q_EMIT startAtLoginSettled(enabled);
    });
    // The user may have removed the entry in the desktop's settings; a file-based entry is
    // rewritten so it runs this copy (an AppImage that moved, a new version).
    if (services::Autostart::method() == services::Autostart::Method::DesktopFile) {
        if (m_settings.startAtLogin() && m_autostart.isEnabled()) {
            m_autostart.setEnabled(true);
        } else {
            m_settings.setStartAtLogin(m_autostart.isEnabled());
        }
    }
    window.setTray(this);
    update();
}

Tray::~Tray()
{
    delete m_menu; // not parented: a QMenu's parent would have to be a widget
}

bool Tray::systemTrayAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

bool Tray::keepsRunning() const
{
    return m_available && m_settings.keepInTray();
}

void Tray::buildMenu()
{
    auto* title = m_menu->addAction(icons::brand(), u"Omnidict"_s);
    title->setEnabled(false);
    m_menu->addSeparator();
    QAction* open = m_menu->addAction(tr("Open Omnidict"), &m_window, &MainWindow::bringToFront);
    open->setObjectName(u"trayOpen"_s);
    m_glyphs.append({open, u"book"_s});
    m_quickLookup =
        m_menu->addAction(tr("Quick lookup"), &m_window, [this] { m_window.showQuickLookup({}, {}, false); });
    m_quickLookup->setObjectName(u"trayQuickLookup"_s);
    m_glyphs.append({m_quickLookup, u"search"_s});
    m_menu->addSeparator();
    m_glyphs.append({m_menu->addAction(tr("Dictionaries..."), &m_window,
                                       [this] {
                                           m_window.bringToFront();
                                           m_window.showDictionaries();
                                       }),
                     u"books"_s});
    m_glyphs.append({m_menu->addAction(tr("Settings..."), &m_window,
                                       [this] {
                                           m_window.bringToFront();
                                           m_window.showSettings();
                                       }),
                     u"settings"_s});
    m_menu->addSeparator();
    QAction* quit = m_menu->addAction(tr("Quit Omnidict"), &m_window, &MainWindow::quit);
    quit->setObjectName(u"trayQuit"_s);
    m_glyphs.append({quit, u"power"_s});
}

void Tray::refreshIcons()
{
    // The main menu's glyphs, in the same colour (mocks/tray.html).
    const QColor color = Tokens::current().muted;
    for (const auto& [action, glyph] : std::as_const(m_glyphs)) {
        action->setIcon(icons::themed(glyph, color));
    }
}

bool Tray::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange) {
        refreshIcons();
    }
    return QObject::eventFilter(watched, event);
}

void Tray::update()
{
    const bool keep = keepsRunning();
    m_icon->setVisible(keep);
    // Without the tray, closing the main window ends the app as before.
    QApplication::setQuitOnLastWindowClosed(!keep);
}

void Tray::setGlobalShortcuts(services::GlobalShortcuts* shortcuts)
{
    if (m_shortcuts != nullptr) {
        disconnect(m_shortcuts, nullptr, this, nullptr);
    }
    m_shortcuts = shortcuts;
    if (shortcuts != nullptr) {
        connect(shortcuts, &services::GlobalShortcuts::stateChanged, this, &Tray::updateQuickLookupKey);
    }
    updateQuickLookupKey();
}

void Tray::updateQuickLookupKey()
{
    // The key the desktop bound, shown beside the item (mocks/tray.html note 2); none
    // without one. Only a label: the desktop owns the key.
    QKeySequence key;
    if (const services::GlobalShortcuts* shortcuts = m_shortcuts.data();
        shortcuts != nullptr && shortcuts->state() == services::GlobalShortcuts::State::Active) {
        // The first key when the desktop bound several ("Meta+Shift+O, Ctrl+Alt+D").
        key = QKeySequence::fromString(shortcuts->trigger().section(u", "_s, 0, 0), QKeySequence::NativeText);
    }
    m_quickLookup->setShortcut(key);
    m_quickLookup->setShortcutContext(Qt::WidgetShortcut);
}

void Tray::showFirstCloseNotice()
{
    if (m_settings.trayNoticeShown()) {
        return;
    }
    m_settings.setTrayNoticeShown(true);
    m_icon->showMessage(tr("Omnidict is still running"),
                        tr("Quick lookup keeps working. Open or quit Omnidict from its icon in the panel."),
                        icons::brand(), kNoticeMs);
}

void Tray::setStartAtLogin(bool enabled)
{
    m_autostart.setEnabled(enabled);
}

void Tray::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason != QSystemTrayIcon::Trigger) {
        return; // the context menu opens by itself
    }
    // A left click shows the window, or hides it when it is already in front (note 1).
    if (m_window.isVisible() && m_window.isActiveWindow()) {
        m_window.hide();
        return;
    }
    m_window.bringToFront();
}

} // namespace omnidict::ui
