#include "app/debug_hooks.h"
#include "app/lookup_command.h"
#include "app/version.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

#include <cstring>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

namespace version = omnidict::app::version;

/// `--lookup` runs without a window, so it must not need a display.
bool wantsWindow(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--lookup") == 0 || std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--version") == 0 ||
            std::strcmp(argv[i], "-v") == 0) {
            return false;
        }
    }
    return true;
}

void setIdentity()
{
    QCoreApplication::setApplicationName(QString::fromLatin1(version::kApplicationName));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(version::kVersion));
    QCoreApplication::setOrganizationName(QString::fromLatin1(version::kOrganizationName));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(version::kOrganizationDomain));
}

} // namespace

int main(int argc, char* argv[])
{
    const std::unique_ptr<QCoreApplication> app = wantsWindow(argc, argv)
                                                      ? std::make_unique<QApplication>(argc, argv)
                                                      : std::make_unique<QCoreApplication>(argc, argv);
    setIdentity();
    QGuiApplication::setDesktopFileName(QString::fromLatin1(version::kDesktopId));
    QGuiApplication::setApplicationDisplayName(QString::fromLatin1(version::kDisplayName));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Offline dictionary."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption lookup(
        u"lookup"_s,
        QCoreApplication::translate("main", "Look <word> up in the dictionary file <bundle> and exit."),
        QCoreApplication::translate("main", "bundle"));
    const QCommandLineOption bundles(
        u"bundles"_s,
        QCoreApplication::translate("main", "Also use the dictionaries in <directory> (may be repeated)."),
        QCoreApplication::translate("main", "directory"));
    parser.addOption(lookup);
    parser.addOption(bundles);
    parser.addPositionalArgument(u"word"_s, QCoreApplication::translate("main", "The word to look up."),
                                 u"[word]"_s);
    parser.process(*app);

    if (parser.isSet(lookup)) {
        QTextStream out(stdout);
        QTextStream err(stderr);
        const QStringList words = parser.positionalArguments();
        if (words.isEmpty()) {
            err << QCoreApplication::translate("main", "--lookup needs a word.") << Qt::endl;
            return 2;
        }
        return omnidict::app::runLookup(parser.value(lookup), words.join(u' '), out, err);
    }

    // Installed dictionaries (PLAN 5.3) plus any given on the command line.
    QStringList roots = parser.values(bundles);
    for (QString& root : roots) {
        root = QDir(root).absolutePath();
    }
    roots << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/dictionaries"_s;

    omnidict::ui::MainWindow window(roots);
    window.resize(1000, 680);
    omnidict::app::installDebugHooks(window);
    window.show();
    const QStringList words = parser.positionalArguments();
    if (!words.isEmpty()) {
        QObject::connect(
            &window, &omnidict::ui::MainWindow::libraryReady, &window,
            [&window, words] { window.setQuery(words.join(u' ')); }, Qt::SingleShotConnection);
    }
    return QCoreApplication::exec();
}
