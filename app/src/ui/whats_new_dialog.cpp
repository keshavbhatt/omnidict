#include "ui/whats_new_dialog.h"

#include "core/settings.h"

#include <QComboBox>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

QString WhatsNewDialog::bundledChangelog()
{
    QFile file(u":/text/CHANGELOG.md"_s);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool WhatsNewDialog::shouldShowWhatsNew(const core::Settings& settings, const QString& currentVersion)
{
    const QString seen = settings.whatsNewSeenVersion();
    return !seen.isEmpty() && seen != currentVersion;
}

WhatsNewDialog::WhatsNewDialog(QString runningVersion, QWidget* parent)
    : QDialog(parent)
    , m_running(std::move(runningVersion))
    , m_markdown(bundledChangelog())
    , m_releases(core::changelogReleases(m_markdown))
    , m_picker(new QComboBox(this))
    , m_notes(new QTextBrowser(this))
{
    setWindowTitle(tr("What's new"));
    setModal(true);
    setMinimumWidth(560);
    resize(600, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addLayout(buildHeader());
    root->addWidget(buildNotesCard(), 1);
    root->addWidget(buildFooter());

    // Open on the running version, or the newest release when the running
    // version has no section yet (a dev build ahead of the changelog).
    const int running = m_picker->findData(m_running);
    if (m_picker->count() > 0) {
        m_picker->setCurrentIndex(qMax(0, running));
        showVersion(m_picker->currentData().toString());
    }
}

QLayout* WhatsNewDialog::buildHeader()
{
    auto* titleRow = new QVBoxLayout;
    titleRow->setContentsMargins(20, 20, 20, 8);
    titleRow->setSpacing(14);
    auto* title = new QLabel(tr("What's new in Omnidict"), this);
    title->setProperty("title", true);
    titleRow->addWidget(title);

    auto* pickerRow = new QHBoxLayout;
    pickerRow->setSpacing(8);
    auto* pickerLabel = new QLabel(tr("Version"), this);
    pickerLabel->setProperty("muted", true);
    pickerLabel->setProperty("small", true);
    pickerRow->addWidget(pickerLabel);
    m_picker->setObjectName(u"versionPicker"_s);
    m_picker->setAccessibleName(tr("Version"));
    for (qsizetype i = 0; i < m_releases.size(); ++i) {
        const core::ChangelogRelease& release = m_releases.at(i);
        m_picker->addItem(i == 0 ? tr("%1 (latest)").arg(release.version) : release.version, release.version);
    }
    pickerLabel->setBuddy(m_picker);
    pickerRow->addWidget(m_picker, 1);
    connect(m_picker, &QComboBox::currentIndexChanged, this,
            [this](int index) { showVersion(m_picker->itemData(index).toString()); });
    titleRow->addLayout(pickerRow);
    return titleRow;
}

QWidget* WhatsNewDialog::buildNotesCard()
{
    auto* body = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(20, 0, 20, 16);
    auto* card = new QFrame(body);
    card->setProperty("card", true);
    auto* cardLayout = new QVBoxLayout(card);
    m_notes->setObjectName(u"whatsNewNotes"_s);
    m_notes->setReadOnly(true);
    m_notes->setFrameShape(QFrame::NoFrame);
    m_notes->viewport()->setAutoFillBackground(false);
    m_notes->setOpenExternalLinks(true);
    m_notes->setAccessibleName(tr("Release notes"));
    cardLayout->addWidget(m_notes);
    bodyLayout->addWidget(card, 1);
    return body;
}

QWidget* WhatsNewDialog::buildFooter()
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

void WhatsNewDialog::showVersion(const QString& version)
{
    if (const int index = m_picker->findData(version); index >= 0 && index != m_picker->currentIndex()) {
        m_picker->setCurrentIndex(index); // re-enters through currentIndexChanged
        return;
    }
    m_notes->setMarkdown(core::changelogSection(m_markdown, version));
}

QString WhatsNewDialog::shownVersion() const
{
    return m_picker->count() > 0 ? m_picker->currentData().toString() : m_running;
}

} // namespace omnidict::ui
