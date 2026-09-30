#include "app/debug_hooks.h"
#include "app/lookup_command.h"
#include "app/single_instance.h"
#include "app/version.h"
#include "core/log_sink.h"
#include "core/settings.h"
#include "services/dictionary_manager.h"
#include "services/global_shortcuts.h"
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
#include <QTimer>

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
    QCommandLineOption popup{
        u"popup"_s, QCoreApplication::translate(
                        "main", "Open the quick lookup popup, with [word] or else the selected text.")};
};

/// What a second launch hands to the running one: the word, whether it wants the
/// popup, and the tokens that let the running window come to the front (Wayland's
/// xdg-activation token, X11's startup id), which only the launched process was given.
QJsonObject commandFor(const QString& query, bool popup)
{
    return {{u"query"_s, query},
            {u"popup"_s, popup},
            {u"activationToken"_s, qEnvironmentVariable("XDG_ACTIVATION_TOKEN")},
            {u"startupId"_s, qEnvironmentVariable("DESKTOP_STARTUP_ID")}};
}

/// Takes the launching process's tokens (see commandFor) before a window is raised.
QByteArray adoptActivation(const QJsonObject& command)
{
    const QByteArray startupId = command.value(u"startupId"_s).toString().toUtf8();
    if (!startupId.isEmpty()) {
        qputenv("DESKTOP_STARTUP_ID", startupId);
    }
    return command.value(u"activationToken"_s).toString().toUtf8();
}

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

/// A second launch's command: the popup, or the main window brought up with the word.
void handleCommand(omnidict::ui::MainWindow& window, const QJsonObject& command)
{
    const QByteArray token = adoptActivation(command);
    const QString word = command.value(u"query"_s).toString();
    if (command.value(u"popup"_s).toBool()) {
        window.showQuickLookup(word, token);
        return;
    }
    if (!token.isEmpty()) {
        qputenv("XDG_ACTIVATION_TOKEN", token);
    }
    window.showNormal();
    window.raise();
    window.activateWindow();
    if (!word.isEmpty()) {
        window.setQuery(word);
    }
}

/// The main window, or with `--popup` only the Quick Lookup popup; the main window then
/// waits for "Open in Omnidict".
void showFirstWindow(omnidict::ui::MainWindow& window, const QString& query, bool popupOnly)
{
    if (popupOnly) {
        QObject::connect(
            &window, &omnidict::ui::MainWindow::libraryReady, &window,
            [&window, query] { window.showQuickLookup(query, qgetenv("XDG_ACTIVATION_TOKEN")); },
            Qt::SingleShotConnection);
        return;
    }
    window.show();
    window.showWhatsNewIfUpdated();
    if (!query.isEmpty()) {
        QObject::connect(
            &window, &omnidict::ui::MainWindow::libraryReady, &window,
            [&window, query] { window.setQuery(query); }, Qt::SingleShotConnection);
    }
}

/// The Quick Lookup shortcut (DOCS/quick-lookup.md): registered with the desktop, which
/// asks the user once; pressing it opens the popup over whatever has focus.
void connectQuickLookupShortcut(omnidict::services::GlobalShortcuts& shortcuts,
                                omnidict::ui::MainWindow& window)
{
    window.setGlobalShortcuts(&shortcuts);
    QObject::connect(&shortcuts, &omnidict::services::GlobalShortcuts::activated, &window,
                     [&window](const QByteArray& token) { window.showQuickLookup({}, token); });
    // After the window is up: the desktop's one-time question comes over a visible app.
    // Not headless (tests, screenshots): there is no screen to press a shortcut on, and
    // the desktop would put its question up on the real session's screen.
    if (QGuiApplication::platformName() != u"offscreen"_s) {
        QTimer::singleShot(0, &shortcuts, &omnidict::services::GlobalShortcuts::start);
    }
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
        instance.sendToPrimary(commandFor(query, parser.isSet(options.popup)));
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

    omnidict::services::GlobalShortcuts shortcuts(QString::fromLatin1(version::kDesktopId));
    connectQuickLookupShortcut(shortcuts, window);
    showFirstWindow(window, query, parser.isSet(options.popup));
    QObject::connect(&instance, &omnidict::app::SingleInstance::commandReceived, &window,
                     [&window](const QJsonObject& command) { handleCommand(window, command); });
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
    parser.addOption(options.popup);
    parser.addPositionalArgument(u"word"_s, QCoreApplication::translate("main", "The word to look up."),
                                 u"[word]"_s);
    parser.process(*app);

    return parser.isSet(options.lookup) ? runLookup(parser, options) : runWindow(parser, options);
}
