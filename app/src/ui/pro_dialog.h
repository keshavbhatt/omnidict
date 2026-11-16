#pragma once

#include "services/pro_status.h"

#include <QColor>
#include <QDialog>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QFrame;
class QGridLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QWidget;
QT_END_NAMESPACE

namespace omnidict::ui {

/// Omnidict Pro (mocks/pro.html, ADR-024), Microsoft Store build only: the state in words
/// (trial, Free, Pro Forever, Pro until a date, ending soon, plan ended), what Free and Pro
/// include, the Account ID, Ktechpit Pass, and the links to the checkout and the portal.
class ProDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ProDialog)

public:
    /// What the status box says; public for the tests.
    enum class State
    {
        Trial,
        Free,
        Forever,
        Until,
        EndsSoon,
        Ended,
    };

    explicit ProDialog(services::ProStatus& pro, QWidget* parent = nullptr);
    ~ProDialog() override = default;

    [[nodiscard]] static State stateOf(const services::ProStatus& pro);
    /// A plan for a time counts as ending soon in its last days.
    static constexpr int kEndsSoonDays = 14;

private:
    struct TableRow
    {
        QString name;
        QString detail;
        QString free;
        QString pro;
    };
    struct Status
    {
        QString glyph;
        QColor color;
        QString title;
        QString text;
        bool meter = false;
    };

    QWidget* buildStatus();
    QWidget* buildTable();
    void addTableRow(QGridLayout* grid, int at, const TableRow& row);
    [[nodiscard]] Status statusFor(State state) const;
    void refreshChecked(bool paid);
    QWidget* buildAccount();
    QWidget* buildPass();
    QWidget* buildFooter();
    void refresh();

    services::ProStatus& m_pro;
    QFrame* m_status = nullptr;
    QLabel* m_statusIcon = nullptr;
    QLabel* m_statusTitle = nullptr;
    QLabel* m_statusText = nullptr;
    QProgressBar* m_meter = nullptr;
    QWidget* m_table = nullptr;
    QLabel* m_accountId = nullptr;
    QLabel* m_checked = nullptr;
    QWidget* m_pass = nullptr;
    QPushButton* m_choose = nullptr;
    QPushButton* m_close = nullptr;
    QPushButton* m_moreTime = nullptr;
};

} // namespace omnidict::ui
