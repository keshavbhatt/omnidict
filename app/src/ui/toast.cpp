#include "ui/toast.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kMargin = 16;
constexpr int kMillisecondsShown = 5000;
} // namespace

Toast::Toast(QWidget* parent)
    : QFrame(parent)
    , m_label(new QLabel(this))
    , m_action(new QPushButton(this))
{
    setObjectName(u"toast"_s);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 9, 14, 9);
    layout->setSpacing(10);

    m_label->setWordWrap(false);
    layout->addWidget(m_label);

    m_action->setProperty("linkButton", true);
    m_action->setVisible(false);
    layout->addWidget(m_action);
}

void Toast::reposition()
{
    auto* container = parentWidget();
    if (container == nullptr) {
        return;
    }
    adjustSize();
    move(kMargin, container->height() - height() - kMargin);
}

void Toast::show(QWidget* parent, const QString& text, const QString& action, std::function<void()> onAction)
{
    if (parent == nullptr) {
        return;
    }
    auto* toast = new Toast(parent);
    toast->m_label->setText(text);
    if (!action.isEmpty()) {
        toast->m_action->setText(action);
        toast->m_action->setVisible(true);
        connect(toast->m_action, &QPushButton::clicked, toast, [toast, onAction = std::move(onAction)] {
            if (onAction) {
                onAction();
            }
            toast->close();
        });
    }
    toast->reposition();
    toast->QWidget::show();
    toast->raise();
    QTimer::singleShot(kMillisecondsShown, toast, &QWidget::close);
}

} // namespace omnidict::ui
