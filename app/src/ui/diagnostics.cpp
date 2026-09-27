#include "ui/diagnostics.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSysInfo>
#include <QTextStream>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

QString packageKind()
{
    if (qEnvironmentVariableIsSet("SNAP")) {
        return u"snap"_s;
    }
    if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
        return u"flatpak"_s;
    }
    return u"none"_s;
}

QString desktop()
{
    const QString value = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    return value.isEmpty() ? QCoreApplication::translate("Diagnostics", "unknown") : value;
}

} // namespace

QString diagnosticsText(const QList<services::DictionaryInfo>& dictionaries)
{
    QString text;
    QTextStream out(&text);
    out << u"Omnidict diagnostics\n\n"_s;
    out << u"- App: "_s << QCoreApplication::applicationName() << u' '
        << QCoreApplication::applicationVersion() << u'\n';
    out << u"- Qt: "_s << QString::fromUtf8(qVersion()) << u" (built against "_s
        << QString::fromUtf8(QT_VERSION_STR) << u")\n"_s;
    out << u"- OS: "_s << QSysInfo::prettyProductName() << u", kernel "_s << QSysInfo::kernelVersion()
        << u", CPU: "_s << QSysInfo::currentCpuArchitecture() << u'\n';
    out << u"- Platform: "_s << QGuiApplication::platformName() << u", desktop: "_s << desktop() << u'\n';
    out << u"- Package: "_s << packageKind() << u'\n';
    out << u"- Dictionaries:\n"_s;
    if (dictionaries.isEmpty()) {
        out << u"  (none installed)\n"_s;
    }
    for (const services::DictionaryInfo& dictionary : dictionaries) {
        out << u"  - "_s << dictionary.name << u" ("_s << dictionary.dictId << u"), version "_s
            << dictionary.version << u", "_s << QString::number(dictionary.entryCount) << u" entries\n"_s;
    }
    return text;
}

} // namespace omnidict::ui
