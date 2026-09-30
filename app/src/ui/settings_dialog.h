#pragma once

#include <QDialog>
#include <QString>

namespace omnidict::core {
class Settings;
}

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
QT_END_NAMESPACE

namespace omnidict::services {
class GlobalShortcuts;
} // namespace omnidict::services

namespace omnidict::ui {

/// Settings (settings.html): every control here applies through core::Settings
/// immediately, there is no Apply or OK to remember. "Clear history" only
/// asks; it never clears anything itself, so the caller can show the actual
/// history and favourites and decide what "history" means.
class SettingsDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SettingsDialog)

public:
    /// `shortcuts`: the Quick Lookup global shortcut, whose state the Quick lookup
    /// section shows; none (tests) shows the custom-shortcut route.
    SettingsDialog(core::Settings& settings, const QString& dictionaryFolder, QWidget* parent = nullptr,
                   services::GlobalShortcuts* shortcuts = nullptr);

    /// The command a desktop's custom shortcut should run to open Quick Lookup, for the
    /// way this copy was installed (Flatpak, snap, AppImage or a plain install).
    [[nodiscard]] static QString popupCommand();
    ~SettingsDialog() override = default;

Q_SIGNALS:
    void clearHistoryRequested();

private:
    void setupAppearance(QVBoxLayout* body, QWidget* content);
    [[nodiscard]] QWidget* buildThemeSegment(QWidget* content);
    [[nodiscard]] QWidget* buildTextSizeRow(QWidget* content);
    void setupSearch(QVBoxLayout* body, QWidget* content);
    void setupQuickLookup(QVBoxLayout* body, QWidget* content, services::GlobalShortcuts* shortcuts);
    [[nodiscard]] QWidget* buildQuickLookupShortcutRow(QWidget* content);
    [[nodiscard]] QWidget* buildQuickLookupFallbackRow(QWidget* content);
    void refreshQuickLookup();
    void setupHistory(QVBoxLayout* body, QWidget* content);
    void setupStorage(QVBoxLayout* body, QWidget* content, const QString& dictionaryFolder);
    void updateThemeButtons();
    void updateTextSizeLabel();

    core::Settings& m_settings;
    QLabel* m_textSizeLabel = nullptr;
    QToolButton* m_systemButton = nullptr;
    QToolButton* m_lightButton = nullptr;
    QToolButton* m_darkButton = nullptr;
    services::GlobalShortcuts* m_shortcuts = nullptr;
    QWidget* m_quickKeys = nullptr;
    QPushButton* m_quickChange = nullptr;
    QWidget* m_quickShortcutRow = nullptr;
    QWidget* m_quickFallbackRow = nullptr;
};

} // namespace omnidict::ui
