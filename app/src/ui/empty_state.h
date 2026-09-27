#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace omnidict::ui {

/// A centred message in place of an entry (mocks/main-empty.html,
/// main-no-results.html): a glyph on a soft tile, a title, a line of text,
/// optional muted tips and an optional button.
class EmptyState : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(EmptyState)

public:
    explicit EmptyState(QWidget* parent = nullptr);
    ~EmptyState() override = default;

    /// An empty `buttonText` hides the button.
    void setContent(const QString& glyph, const QString& title, const QString& text,
                    const QStringList& tips = {}, const QString& buttonText = {});

Q_SIGNALS:
    void buttonClicked();

protected:
    void changeEvent(QEvent* event) override;

private:
    void paintGlyph();

    QString m_glyph;
    QLabel* m_tile = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_text = nullptr;
    QVBoxLayout* m_tips = nullptr;
    QPushButton* m_button = nullptr;
};

} // namespace omnidict::ui
