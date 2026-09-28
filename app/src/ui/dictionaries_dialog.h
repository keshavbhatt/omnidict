#pragma once

#include "core/catalog.h"
#include "services/lookup_service.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace omnidict::core {
class Settings;
struct CatalogEntry;
} // namespace omnidict::core

namespace omnidict::services {
class DictionaryManager;
struct DownloadStatus;
} // namespace omnidict::services

QT_BEGIN_NAMESPACE
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QToolButton;
class QVBoxLayout;
QT_END_NAMESPACE

namespace omnidict::ui {

class SwitchButton;

/// Dictionaries (DOCS/mocks/dictionaries.html, dictionaries-available.html):
/// reorder, switch off, update and remove installed dictionaries, and browse
/// the catalogue to download more. Holds no dictionary data of its own:
/// everything shown comes from `manager`, `settings` and the `installed` list
/// the caller hands in; `setInstalled` is how the caller refreshes it after an
/// install or a removal reopens the library.
class DictionariesDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DictionariesDialog)

public:
    DictionariesDialog(services::DictionaryManager& manager, core::Settings& settings,
                       QList<services::DictionaryInfo> installed, QString dictionariesRoot,
                       QWidget* parent = nullptr);
    ~DictionariesDialog() override = default;

    /// Refreshes the Installed tab; the caller calls this after an install or
    /// removal reopens the library.
    void setInstalled(const QList<services::DictionaryInfo>& installed);
    /// Selects the Available tab.
    void showAvailable();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QWidget* buildInstalledTab();
    QWidget* buildAvailableTab();
    QHBoxLayout* buildAvailableFilterRow(QWidget* content);
    QScrollArea* buildAvailableScroll(QWidget* content);
    QFrame* buildAvailableFoot(QWidget* page);
    QWidget* buildInstalledRow(const services::DictionaryInfo& info, bool isLast);
    static QVBoxLayout* buildInstalledText(QWidget* row, const services::DictionaryInfo& info);
    void appendUpdateControls(QHBoxLayout* layout, QWidget* row, const services::DictionaryInfo& info);
    SwitchButton* buildDictSwitch(QWidget* row, const services::DictionaryInfo& info);
    QWidget* buildAvailableRow(const core::CatalogEntry& entry, bool isLast);
    void appendDownloadState(QHBoxLayout* layout, QWidget* row, const core::CatalogEntry& entry,
                             const services::DownloadStatus& status);
    QToolButton* makeCancelButton(QWidget* parent, const QString& dictId);
    void attachRowMenu(QToolButton* button, const services::DictionaryInfo& info);
    void confirmRemove(const QString& dictId, const QString& name, qint64 freed);
    void startDownload(const core::CatalogEntry& entry);
    void rebuildInstalledRows();
    void rebuildAvailableRows();
    /// Download, install and removal signals: each updates only the rows it concerns.
    void connectDownloads();
    /// Refreshes one row's right-hand side (download state) without touching the rest.
    void updateAvailableRow(const QString& dictId);
    void fillAvailableState(QWidget* row, const core::CatalogEntry& entry);
    [[nodiscard]] static QString progressText(int percent, qint64 received, qint64 total);
    void refreshLanguageFilters();
    void updateInstalledFooter();
    void updateAvailableFooter();
    /// Reads the catalogue now (Refresh, F5).
    void refreshCatalog();
    [[nodiscard]] QStringList orderedInstalledIds() const;
    void moveSelectedRow(int direction);
    void writeOrderFromList();

    services::DictionaryManager& m_manager;
    core::Settings& m_settings;
    QList<services::DictionaryInfo> m_installed;
    QString m_dictionariesRoot;
    QString m_catalogFailure; ///< the reason from the last catalogFailed(); empty when it last succeeded
    QString m_selectedDictId; ///< survives a rebuild, for repeated Alt+Up / Alt+Down

    QTabWidget* m_tabs = nullptr;
    QListWidget* m_installedList = nullptr;
    QLabel* m_installedFooter = nullptr;

    QLineEdit* m_filterField = nullptr;
    QComboBox* m_fromCombo = nullptr;
    QComboBox* m_toCombo = nullptr;
    QWidget* m_availableList = nullptr; ///< a QVBoxLayout of rows, plus a trailing stretch
    QScrollArea* m_availableScroll = nullptr;
    QHash<QString, QWidget*> m_availableStates;            ///< each row's right-hand side, by dictionary id
    QHash<QString, core::CatalogEntry> m_availableEntries; ///< the entry each shown row stands for
    QSet<QString> m_rowsWithDownload;                      ///< rows last drawn with a download state
    QLabel* m_availableFooter = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_tryAgainButton = nullptr;

    /// dictId -> version, set when install() is called and consumed when
    /// installed() fires, so the Available tab can show that dictionary as
    /// installed before the caller has reopened the library and called
    /// setInstalled().
    QHash<QString, QString> m_pendingInstallVersion;
    QHash<QString, QString> m_installedOverride;
};

} // namespace omnidict::ui
