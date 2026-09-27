#include "app/debug_hooks.h"
#include "app/lookup_command.h"
#include "app/single_instance.h"
#include "app/version.h"
#include "core/log_sink.h"
#include "core/settings.h"
#include "services/dictionary_manager.h"
#include "ui/icons.h"
#include "ui/main_window.h"
#include "ui/theme_applier.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>

#include <cstring>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

namespace version = omnidict::app::version;

constexpr int kUsageError = 2;
constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 680;

struct Options
{
    QCommandLineOption lookup{
        u"lookup"_s,
        QCoreApplication::translate("main", "Look <word> up in the dictionary file <bundle> and exit."),
        QCoreApplication::translate("main", "bundle")};
    QCommandLineOption bundles{
        u"bundles"_s,
        QCoreApplication::translate("main", "Also use the dictionaries in <directory> (may be repeated)."),
        QCoreApplication::translate("main", "directory")};
    QCommandLineOption profile{
        u"profile"_s,
        QCoreApplication::translate("main", "Keep history, favourites and dictionaries in <directory>."),
        QCoreApplication::translate("main", "directory")};
};

/// `--lookup`, `--help` and `--version` run without a window, so they must not need a display.
bool wantsWindow(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        for (const char* flag : {"--lookup", "--help", "-h", "--version", "-v"}) {
            if (std::strcmp(argv[i], flag) == 0) {
                return false;
            }
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
    QGuiApplication::setDesktopFileName(QString::fromLatin1(version::kDesktopId));
    QGuiApplication::setApplicationDisplayName(QString::fromLatin1(version::kDisplayName));
}

int runLookup(const QCommandLineParser& parser, const Options& options)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const QStringList words = parser.positionalArguments();
    if (words.isEmpty()) {
        err << QCoreApplication::translate("main", "--lookup needs a word.") << Qt::endl;
        return kUsageError;
    }
    return omnidict::app::runLookup(parser.value(options.lookup), words.join(u' '), out, err);
}

int runWindow(const QCommandLineParser& parser, const Options& options)
{
    // Everything the app keeps lives in one directory, a scratch one with --profile.
    const QString dataDir = parser.isSet(options.profile)
                                ? QDir(parser.value(options.profile)).absolutePath()
                                : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // Installed dictionaries (PLAN 5.3) plus any given on the command line.
    QStringList roots = parser.values(options.bundles);
    for (QString& root : roots) {
        root = QDir(root).absolutePath();
    }
    roots << dataDir + u"/dictionaries"_s;

    QDir().mkpath(dataDir);
    // A second launch on the same profile hands its word to the running window and ends.
    const QString query = parser.positionalArguments().join(u' ');
    omnidict::app::SingleInstance instance(omnidict::app::instanceKeyFor(dataDir));
    if (!instance.isPrimary()) {
        instance.sendToPrimary({{u"query"_s, query}});
        return 0;
    }
    omnidict::core::LogSink::setLogFile(dataDir + u"/logs/omnidict.log"_s);
    omnidict::core::Settings settings(dataDir + u"/settings.ini"_s);
    const omnidict::ui::ThemeApplier theme(settings);
    QApplication::setWindowIcon(omnidict::ui::icons::brand());

    omnidict::services::DictionaryManager dictionaries(
        dataDir + u"/dictionaries"_s, dataDir + u"/cache"_s,
        omnidict::services::DictionaryManager::defaultCatalogUrl());
    omnidict::ui::MainWindow window(settings, dictionaries, roots, dataDir + u"/userdata.sqlite"_s);
    if (settings.windowGeometry().isEmpty()) {
        window.resize(kWindowWidth, kWindowHeight);
    }
    omnidict::app::installDebugHooks(window, dictionaries);
    window.show();
    window.showWhatsNewIfUpdated();
    QObject::connect(&instance, &omnidict::app::SingleInstance::commandReceived, &window,
                     [&window](const QJsonObject& command) {
                         window.showNormal();
                         window.raise();
                         window.activateWindow();
                         const QString word = command.value(u"query"_s).toString();
                         if (!word.isEmpty()) {
                             window.setQuery(word);
                         }
                     });
    if (!query.isEmpty()) {
        QObject::connect(
            &window, &omnidict::ui::MainWindow::libraryReady, &window,
            [&window, query] { window.setQuery(query); }, Qt::SingleShotConnection);
    }
    return QCoreApplication::exec();
}

} // namespace

int main(int argc, char* argv[])
{
    omnidict::core::LogSink::install();
    const std::unique_ptr<QCoreApplication> app = wantsWindow(argc, argv)
                                                      ? std::make_unique<QApplication>(argc, argv)
                                                      : std::make_unique<QCoreApplication>(argc, argv);
    setIdentity();

    const Options options;
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Offline dictionary."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(options.lookup);
    parser.addOption(options.bundles);
    parser.addOption(options.profile);
    parser.addPositionalArgument(u"word"_s, QCoreApplication::translate("main", "The word to look up."),
                                 u"[word]"_s);
    parser.process(*app);

    return parser.isSet(options.lookup) ? runLookup(parser, options) : runWindow(parser, options);
}
