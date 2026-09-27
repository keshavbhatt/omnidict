#include "ui/dictionaries_dialog.h"

#include "core/catalog.h"
#include "core/installer.h"
#include "core/settings.h"
#include "services/dictionary_manager.h"
#include "ui/icons.h"
#include "ui/style.h"
#include "ui/switch_button.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
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
    if (!isLast) {
        row->setStyleSheet(u"border-bottom: 1px solid %1;"_s.arg(Tokens::current().border.name()));
    }
}

/// The native language name for a BCP-47 code, capitalised; the code itself,
/// also capitalised, when Qt does not recognise it.
QString languageLabel(const QString& code)
{
    QString name = QLocale(code).nativeLanguageName();
    if (name.isEmpty()) {
        name = code;
    }
    if (!name.isEmpty()) {
        name[0] = name.at(0).toUpper();
    }
    return name;
}

QString pillStyleSheet(bool accent)
{
    const Tokens& t = Tokens::current();
    const QColor bg = accent ? t.accentSoft : t.hover;
    const QColor fg = accent ? t.accent : t.muted;
    return u"QLabel { background: %1; color: %2; border-radius: 11px; padding: 2px 10px; font-size: 12px; }"_s
        .arg(bg.name(), fg.name());
}

void makeSmall(QPushButton* button)
{
    button->setFixedHeight(28);
    button->setStyleSheet(u"QPushButton { padding: 0 10px; min-height: 0; }"_s);
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

void populateLanguageCombo(QComboBox* combo, const QSet<QString>& codes, const QString& anyLabel)
{
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem(anyLabel, QString());
    QStringList sorted(codes.begin(), codes.end());
    std::ranges::sort(sorted, [](const QString& a, const QString& b) {
        return languageLabel(a).localeAwareCompare(languageLabel(b)) < 0;
    });
    for (const QString& code : sorted) {
        if (!code.isEmpty()) {
            combo->addItem(languageLabel(code), code);
        }
    }
}

void restoreComboSelection(QComboBox* combo, const QString& code)
{
    const int index = combo->findData(code);
    combo->setCurrentIndex(index >= 0 ? index : 0);
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

    void setPercent(int percent) { m_percent = qBound(0, percent, 100); }

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
        if (m_percent > 0) {
            QPen arcPen(t.accent);
            arcPen.setWidthF(3.0);
            arcPen.setCapStyle(Qt::RoundCap);
            painter.setPen(arcPen);
            const int span = m_percent * 360 * 16 / 100;
            painter.drawArc(bounds, 90 * 16, -span);
        }
    }

private:
    int m_percent = 0;
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
    setWindowTitle(tr("Dictionaries"));
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

    connect(&m_manager, &services::DictionaryManager::catalogChanged, this, [this] {
        m_catalogFailure.clear();
        refreshLanguageFilters();
        rebuildInstalledRows();
        rebuildAvailableRows();
        updateAvailableFooter();
    });
    connect(&m_manager, &services::DictionaryManager::catalogFailed, this, [this](const QString& reason) {
        m_catalogFailure = reason;
        updateAvailableFooter();
    });
    connect(&m_manager, &services::DictionaryManager::downloadChanged, this,
            [this](const services::DownloadStatus& /*status*/) { rebuildAvailableRows(); });
    connect(&m_manager, &services::DictionaryManager::installed, this, [this](const QString& dictId) {
        m_installedOverride.insert(dictId, m_pendingInstallVersion.take(dictId));
        rebuildAvailableRows();
    });
    connect(&m_manager, &services::DictionaryManager::removed, this, [this](const QString& dictId) {
        m_installedOverride.remove(dictId);
        m_pendingInstallVersion.remove(dictId);
        rebuildAvailableRows();
    });

    refreshLanguageFilters();
    rebuildInstalledRows();
    rebuildAvailableRows();
    updateAvailableFooter();

    m_manager.refreshCatalog(false);
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
    m_tabs->setCurrentIndex(1);
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
    m_installedList->setSpacing(0);
    // The row widgets carry their own look (including the hairline between
    // rows); the list itself contributes no selection or hover colour of its
    // own; Alt+Up/Alt+Down still work through the model's current index.
    m_installedList->setStyleSheet(
        u"QListWidget { border: none; background: transparent; }"
        u"QListWidget::item { border: none; padding: 0; }"
        u"QListWidget::item:selected, QListWidget::item:hover { background: transparent; }"
        u"QListWidget:focus { outline: none; }"_s);
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
    pageLayout->addWidget(m_installedList, 1);

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
    auto* checkUpdates = new QPushButton(tr("Check for updates"), foot);
    connect(checkUpdates, &QPushButton::clicked, this, [this] { m_manager.refreshCatalog(true); });
    footLayout->addWidget(checkUpdates);
    auto* done = new QPushButton(tr("Done"), foot);
    done->setProperty("primary", true);
    done->setDefault(true);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    footLayout->addWidget(done);
    pageLayout->addWidget(foot);

    return page;
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
    connect(m_filterField, &QLineEdit::textChanged, this, [this] { rebuildAvailableRows(); });
    filterRow->addWidget(m_filterField, 1);
    m_fromCombo = new QComboBox(content);
    m_fromCombo->setObjectName(u"fromLanguage"_s);
    connect(m_fromCombo, &QComboBox::currentIndexChanged, this, [this] { rebuildAvailableRows(); });
    filterRow->addWidget(m_fromCombo);
    m_toCombo = new QComboBox(content);
    m_toCombo->setObjectName(u"toLanguage"_s);
    connect(m_toCombo, &QComboBox::currentIndexChanged, this, [this] { rebuildAvailableRows(); });
    filterRow->addWidget(m_toCombo);
    return filterRow;
}

QScrollArea* DictionariesDialog::buildAvailableScroll(QWidget* content)
{
    auto* scroll = new QScrollArea(content);
    scroll->setObjectName(u"availableScroll"_s);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // A vertical scrollbar that only appears "as needed" reserves its width a
    // layout pass late (visible in the offscreen QPA plugin used for
    // headless screenshots and tests): the content briefly gets the wider,
    // scrollbar-less width, and a row's rightmost control is then clipped by
    // its own row widget. Reserving the scrollbar's space unconditionally
    // avoids that resize race.
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
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
    m_availableFooter->setWordWrap(true);
    footLayout->addWidget(m_availableFooter, 1);
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
    auto* pill = new QLabel(tr("Update %1").arg(newer->version), row);
    pill->setStyleSheet(pillStyleSheet(true));
    layout->addWidget(pill);
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
    confirm.setWindowTitle(tr("Remove dictionary"));
    confirm.setModal(true);
    auto* layout = new QVBoxLayout(&confirm);
    layout->setContentsMargins(20, 20, 20, 0);
    layout->setSpacing(16);
    layout->addWidget(new QLabel(tr("Remove %1? This frees %2.").arg(name, humanSize(freed)), &confirm));
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

    confirm.resize(380, confirm.sizeHint().height());
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
    auto* desc = new QLabel(entry.publisher, row);
    desc->setProperty("muted", true);
    desc->setProperty("small", true);
    text->addWidget(desc);
    layout->addLayout(text, 1);

    const std::optional<services::DownloadStatus> status = findDownload(m_manager.downloads(), entry.dictId);
    if (status) {
        appendDownloadState(layout, row, entry, *status);
        return row;
    }

    const std::optional<QString> installedVersion =
        effectiveVersion(m_installed, m_installedOverride, entry.dictId);
    if (!installedVersion) {
        auto* size = new QLabel(humanSize(entry.sizeCompressed), row);
        size->setProperty("muted", true);
        size->setProperty("small", true);
        layout->addWidget(size);
        auto* download = new QPushButton(tr("Download"), row);
        download->setObjectName(u"downloadButton_"_s + entry.dictId);
        download->setProperty("primary", true);
        makeSmall(download);
        connect(download, &QPushButton::clicked, this, [this, entry] { startDownload(entry); });
        layout->addWidget(download);
    } else if (core::compareVersions(*installedVersion, entry.version) < 0) {
        auto* update = new QPushButton(tr("Update"), row);
        update->setObjectName(u"availableUpdateButton_"_s + entry.dictId);
        makeSmall(update);
        connect(update, &QPushButton::clicked, this, [this, entry] { startDownload(entry); });
        layout->addWidget(update);
    } else {
        auto* check = new QLabel(row);
        check->setPixmap(
            icons::pixmap(u"check"_s, Tokens::current().success, kIconSize, row->devicePixelRatioF()));
        layout->addWidget(check);
        auto* installedLabel = new QLabel(tr("Installed"), row);
        installedLabel->setProperty("muted", true);
        installedLabel->setProperty("small", true);
        layout->addWidget(installedLabel);
    }
    return row;
}

void DictionariesDialog::appendDownloadState(QHBoxLayout* layout, QWidget* row,
                                             const core::CatalogEntry& entry,
                                             const services::DownloadStatus& status)
{
    switch (status.state) {
    case services::DownloadStatus::Waiting: {
        auto* pill = new QLabel(tr("Waiting"), row);
        pill->setStyleSheet(pillStyleSheet(false));
        layout->addWidget(pill);
        layout->addWidget(makeCancelButton(row, entry.dictId));
        break;
    }
    case services::DownloadStatus::Downloading: {
        const int percent = status.total > 0 ? static_cast<int>(status.received * 100 / status.total) : 0;
        auto* ring = new ProgressRing(row);
        ring->setPercent(percent);
        layout->addWidget(ring);
        auto* label = new QLabel(
            tr("%1%, %2 of %3").arg(percent).arg(humanSize(status.received), humanSize(status.total)), row);
        label->setProperty("muted", true);
        label->setProperty("small", true);
        layout->addWidget(label);
        layout->addWidget(makeCancelButton(row, entry.dictId));
        break;
    }
    case services::DownloadStatus::Installing: {
        auto* ring = new ProgressRing(row);
        ring->setPercent(100);
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
        item->setSizeHint(widget->sizeHint());
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
}

void DictionariesDialog::rebuildAvailableRows()
{
    auto* listLayout = qobject_cast<QVBoxLayout*>(m_availableList->layout());
    while (listLayout->count() > 1) {
        const QLayoutItem* const item = listLayout->takeAt(0);
        delete item->widget();
        delete item;
    }

    const QString filterText = m_filterField->text().trimmed();
    const QString fromCode = m_fromCombo->currentData().toString();
    const QString toCode = m_toCombo->currentData().toString();

    QList<core::CatalogEntry> entries = m_manager.catalog().dictionaries;
    std::ranges::sort(entries, [](const core::CatalogEntry& a, const core::CatalogEntry& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    QList<core::CatalogEntry> shown;
    for (const core::CatalogEntry& entry : entries) {
        if (!filterText.isEmpty() && !entry.name.contains(filterText, Qt::CaseInsensitive)) {
            continue;
        }
        if (!fromCode.isEmpty() && entry.sourceLang != fromCode) {
            continue;
        }
        if (!toCode.isEmpty() && entry.targetLang != toCode) {
            continue;
        }
        shown << entry;
    }

    for (qsizetype i = 0; i < shown.size(); ++i) {
        listLayout->insertWidget(static_cast<int>(i), buildAvailableRow(shown.at(i), i == shown.size() - 1));
    }
}

void DictionariesDialog::refreshLanguageFilters()
{
    const QString previousFrom = m_fromCombo->currentData().toString();
    const QString previousTo = m_toCombo->currentData().toString();
    QSet<QString> sources;
    QSet<QString> targets;
    for (const core::CatalogEntry& entry : m_manager.catalog().dictionaries) {
        sources.insert(entry.sourceLang);
        targets.insert(entry.targetLang);
    }
    const QString anyLanguage = tr("Any language");
    populateLanguageCombo(m_fromCombo, sources, anyLanguage);
    populateLanguageCombo(m_toCombo, targets, anyLanguage);
    restoreComboSelection(m_fromCombo, previousFrom);
    restoreComboSelection(m_toCombo, previousTo);
}

void DictionariesDialog::updateInstalledFooter()
{
    qint64 total = 0;
    for (const services::DictionaryInfo& info : m_installed) {
        total += QFileInfo(info.path).size();
    }
    m_installedFooter->setText(tr("%1 dictionaries use %2").arg(m_installed.size()).arg(humanSize(total)));
}

void DictionariesDialog::updateAvailableFooter()
{
    if (!m_catalogFailure.isEmpty()) {
        m_availableFooter->setText(tr("Catalogue unavailable: %1").arg(m_catalogFailure));
        m_tryAgainButton->show();
        return;
    }
    m_tryAgainButton->hide();

    const QUrl url = m_manager.catalogUrl();
    QString host = url.host();
    if (url.port() > 0) {
        host += u':' + QString::number(url.port());
    }
    const QDateTime fetchedAt = m_manager.catalogFetchedAt();
    if (!fetchedAt.isValid()) {
        m_availableFooter->setText(tr("Catalogue from %1").arg(host));
        return;
    }
    const QDate fetchedDate = fetchedAt.toLocalTime().date();
    const QDate today = QDate::currentDate();
    QString when;
    if (fetchedDate == today) {
        when = tr("today");
    } else if (fetchedDate == today.addDays(-1)) {
        when = tr("yesterday");
    } else {
        when = QLocale().toString(fetchedDate, QLocale::ShortFormat);
    }
    m_availableFooter->setText(tr("Catalogue from %1, updated %2").arg(host, when));
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
