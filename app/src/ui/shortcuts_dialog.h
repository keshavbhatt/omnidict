#pragma once

#include <QDialog>
#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QLabel;
class QLayout;
class QLineEdit;
class QVBoxLayout;
QT_END_NAMESPACE

namespace omnidict::ui {

/// One row of the shortcuts sheet: the group it belongs to ("Search", "Entry",
/// "App", ...), what it does, and its keys as portable QKeySequence text.
/// Alternative chords for one action are joined with " / " or " or "
/// (shortcuts.html: "Ctrl+F / Ctrl+K", "F1 or Ctrl+/").
struct ShortcutRow
{
    QString group;
    QString label;
    QString keys;
};

/// The keyboard shortcuts sheet (shortcuts.html): a live filter, two columns
/// of groups, keys drawn as keycaps. Takes its rows rather than reading the
/// app's actions directly, so it stays a plain view: whoever wires up the
/// real shortcuts (main_window) decides what appears here.
class ShortcutsDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ShortcutsDialog)

public:
    explicit ShortcutsDialog(const QList<ShortcutRow>& rows, QWidget* parent = nullptr);
    ~ShortcutsDialog() override = default;

private:
    struct RowWidget
    {
        QWidget* row = nullptr;
        QString searchText; ///< label and keys, lower-cased, for the filter
    };
    struct GroupWidget
    {
        QLabel* heading = nullptr;
        QList<RowWidget> rows;
    };

    [[nodiscard]] QWidget* makeRow(const ShortcutRow& row);
    [[nodiscard]] QWidget* makeKeys(const QString& keys);
    [[nodiscard]] QLayout* buildColumns(const QList<ShortcutRow>& rows);
    [[nodiscard]] QWidget* buildFooter();
    void addGroup(QVBoxLayout* column, const QString& title, const QList<ShortcutRow>& rows);
    void filter(const QString& needle);

    QLineEdit* m_filter = nullptr;
    QList<GroupWidget> m_groups;
};

/// The rows of shortcuts.html, in the order the mock lists them. The
/// "Dictionaries" row is left out until that screen exists.
[[nodiscard]] QList<ShortcutRow> defaultShortcuts();

} // namespace omnidict::ui
