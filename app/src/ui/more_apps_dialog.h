#pragma once

#include <QDialog>
#include <QString>

namespace omnidict::ui {

/// More apps from Ktechpit (more-apps.html): the KtechpitApps module's full list of the
/// developer's other apps, with its search and Refresh, in an Omnidict sheet with a footer
/// that says how many apps there are or what Refresh did. Opened from the main menu and from
/// See all in About; never on its own.
class MoreAppsDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MoreAppsDialog)

public:
    /// `place` is where it was opened from ("menu", "about"), for the links' ?ref=.
    explicit MoreAppsDialog(const QString& place, QWidget* parent = nullptr);
    ~MoreAppsDialog() override = default;
};

} // namespace omnidict::ui
