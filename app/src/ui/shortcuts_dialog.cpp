#include "ui/shortcuts_dialog.h"

#include "ui/style.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <utility>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

/// "Ctrl+F" -> "Cmd+F" on macOS; anything QKeySequence cannot parse (there is
/// none among the rows we ship) is shown verbatim.
QString nativeChord(const QString& chord)
{
    const QKeySequence sequence(chord, QKeySequence::PortableText);
    if (sequence.isEmpty()) {
        return chord;
    }
    return sequence.toString(QKeySequence::NativeText);
}

/// Splits "Ctrl+F / Ctrl+K" or "F1 or Ctrl+/" into chords and the separator
/// word shown between them ("/" or "or").
QList<std::pair<QString, QString>> splitChords(const QString& keys)
{
    static const QRegularExpression kSeparator(u"\\s+(or|/)\\s+"_s);
    QList<std::pair<QString, QString>> chords;
    qsizetype pos = 0;
    for (auto it = kSeparator.globalMatch(keys); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        chords.append({keys.mid(pos, match.capturedStart() - pos), match.captured(1)});
        pos = match.capturedEnd();
    }
    chords.append({keys.mid(pos), QString()});
    return chords;
}

} // namespace

ShortcutsDialog::ShortcutsDialog(const QList<ShortcutRow>& rows, QWidget* parent)
    : QDialog(parent)
    , m_filter(new QLineEdit(this))
{
    setWindowTitle(tr("Keyboard shortcuts"));
    setModal(true);
    setMinimumWidth(640);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* title = new QLabel(tr("Keyboard shortcuts"), this);
    title->setProperty("title", true);
    auto* titleRow = new QVBoxLayout;
    titleRow->setContentsMargins(20, 16, 20, 8);
    titleRow->addWidget(title);
    root->addLayout(titleRow);

    auto* body = new QVBoxLayout;
    body->setContentsMargins(20, 0, 20, 16);
    body->setSpacing(16);

    m_filter->setObjectName(u"shortcutsFilter"_s);
    m_filter->setPlaceholderText(tr("Filter shortcuts"));
    m_filter->setAccessibleName(tr("Filter shortcuts"));
    connect(m_filter, &QLineEdit::textChanged, this, &ShortcutsDialog::filter);
    body->addWidget(m_filter);
    body->addLayout(buildColumns(rows));
    root->addLayout(body);
    root->addStretch(1);
    root->addWidget(buildFooter());
}

QLayout* ShortcutsDialog::buildColumns(const QList<ShortcutRow>& rows)
{
    // Groups alternate columns in the order they first appear: with the
    // three groups this ships with (Search, Entry, App) that puts Search and
    // App on the left and Entry on the right, matching shortcuts.html.
    QStringList order;
    for (const ShortcutRow& row : rows) {
        if (!order.contains(row.group)) {
            order << row.group;
        }
    }
    auto* columns = new QHBoxLayout;
    columns->setSpacing(24);
    auto* left = new QVBoxLayout;
    auto* right = new QVBoxLayout;
    left->setSpacing(0);
    right->setSpacing(0);
    columns->addLayout(left, 1);
    columns->addLayout(right, 1);
    for (qsizetype i = 0; i < order.size(); ++i) {
        QList<ShortcutRow> groupRows;
        for (const ShortcutRow& row : rows) {
            if (row.group == order.at(i)) {
                groupRows << row;
            }
        }
        addGroup((i % 2 == 0) ? left : right, order.at(i), groupRows);
    }
    // Rows keep their natural height; spare room goes below them.
    left->setAlignment(Qt::AlignTop);
    right->setAlignment(Qt::AlignTop);
    left->addStretch(1);
    right->addStretch(1);
    return columns;
}

QWidget* ShortcutsDialog::buildFooter()
{
    auto* foot = new QFrame(this);
    foot->setProperty("sheetFoot", true);
    auto* footLayout = new QHBoxLayout(foot);
    footLayout->setContentsMargins(20, 12, 20, 12);
    footLayout->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    close->setProperty("primary", true);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    footLayout->addWidget(close);
    return foot;
}

void ShortcutsDialog::addGroup(QVBoxLayout* column, const QString& title, const QList<ShortcutRow>& rows)
{
    if (rows.isEmpty()) {
        return;
    }
    auto* heading = new QLabel(title, this);
    heading->setProperty("section", true);
    heading->setContentsMargins(0, column->count() == 0 ? 0 : 16, 0, 6);
    column->addWidget(heading);
    GroupWidget group{.heading = heading, .rows = {}};
    for (const ShortcutRow& row : rows) {
        QWidget* rowWidget = makeRow(row);
        column->addWidget(rowWidget);
        const QString searchText = QString(row.label + u' ' + row.keys).toCaseFolded();
        group.rows.append(RowWidget{.row = rowWidget, .searchText = searchText});
    }
    m_groups.append(group);
}

QWidget* ShortcutsDialog::makeRow(const ShortcutRow& row)
{
    auto* widget = new QWidget(this);
    widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 5, 0, 5);
    layout->setSpacing(12);
    auto* label = new QLabel(row.label, widget);
    layout->addWidget(label, 1);
    layout->addWidget(makeKeys(row.keys), 0, Qt::AlignRight);
    return widget;
}

QWidget* ShortcutsDialog::makeKeys(const QString& keys)
{
    const Tokens& t = Tokens::current();
    const QString keycapStyle =
        u"QLabel{background:%1;color:%2;border:1px solid %3;"
        "border-bottom:2px solid %3;border-radius:5px;padding:1px 6px;"
        "font-weight:600;font-size:12px;}"_s.arg(t.panel.name(), t.text.name(), t.border.name());
    auto* box = new QWidget(this);
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    const QList<std::pair<QString, QString>> chords = splitChords(keys);
    for (const auto& [chord, separator] : chords) {
        auto* cap = new QLabel(nativeChord(chord), box);
        cap->setStyleSheet(keycapStyle);
        cap->setAlignment(Qt::AlignCenter);
        cap->setMinimumWidth(24);
        layout->addWidget(cap);
        if (!separator.isEmpty()) {
            auto* sep = new QLabel(separator, box);
            sep->setProperty("muted", true);
            sep->setProperty("small", true);
            layout->addWidget(sep);
        }
    }
    return box;
}

void ShortcutsDialog::filter(const QString& needle)
{
    const QString term = needle.toCaseFolded();
    for (const GroupWidget& group : std::as_const(m_groups)) {
        bool anyVisible = false;
        for (const RowWidget& row : group.rows) {
            const bool visible = term.isEmpty() || row.searchText.contains(term);
            row.row->setVisible(visible);
            anyVisible = anyVisible || visible;
        }
        group.heading->setVisible(anyVisible);
    }
}

QList<ShortcutRow> defaultShortcuts()
{
    return {
        {.group = u"Search"_s, .label = ShortcutsDialog::tr("Focus search"), .keys = u"Ctrl+F / Ctrl+K"_s},
        {.group = u"Search"_s, .label = ShortcutsDialog::tr("Clear search"), .keys = u"Esc"_s},
        {.group = u"Search"_s, .label = ShortcutsDialog::tr("Move to results"), .keys = u"Down"_s},
        {.group = u"Search"_s, .label = ShortcutsDialog::tr("Open entry"), .keys = u"Enter"_s},
        {.group = u"Search"_s, .label = ShortcutsDialog::tr("Pick dictionary"), .keys = u"Ctrl+L"_s},
        {.group = u"Entry"_s, .label = ShortcutsDialog::tr("Back"), .keys = u"Alt+Left"_s},
        {.group = u"Entry"_s, .label = ShortcutsDialog::tr("Forward"), .keys = u"Alt+Right"_s},
        {.group = u"Entry"_s, .label = ShortcutsDialog::tr("Add to favourites"), .keys = u"Ctrl+D"_s},
        {.group = u"Entry"_s, .label = ShortcutsDialog::tr("Copy entry"), .keys = u"Ctrl+Shift+C"_s},
        {.group = u"Entry"_s, .label = ShortcutsDialog::tr("Larger text"), .keys = u"Ctrl++"_s},
        {.group = u"Entry"_s, .label = ShortcutsDialog::tr("Smaller text"), .keys = u"Ctrl+-"_s},
        {.group = u"Entry"_s, .label = ShortcutsDialog::tr("Reset text size"), .keys = u"Ctrl+0"_s},
        {.group = u"App"_s, .label = ShortcutsDialog::tr("Dictionaries"), .keys = u"Ctrl+Shift+D"_s},
        {.group = u"App"_s, .label = ShortcutsDialog::tr("Settings"), .keys = u"Ctrl+,"_s},
        {.group = u"App"_s, .label = ShortcutsDialog::tr("Main menu"), .keys = u"F10"_s},
        {.group = u"App"_s, .label = ShortcutsDialog::tr("Keyboard shortcuts"), .keys = u"F1 or Ctrl+/"_s},
        {.group = u"App"_s, .label = ShortcutsDialog::tr("Quit"), .keys = u"Ctrl+Q"_s},
    };
}

} // namespace omnidict::ui
