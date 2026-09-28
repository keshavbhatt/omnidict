#include "ui/about_dialog.h"

#include "ui/diagnostics.h"
#include "ui/icons.h"
#include "ui/style.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

constexpr int kIconSize = 88;
const QString kWebsiteUrl = u"https://ktechpit.com"_s;
const QString kDonateUrl = u"https://ktechpit.com/donate"_s;
const QString kMoreAppsUrl = u"https://snapcraft.io/publisher/keshavnrj"_s;
const QString kContactUrl = u"mailto:connect@ktechpit.com"_s;

struct OpenSourceNotice
{
    QString name;
    QString license;
    QString url;
};

const QList<OpenSourceNotice>& openSourceNotices()
{
    static const QList<OpenSourceNotice> kNotices = {
        {.name = u"Qt"_s, .license = u"LGPL-3.0"_s, .url = u"https://www.gnu.org/licenses/lgpl-3.0.html"_s},
        {.name = u"Qt Svg"_s,
         .license = u"LGPL-3.0"_s,
         .url = u"https://www.gnu.org/licenses/lgpl-3.0.html"_s},
        {.name = u"SQLite"_s,
         .license = u"public domain"_s,
         .url = u"https://www.sqlite.org/copyright.html"_s},
        {.name = u"ICU"_s, .license = u"Unicode-3.0"_s, .url = u"https://www.unicode.org/license.txt"_s},
        {.name = u"Lucide icons"_s,
         .license = u"ISC (the Feather part MIT)"_s,
         .url = u"https://github.com/lucide-icons/lucide/blob/main/LICENSE"_s},
        {.name = u"Omnidict"_s,
         .license = u"GPL-3.0-or-later"_s,
         .url = u"https://www.gnu.org/licenses/gpl-3.0.html"_s},
    };
    return kNotices;
}

QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setProperty("card", true);
    return card;
}

QPushButton* makeLinkButton(QWidget* parent, const QString& text, const QString& url)
{
    auto* button = new QPushButton(text, parent);
    QObject::connect(button, &QPushButton::clicked, parent, [url] { QDesktopServices::openUrl(QUrl(url)); });
    return button;
}

} // namespace

AboutDialog::AboutDialog(const QList<services::DictionaryInfo>& dictionaries, QWidget* parent)
    : QDialog(parent)
    , m_dictionaries(dictionaries)
{
    setWindowTitle(titleWithApp(tr("About Omnidict")));
    setModal(true);
    setMinimumWidth(600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 0);
    root->setSpacing(14);
    root->addLayout(buildHero());
    root->addLayout(buildLinks());
    root->addWidget(buildTabs(), 1);
    root->addWidget(buildFooter());

    resize(680, 620);
}

QLayout* AboutDialog::buildHero()
{
    auto* hero = new QHBoxLayout;
    hero->setSpacing(18);
    auto* icon = new QLabel(this);
    icon->setPixmap(icons::brand().pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));
    icon->setFixedSize(kIconSize, kIconSize);
    hero->addWidget(icon, 0, Qt::AlignTop);

    auto* identity = new QVBoxLayout;
    identity->setSpacing(3);
    auto* name = new QLabel(
        QCoreApplication::applicationName().isEmpty() ? u"Omnidict"_s : qApp->applicationDisplayName(), this);
    name->setProperty("title", true);
    identity->addWidget(name);
    identity->addWidget(new QLabel(tr("Offline dictionaries for every language."), this));

    auto* versionRow = new QHBoxLayout;
    versionRow->setSpacing(6);
    auto* version = new QLabel(tr("Version %1").arg(QCoreApplication::applicationVersion()), this);
    version->setProperty("muted", true);
    version->setProperty("small", true);
    versionRow->addWidget(version);
    auto* whatsNew = new QPushButton(tr("What's new"), this);
    whatsNew->setProperty("linkButton", true);
    whatsNew->setCursor(Qt::PointingHandCursor);
    connect(whatsNew, &QPushButton::clicked, this, &AboutDialog::whatsNewRequested);
    versionRow->addWidget(whatsNew);
    versionRow->addStretch(1);
    identity->addLayout(versionRow);

    auto* author = new QLabel(
        tr("Designed and developed by Keshav Bhatt, <a href=\"%1\">ktechpit.com</a>").arg(kWebsiteUrl), this);
    author->setTextFormat(Qt::RichText);
    author->setOpenExternalLinks(true);
    identity->addWidget(author);
    hero->addLayout(identity, 1);
    return hero;
}

QLayout* AboutDialog::buildLinks()
{
    auto* links = new QHBoxLayout;
    links->setSpacing(8);
    auto* reportBug = new QPushButton(tr("Report a bug..."), this);
    connect(reportBug, &QPushButton::clicked, this, &AboutDialog::bugReportRequested);
    links->addWidget(reportBug);
    links->addWidget(makeLinkButton(this, tr("Contact"), kContactUrl));
    links->addWidget(makeLinkButton(this, tr("Donate"), kDonateUrl));
    links->addWidget(makeLinkButton(this, tr("More apps"), kMoreAppsUrl));
    links->addStretch(1);
    return links;
}

QWidget* AboutDialog::buildTabs()
{
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(u"aboutTabs"_s);
    tabs->addTab(buildDictionariesTab(tabs), tr("Dictionaries"));
    tabs->addTab(buildOpenSourceTab(tabs), tr("Open source"));
    tabs->addTab(buildDiagnosticsTab(tabs), tr("Diagnostics"));
    return tabs;
}

QWidget* AboutDialog::buildDictionariesTab(QWidget* tabParent)
{
    auto* dictionariesTab = new QScrollArea(tabParent);
    dictionariesTab->setWidgetResizable(true);
    dictionariesTab->setFrameShape(QFrame::NoFrame);
    auto* dictionariesContent = new QWidget(dictionariesTab);
    auto* dictionariesLayout = new QVBoxLayout(dictionariesContent);
    dictionariesLayout->setSpacing(10);
    const QLocale locale;
    if (m_dictionaries.isEmpty()) {
        dictionariesLayout->addWidget(new QLabel(tr("No dictionaries are installed."), dictionariesContent));
    }
    for (const services::DictionaryInfo& dictionary : m_dictionaries) {
        QFrame* card = makeCard(dictionariesContent);
        auto* cardLayout = new QVBoxLayout(card);
        auto* nameLabel = new QLabel(dictionary.name, card);
        nameLabel->setProperty("heading", true);
        cardLayout->addWidget(nameLabel);
        const QString license = dictionary.licenseUrl.isEmpty()
                                    ? dictionary.license.toHtmlEscaped()
                                    : u"<a href=\"%1\">%2</a>"_s.arg(dictionary.licenseUrl.toHtmlEscaped(),
                                                                     dictionary.license.toHtmlEscaped());
        auto* attribution =
            new QLabel(tr("%1. Licence: %2").arg(dictionary.attribution.toHtmlEscaped(), license), card);
        attribution->setProperty("muted", true);
        attribution->setProperty("small", true);
        attribution->setTextFormat(Qt::RichText);
        attribution->setOpenExternalLinks(true);
        attribution->setWordWrap(true);
        cardLayout->addWidget(attribution);
        auto* versionLabel = new QLabel(
            tr("Version %1, %2 entries").arg(dictionary.version, locale.toString(dictionary.entryCount)),
            card);
        versionLabel->setProperty("muted", true);
        versionLabel->setProperty("small", true);
        cardLayout->addWidget(versionLabel);
        dictionariesLayout->addWidget(card);
    }
    dictionariesLayout->addStretch(1);
    dictionariesTab->setWidget(dictionariesContent);
    return dictionariesTab;
}

/* static */ QWidget* AboutDialog::buildOpenSourceTab(QWidget* tabParent)
{
    auto* openSourceTab = new QScrollArea(tabParent);
    openSourceTab->setWidgetResizable(true);
    openSourceTab->setFrameShape(QFrame::NoFrame);
    auto* openSourceContent = new QWidget(openSourceTab);
    auto* openSourceLayout = new QVBoxLayout(openSourceContent);
    openSourceLayout->setSpacing(10);
    for (const OpenSourceNotice& notice : openSourceNotices()) {
        QFrame* card = makeCard(openSourceContent);
        auto* cardLayout = new QVBoxLayout(card);
        auto* nameLabel = new QLabel(notice.name, card);
        nameLabel->setProperty("heading", true);
        cardLayout->addWidget(nameLabel);
        auto* licenseLabel = new QLabel(
            u"<a href=\"%1\">%2</a>"_s.arg(notice.url.toHtmlEscaped(), notice.license.toHtmlEscaped()), card);
        licenseLabel->setProperty("muted", true);
        licenseLabel->setProperty("small", true);
        licenseLabel->setTextFormat(Qt::RichText);
        licenseLabel->setOpenExternalLinks(true);
        cardLayout->addWidget(licenseLabel);
        openSourceLayout->addWidget(card);
    }
    openSourceLayout->addStretch(1);
    openSourceTab->setWidget(openSourceContent);
    return openSourceTab;
}

QWidget* AboutDialog::buildDiagnosticsTab(QWidget* tabParent)
{
    auto* diagnosticsTab = new QWidget(tabParent);
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsTab);
    auto* copyRow = new QHBoxLayout;
    copyRow->addStretch(1);
    auto* copy = new QPushButton(tr("Copy"), diagnosticsTab);
    copy->setProperty("small", true);
    connect(copy, &QPushButton::clicked, this, &AboutDialog::copyDiagnostics);
    copyRow->addWidget(copy);
    diagnosticsLayout->addLayout(copyRow);
    auto* diagnosticsView = new QPlainTextEdit(diagnosticsTab);
    disableWheelZoom(diagnosticsView);
    diagnosticsView->setObjectName(u"aboutDiagnostics"_s);
    diagnosticsView->setReadOnly(true);
    diagnosticsView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    diagnosticsView->setPlainText(diagnosticsText(m_dictionaries));
    diagnosticsLayout->addWidget(diagnosticsView, 1);
    return diagnosticsTab;
}

QWidget* AboutDialog::buildFooter()
{
    auto* foot = new QFrame(this);
    foot->setProperty("sheetFoot", true);
    auto* footLayout = new QHBoxLayout(foot);
    footLayout->setContentsMargins(20, 12, 20, 12);
    footLayout->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    close->setProperty("primary", true);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footLayout->addWidget(close);
    return foot;
}

void AboutDialog::copyDiagnostics()
{
    QApplication::clipboard()->setText(diagnosticsText(m_dictionaries));
}

QString AboutDialog::aboutHtml(const QList<services::DictionaryInfo>& dictionaries)
{
    // Identity comes from the application object, which main() sets from app/version.h.
    QString html = u"<h2>%1</h2><p>%2 %3</p><p>%4</p>"_s.arg(
        QGuiApplication::applicationDisplayName().toHtmlEscaped(), QObject::tr("Version"),
        QCoreApplication::applicationVersion().toHtmlEscaped(),
        QObject::tr("An offline dictionary with downloadable dictionaries."));
    html += u"<h3>"_s + QObject::tr("Dictionaries") + u"</h3>"_s;
    if (dictionaries.isEmpty()) {
        html += u"<p>"_s + QObject::tr("No dictionaries are installed.") + u"</p>"_s;
    }
    const QLocale locale;
    for (const services::DictionaryInfo& dictionary : dictionaries) {
        const QString license = dictionary.licenseUrl.isEmpty()
                                    ? dictionary.license.toHtmlEscaped()
                                    : u"<a href=\"%1\">%2</a>"_s.arg(dictionary.licenseUrl.toHtmlEscaped(),
                                                                     dictionary.license.toHtmlEscaped());
        html += u"<p><b>%1</b><br>%2<br>%3: %4. %5 %6, %7 %8.</p>"_s.arg(
            dictionary.name.toHtmlEscaped(), dictionary.attribution.toHtmlEscaped(), QObject::tr("Licence"),
            license, QObject::tr("Version"), dictionary.version.toHtmlEscaped(),
            locale.toString(dictionary.entryCount), QObject::tr("entries"));
    }
    return html;
}

} // namespace omnidict::ui
