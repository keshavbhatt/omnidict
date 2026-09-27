#pragma once

#include "core/changelog.h"

#include <QDialog>
#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLayout;
class QTextBrowser;
QT_END_NAMESPACE

namespace omnidict::core {
class Settings;
}

namespace omnidict::ui {

/// Release notes from the bundled CHANGELOG.md (whats-new.html): a Version
/// picker over every released and unreleased section, the chosen one's
/// bullets in a card. Shown once per version after an update (see
/// shouldShowWhatsNew) and on demand from About; the caller stores the shown
/// version with core::Settings::setWhatsNewSeenVersion after showing it.
class WhatsNewDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WhatsNewDialog)

public:
    explicit WhatsNewDialog(QString runningVersion, QWidget* parent = nullptr);
    ~WhatsNewDialog() override = default;

    /// The bundled changelog (:/text/CHANGELOG.md).
    [[nodiscard]] static QString bundledChangelog();
    /// True once the app has been run before (a seen version is on record)
    /// and that version is not the one running now: an update just happened.
    /// False on the very first run, when there is nothing to compare against.
    [[nodiscard]] static bool shouldShowWhatsNew(const core::Settings& settings,
                                                 const QString& currentVersion);

    /// Shows `version`'s notes; ignored when the changelog has no such release.
    void showVersion(const QString& version);
    [[nodiscard]] QString shownVersion() const;

private:
    [[nodiscard]] QLayout* buildHeader();
    [[nodiscard]] QWidget* buildNotesCard();
    [[nodiscard]] QWidget* buildFooter();

    QString m_running;
    QString m_markdown;
    QList<core::ChangelogRelease> m_releases;
    QComboBox* m_picker = nullptr;
    QTextBrowser* m_notes = nullptr;
};

} // namespace omnidict::ui
