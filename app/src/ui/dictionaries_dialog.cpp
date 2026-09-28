#include "ui/dictionaries_dialog.h"

#include "core/catalog.h"
#include "core/installer.h"
#include "core/settings.h"
#include "services/dictionary_manager.h"
#include "ui/empty_state.h"
#include "ui/icons.h"
#include "ui/style.h"
#include "ui/switch_button.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

constexpr int kSheetWidth = 760;
constexpr int kIconSize = 18;
constexpr int kGripSize = 16;
constexpr int kLanguageComboVisibleItems = 12;
constexpr int kAvailableTab = 1;
constexpr int kFilterTypingPauseMs = 150;
constexpr int kActionSpacing = 6;
constexpr int kConfirmWidth = 380;
/// A drop-down's width beyond its text: the sheet's 12 px left and 4 px right
/// padding, the 26 px arrow, and a little air.
constexpr int kComboChrome = 12 + 4 + 26 + 8;
constexpr int kFilterFieldMinWidth = 120;
constexpr int kSmallLabelPixels = 12; ///< style.cpp QLabel[small="true"]
constexpr int kRingSize = 28;
constexpr qreal kDimmedOpacity = 0.55;

QString humanSize(qint64 bytes)
{
    // SI (decimal) units, to match DOCS/mocks (169 MB, 1.0 GB), not the IEC
    // default (169 MiB).
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

/// A hairline under every row but the last (mock.css `.set` / `.set:last-child`).
void applyRowBorder(QWidget* row, bool isLast)
{
    row->setAttribute(Qt::WA_StyledBackground, true);
    // The hairline itself is in the app style sheet (ui/style.cpp): a per-row sheet
    // would change how the app sheet applies to the row's buttons.
    row->setProperty("dictionaryRow", !isLast);
}

/// A language's English name, as the dictionary names in the list spell it ("Dhivehi"
/// in "Dhivehi-English"): the catalogue's name (schema 3), else Qt's English name for
/// the code, else the code itself, capitalised. Native names were dropped: a script with
/// no installed font (Thaana, ...) showed empty boxes, and looking for a font that has
/// it cost the sheet half a second on every opening.
QString languageLabel(const QString& code, const QString& catalogueName)
{
    QString name = catalogueName;
    if (name.isEmpty()) {
        const QLocale::Language language = QLocale(code).language();
        if (language != QLocale::C && language != QLocale::AnyLanguage) {
            name = QLocale::languageToString(language);
        }
    }
    if (name.isEmpty()) {
        name = code;
    }
    if (!name.isEmpty()) {
        name[0] = name.at(0).toUpper();
    }
    return name;
}

/// A rounded pill (mock.css .pill), painted: a style sheet on a label did not
/// give it the padding and rounding.
class Pill : public QLabel
{
public:
    Pill(const QString& text, bool accent, QWidget* parent)
        : QLabel(text, parent)
        , m_accent(accent)
    {
        QFont small = font();
        small.setPixelSize(kPillTextPixels);
        setFont(small);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        return {metrics.horizontalAdvance(text()) + (2 * kPillPadX), metrics.height() + (2 * kPillPadY)};
    }
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Tokens& t = Tokens::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_accent ? t.accentSoft : t.hover);
        const qreal radius = height() / 2.0;
        painter.drawRoundedRect(QRectF(rect()), radius, radius);
        painter.setPen(m_accent ? t.accent : t.muted);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    static constexpr int kPillTextPixels = 12;
    static constexpr int kPillPadX = 10;
    static constexpr int kPillPadY = 3;
    bool m_accent;
};

void makeSmall(QPushButton* button)
{
    button->setFixedHeight(28);
    button->setProperty("small", true); // app style sheet: less padding, no minimum height
}

bool isUnderRoot(const QString& path, const QString& root)
{
    if (root.isEmpty() || path.isEmpty()) {
        return false;
    }
    const QString absPath = QFileInfo(path).absoluteFilePath();
    const QString absRoot = QDir(root).absolutePath();
    return absPath == absRoot || absPath.startsWith(absRoot + u'/');
}

std::optional<core::CatalogEntry> newerVersion(const core::Catalog& catalog,
                                               const services::DictionaryInfo& info)
{
    for (const core::CatalogEntry& entry : catalog.dictionaries) {
        if (entry.dictId == info.dictId && core::compareVersions(entry.version, info.version) > 0) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<services::DownloadStatus> findDownload(const QList<services::DownloadStatus>& downloads,
                                                     const QString& dictId)
{
    for (const services::DownloadStatus& status : downloads) {
        if (status.dictId == dictId) {
            return status;
        }
    }
    return std::nullopt;
}

std::optional<QString> effectiveVersion(const QList<services::DictionaryInfo>& installed,
                                        const QHash<QString, QString>& overrides, const QString& dictId)
{
    const auto overrideIt = overrides.constFind(dictId);
    if (overrideIt != overrides.constEnd()) {
        return overrideIt.value();
    }
    for (const services::DictionaryInfo& info : installed) {
        if (info.dictId == dictId) {
            return info.version;
        }
    }
    return std::nullopt;
}

void populateLanguageCombo(QComboBox* combo, const QHash<QString, QString>& codes, const QString& anyLabel)
{
    const QSignalBlocker blocker(combo);
    // A few hundred languages: a short scrolling list, not a popup the height of
    // the screen (the style sheet's `combobox-popup: 0` makes Qt honour this).
    combo->setMaxVisibleItems(kLanguageComboVisibleItems);
    combo->clear();
    combo->addItem(anyLabel, QString());
    QList<std::pair<QString, QString>> items; // (label, code)
    for (auto it = codes.cbegin(); it != codes.cend(); ++it) {
        if (!it.key().isEmpty()) {
            items.append({languageLabel(it.key(), it.value()), it.key()});
        }
    }
    std::ranges::sort(items,
                      [](const auto& a, const auto& b) { return a.first.localeAwareCompare(b.first) < 0; });
    for (const auto& [label, code] : items) {
        combo->addItem(label, code);
    }
}

/// A drop-down as wide as the widest of `samples` needs, measured in its own font.
void fitBoxWidth(QComboBox* combo, const QStringList& samples)
{
    const QFontMetrics metrics(combo->font());
    int widest = 0;
    for (const QString& sample : samples) {
        widest = std::max(widest, metrics.horizontalAdvance(sample));
    }
    combo->setMinimumWidth(widest + kComboChrome);
}

/// Lets a drop-down's popup be wider than the box, so long names show in full.
void fitPopupWidth(QComboBox* combo)
{
    QAbstractItemView* view = combo->view();
    const int scrollBar = view->verticalScrollBar()->sizeHint().width();
    view->setMinimumWidth(view->sizeHintForColumn(0) + scrollBar + (2 * view->frameWidth()));
}

void restoreComboSelection(QComboBox* combo, const QString& code)
{
    fitPopupWidth(combo);
    const int index = combo->findData(code);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

/// "Any", then every provider in the catalogue with its count: "Wiktionary (14)".
void populateProviderCombo(QComboBox* combo, const QList<core::CatalogEntry>& entries,
                           const QString& anyLabel)
{
    const QSignalBlocker blocker(combo);
    combo->setMaxVisibleItems(kLanguageComboVisibleItems);
    combo->clear();
    combo->addItem(anyLabel, QString());
    QHash<QString, int> counts;
    for (const core::CatalogEntry& entry : entries) {
        ++counts[core::providerName(entry)];
    }
    QStringList providers = counts.keys();
    std::ranges::sort(providers,
                      [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
    for (const QString& provider : providers) {
        combo->addItem(u"%1 (%2)"_s.arg(provider).arg(counts.value(provider)), provider);
    }
}

/// A muted small label, right-aligned at least `minWidth` wide: one cell of an Available row.
QLabel* makeColumnLabel(const QString& text, int minWidth, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("muted", true);
    label->setProperty("small", true);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setMinimumWidth(minWidth);
    return label;
}

/// The width a muted small label needs for `sample` (the widest value its column shows).
int columnWidth(const QWidget* reference, const QString& sample)
{
    QFont small = reference->font();
    small.setPixelSize(kSmallLabelPixels);
    return QFontMetrics(small).horizontalAdvance(sample) + 2;
}

/// A small painted progress indicator (mock.css `.ring`): an accent arc over
/// the border colour, `percent` of the way round clockwise from the top.
class ProgressRing : public QWidget
{
public:
    explicit ProgressRing(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(kRingSize, kRingSize);
    }

    void setPercent(int percent)
    {
        m_percent = qBound(0, percent, 100);
        update();
    }

    /// While installing, the amount of work is unknown: a quarter arc turns
    /// instead of filling.
    void setBusy()
    {
        constexpr int kFrameMs = 16;
        constexpr int kDegreesPerFrame = 6;
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            m_angle = (m_angle + kDegreesPerFrame) % 360;
            update();
        });
        timer->start(kFrameMs);
        m_busy = true;
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Tokens& t = Tokens::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal inset = 2.0;
        const QRectF bounds(inset, inset, width() - (2 * inset), height() - (2 * inset));
        QPen basePen(t.border);
        basePen.setWidthF(3.0);
        painter.setPen(basePen);
        painter.drawEllipse(bounds);
        QPen arcPen(t.accent);
        arcPen.setWidthF(3.0);
        arcPen.setCapStyle(Qt::RoundCap);
        painter.setPen(arcPen);
        if (m_busy) {
            constexpr int kQuarter = 90 * 16;
            painter.drawArc(bounds, (90 - m_angle) * 16, -kQuarter);
        } else if (m_percent > 0) {
            const int span = m_percent * 360 * 16 / 100;
            painter.drawArc(bounds, 90 * 16, -span);
        }
    }

private:
    int m_percent = 0;
    bool m_busy = false;
    int m_angle = 0; ///< degrees turned clockwise from the top, while busy
};

} // namespace

DictionariesDialog::DictionariesDialog(services::DictionaryManager& manager, core::Settings& settings,
                                       QList<services::DictionaryInfo> installed, QString dictionariesRoot,
                                       QWidget* parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_settings(settings)
    , m_installed(std::move(installed))
    , m_dictionariesRoot(std::move(dictionariesRoot))
    , m_tabs(new QTabWidget(this))
{
    setWindowTitle(titleWithApp(tr("Dictionaries")));
    setModal(true);
    resize(kSheetWidth, 560);
    setMinimumWidth(kSheetWidth);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* title = new QLabel(tr("Dictionaries"), this);
    title->setProperty("title", true);
    auto* titleRow = new QVBoxLayout;
    titleRow->setContentsMargins(20, 20, 20, 8);
    titleRow->addWidget(title);
    root->addLayout(titleRow);

    m_tabs->setObjectName(u"dictionariesTabs"_s);
    m_tabs->addTab(buildInstalledTab(), tr("Installed"));
    m_tabs->addTab(buildAvailableTab(), tr("Available"));
    root->addWidget(m_tabs, 1);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == kAvailableTab && m_availableRowsStale) {
            rebuildAvailableRows();
        }
    });

    connect(&m_manager, &services::DictionaryManager::catalogChanged, this, [this] {
        m_catalogFailure.clear();
        refreshFilters();
        rebuildInstalledRows();
        rebuildAvailableRows();
        updateAvailableFooter();
        finishUpdateCheck({});
    });
    connect(&m_manager, &services::DictionaryManager::catalogFailed, this, [this](const QString& reason) {
        m_catalogFailure = reason;
        updateAvailableFooter();
        finishUpdateCheck(reason);
    });
    connectDownloads();

    refreshFilters();
    rebuildInstalledRows();
    rebuildAvailableRows();
    updateAvailableFooter();

    m_manager.refreshCatalog(false);
}

void DictionariesDialog::connectDownloads()
{
    connect(&m_manager, &services::DictionaryManager::downloadChanged, this,
            [this](const services::DownloadStatus& status) { updateAvailableRow(status.dictId); });
    connect(&m_manager, &services::DictionaryManager::downloadsChanged, this, [this] {
        // A download left the list (finished or cancelled): refresh the rows that had one.
        QSet<QString> ids = m_rowsWithDownload;
        for (const services::DownloadStatus& status : m_manager.downloads()) {
            ids.insert(status.dictId);
        }
        for (const QString& dictId : std::as_const(ids)) {
            updateAvailableRow(dictId);
        }
    });
    connect(&m_manager, &services::DictionaryManager::installed, this, [this](const QString& dictId) {
        m_installedOverride.insert(dictId, m_pendingInstallVersion.take(dictId));
        updateAvailableRow(dictId);
    });
    connect(&m_manager, &services::DictionaryManager::removed, this, [this](const QString& dictId) {
        m_installedOverride.remove(dictId);
        m_pendingInstallVersion.remove(dictId);
        updateAvailableRow(dictId);
    });
}

void DictionariesDialog::setInstalled(const QList<services::DictionaryInfo>& installed)
{
    m_installed = installed;
    m_installedOverride.clear(); // the real library is now authoritative
    rebuildInstalledRows();
    rebuildAvailableRows();
}

void DictionariesDialog::showAvailable()
{
    m_tabs->setCurrentIndex(kAvailableTab);
}

bool DictionariesDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_installedList && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->modifiers().testFlag(Qt::AltModifier) &&
            (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down)) {
            moveSelectedRow(keyEvent->key() == Qt::Key_Up ? -1 : 1);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void DictionariesDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        // Icons, pills and the progress ring paint from Tokens::current() at
        // build time; a full rebuild is the simplest way to repaint them all.
        rebuildInstalledRows();
        rebuildAvailableRows();
    }
}

QWidget* DictionariesDialog::buildInstalledTab()
{
    auto* page = new QWidget(m_tabs);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    m_installedList = new QListWidget(page);
    m_installedList->setObjectName(u"installedList"_s);
    m_installedList->setFrameShape(QFrame::NoFrame);
    m_installedList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_installedList->setDragDropMode(QAbstractItemView::InternalMove);
    m_installedList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Rows span the list's width and follow it when the sheet resizes.
    m_installedList->setResizeMode(QListView::Adjust);
    m_installedList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_installedList->setSpacing(0);
    // The row widgets carry their own look (including the hairline between
    // rows); the list itself contributes no selection or hover colour of its
    // own; Alt+Up/Alt+Down still work through the model's current index.
    m_installedList->setStyleSheet(
        u"QListWidget { border: none; background: transparent; }"
        u"QListWidget::item { border: none; padding: 0; }"
        u"QListWidget::item:hover { background: transparent; }"
        u"QListWidget::item:selected { background: %1; border-radius: 8px; }"
        u"QListWidget:focus { outline: none; }"_s.arg(Tokens::current().hover.name()));
    m_installedList->installEventFilter(this);
    connect(m_installedList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem* /*previous*/) {
                m_selectedDictId = current != nullptr ? current->data(Qt::UserRole).toString() : QString();
            });
    // QListWidget's item widgets do not reliably follow their item across an
    // internal drag-and-drop move, so a move just triggers a full rebuild
    // (which reattaches every row widget in the new order) instead of trying
    // to track individual rows through the drop.
    connect(m_installedList->model(), &QAbstractItemModel::rowsMoved, this,
            [this] { QTimer::singleShot(0, this, &DictionariesDialog::writeOrderFromList); });

    m_installedEmpty = new EmptyState(page);
    m_installedEmpty->setObjectName(u"installedEmpty"_s);
    m_installedEmpty->setButtonPrimary(true);
    connect(m_installedEmpty, &EmptyState::buttonClicked, this, &DictionariesDialog::showAvailable);
    m_installedStack = new QStackedWidget(page);
    m_installedStack->addWidget(m_installedList);
    m_installedStack->addWidget(m_installedEmpty);
    pageLayout->addWidget(m_installedStack, 1);

    pageLayout->addWidget(buildInstalledFoot(page));

    return page;
}

QFrame* DictionariesDialog::buildInstalledFoot(QWidget* page)
{
    auto* foot = new QFrame(page);
    foot->setProperty("sheetFoot", true);
    auto* footLayout = new QHBoxLayout(foot);
    footLayout->setContentsMargins(20, 12, 20, 12);
    m_installedFooter = new QLabel(foot);
    m_installedFooter->setObjectName(u"installedFooter"_s);
    m_installedFooter->setProperty("muted", true);
    m_installedFooter->setProperty("small", true);
    footLayout->addWidget(m_installedFooter);
    footLayout->addStretch(1);
    m_checkResult = new QLabel(foot);
    m_checkResult->setObjectName(u"checkResult"_s);
    m_checkResult->setProperty("small", true);
    m_checkResult->hide();
    footLayout->addWidget(m_checkResult);
    m_checkUpdatesButton = new QPushButton(tr("Check for updates"), foot);
    m_checkUpdatesButton->setObjectName(u"checkUpdatesButton"_s);
    connect(m_checkUpdatesButton, &QPushButton::clicked, this, &DictionariesDialog::checkForUpdates);
    footLayout->addWidget(m_checkUpdatesButton);
    auto* done = new QPushButton(tr("Done"), foot);
    done->setProperty("primary", true);
    done->setDefault(true);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    footLayout->addWidget(done);
    return foot;
}

QWidget* DictionariesDialog::buildAvailableTab()
{
    auto* page = new QWidget(m_tabs);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto* content = new QWidget(page);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(20, 12, 20, 0);
    contentLayout->setSpacing(8);
    contentLayout->addLayout(buildAvailableFilterRow(content));
    contentLayout->addWidget(buildAvailableScroll(content), 1);
    pageLayout->addWidget(content, 1);
    pageLayout->addWidget(buildAvailableFoot(page));

    return page;
}

QHBoxLayout* DictionariesDialog::buildAvailableFilterRow(QWidget* content)
{
    auto* filterRow = new QHBoxLayout;
    filterRow->setSpacing(8);
    m_filterField = new QLineEdit(content);
    m_filterField->setObjectName(u"availableFilter"_s);
    m_filterField->setPlaceholderText(tr("Filter by name"));
    // One rebuild when typing pauses, not one per keystroke (hundreds of rows each time).
    auto* typingPause = new QTimer(this);
    typingPause->setSingleShot(true);
    typingPause->setInterval(kFilterTypingPauseMs);
    connect(typingPause, &QTimer::timeout, this, [this] { rebuildAvailableRows(); });
    connect(m_filterField, &QLineEdit::textChanged, typingPause, qOverload<>(&QTimer::start));
    m_filterField->setMinimumWidth(kFilterFieldMinWidth);
    filterRow->addWidget(m_filterField, 1);
    m_fromCombo = addFilterCombo(filterRow, content, tr("From"), u"fromLanguage"_s);
    m_toCombo = addFilterCombo(filterRow, content, tr("To"), u"toLanguage"_s);
    // mocks/dictionaries-available.html note 7: filter by who provides the content.
    m_providerCombo = addFilterCombo(filterRow, content, tr("Provider"), u"provider"_s);
    return filterRow;
}

QComboBox* DictionariesDialog::addFilterCombo(QHBoxLayout* row, QWidget* parent, const QString& label,
                                              const QString& name)
{
    auto* caption = new QLabel(label, parent);
    caption->setProperty("muted", true);
    row->addWidget(caption);
    auto* combo = new QComboBox(parent);
    combo->setObjectName(name);
    // Sized for "Any language", not the longest name in the list, so the name
    // filter keeps its room; the popup widens to fit (fitPopupWidth).
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(1);
    fitBoxWidth(combo, {tr("Any language")});
    styleComboPopup(combo);
    combo->setAccessibleName(label);
    connect(combo, &QComboBox::currentIndexChanged, this, [this] { rebuildAvailableRows(); });
    row->addWidget(combo);
    return combo;
}

QScrollArea* DictionariesDialog::buildAvailableScroll(QWidget* content)
{
    auto* scroll = new QScrollArea(content);
    m_availableScroll = scroll;
    scroll->setObjectName(u"availableScroll"_s);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The scrollbar shows only when the rows do not fit (Qt's default).
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_availableList = new QWidget(scroll);
    m_availableList->setObjectName(u"availableList"_s);
    auto* listLayout = new QVBoxLayout(m_availableList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    listLayout->addStretch(1); // rows are always inserted before this
    scroll->setWidget(m_availableList);
    return scroll;
}

QFrame* DictionariesDialog::buildAvailableFoot(QWidget* page)
{
    auto* foot = new QFrame(page);
    foot->setProperty("sheetFoot", true);
    auto* footLayout = new QHBoxLayout(foot);
    footLayout->setContentsMargins(20, 12, 20, 12);
    m_availableFooter = new QLabel(foot);
    m_availableFooter->setObjectName(u"availableFooter"_s);
    m_availableFooter->setProperty("muted", true);
    m_availableFooter->setProperty("small", true);
    footLayout->addWidget(m_availableFooter);
    // mocks/dictionaries-available.html: Refresh reads the catalogue now (also F5).
    m_refreshButton =
        new QPushButton(icons::themed(u"refresh"_s, Tokens::current().text), tr("Refresh"), foot);
    m_refreshButton->setObjectName(u"refreshCatalogButton"_s);
    m_refreshButton->setProperty("flat", true);
    makeSmall(m_refreshButton);
    m_refreshButton->setToolTip(tr("Read the catalogue again (F5)"));
    connect(m_refreshButton, &QPushButton::clicked, this, &DictionariesDialog::refreshCatalog);
    footLayout->addWidget(m_refreshButton);
    footLayout->addStretch(1);
    auto* refreshKey = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refreshKey, &QShortcut::activated, this, &DictionariesDialog::refreshCatalog);
    m_tryAgainButton = new QPushButton(tr("Try again"), foot);
    m_tryAgainButton->setObjectName(u"tryAgainButton"_s);
    m_tryAgainButton->hide();
    connect(m_tryAgainButton, &QPushButton::clicked, this, [this] { m_manager.refreshCatalog(true); });
    footLayout->addWidget(m_tryAgainButton);
    auto* done = new QPushButton(tr("Done"), foot);
    done->setProperty("primary", true);
    done->setDefault(true);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    footLayout->addWidget(done);
    return foot;
}

QWidget* DictionariesDialog::buildInstalledRow(const services::DictionaryInfo& info, bool isLast)
{
    auto* row = new QWidget;
    row->setObjectName(u"installedRow_"_s + info.dictId);
    applyRowBorder(row, isLast);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(20, 10, 20, 10);
    layout->setSpacing(12);

    auto* grip = new QLabel(row);
    grip->setPixmap(icons::pixmap(u"grip"_s, Tokens::current().muted, kGripSize, row->devicePixelRatioF()));
    grip->setFixedSize(kGripSize, kGripSize);
    grip->setCursor(Qt::OpenHandCursor);
    layout->addWidget(grip);
    layout->addLayout(buildInstalledText(row, info), 1);
    appendUpdateControls(layout, row, info);

    auto* toggle = buildDictSwitch(row, info);
    layout->addWidget(toggle);

    auto* more = new QToolButton(row);
    more->setObjectName(u"moreButton_"_s + info.dictId);
    more->setAutoRaise(true);
    more->setToolTip(tr("More"));
    more->setAccessibleName(tr("More"));
    more->setIcon(icons::themed(u"more"_s, Tokens::current().text));
    attachRowMenu(more, info);
    layout->addWidget(more);

    if (!toggle->isChecked()) {
        auto* effect = new QGraphicsOpacityEffect(row);
        effect->setOpacity(kDimmedOpacity);
        row->setGraphicsEffect(effect);
    }
    return row;
}

/* static */ QVBoxLayout* DictionariesDialog::buildInstalledText(QWidget* row,
                                                                 const services::DictionaryInfo& info)
{
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(new QLabel(info.name, row));
    const qint64 size = QFileInfo(info.path).size();
    auto* desc = new QLabel(tr("%1, %2 entries, %3 on disk")
                                .arg(info.publisher, QLocale().toString(info.entryCount), humanSize(size)),
                            row);
    desc->setProperty("muted", true);
    desc->setProperty("small", true);
    text->addWidget(desc);
    return text;
}

void DictionariesDialog::appendUpdateControls(QHBoxLayout* layout, QWidget* row,
                                              const services::DictionaryInfo& info)
{
    const std::optional<core::CatalogEntry> newer = newerVersion(m_manager.catalog(), info);
    if (!newer) {
        return;
    }
    auto* pill = new Pill(tr("Update %1").arg(newer->version), /*accent=*/true, row);
    layout->addWidget(pill, 0, Qt::AlignVCenter);
    auto* update = new QPushButton(tr("Update"), row);
    update->setObjectName(u"installedUpdateButton_"_s + info.dictId);
    makeSmall(update);
    connect(update, &QPushButton::clicked, this, [this, entry = *newer] { startDownload(entry); });
    layout->addWidget(update);
}

SwitchButton* DictionariesDialog::buildDictSwitch(QWidget* row, const services::DictionaryInfo& info)
{
    auto* toggle = new SwitchButton(tr("Show %1 in search").arg(info.name), row);
    toggle->setObjectName(u"dictSwitch_"_s + info.dictId);
    toggle->setChecked(!m_settings.disabledDictionaries().contains(info.dictId));
    connect(toggle, &SwitchButton::toggled, this, [this, dictId = info.dictId](bool on) {
        QStringList disabled = m_settings.disabledDictionaries();
        disabled.removeAll(dictId);
        if (!on) {
            disabled.append(dictId);
        }
        m_settings.setDisabledDictionaries(disabled);
        rebuildInstalledRows();
    });
    return toggle;
}

void DictionariesDialog::attachRowMenu(QToolButton* button, const services::DictionaryInfo& info)
{
    button->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(button);
    menu->setObjectName(u"moreMenu_"_s + info.dictId);
    const QAction* const openFolder =
        menu->addAction(icons::themed(u"folder"_s, Tokens::current().text), tr("Show in folder..."));
    connect(openFolder, &QAction::triggered, this, [path = info.path] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    if (isUnderRoot(info.path, m_dictionariesRoot)) {
        QAction* const remove =
            menu->addAction(icons::themed(u"trash"_s, Tokens::current().danger), tr("Remove..."));
        remove->setObjectName(u"removeAction_"_s + info.dictId);
        connect(remove, &QAction::triggered, this, [this, dictId = info.dictId, name = info.name] {
            confirmRemove(dictId, name, core::installedSize(dictId, m_dictionariesRoot));
        });
    }
    button->setMenu(menu);
}

void DictionariesDialog::confirmRemove(const QString& dictId, const QString& name, qint64 freed)
{
    QDialog confirm(this);
    confirm.setWindowTitle(titleWithApp(tr("Remove dictionary")));
    confirm.setModal(true);
    confirm.setObjectName(u"confirmRemoveDialog"_s);
    // The footer spans the sheet edge to edge (mock.css .sheet-foot); only the message is inset.
    auto* layout = new QVBoxLayout(&confirm);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* message = new QLabel(tr("Remove %1? This frees %2.").arg(name, humanSize(freed)), &confirm);
    message->setWordWrap(true);
    auto* body = new QVBoxLayout; // the app sheet overrides a label's own margins
    body->setContentsMargins(20, 20, 20, 20);
    body->addWidget(message);
    layout->addLayout(body);
    layout->addStretch(1);

    auto* foot = new QFrame(&confirm);
    foot->setProperty("sheetFoot", true);
    auto* footLayout = new QHBoxLayout(foot);
    footLayout->setContentsMargins(20, 12, 20, 12);
    footLayout->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), &confirm);
    connect(cancel, &QPushButton::clicked, &confirm, &QDialog::reject);
    footLayout->addWidget(cancel);
    auto* remove = new QPushButton(tr("Remove"), &confirm);
    remove->setObjectName(u"confirmRemoveButton"_s);
    remove->setProperty("danger", true);
    remove->setDefault(true);
    connect(remove, &QPushButton::clicked, &confirm, &QDialog::accept);
    footLayout->addWidget(remove);
    layout->addWidget(foot);

    foot->setObjectName(u"confirmRemoveFoot"_s);
    // A known width first, so the wrapped message gets its height right (DOCS/LESSONS.md).
    confirm.setMinimumWidth(kConfirmWidth);
    confirm.adjustSize();
    if (confirm.exec() == QDialog::Accepted) {
        m_manager.remove(dictId);
    }
}

QWidget* DictionariesDialog::buildAvailableRow(const core::CatalogEntry& entry, bool isLast)
{
    auto* row = new QWidget;
    row->setObjectName(u"availableRow_"_s + entry.dictId);
    applyRowBorder(row, isLast);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 10, 0, 10);
    layout->setSpacing(12);

    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(new QLabel(entry.name, row));
    auto* desc = new QLabel(core::providerName(entry), row);
    desc->setProperty("muted", true);
    desc->setProperty("small", true);
    text->addWidget(desc);
    layout->addLayout(text, 1);

    // mocks/dictionaries-available.html note 8: entries in their own column, before the size.
    auto* entries =
        makeColumnLabel(tr("%1 entries").arg(QLocale().toString(entry.entryCount)),
                        columnWidth(row, tr("%1 entries").arg(QLocale().toString(9'999'999))), row);
    entries->setObjectName(u"entryCount_"_s + entry.dictId);
    layout->addWidget(entries);

    // The right-hand side changes with downloads; it is refilled on its own
    // (updateAvailableRow), so a download never rebuilds the whole list.
    auto* state = new QWidget(row);
    state->setObjectName(u"availableState_"_s + entry.dictId);
    auto* stateLayout = new QHBoxLayout(state);
    stateLayout->setContentsMargins(0, 0, 0, 0);
    stateLayout->setSpacing(12);
    layout->addWidget(state);
    m_availableStates.insert(entry.dictId, state);
    m_availableEntries.insert(entry.dictId, entry);
    fillAvailableState(state, entry);
    return row;
}

void DictionariesDialog::fillAvailableState(QWidget* row, const core::CatalogEntry& entry)
{
    auto* layout = qobject_cast<QHBoxLayout*>(row->layout());
    while (const QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const std::optional<services::DownloadStatus> status = findDownload(m_manager.downloads(), entry.dictId);
    if (status) {
        appendDownloadState(layout, row, entry, *status);
        return;
    }

    // Size, then the action, each in a column of its own width so every row lines up.
    layout->addWidget(makeColumnLabel(humanSize(entry.sizeCompressed), columnWidth(row, u"999.9 MB"_s), row));
    QWidget* action = makeActionColumn(row);
    auto* actionLayout = qobject_cast<QHBoxLayout*>(action->layout());
    layout->addWidget(action);

    const std::optional<QString> installedVersion =
        effectiveVersion(m_installed, m_installedOverride, entry.dictId);
    if (!installedVersion) {
        auto* download = new QPushButton(tr("Download"), action);
        download->setObjectName(u"downloadButton_"_s + entry.dictId);
        download->setProperty("primary", true);
        makeSmall(download);
        connect(download, &QPushButton::clicked, this, [this, entry] { startDownload(entry); });
        actionLayout->addWidget(download);
    } else if (core::compareVersions(*installedVersion, entry.version) < 0) {
        auto* update = new QPushButton(tr("Update"), action);
        update->setObjectName(u"availableUpdateButton_"_s + entry.dictId);
        makeSmall(update);
        connect(update, &QPushButton::clicked, this, [this, entry] { startDownload(entry); });
        actionLayout->addWidget(update);
    } else {
        auto* check = new QLabel(action);
        check->setPixmap(
            icons::pixmap(u"check"_s, Tokens::current().success, kIconSize, row->devicePixelRatioF()));
        actionLayout->addWidget(check);
        auto* installedLabel = new QLabel(tr("Installed"), action);
        installedLabel->setProperty("muted", true);
        installedLabel->setProperty("small", true);
        actionLayout->addWidget(installedLabel);
    }
}

QWidget* DictionariesDialog::makeActionColumn(QWidget* parent) const
{
    auto* action = new QWidget(parent);
    auto* layout = new QHBoxLayout(action);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kActionSpacing);
    layout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    action->setMinimumWidth(m_actionWidth);
    return action;
}

int DictionariesDialog::measureActionWidth() const
{
    // As wide as the widest thing the column holds: a Download button, or the check and "Installed".
    QPushButton probe(tr("Download"));
    probe.setProperty("primary", true);
    makeSmall(&probe);
    probe.ensurePolished();
    const int installedWidth = kIconSize + kActionSpacing + columnWidth(this, tr("Installed"));
    return std::max(probe.sizeHint().width(), installedWidth);
}

QString DictionariesDialog::progressText(int percent, qint64 received, qint64 total)
{
    return tr("%1%, %2 of %3").arg(percent).arg(humanSize(received), humanSize(total));
}

void DictionariesDialog::appendDownloadState(QHBoxLayout* layout, QWidget* row,
                                             const core::CatalogEntry& entry,
                                             const services::DownloadStatus& status)
{
    switch (status.state) {
    case services::DownloadStatus::Waiting: {
        auto* pill = new Pill(tr("Waiting"), /*accent=*/false, row);
        layout->addWidget(pill, 0, Qt::AlignVCenter);
        layout->addWidget(makeCancelButton(row, entry.dictId));
        break;
    }
    case services::DownloadStatus::Downloading: {
        const int percent = status.total > 0 ? static_cast<int>(status.received * 100 / status.total) : 0;
        auto* ring = new ProgressRing(row);
        ring->setObjectName(u"progressRing_"_s + entry.dictId);
        ring->setPercent(percent);
        layout->addWidget(ring);
        auto* label = new QLabel(progressText(percent, status.received, status.total), row);
        label->setObjectName(u"progressLabel_"_s + entry.dictId);
        label->setProperty("muted", true);
        label->setProperty("small", true);
        layout->addWidget(label);
        layout->addWidget(makeCancelButton(row, entry.dictId));
        break;
    }
    case services::DownloadStatus::Installing: {
        auto* ring = new ProgressRing(row);
        ring->setBusy();
        layout->addWidget(ring);
        auto* label = new QLabel(tr("Installing..."), row);
        label->setProperty("muted", true);
        label->setProperty("small", true);
        layout->addWidget(label);
        break;
    }
    case services::DownloadStatus::Failed: {
        auto* reason = new QLabel(status.error, row);
        reason->setObjectName(u"failureReason_"_s + entry.dictId);
        reason->setStyleSheet(u"color: %1;"_s.arg(Tokens::current().danger.name()));
        layout->addWidget(reason);
        auto* retry = new QPushButton(tr("Retry"), row);
        retry->setObjectName(u"retryButton_"_s + entry.dictId);
        connect(retry, &QPushButton::clicked, this, [this, entry] { startDownload(entry); });
        layout->addWidget(retry);
        break;
    }
    }
}

QToolButton* DictionariesDialog::makeCancelButton(QWidget* parent, const QString& dictId)
{
    auto* cancel = new QToolButton(parent);
    cancel->setObjectName(u"cancelButton_"_s + dictId);
    cancel->setAutoRaise(true);
    cancel->setToolTip(tr("Cancel"));
    cancel->setAccessibleName(tr("Cancel"));
    cancel->setIcon(icons::themed(u"x"_s, Tokens::current().text));
    connect(cancel, &QToolButton::clicked, this, [this, dictId] { m_manager.cancel(dictId); });
    return cancel;
}

void DictionariesDialog::startDownload(const core::CatalogEntry& entry)
{
    m_pendingInstallVersion.insert(entry.dictId, entry.version);
    m_manager.install(entry);
}

void DictionariesDialog::rebuildInstalledRows()
{
    const QSignalBlocker blocker(m_installedList->model());
    m_installedList->clear();
    QList<const services::DictionaryInfo*> ordered;
    for (const QString& dictId : orderedInstalledIds()) {
        const auto it = std::ranges::find_if(
            m_installed, [&](const services::DictionaryInfo& i) { return i.dictId == dictId; });
        if (it != m_installed.end()) {
            ordered << &(*it);
        }
    }
    for (qsizetype i = 0; i < ordered.size(); ++i) {
        const services::DictionaryInfo& info = *ordered.at(i);
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, info.dictId);
        item->setFlags((item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
        QWidget* widget = buildInstalledRow(info, i == ordered.size() - 1);
        // Height from the row; the width is the list's (QListView sizes items by their hint).
        item->setSizeHint(QSize(0, widget->sizeHint().height()));
        m_installedList->addItem(item);
        m_installedList->setItemWidget(item, widget);
    }
    QListWidgetItem* toSelect = nullptr;
    for (int i = 0; i < m_installedList->count(); ++i) {
        if (m_installedList->item(i)->data(Qt::UserRole).toString() == m_selectedDictId) {
            toSelect = m_installedList->item(i);
            break;
        }
    }
    if (toSelect == nullptr && m_installedList->count() > 0) {
        toSelect = m_installedList->item(0);
    }
    if (toSelect != nullptr) {
        m_installedList->setCurrentItem(toSelect);
    }
    m_tabs->setTabText(0, tr("Installed (%1)").arg(m_installed.size()));
    updateInstalledFooter();
    updateInstalledEmptyState();
}

void DictionariesDialog::updateInstalledEmptyState()
{
    const bool empty = m_installed.isEmpty();
    m_installedStack->setCurrentWidget(empty ? static_cast<QWidget*>(m_installedEmpty) : m_installedList);
    m_installedFooter->setVisible(!empty);
    m_checkUpdatesButton->setVisible(!empty);
    if (empty) {
        m_checkResult->hide();
    }
    if (!empty) {
        return;
    }
    const qsizetype offered = m_manager.catalog().dictionaries.size();
    const QString offer = offered > 0 ? tr("Choose from %1 free dictionaries.").arg(offered)
                                      : tr("Choose from free dictionaries in many languages.");
    m_installedEmpty->setContent(u"books"_s, tr("No dictionaries yet"),
                                 offer + u' ' +
                                     tr("Once downloaded, they work without an internet connection."),
                                 {}, tr("Browse dictionaries"));
}

void DictionariesDialog::updateAvailableRow(const QString& dictId)
{
    const bool downloading = findDownload(m_manager.downloads(), dictId).has_value();
    if (downloading) {
        m_rowsWithDownload.insert(dictId);
    } else {
        m_rowsWithDownload.remove(dictId);
    }
    QWidget* state = m_availableStates.value(dictId);
    if (state == nullptr) {
        return; // filtered out of the list
    }
    // Progress within one download: move the ring and the numbers, nothing else.
    const std::optional<services::DownloadStatus> status = findDownload(m_manager.downloads(), dictId);
    auto* ring = dynamic_cast<ProgressRing*>(state->findChild<QWidget*>(u"progressRing_"_s + dictId));
    auto* label = state->findChild<QLabel*>(u"progressLabel_"_s + dictId);
    if (status && status->state == services::DownloadStatus::Downloading && ring != nullptr &&
        label != nullptr) {
        const int percent = status->total > 0 ? static_cast<int>(status->received * 100 / status->total) : 0;
        ring->setPercent(percent);
        label->setText(progressText(percent, status->received, status->total));
        return;
    }
    fillAvailableState(state, m_availableEntries.value(dictId));
}

void DictionariesDialog::rebuildAvailableRows()
{
    QList<core::CatalogEntry> entries = m_manager.catalog().dictionaries;
    std::ranges::sort(entries, [](const core::CatalogEntry& a, const core::CatalogEntry& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    QList<core::CatalogEntry> shown;
    for (const core::CatalogEntry& entry : entries) {
        if (matchesFilters(entry)) {
            shown << entry;
        }
    }
    // mocks/dictionaries-available.html note 6: the catalogue's size, and how much a filter shows.
    m_shownCount = shown.size();
    m_tabs->setTabText(kAvailableTab,
                       entries.isEmpty() ? tr("Available") : tr("Available (%1)").arg(entries.size()));
    updateAvailableFooter();

    // Hundreds of row widgets take a noticeable moment: they are built only while the
    // Available tab is showing, and on switching to it when something changed meanwhile.
    m_availableRowsStale = m_tabs->currentIndex() != kAvailableTab;
    if (m_availableRowsStale) {
        return;
    }

    // Keep the reader's place: rebuilding (a filter, a new catalogue) must not jump to the top.
    QScrollBar* scrollBar = m_availableScroll->verticalScrollBar();
    const int position = scrollBar->value();
    m_availableList->setUpdatesEnabled(false);
    m_availableStates.clear();
    m_availableEntries.clear();
    auto* listLayout = qobject_cast<QVBoxLayout*>(m_availableList->layout());
    while (listLayout->count() > 1) {
        const QLayoutItem* const item = listLayout->takeAt(0);
        delete item->widget();
        delete item;
    }
    m_actionWidth = measureActionWidth();
    for (qsizetype i = 0; i < shown.size(); ++i) {
        listLayout->insertWidget(static_cast<int>(i), buildAvailableRow(shown.at(i), i == shown.size() - 1));
    }
    m_availableList->setUpdatesEnabled(true);
    // The new rows are laid out on the next pass; restore the place after it.
    QTimer::singleShot(0, this, [scrollBar, position] { scrollBar->setValue(position); });
}

bool DictionariesDialog::matchesFilters(const core::CatalogEntry& entry) const
{
    const QString filterText = m_filterField->text().trimmed();
    const QString fromCode = m_fromCombo->currentData().toString();
    const QString toCode = m_toCombo->currentData().toString();
    const QString provider = m_providerCombo->currentData().toString();
    return (filterText.isEmpty() || entry.name.contains(filterText, Qt::CaseInsensitive)) &&
           (fromCode.isEmpty() || entry.sourceLang == fromCode) &&
           (toCode.isEmpty() || entry.targetLang == toCode) &&
           (provider.isEmpty() || core::providerName(entry) == provider);
}

void DictionariesDialog::refreshFilters()
{
    const QString previousFrom = m_fromCombo->currentData().toString();
    const QString previousTo = m_toCombo->currentData().toString();
    const QString previousProvider = m_providerCombo->currentData().toString();
    // code -> the catalogue's English name for it (empty for older bundles)
    QHash<QString, QString> sources;
    QHash<QString, QString> targets;
    for (const core::CatalogEntry& entry : m_manager.catalog().dictionaries) {
        if (sources.value(entry.sourceLang).isEmpty()) {
            sources.insert(entry.sourceLang, entry.sourceLangName);
        }
        if (targets.value(entry.targetLang).isEmpty()) {
            targets.insert(entry.targetLang, entry.targetLangName);
        }
    }
    const QString anyLanguage = tr("Any language");
    populateLanguageCombo(m_fromCombo, sources, anyLanguage);
    populateLanguageCombo(m_toCombo, targets, anyLanguage);
    restoreComboSelection(m_fromCombo, previousFrom);
    restoreComboSelection(m_toCombo, previousTo);
    populateProviderCombo(m_providerCombo, m_manager.catalog().dictionaries, tr("Any"));
    restoreComboSelection(m_providerCombo, previousProvider);
    // The provider list is short: the box fits every choice, "Wiktionary (310)" included.
    QStringList providers;
    for (int i = 0; i < m_providerCombo->count(); ++i) {
        providers << m_providerCombo->itemText(i);
    }
    fitBoxWidth(m_providerCombo, providers);
}

void DictionariesDialog::updateInstalledFooter()
{
    qint64 total = 0;
    for (const services::DictionaryInfo& info : m_installed) {
        total += QFileInfo(info.path).size();
    }
    m_installedFooter->setText(tr("%1 dictionaries use %2").arg(m_installed.size()).arg(humanSize(total)));
}

void DictionariesDialog::checkForUpdates()
{
    m_checkingUpdates = true;
    m_checkResult->hide();
    m_checkUpdatesButton->setEnabled(false);
    m_checkUpdatesButton->setText(tr("Checking..."));
    m_manager.refreshCatalog(/*force=*/true);
}

void DictionariesDialog::finishUpdateCheck(const QString& failure)
{
    if (!m_checkingUpdates) {
        return;
    }
    m_checkingUpdates = false;
    m_checkUpdatesButton->setEnabled(true);
    m_checkUpdatesButton->setText(tr("Check for updates"));
    QString tone;
    if (!failure.isEmpty()) {
        tone = u"danger"_s;
        m_checkResult->setText(tr("Could not check: %1").arg(failure));
    } else {
        const auto updates = std::ranges::count_if(m_installed, [this](const services::DictionaryInfo& info) {
            return newerVersion(m_manager.catalog(), info).has_value();
        });
        // Plurals by hand: without a translation loaded, %n forms render literally.
        if (updates == 0) {
            m_checkResult->setText(tr("All dictionaries are up to date"));
        } else {
            tone = u"accent"_s;
            m_checkResult->setText(updates == 1 ? tr("1 update available")
                                                : tr("%1 updates available").arg(updates));
        }
    }
    m_checkResult->setProperty("muted", tone.isEmpty());
    m_checkResult->setProperty("tone", tone);
    m_checkResult->style()->unpolish(m_checkResult);
    m_checkResult->style()->polish(m_checkResult);
    m_checkResult->show();
}

void DictionariesDialog::refreshCatalog()
{
    m_manager.refreshCatalog(/*force=*/true);
    updateAvailableFooter();
}

void DictionariesDialog::updateAvailableFooter()
{
    const bool refreshing = m_manager.isRefreshingCatalog();
    m_refreshButton->setEnabled(!refreshing);
    m_refreshButton->setText(refreshing ? tr("Refreshing...") : tr("Refresh"));
    const bool failed = !m_catalogFailure.isEmpty() && !refreshing;
    // One line beside Refresh; only the longer failure reason, shown alone, wraps.
    m_availableFooter->setWordWrap(failed);
    m_availableFooter->setSizePolicy(failed ? QSizePolicy::Expanding : QSizePolicy::Preferred,
                                     QSizePolicy::Preferred);
    if (failed) {
        m_availableFooter->setText(tr("Catalogue unavailable: %1").arg(m_catalogFailure));
        m_refreshButton->hide(); // Try again says the same
        m_tryAgainButton->show();
        return;
    }
    m_refreshButton->show();
    m_tryAgainButton->hide();

    const QUrl url = m_manager.catalogUrl();
    QString host = url.host();
    if (url.port() > 0) {
        host += u':' + QString::number(url.port());
    }
    const QDateTime fetchedAt = m_manager.catalogFetchedAt();
    if (!fetchedAt.isValid()) {
        m_availableFooter->setText(shownPrefix() + tr("Catalogue from %1").arg(host));
        return;
    }
    const QDate fetchedDate = fetchedAt.toLocalTime().date();
    const QDate today = QDate::currentDate();
    const qint64 minutes = fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) / 60;
    constexpr qint64 kMinutesPerHour = 60;
    QString when;
    // Plurals by hand: without a translation loaded, %n forms render literally.
    if (minutes < 1) {
        when = tr("just now");
    } else if (minutes < kMinutesPerHour) {
        when = minutes == 1 ? tr("1 minute ago") : tr("%1 minutes ago").arg(minutes);
    } else if (fetchedDate == today) {
        const qint64 hours = minutes / kMinutesPerHour;
        when = hours == 1 ? tr("1 hour ago") : tr("%1 hours ago").arg(hours);
    } else if (fetchedDate == today.addDays(-1)) {
        when = tr("yesterday");
    } else {
        when = QLocale().toString(fetchedDate, QLocale::ShortFormat);
    }
    m_availableFooter->setText(shownPrefix() + tr("Catalogue from %1, updated %2").arg(host, when));
}

QString DictionariesDialog::shownPrefix() const
{
    const qsizetype total = m_manager.catalog().dictionaries.size();
    if (m_shownCount >= total) {
        return {};
    }
    return tr("Showing %1 of %2 dictionaries.").arg(m_shownCount).arg(total) + u' ';
}

QStringList DictionariesDialog::orderedInstalledIds() const
{
    const QStringList order = m_settings.dictionaryOrder();
    QStringList result;
    QSet<QString> seen;
    for (const QString& dictId : order) {
        const bool present = std::ranges::any_of(
            m_installed, [&](const services::DictionaryInfo& i) { return i.dictId == dictId; });
        if (present && !seen.contains(dictId)) {
            result << dictId;
            seen.insert(dictId);
        }
    }
    QList<services::DictionaryInfo> rest;
    for (const services::DictionaryInfo& info : m_installed) {
        if (!seen.contains(info.dictId)) {
            rest << info;
        }
    }
    std::ranges::sort(rest, [](const services::DictionaryInfo& a, const services::DictionaryInfo& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    for (const services::DictionaryInfo& info : rest) {
        result << info.dictId;
    }
    return result;
}

void DictionariesDialog::moveSelectedRow(int direction)
{
    QStringList order = orderedInstalledIds();
    const qsizetype from = order.indexOf(m_selectedDictId);
    if (from < 0) {
        return;
    }
    const qsizetype to = from + direction;
    if (to < 0 || to >= order.size()) {
        return;
    }
    order.move(from, to);
    m_settings.setDictionaryOrder(order);
    rebuildInstalledRows();
}

void DictionariesDialog::writeOrderFromList()
{
    QStringList order;
    for (int i = 0; i < m_installedList->count(); ++i) {
        order << m_installedList->item(i)->data(Qt::UserRole).toString();
    }
    m_settings.setDictionaryOrder(order);
    rebuildInstalledRows();
}

} // namespace omnidict::ui
