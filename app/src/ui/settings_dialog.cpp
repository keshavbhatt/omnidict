#include "ui/settings_dialog.h"

#include "core/settings.h"
#include "ui/style.h"
#include "ui/switch_button.h"

#include <QButtonGroup>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

QLabel* makeSectionLabel(QWidget* parent, const QString& text, bool first)
{
    auto* label = new QLabel(text, parent);
    ui::makeSectionLabel(label);
    label->setContentsMargins(0, first ? 0 : 16, 0, 4);
    return label;
}

QWidget* makeRow(QWidget* parent, const QString& title, const QString& description, QWidget* control)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 10, 0, 10);
    layout->setSpacing(16);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(new QLabel(title, row));
    if (!description.isEmpty()) {
        auto* descriptionLabel = new QLabel(description, row);
        descriptionLabel->setProperty("muted", true);
        descriptionLabel->setProperty("small", true);
        descriptionLabel->setWordWrap(true);
        text->addWidget(descriptionLabel);
    }
    layout->addLayout(text, 1);
    layout->addWidget(control, 0, Qt::AlignVCenter);
    return row;
}

QFrame* makeSeparator(QWidget* parent)
{
    auto* separator = new QFrame(parent);
    separator->setProperty("separator", true);
    return separator;
}

} // namespace

SettingsDialog::SettingsDialog(core::Settings& settings, const QString& dictionaryFolder, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(titleWithApp(tr("Settings")));
    setModal(true);
    setMinimumSize(560, 480);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* title = new QLabel(tr("Settings"), this);
    title->setProperty("title", true);
    auto* titleRow = new QVBoxLayout;
    titleRow->setContentsMargins(20, 20, 20, 8);
    titleRow->addWidget(title);
    root->addLayout(titleRow);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* body = new QVBoxLayout(content);
    body->setContentsMargins(20, 0, 20, 16);
    body->setSpacing(0);

    setupAppearance(body, content);
    setupSearch(body, content);
    setupHistory(body, content);
    setupStorage(body, content, dictionaryFolder);

    scroll->setWidget(content);
    root->addWidget(scroll, 1);

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
    root->addWidget(foot);
}

void SettingsDialog::setupAppearance(QVBoxLayout* body, QWidget* content)
{
    // Appearance: the theme segment and the entry text size row.
    body->addWidget(makeSectionLabel(content, tr("Appearance"), true));
    body->addWidget(makeRow(content, tr("Theme"), {}, buildThemeSegment(content)));
    body->addWidget(makeSeparator(content));
    body->addWidget(
        makeRow(content, tr("Text size"), tr("The entry and the result list"), buildTextSizeRow(content)));
}

QWidget* SettingsDialog::buildThemeSegment(QWidget* content)
{
    auto* themeFrame = new QFrame(content);
    themeFrame->setObjectName(u"themeSegment"_s);
    themeFrame->setProperty("segmented", true); // the look is in the app style sheet (ui/style.cpp)
    auto* themeLayout = new QHBoxLayout(themeFrame);
    themeLayout->setContentsMargins(0, 0, 0, 0);
    themeLayout->setSpacing(0);
    auto* themeGroup = new QButtonGroup(this);
    themeGroup->setExclusive(true);
    m_systemButton = new QToolButton(themeFrame);
    m_systemButton->setObjectName(u"themeSystem"_s);
    m_systemButton->setText(tr("System"));
    m_lightButton = new QToolButton(themeFrame);
    m_lightButton->setObjectName(u"themeLight"_s);
    m_lightButton->setText(tr("Light"));
    m_darkButton = new QToolButton(themeFrame);
    m_darkButton->setObjectName(u"themeDark"_s);
    m_darkButton->setText(tr("Dark"));
    for (QToolButton* button : {m_systemButton, m_lightButton, m_darkButton}) {
        button->setCheckable(true);
        button->setAutoRaise(true);
        themeGroup->addButton(button);
        themeLayout->addWidget(button);
    }
    m_systemButton->setProperty("segmentEdge", u"first"_s);
    m_darkButton->setProperty("segmentEdge", u"last"_s);
    connect(m_systemButton, &QToolButton::clicked, this,
            [this] { m_settings.setTheme(core::Theme::System); });
    connect(m_lightButton, &QToolButton::clicked, this, [this] { m_settings.setTheme(core::Theme::Light); });
    connect(m_darkButton, &QToolButton::clicked, this, [this] { m_settings.setTheme(core::Theme::Dark); });
    connect(&m_settings, &core::Settings::themeChanged, this, [this] { updateThemeButtons(); });
    updateThemeButtons();
    return themeFrame;
}

QWidget* SettingsDialog::buildTextSizeRow(QWidget* content)
{
    auto* sizeRow = new QWidget(content);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(8);
    auto* decrease = new QPushButton(u"A-"_s, sizeRow);
    decrease->setObjectName(u"entryTextSizeDecrease"_s);
    decrease->setFixedWidth(32);
    decrease->setProperty("compact", true); // the usual 14 px padding would clip "A-"
    decrease->setAccessibleName(tr("Smaller text"));
    connect(decrease, &QPushButton::clicked, this,
            [this] { m_settings.setEntryTextSize(m_settings.entryTextSize() - 1); });
    sizeLayout->addWidget(decrease);
    m_textSizeLabel = new QLabel(sizeRow);
    m_textSizeLabel->setObjectName(u"entryTextSizeLabel"_s);
    m_textSizeLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    sizeLayout->addWidget(m_textSizeLabel);
    auto* increase = new QPushButton(u"A+"_s, sizeRow);
    increase->setObjectName(u"entryTextSizeIncrease"_s);
    increase->setFixedWidth(32);
    increase->setProperty("compact", true);
    increase->setAccessibleName(tr("Larger text"));
    connect(increase, &QPushButton::clicked, this,
            [this] { m_settings.setEntryTextSize(m_settings.entryTextSize() + 1); });
    sizeLayout->addWidget(increase);
    auto* reset = new QPushButton(tr("Reset"), sizeRow);
    reset->setObjectName(u"entryTextSizeReset"_s);
    reset->setProperty("linkButton", true);
    connect(reset, &QPushButton::clicked, this,
            [this] { m_settings.setEntryTextSize(core::Settings::kDefaultEntryTextSize); });
    sizeLayout->addWidget(reset);
    sizeLayout->addStretch(1);
    connect(&m_settings, &core::Settings::entryTextSizeChanged, this, [this] { updateTextSizeLabel(); });
    updateTextSizeLabel();
    return sizeRow;
}

void SettingsDialog::setupSearch(QVBoxLayout* body, QWidget* content)
{
    // Search: three switches, applied immediately.
    body->addWidget(makeSectionLabel(content, tr("Search"), false));
    auto* bestMatch = new SwitchButton(tr("Show the best match while typing"), content);
    bestMatch->setObjectName(u"showBestMatchSwitch"_s);
    bestMatch->setChecked(m_settings.showBestMatch());
    connect(bestMatch, &SwitchButton::toggled, this, [this](bool on) { m_settings.setShowBestMatch(on); });
    body->addWidget(makeRow(content, tr("Show the best match while typing"), {}, bestMatch));
    body->addWidget(makeSeparator(content));

    auto* searchDefinitions = new SwitchButton(tr("Also search inside definitions"), content);
    searchDefinitions->setObjectName(u"searchDefinitionsSwitch"_s);
    searchDefinitions->setChecked(m_settings.searchDefinitions());
    connect(searchDefinitions, &SwitchButton::toggled, this,
            [this](bool on) { m_settings.setSearchDefinitions(on); });
    body->addWidget(makeRow(content, tr("Also search inside definitions"),
                            tr("From 3 letters, below the headword matches"), searchDefinitions));
    body->addWidget(makeSeparator(content));

    auto* suggestSpellings = new SwitchButton(tr("Suggest spellings when nothing matches"), content);
    suggestSpellings->setObjectName(u"suggestSpellingsSwitch"_s);
    suggestSpellings->setChecked(m_settings.suggestSpellings());
    connect(suggestSpellings, &SwitchButton::toggled, this,
            [this](bool on) { m_settings.setSuggestSpellings(on); });
    body->addWidget(makeRow(content, tr("Suggest spellings when nothing matches"), {}, suggestSpellings));
    body->addWidget(makeSeparator(content));

    connect(&m_settings, &core::Settings::searchOptionsChanged, this,
            [this, bestMatch, searchDefinitions, suggestSpellings] {
                const QSignalBlocker b1(bestMatch);
                const QSignalBlocker b2(searchDefinitions);
                const QSignalBlocker b3(suggestSpellings);
                bestMatch->setChecked(m_settings.showBestMatch());
                searchDefinitions->setChecked(m_settings.searchDefinitions());
                suggestSpellings->setChecked(m_settings.suggestSpellings());
            });
}

void SettingsDialog::setupHistory(QVBoxLayout* body, QWidget* content)
{
    // History: remembering it, and a way to ask for it to be cleared.
    body->addWidget(makeSectionLabel(content, tr("History"), false));
    auto* remember = new SwitchButton(tr("Remember opened entries"), content);
    remember->setObjectName(u"rememberHistorySwitch"_s);
    remember->setChecked(m_settings.rememberHistory());
    connect(remember, &SwitchButton::toggled, this, [this](bool on) { m_settings.setRememberHistory(on); });
    connect(&m_settings, &core::Settings::rememberHistoryChanged, this, [remember](bool on) {
        const QSignalBlocker blocker(remember);
        remember->setChecked(on);
    });
    body->addWidget(makeRow(content, tr("Remember opened entries"), {}, remember));
    body->addWidget(makeSeparator(content));

    auto* clearHistory = new QPushButton(tr("Clear history"), content);
    clearHistory->setObjectName(u"clearHistoryButton"_s);
    clearHistory->setProperty("danger", true);
    connect(clearHistory, &QPushButton::clicked, this, &SettingsDialog::clearHistoryRequested);
    body->addWidget(makeRow(content, tr("Clear history"), tr("Favourites are kept"), clearHistory));
}

void SettingsDialog::setupStorage(QVBoxLayout* body, QWidget* content, const QString& dictionaryFolder)
{
    // Storage: where dictionaries live, read-only here (installing them is a later screen).
    body->addWidget(makeSectionLabel(content, tr("Storage"), false));
    auto* folderRow = new QWidget(content);
    auto* folderLayout = new QHBoxLayout(folderRow);
    folderLayout->setContentsMargins(0, 10, 0, 10);
    folderLayout->setSpacing(16);
    auto* folderText = new QVBoxLayout;
    folderText->setSpacing(2);
    folderText->addWidget(new QLabel(tr("Dictionary folder"), folderRow));
    auto* folderPath = new QLabel(dictionaryFolder, folderRow);
    folderPath->setProperty("muted", true);
    folderPath->setProperty("small", true);
    folderPath->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    folderPath->setWordWrap(true);
    folderPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderText->addWidget(folderPath);
    folderLayout->addLayout(folderText, 1);
    auto* openFolder = new QPushButton(tr("Open folder"), folderRow);
    connect(openFolder, &QPushButton::clicked, this,
            [dictionaryFolder] { QDesktopServices::openUrl(QUrl::fromLocalFile(dictionaryFolder)); });
    folderLayout->addWidget(openFolder, 0, Qt::AlignVCenter);
    body->addWidget(folderRow);
}

void SettingsDialog::updateThemeButtons()
{
    const core::Theme theme = m_settings.theme();
    const QSignalBlocker b1(m_systemButton);
    const QSignalBlocker b2(m_lightButton);
    const QSignalBlocker b3(m_darkButton);
    m_systemButton->setChecked(theme == core::Theme::System);
    m_lightButton->setChecked(theme == core::Theme::Light);
    m_darkButton->setChecked(theme == core::Theme::Dark);
}

void SettingsDialog::updateTextSizeLabel()
{
    m_textSizeLabel->setText(tr("%1 px").arg(m_settings.entryTextSize()));
}

} // namespace omnidict::ui
