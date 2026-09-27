#pragma once

#include <QHash>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace omnidict::services {
class DictionaryManager;
struct DownloadStatus;
} // namespace omnidict::services

namespace omnidict::ui {

/// What the window shows while no dictionary is installed
/// (mocks/main-no-dictionaries.html): up to three suggested dictionaries from
/// the catalogue with a Download button each, and a way to all of them.
class FirstRunPanel : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FirstRunPanel)

public:
    explicit FirstRunPanel(services::DictionaryManager& manager, QWidget* parent = nullptr);
    ~FirstRunPanel() override = default;

Q_SIGNALS:
    /// "Browse all dictionaries": the Dictionaries sheet, on its Available tab.
    void browseRequested();

private:
    void rebuild();
    void onDownloadChanged(const services::DownloadStatus& status);

    services::DictionaryManager& m_manager;
    QLabel* m_text = nullptr;
    QHBoxLayout* m_cards = nullptr;
    QPushButton* m_retry = nullptr;
    QHash<QString, QPushButton*> m_buttons; ///< Download button per dictionary id
};

} // namespace omnidict::ui
