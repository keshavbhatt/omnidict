#pragma once

#include <QAbstractButton>
#include <QSize>

QT_BEGIN_NAMESPACE
class QPaintEvent;
QT_END_NAMESPACE

namespace omnidict::ui {

/// The on/off switch of DOCS/mocks/mock.css (`.switch`): a 36x20 pill, the
/// accent colour when checked, a soft focus halo when it holds keyboard
/// focus. Used instead of QCheckBox for every immediate-effect toggle
/// (settings.html). Its accessible name is its text.
class SwitchButton : public QAbstractButton
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SwitchButton)

public:
    explicit SwitchButton(QWidget* parent = nullptr);
    /// `text` is never painted (the switch has no label of its own); it
    /// becomes the accessible name and the tooltip, for the row's own label
    /// to describe what the switch does.
    explicit SwitchButton(const QString& text, QWidget* parent = nullptr);
    ~SwitchButton() override = default;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void init();
};

} // namespace omnidict::ui
