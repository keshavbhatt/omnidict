#include "ui/first_run_panel.h"

#include "services/dictionary_manager.h"
#include "ui/icons.h"
#include "ui/style.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kCardWidth = 200;
constexpr int kCards = 3;
constexpr int kPercent = 100;
constexpr int kColumnWidth = 680;

/// The catalogue's dictionaries, the suggested ones first.
QList<core::CatalogEntry> suggested(const QList<core::CatalogEntry>& all)
{
    static const QStringList kFirst = {u"wikt-en"_s, u"wikt-es-en"_s, u"wikt-hi-en"_s};
    QList<core::CatalogEntry> entries = all;
    const auto rank = [](const core::CatalogEntry& entry) {
        const qsizetype at = kFirst.indexOf(entry.dictId);
        return at < 0 ? kFirst.size() : at;
    };
    std::ranges::stable_sort(entries, {}, rank);
    if (entries.size() > kCards) {
        entries.resize(kCards);
    }
    return entries;
}

/// "Spanish-English (Wiktionary)" without the source in brackets.
QString shortName(const QString& name)
{
    const qsizetype open = name.lastIndexOf(u" ("_s);
    return open > 0 && name.endsWith(u')') ? name.left(open) : name;
}

QLabel* mutedLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("muted", true);
    label->setProperty("small", true);
    label->setWordWrap(true);
    return label;
}

} // namespace

FirstRunPanel::FirstRunPanel(services::DictionaryManager& manager, QWidget* parent)
    : QWidget(parent)
    , m_manager(manager)
    , m_text(new QLabel(this))
    , m_cards(new QHBoxLayout)
    , m_retry(new QPushButton(tr("Try again"), this))
{
    auto* title = new QLabel(tr("Add your first dictionary"), this);
    title->setProperty("title", true);
    title->setAlignment(Qt::AlignCenter);
    m_text->setProperty("muted", true);
    m_text->setAlignment(Qt::AlignCenter);
    m_text->setWordWrap(true);
    m_cards->setSpacing(16);
    m_retry->hide();
    auto* browse = new QPushButton(tr("Browse all dictionaries"), this);
    browse->setProperty("flat", true);
    connect(browse, &QPushButton::clicked, this, &FirstRunPanel::browseRequested);
    connect(m_retry, &QPushButton::clicked, this, [this] { m_manager.refreshCatalog(true); });

    auto* column = new QWidget(this);
    column->setFixedWidth(kColumnWidth);
    auto* layout = new QVBoxLayout(column);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(m_text);
    layout->addSpacing(16);
    layout->addLayout(m_cards);
    layout->addWidget(m_retry, 0, Qt::AlignHCenter);
    layout->addSpacing(8);
    layout->addWidget(browse, 0, Qt::AlignHCenter);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(column);
    row->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);

    connect(&m_manager, &services::DictionaryManager::catalogChanged, this, &FirstRunPanel::rebuild);
    connect(&m_manager, &services::DictionaryManager::catalogFailed, this, [this](const QString& reason) {
        if (m_buttons.isEmpty()) {
            m_text->setText(tr("The dictionary catalogue cannot be reached: %1").arg(reason));
            m_retry->show();
        }
    });
    connect(&m_manager, &services::DictionaryManager::downloadChanged, this,
            &FirstRunPanel::onDownloadChanged);
    rebuild();
    m_manager.refreshCatalog(false);
}

void FirstRunPanel::rebuild()
{
    while (const QLayoutItem* item = m_cards->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_buttons.clear();
    const QList<core::CatalogEntry> entries = suggested(m_manager.catalog().dictionaries);
    m_retry->setVisible(false);
    m_text->setText(entries.isEmpty() ? tr("Looking for dictionaries...")
                                      : tr("Dictionaries download once and then work offline."));
    const QLocale locale;
    const Tokens& t = Tokens::current();
    m_cards->addStretch(1);
    for (const core::CatalogEntry& entry : entries) {
        auto* card = new QFrame(this);
        card->setProperty("card", true);
        card->setFixedWidth(kCardWidth);
        auto* name = new QLabel(shortName(entry.name), card);
        name->setProperty("heading", true);
        auto* download = new QPushButton(icons::themed(u"download"_s, t.accentText), tr("Download"), card);
        download->setProperty("primary", true);
        download->setAccessibleName(tr("Download %1").arg(entry.name));
        connect(download, &QPushButton::clicked, this, [this, entry] { m_manager.install(entry); });
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 12, 14, 14);
        layout->setSpacing(4);
        layout->addWidget(name);
        layout->addWidget(mutedLabel(entry.publisher, card));
        layout->addWidget(mutedLabel(tr("%1 entries").arg(locale.toString(entry.entryCount)), card));
        const QString size =
            locale.formattedDataSize(entry.sizeCompressed, 0, QLocale::DataSizeTraditionalFormat);
        layout->addWidget(mutedLabel(tr("%1 download").arg(size), card));
        layout->addSpacing(8);
        layout->addWidget(download);
        m_cards->addWidget(card);
        m_buttons.insert(entry.dictId, download);
    }
    m_cards->addStretch(1);
    if (!m_buttons.isEmpty()) {
        m_buttons.value(entries.constFirst().dictId)->setFocus();
    }
    for (const services::DownloadStatus& status : m_manager.downloads()) {
        onDownloadChanged(status);
    }
}

void FirstRunPanel::onDownloadChanged(const services::DownloadStatus& status)
{
    QPushButton* button = m_buttons.value(status.dictId);
    if (button == nullptr) {
        return;
    }
    switch (status.state) {
    case services::DownloadStatus::Waiting:
        button->setEnabled(false);
        button->setText(tr("Waiting"));
        break;
    case services::DownloadStatus::Downloading:
        button->setEnabled(false);
        button->setText(status.total > 0
                            ? tr("Downloading %1%").arg(status.received * kPercent / status.total)
                            : tr("Downloading"));
        break;
    case services::DownloadStatus::Installing:
        button->setEnabled(false);
        button->setText(tr("Installing..."));
        break;
    case services::DownloadStatus::Failed:
        button->setEnabled(true);
        button->setText(tr("Retry"));
        button->setToolTip(status.error);
        break;
    }
}

} // namespace omnidict::ui
