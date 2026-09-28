#include "ui/bug_report_dialog.h"

#include "ui/style.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

constexpr int kMaxTitleLength = 80;
const QString kGitHubIssues = u"https://github.com/keshavbhatt/omnidict/issues/new"_s;
const QString kSupportMail = u"mailto:connect@ktechpit.com"_s;

QString reportBody(const QString& description)
{
    const QString described =
        description.trimmed().isEmpty() ? QObject::tr("(no description given)") : description.trimmed();
    return described + u"\n\n"_s +
           QObject::tr("The diagnostics are on your clipboard: paste them at the end of this report.");
}

} // namespace

BugReportDialog::BugReportDialog(QString diagnostics, QWidget* parent)
    : QDialog(parent)
    , m_diagnostics(std::move(diagnostics))
    , m_title(new QLineEdit(this))
    , m_description(new QPlainTextEdit(this))
{
    disableWheelZoom(m_description);
    setWindowTitle(tr("Report a bug"));
    setModal(true);
    setMinimumWidth(560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());
    root->addWidget(buildForm(), 1);
    root->addWidget(buildFooter());
}

QWidget* BugReportDialog::buildHeader()
{
    auto* header = new QWidget(this);
    auto* titleRow = new QVBoxLayout(header);
    titleRow->setContentsMargins(20, 20, 20, 8);
    titleRow->setSpacing(6);
    auto* title = new QLabel(tr("Report a bug"), header);
    title->setProperty("title", true);
    titleRow->addWidget(title);
    auto* intro = new QLabel(
        tr("A pre-filled report opens in your browser or mail app, and the diagnostics are copied to "
           "your clipboard."),
        header);
    intro->setProperty("muted", true);
    intro->setProperty("small", true);
    intro->setWordWrap(true);
    titleRow->addWidget(intro);
    return header;
}

QWidget* BugReportDialog::buildForm()
{
    auto* form = new QWidget(this);
    auto* body = new QVBoxLayout(form);
    body->setContentsMargins(20, 0, 20, 16);
    body->setSpacing(10);

    auto* titleLabel = new QLabel(tr("Title"), form);
    titleLabel->setProperty("small", true);
    body->addWidget(titleLabel);
    m_title->setMaxLength(kMaxTitleLength);
    m_title->setPlaceholderText(tr("Short summary of the problem"));
    titleLabel->setBuddy(m_title);
    body->addWidget(m_title);

    auto* descriptionLabel = new QLabel(tr("What happened?"), form);
    descriptionLabel->setProperty("small", true);
    body->addWidget(descriptionLabel);
    m_description->setPlaceholderText(
        tr("e.g. Searching for perro shows no results after I add a dictionary."));
    m_description->setMinimumHeight(110);
    descriptionLabel->setBuddy(m_description);
    body->addWidget(m_description, 1);

    // A collapsed disclosure: the diagnostics are shown, not hidden, since
    // the reporter should know exactly what will be pasted into their report.
    auto* disclosureRow = new QWidget(form);
    auto* disclosureLayout = new QHBoxLayout(disclosureRow);
    disclosureLayout->setContentsMargins(0, 8, 0, 8);
    auto* disclosureLabel = new QLabel(tr("Diagnostics that will be included"), disclosureRow);
    disclosureLabel->setProperty("small", true);
    disclosureLayout->addWidget(disclosureLabel, 1);
    auto* disclosureToggle = new QToolButton(disclosureRow);
    disclosureToggle->setArrowType(Qt::RightArrow);
    disclosureToggle->setAutoRaise(true);
    disclosureToggle->setCheckable(true);
    disclosureToggle->setAccessibleName(tr("Diagnostics that will be included"));
    disclosureLayout->addWidget(disclosureToggle);
    body->addWidget(disclosureRow);

    auto* diagnosticsView = new QPlainTextEdit(form);
    disableWheelZoom(diagnosticsView);
    diagnosticsView->setObjectName(u"diagnosticsView"_s);
    diagnosticsView->setReadOnly(true);
    diagnosticsView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    diagnosticsView->setPlainText(m_diagnostics);
    diagnosticsView->setMaximumHeight(160);
    diagnosticsView->setVisible(false);
    body->addWidget(diagnosticsView);
    connect(disclosureToggle, &QToolButton::toggled, this, [disclosureToggle, diagnosticsView](bool on) {
        disclosureToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        diagnosticsView->setVisible(on);
    });
    return form;
}

QWidget* BugReportDialog::buildFooter()
{
    auto* foot = new QFrame(this);
    foot->setProperty("sheetFoot", true);
    auto* footLayout = new QHBoxLayout(foot);
    footLayout->setContentsMargins(20, 12, 20, 12);
    auto* copy = new QPushButton(tr("Copy diagnostics"), this);
    connect(copy, &QPushButton::clicked, this, &BugReportDialog::copyDiagnostics);
    footLayout->addWidget(copy);
    footLayout->addStretch(1);
    auto* mail = new QPushButton(tr("Send by email"), this);
    connect(mail, &QPushButton::clicked, this, &BugReportDialog::openMail);
    footLayout->addWidget(mail);
    auto* issue = new QPushButton(tr("Open GitHub issue"), this);
    issue->setProperty("primary", true);
    issue->setDefault(true);
    connect(issue, &QPushButton::clicked, this, &BugReportDialog::openIssue);
    footLayout->addWidget(issue);
    auto* close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    footLayout->addWidget(close);
    return foot;
}

void BugReportDialog::copyDiagnostics()
{
    QApplication::clipboard()->setText(m_diagnostics);
}

void BugReportDialog::openIssue()
{
    copyDiagnostics();
    QUrl url(kGitHubIssues);
    QUrlQuery query;
    query.addQueryItem(u"title"_s, m_title->text());
    query.addQueryItem(u"body"_s, reportBody(m_description->toPlainText()));
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

void BugReportDialog::openMail()
{
    copyDiagnostics();
    QUrl url(kSupportMail);
    QUrlQuery query;
    query.addQueryItem(u"subject"_s, m_title->text());
    query.addQueryItem(u"body"_s, reportBody(m_description->toPlainText()));
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

} // namespace omnidict::ui
