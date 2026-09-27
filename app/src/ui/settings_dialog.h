#pragma once

#include <QDialog>
#include <QString>

namespace omnidict::core {
class Settings;
}

QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
class QVBoxLayout;
QT_END_NAMESPACE

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
    SettingsDialog(core::Settings& settings, const QString& dictionaryFolder, QWidget* parent = nullptr);
    ~SettingsDialog() override = default;

Q_SIGNALS:
    void clearHistoryRequested();

private:
    void setupAppearance(QVBoxLayout* body, QWidget* content);
    [[nodiscard]] QWidget* buildThemeSegment(QWidget* content);
    [[nodiscard]] QWidget* buildTextSizeRow(QWidget* content);
    void setupSearch(QVBoxLayout* body, QWidget* content);
    void setupHistory(QVBoxLayout* body, QWidget* content);
    void setupStorage(QVBoxLayout* body, QWidget* content, const QString& dictionaryFolder);
    void updateThemeButtons();
    void updateTextSizeLabel();

    core::Settings& m_settings;
    QLabel* m_textSizeLabel = nullptr;
    QToolButton* m_systemButton = nullptr;
    QToolButton* m_lightButton = nullptr;
    QToolButton* m_darkButton = nullptr;
};

} // namespace omnidict::ui
