#pragma once

#include <QFrame>
#include <QString>

#include <functional>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace omnidict::ui {

/// A transient message over the bottom-left corner of a widget (mock.css
/// `.toast`), for actions that can be undone (settings.html note 4: clearing
/// history). Hides itself five seconds after it is shown. Never modal, never
/// blocks input to the widget behind it.
class Toast : public QFrame
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Toast)

public:
    ~Toast() override = default;

    /// Shows `text` over `parent`'s bottom-left corner, with an optional
    /// action button (for example "Undo") that calls `onAction` when clicked.
    /// Does nothing when `parent` is null.
    static void show(QWidget* parent, const QString& text, const QString& action = {},
                     std::function<void()> onAction = {});

private:
    explicit Toast(QWidget* parent);
    void reposition();

    QLabel* m_label = nullptr;
    QPushButton* m_action = nullptr;
};

} // namespace omnidict::ui
