#include "app/lookup_command.h"
#include "app/version.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    namespace version = omnidict::app::version;

    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QString::fromLatin1(version::kApplicationName));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(version::kVersion));
    QCoreApplication::setOrganizationName(QString::fromLatin1(version::kOrganizationName));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(version::kOrganizationDomain));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Offline dictionary."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption lookup(
        u"lookup"_s,
        QCoreApplication::translate("main", "Look <word> up in the dictionary file <bundle> and exit."),
        QCoreApplication::translate("main", "bundle"));
    parser.addOption(lookup);
    parser.addPositionalArgument(u"word"_s, QCoreApplication::translate("main", "The word to look up."),
                                 u"[word]"_s);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (parser.isSet(lookup)) {
        const QStringList words = parser.positionalArguments();
        if (words.isEmpty()) {
            err << QCoreApplication::translate("main", "--lookup needs a word.") << Qt::endl;
            return 2;
        }
        return omnidict::app::runLookup(parser.value(lookup), words.join(u' '), out, err);
    }

    out << parser.helpText();
    return 0;
}
