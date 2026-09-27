#pragma once

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPlainTextEdit;
QT_END_NAMESPACE

namespace omnidict::ui {

/// Report a bug (bug-report.html): a title, what happened, and the
/// diagnostics that will ride along, always as something the reporter sends
/// themselves. Nothing leaves the machine on its own: Copy diagnostics, Send
/// by email and Open GitHub issue each hand off to the clipboard, the mail
/// app or the browser.
class BugReportDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(BugReportDialog)

public:
    explicit BugReportDialog(QString diagnostics, QWidget* parent = nullptr);
    ~BugReportDialog() override = default;

private:
    [[nodiscard]] QWidget* buildHeader();
    [[nodiscard]] QWidget* buildForm();
    [[nodiscard]] QWidget* buildFooter();
    void copyDiagnostics();
    void openIssue();
    void openMail();

    QString m_diagnostics;
    QLineEdit* m_title = nullptr;
    QPlainTextEdit* m_description = nullptr;
};

} // namespace omnidict::ui
