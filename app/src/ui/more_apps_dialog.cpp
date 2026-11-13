#include "ui/more_apps_dialog.h"

#include "ui/style.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <MoreAppsView.h>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kColumns = 3;
constexpr int kWidth = 820;
constexpr int kHeight = 640;
constexpr int kMinimumWidth = 560;
} // namespace

MoreAppsDialog::MoreAppsDialog(const QString& place, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(u"moreAppsDialog"_s);
    setWindowTitle(titleWithApp(tr("More apps from Ktechpit")));
    // Over the main window only: the quick lookup popup stays usable meanwhile.
    setWindowModality(Qt::WindowModal);
    setMinimumWidth(kMinimumWidth);
    resize(kWidth, kHeight);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QVBoxLayout;
    body->setContentsMargins(20, 20, 20, 16);
    body->setSpacing(10);
    auto* title = new QLabel(tr("More apps from Ktechpit"), this);
    title->setProperty("title", true);
    body->addWidget(title);
    auto* view = new ktechpit::MoreAppsView(place, kColumns, this);
    view->setIntro(tr("Other apps by the developer of Omnidict. Get opens your software centre."));
    body->addWidget(view, 1);
    root->addLayout(body, 1);

    auto* foot = new QFrame(this);
    foot->setProperty("sheetFoot", true);
    auto* footLayout = new QHBoxLayout(foot);
    footLayout->setContentsMargins(20, 12, 20, 12);
    auto* status = new QLabel(view->status(), foot);
    status->setObjectName(u"moreAppsStatus"_s);
    status->setProperty("muted", true);
    status->setProperty("small", true);
    connect(view, &ktechpit::MoreAppsView::statusChanged, status, &QLabel::setText);
    footLayout->addWidget(status);
    footLayout->addStretch(1);
    auto* close = new QPushButton(tr("Close"), foot);
    close->setProperty("primary", true);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footLayout->addWidget(close);
    root->addWidget(foot);
}

} // namespace omnidict::ui
