#include "app/debug_hooks.h"

#include "ui/main_window.h"

#include <QApplication>
#include <QLineEdit>
#include <QPixmap>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace omnidict::app {

namespace {
constexpr int kSettleMs = 400;    // let the entry lay out before the grab
constexpr int kGiveUpMs = 15'000; // quit even if no entry ever shows
constexpr int kGrabFailedExit = 3;
} // namespace

void installDebugHooks(ui::MainWindow& window)
{
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
    if (qEnvironmentVariable("OMNIDICT_DEBUG_OPEN") == u"about"_s) {
        // Grab the dialog from inside its own modal loop, then quit.
        QObject::connect(
            &window, &ui::MainWindow::libraryReady, &window,
            [&window, grabPath] {
                QTimer::singleShot(kSettleMs, &window, [grabPath] {
                    QWidget* dialog = QApplication::activeModalWidget();
                    const bool saved =
                        dialog != nullptr && (grabPath.isEmpty() || dialog->grab().save(grabPath));
                    QCoreApplication::exit(saved ? 0 : kGrabFailedExit);
                });
                window.showAbout();
            },
            Qt::SingleShotConnection);
        return;
    }
    if (grabPath.isEmpty()) {
        return;
    }
    // Grab once the window has settled after the last result or entry: a search
    // that matches nothing shows results but never an entry.
    auto* settle = new QTimer(&window);
    settle->setSingleShot(true);
    settle->setInterval(kSettleMs);
    QObject::connect(settle, &QTimer::timeout, &window, [&window, grabPath] {
        const bool saved = window.grab().save(grabPath);
        QCoreApplication::exit(saved ? 0 : kGrabFailedExit);
    });
    const bool wantsQuery = !query.isEmpty();
    QObject::connect(&window, &ui::MainWindow::entryShown, settle, qOverload<>(&QTimer::start));
    QObject::connect(&window, &ui::MainWindow::resultsShown, settle, [&window, settle, wantsQuery] {
        // The saved list shown on start is not the answer to the debug query.
        if (!wantsQuery || !window.findChild<QLineEdit*>(u"search"_s)->text().isEmpty()) {
            settle->start();
        }
    });
    QTimer::singleShot(kGiveUpMs, &window, [] { QCoreApplication::exit(kGrabFailedExit); });
}

} // namespace omnidict::app
