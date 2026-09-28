#include "app/debug_hooks.h"

#include "services/dictionary_manager.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QHash>
#include <QLineEdit>
#include <QPixmap>
#include <QTimer>

#include <functional>

using namespace Qt::StringLiterals;

namespace omnidict::app {

namespace {
constexpr int kSettleMs = 400;         // let the entry lay out before the grab
constexpr int kGiveUpMs = 15'000;      // quit even if no entry ever shows
constexpr int kCatalogSettleMs = 1500; // the Available tab lays out hundreds of rows
constexpr int kGrabFailedExit = 3;

/// OMNIDICT_DEBUG_INSTALL: the whole install path against the real catalogue.
void installWhenListed(ui::MainWindow& window, services::DictionaryManager& manager, const QString& dictId)
{
    const auto tryInstall = [&manager, dictId] {
        for (const core::CatalogEntry& entry : manager.catalog().dictionaries) {
            if (entry.dictId == dictId) {
                manager.install(entry);
                return true;
            }
        }
        return false;
    };
    if (!tryInstall()) {
        QObject::connect(&manager, &services::DictionaryManager::catalogChanged, &window, tryInstall,
                         Qt::SingleShotConnection);
        QObject::connect(&manager, &services::DictionaryManager::catalogFailed, &window,
                         [](const QString& reason) {
                             qWarning("catalogue unavailable: %s", qPrintable(reason));
                             QCoreApplication::exit(kGrabFailedExit);
                         });
        manager.refreshCatalog(true);
    }
    QObject::connect(&manager, &services::DictionaryManager::downloadChanged, &window,
                     [](const services::DownloadStatus& status) {
                         if (status.state == services::DownloadStatus::Failed) {
                             qWarning("install failed: %s", qPrintable(status.error));
                             QCoreApplication::exit(kGrabFailedExit);
                         }
                     });
    // Installed, and the window has reopened its dictionaries with it.
    QObject::connect(&manager, &services::DictionaryManager::installed, &window, [&window] {
        QObject::connect(
            &window, &ui::MainWindow::libraryReady, &window, [] { QCoreApplication::exit(0); },
            Qt::SingleShotConnection);
    });
}

/// OMNIDICT_DEBUG_OPEN names a sheet: grab it from inside its own modal loop, then quit.
/// A catalogue still being read (the Dictionaries sheet on a fresh profile) is waited for,
/// so the grab shows the list rather than "Refreshing...". True when it named a sheet.
bool grabSheet(ui::MainWindow& window, services::DictionaryManager& manager, const QString& sheet,
               const QString& grabPath)
{
    using Open = std::function<void(ui::MainWindow&)>;
    const QHash<QString, Open> sheets = {
        {u"about"_s, [](ui::MainWindow& w) { w.showAbout(); }},
        {u"settings"_s, [](ui::MainWindow& w) { w.showSettings(); }},
        {u"shortcuts"_s, [](ui::MainWindow& w) { w.showShortcuts(); }},
        {u"whatsnew"_s, [](ui::MainWindow& w) { w.showWhatsNew(); }},
        {u"bugreport"_s, [](ui::MainWindow& w) { w.showBugReport(); }},
        {u"dictionaries"_s, [](ui::MainWindow& w) { w.showDictionaries(); }},
        {u"available"_s, [](ui::MainWindow& w) { w.showDictionaries(true); }},
    };
    if (!sheets.contains(sheet)) {
        return false;
    }
    const Open open = sheets.value(sheet);
    const auto grab = [grabPath] {
        QWidget* dialog = QApplication::activeModalWidget();
        const bool saved = dialog != nullptr && (grabPath.isEmpty() || dialog->grab().save(grabPath));
        QCoreApplication::exit(saved ? 0 : kGrabFailedExit);
    };
    QObject::connect(
        &window, &ui::MainWindow::libraryReady, &window,
        [&window, &manager, grab, open] {
            QTimer::singleShot(kSettleMs, &window, [&window, &manager, grab] {
                if (!manager.isRefreshingCatalog()) {
                    grab();
                    return;
                }
                const auto settleThenGrab = [&window, grab] {
                    QTimer::singleShot(kCatalogSettleMs, &window, grab);
                };
                QObject::connect(&manager, &services::DictionaryManager::catalogChanged, &window,
                                 settleThenGrab, Qt::SingleShotConnection);
                QObject::connect(&manager, &services::DictionaryManager::catalogFailed, &window,
                                 settleThenGrab, Qt::SingleShotConnection);
            });
            open(window);
        },
        Qt::SingleShotConnection);
    return true;
}

/// Grabs the window once it has settled after the last result or entry: a
/// search that matches nothing shows results but never an entry.
void grabWindow(ui::MainWindow& window, const QString& grabPath, bool wantsQuery)
{
    auto* settle = new QTimer(&window);
    settle->setSingleShot(true);
    settle->setInterval(kSettleMs);
    QObject::connect(settle, &QTimer::timeout, &window, [&window, grabPath] {
        const bool saved = window.grab().save(grabPath);
        QCoreApplication::exit(saved ? 0 : kGrabFailedExit);
    });
    QObject::connect(&window, &ui::MainWindow::entryShown, settle, qOverload<>(&QTimer::start));
    QObject::connect(&window, &ui::MainWindow::resultsShown, settle, [&window, settle, wantsQuery] {
        // The saved list shown on start is not the answer to the debug query.
        if (!wantsQuery || !window.findChild<QLineEdit*>(u"search"_s)->text().isEmpty()) {
            settle->start();
        }
    });
    QTimer::singleShot(kGiveUpMs, &window, [] { QCoreApplication::exit(kGrabFailedExit); });
}

} // namespace

void installDebugHooks(ui::MainWindow& window, services::DictionaryManager& manager)
{
    const QString install = qEnvironmentVariable("OMNIDICT_DEBUG_INSTALL");
    if (!install.isEmpty()) {
        installWhenListed(window, manager, install);
    }
    const QString size = qEnvironmentVariable("OMNIDICT_DEBUG_WINDOW_SIZE");
    const QStringList parts = size.split(u'x');
    if (parts.size() == 2) {
        window.resize(parts.at(0).toInt(), parts.at(1).toInt());
    }

    const QString query = qEnvironmentVariable("OMNIDICT_DEBUG_QUERY");
    if (!query.isEmpty()) {
        QObject::connect(
            &window, &ui::MainWindow::libraryReady, &window, [&window, query] { window.setQuery(query); },
            Qt::SingleShotConnection);
    }

    const QString grabPath = qEnvironmentVariable("OMNIDICT_DEBUG_GRAB");
    if (grabSheet(window, manager, qEnvironmentVariable("OMNIDICT_DEBUG_OPEN"), grabPath) ||
        grabPath.isEmpty()) {
        return;
    }
    grabWindow(window, grabPath, !query.isEmpty());
}

} // namespace omnidict::app
