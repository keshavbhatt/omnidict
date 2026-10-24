#include "ui/settings_dialog.h"

#include "core/settings.h"
#include "services/global_shortcuts.h"
#include "ui/icons.h"
#include "ui/style.h"
#include "ui/switch_button.h"
#include "ui/tray.h"

#include <QButtonGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

/// The key Omnidict suggests to the desktop, as people write it ("Ctrl+Alt+D").
QString suggestedKey()
{
    return QKeySequence(QString::fromLatin1(services::GlobalShortcuts::kPreferredTrigger).toUpper())
        .toString(QKeySequence::NativeText);
}

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

SettingsDialog::SettingsDialog(core::Settings& settings, const QString& dictionaryFolder, QWidget* parent,
                               services::GlobalShortcuts* shortcuts, Tray* tray)
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
    setupBackground(body, content, tray);
    setupQuickLookup(body, content, shortcuts);
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

void SettingsDialog::setupBackground(QVBoxLayout* body, QWidget* content, Tray* tray)
{
    // Background (mocks/tray.html notes 4 to 6): both rows need a tray.
    const bool available = tray != nullptr && tray->isAvailable();
    body->addWidget(makeSectionLabel(content, tr("Background"), false));
    auto* keep = new SwitchButton(tr("Keep running in the tray when the window is closed"), content);
    keep->setObjectName(u"keepInTraySwitch"_s);
    keep->setChecked(available && m_settings.keepInTray());
    keep->setEnabled(available);
    connect(keep, &SwitchButton::toggled, this, [this](bool on) { m_settings.setKeepInTray(on); });
    body->addWidget(makeRow(content, tr("Keep running in the tray when the window is closed"),
                            available ? tr("Quick lookup works only while Omnidict runs")
                                      : tr("This desktop shows no tray icons. On GNOME, the AppIndicator "
                                           "extension adds them."),
                            keep));
    body->addWidget(makeSeparator(content));

    auto* login = new SwitchButton(tr("Start at login, in the tray"), content);
    login->setObjectName(u"startAtLoginSwitch"_s);
    login->setChecked(available && m_settings.startAtLogin());
    login->setEnabled(available);
    if (available) {
        connect(login, &SwitchButton::toggled, tray, &Tray::setStartAtLogin);
        // The desktop may refuse (the portal's dialog): the switch shows what is in effect.
        connect(tray, &Tray::startAtLoginSettled, login, [login](bool enabled) {
            const QSignalBlocker blocker(login);
            login->setChecked(enabled);
        });
    }
    body->addWidget(makeRow(content, tr("Start at login, in the tray"),
                            available ? tr("Ready for quick lookup without opening the window")
                                      : tr("Needs a tray, as above"),
                            login));
    body->addWidget(makeSeparator(content));
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

QString SettingsDialog::popupCommand()
{
    if (const QString flatpak = qEnvironmentVariable("FLATPAK_ID"); !flatpak.isEmpty()) {
        return u"flatpak run %1 --popup"_s.arg(flatpak);
    }
    if (const QString snap = qEnvironmentVariable("SNAP_NAME"); !snap.isEmpty()) {
        return u"snap run %1 --popup"_s.arg(snap);
    }
    if (const QString appImage = qEnvironmentVariable("APPIMAGE"); !appImage.isEmpty()) {
        return u"\"%1\" --popup"_s.arg(appImage);
    }
    return u"omnidict --popup"_s;
}

QProcessEnvironment SettingsDialog::systemEnvironment()
{
    // What points at Omnidict's own libraries (the dev runtime, an AppImage) would make a
    // desktop program load them instead of the system's: System Settings then failed with
    // "module org.kde.kirigami is not installed".
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const char* name : {"LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                             "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_FORCE_STDERR_LOGGING"}) {
        environment.remove(QString::fromLatin1(name));
    }
    return environment;
}

void SettingsDialog::startWithSystemEnvironment(const QStringList& command)
{
    if (command.isEmpty()) {
        return;
    }
    QProcess process;
    process.setProgram(command.first());
    process.setArguments(command.mid(1));
    process.setProcessEnvironment(systemEnvironment());
    process.startDetached();
}

QUrl SettingsDialog::shortcutSettingsUrl()
{
    const bool sandboxed = !qEnvironmentVariableIsEmpty("FLATPAK_ID") || !qEnvironmentVariableIsEmpty("SNAP");
    if (!sandboxed || !qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(u"KDE"_s)) {
        return {};
    }
    return {u"systemsettings://kcm_keys"_s};
}

QStringList SettingsDialog::shortcutSettingsCommand()
{
    const bool sandboxed = !qEnvironmentVariableIsEmpty("FLATPAK_ID") || !qEnvironmentVariableIsEmpty("SNAP");
    if (sandboxed || !qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(u"KDE"_s)) {
        return {};
    }
    return {u"systemsettings"_s, u"kcm_keys"_s};
}

void SettingsDialog::openShortcutSettings(services::GlobalShortcuts* shortcuts)
{
    if (shortcuts != nullptr && shortcuts->canConfigure()) {
        shortcuts->configure(); // the portal's own dialog (interface version 2)
        return;
    }
    if (const QUrl url = shortcutSettingsUrl(); url.isValid()) {
        QDesktopServices::openUrl(url); // from the Flatpak or snap, through the portal
        return;
    }
    startWithSystemEnvironment(shortcutSettingsCommand());
}

bool SettingsDialog::canOpenShortcutSettings(const services::GlobalShortcuts* shortcuts)
{
    return shortcuts != nullptr && (shortcuts->canConfigure() || !shortcutSettingsCommand().isEmpty() ||
                                    shortcutSettingsUrl().isValid());
}

QString SettingsDialog::shortcutEntryName()
{
    if (const QString snap = qEnvironmentVariable("SNAP_NAME"); !snap.isEmpty()) {
        return u"snap."_s + snap;
    }
    return u"Omnidict"_s;
}

void SettingsDialog::setupQuickLookup(QVBoxLayout* body, QWidget* content,
                                      services::GlobalShortcuts* shortcuts)
{
    // mocks/quick-lookup.html notes 4 and 5: the global shortcut where the desktop has
    // the portal, and the custom-shortcut route where it does not.
    m_shortcuts = shortcuts;
    body->addWidget(makeSectionLabel(content, tr("Quick lookup"), false));
    body->addWidget(buildQuickLookupShortcutRow(content));
    body->addWidget(buildQuickLookupFallbackRow(content));
    if (shortcuts != nullptr) {
        connect(m_quickChange, &QPushButton::clicked, this, [shortcuts] { openShortcutSettings(shortcuts); });
        connect(shortcuts, &services::GlobalShortcuts::stateChanged, this,
                &SettingsDialog::refreshQuickLookup);
    }
    refreshQuickLookup();
}

QWidget* SettingsDialog::buildQuickLookupShortcutRow(QWidget* content)
{
    m_quickKeys = new QWidget(content);
    m_quickKeys->setObjectName(u"quickLookupKeys"_s);
    auto* keysLayout = new QHBoxLayout(m_quickKeys);
    keysLayout->setContentsMargins(0, 0, 0, 0);
    keysLayout->setSpacing(8);
    m_quickChange = new QPushButton(tr("Change..."), content);
    m_quickChange->setObjectName(u"quickLookupChange"_s);
    auto* controls = new QWidget(content);
    auto* controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(8);
    controlsLayout->addWidget(m_quickKeys);
    controlsLayout->addWidget(m_quickChange);
    m_quickShortcutRow = makeRow(content, tr("Quick lookup shortcut"), u" "_s, controls);
    m_quickShortcutRow->setObjectName(u"quickLookupShortcutRow"_s);
    // The description (the second label of the row's text column) says where to change the
    // key, which depends on the desktop and the sandbox (refreshQuickLookup).
    for (QLabel* label : m_quickShortcutRow->findChildren<QLabel*>()) {
        if (label->text() == u" "_s) {
            m_quickShortcutHint = label; // the placeholder description above
        }
    }
    m_quickShortcutHint->setObjectName(u"quickLookupHint"_s);
    m_quickShortcutHint->setTextFormat(Qt::RichText); // the entry name in bold (no key)
    return m_quickShortcutRow;
}

QWidget* SettingsDialog::buildQuickLookupFallbackRow(QWidget* content)
{
    auto* copy = new QLabel(content);
    copy->setObjectName(u"quickLookupCommand"_s);
    copy->setTextFormat(Qt::RichText);
    copy->setWordWrap(true);
    copy->setProperty("muted", true);
    copy->setProperty("small", true);
    const QString command = popupCommand();
    copy->setText(
        tr("Add a custom shortcut in its keyboard settings that runs <code>%1</code>. %2")
            .arg(command.toHtmlEscaped(), u"<a href=\"copy\">%1</a>"_s.arg(tr("Copy the command"))));
    connect(copy, &QLabel::linkActivated, this,
            [command] { QGuiApplication::clipboard()->setText(command); });
    m_quickFallbackRow =
        makeRow(content, tr("Where the desktop has no global shortcuts"), {}, new QWidget(content));
    m_quickFallbackRow->setObjectName(u"quickLookupFallbackRow"_s);
    qobject_cast<QVBoxLayout*>(m_quickFallbackRow->layout()->itemAt(0)->layout())->addWidget(copy);
    return m_quickFallbackRow;
}

void SettingsDialog::refreshQuickLookup()
{
    using State = services::GlobalShortcuts::State;
    const State state = m_shortcuts != nullptr ? m_shortcuts->state() : State::Unavailable;
    auto* keysLayout = qobject_cast<QHBoxLayout*>(m_quickKeys->layout());
    while (const QLayoutItem* item = keysLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const bool noKey = state == State::NoKey;
    if (state == State::Active) {
        keysLayout->addWidget(makeKeyCaps(m_shortcuts->trigger(), m_quickKeys));
    } else if (noKey) {
        // mocks/quick-lookup-no-key.html note 2: in words and with a mark, not colour alone.
        auto* mark = new QLabel(m_quickKeys);
        const int size = mark->fontMetrics().height();
        mark->setPixmap(icons::pixmap(u"alert"_s, Tokens::current().warning, size, devicePixelRatioF()));
        auto* status = new QLabel(tr("No key"), m_quickKeys);
        status->setObjectName(u"quickLookupNoKey"_s);
        status->setProperty("tone", u"warning"_s);
        keysLayout->addWidget(mark);
        keysLayout->addWidget(status);
    } else {
        auto* status = new QLabel(state == State::Checking ? tr("Checking...") : tr("Not set"), m_quickKeys);
        status->setProperty("muted", true);
        keysLayout->addWidget(status);
    }
    // Change... where it can do something: the portal's dialog, else KDE's Shortcuts page;
    // a sandboxed copy on KDE says where to go instead (mocks/quick-lookup.html note 4).
    const bool canChange = canOpenShortcutSettings(m_shortcuts);
    m_quickChange->setVisible(canChange);
    m_quickChange->setText(noKey ? tr("Choose a key...") : tr("Change..."));
    m_quickChange->setProperty("primary", noKey);
    m_quickChange->style()->unpolish(m_quickChange);
    m_quickChange->style()->polish(m_quickChange);
    const bool kde = qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(u"KDE"_s);
    QString hint;
    if (noKey) {
        hint = kde ? tr("The desktop accepted the shortcut but gave it no key. Choose one in System "
                        "Settings, Keyboard, Shortcuts, <b>%1</b>: tick the suggested %2 or add your own.")
                         .arg(shortcutEntryName().toHtmlEscaped(), suggestedKey())
                   : tr("The desktop accepted the shortcut but gave it no key. Choose one in the "
                        "desktop's keyboard settings.");
    } else if (!canChange && kde) {
        hint = tr("Select a word in any app and press it. Change it in System Settings, Keyboard, "
                  "Shortcuts, %1.")
                   .arg(shortcutEntryName());
    } else {
        hint = tr("Select a word in any app and press it. Change it in the desktop's keyboard settings.");
    }
    m_quickShortcutHint->setText(hint);
    m_quickShortcutRow->setVisible(state == State::Active || state == State::NoKey ||
                                   state == State::Checking);
    m_quickFallbackRow->setVisible(state == State::Unavailable || state == State::Declined);
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
