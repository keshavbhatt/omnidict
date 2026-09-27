#include "app/debug_hooks.h"

#include "ui/main_window.h"

#include <QCoreApplication>
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
    if (grabPath.isEmpty()) {
        return;
    }
    QObject::connect(
        &window, &ui::MainWindow::entryShown, &window,
        [&window, grabPath] {
            QTimer::singleShot(kSettleMs, &window, [&window, grabPath] {
                const bool saved = window.grab().save(grabPath);
                QCoreApplication::exit(saved ? 0 : kGrabFailedExit);
            });
        },
        Qt::SingleShotConnection);
    QTimer::singleShot(kGiveUpMs, &window, [] { QCoreApplication::exit(kGrabFailedExit); });
}

} // namespace omnidict::app
