#pragma once

#include "services/lookup_service.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLayout;
QT_END_NAMESPACE

namespace omnidict::ui {

/// Who made Omnidict and every dictionary in it (about.html): the app
/// identity, links, each dictionary's attribution and licence (the content
/// licences require this), the open-source notices and a copyable
/// diagnostics panel. Never opens What's new or the bug report sheet itself;
/// it asks its parent to, through whatsNewRequested and bugReportRequested,
/// so it stays independent of how those sheets are wired up.
class AboutDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AboutDialog)

public:
    explicit AboutDialog(const QList<services::DictionaryInfo>& dictionaries, QWidget* parent = nullptr);
    ~AboutDialog() override = default;

    /// The dialog's dictionary credits as HTML. Pure, tested; kept for the
    /// text form of the attribution even though the dialog itself now shows
    /// it as cards.
    [[nodiscard]] static QString aboutHtml(const QList<services::DictionaryInfo>& dictionaries);

Q_SIGNALS:
    void whatsNewRequested();
    void bugReportRequested();

private:
    [[nodiscard]] QLayout* buildHero();
    [[nodiscard]] QLayout* buildLinks();
    [[nodiscard]] QWidget* buildTabs();
    [[nodiscard]] QWidget* buildDictionariesTab(QWidget* tabParent);
    [[nodiscard]] static QWidget* buildOpenSourceTab(QWidget* tabParent);
    [[nodiscard]] QWidget* buildDiagnosticsTab(QWidget* tabParent);
    [[nodiscard]] QWidget* buildFooter();
    void copyDiagnostics();

    QList<services::DictionaryInfo> m_dictionaries;
};

} // namespace omnidict::ui
