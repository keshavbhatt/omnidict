#include "ui/style.h"

#include "ui/icons.h"

#include <QHash>
#include <QLabel>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

Tokens darkTokens()
{
    return {
        .accent = QColor(0x62A0EA),
        .accentStrong = QColor(0x1C71D8),
        .accentHover = QColor(0x3584E4),
        .accentText = QColor(0xFFFFFF),
        .accentSoft = QColor(0x1B2B42),
        .warm = QColor(0xFFA348),
        .warmSoft = QColor(0x3A2616),
        .bg = QColor(0x16181B),
        .bar = QColor(0x1C1E22),
        .panel = QColor(0x1E2024),
        .elevated = QColor(0x272A30),
        .hover = QColor(0x2D3138),
        .input = QColor(0x131518),
        .border = QColor(0x2F333A),
        .text = QColor(0xEEF0F3),
        .muted = QColor(0x9AA2AD),
        .link = QColor(0x78AEED),
        .headword = QColor(0x78AEED),
        .pattern = QColor(0x5CCB84),
        .example = QColor(0xA3ABB6),
        .success = QColor(0x5CCB84),
        .warning = QColor(0xF5B14C),
        .danger = QColor(0xFF6B6B),
    };
}

Tokens lightTokens()
{
    return {
        .accent = QColor(0x1C71D8),
        .accentStrong = QColor(0x1C71D8),
        .accentHover = QColor(0x1A5FB4),
        .accentText = QColor(0xFFFFFF),
        .accentSoft = QColor(0xE4EEFB),
        .warm = QColor(0xC64600),
        .warmSoft = QColor(0xFFF1E3),
        .bg = QColor(0xF4F5F7),
        .bar = QColor(0xFFFFFF),
        .panel = QColor(0xFFFFFF),
        .elevated = QColor(0xFFFFFF),
        .hover = QColor(0xECEEF2),
        .input = QColor(0xFFFFFF),
        .border = QColor(0xDEE1E7),
        .text = QColor(0x16181D),
        .muted = QColor(0x5C6573),
        .link = QColor(0x1C71D8),
        .headword = QColor(0x1A5FB4),
        .pattern = QColor(0x1B7F38),
        .example = QColor(0x5C6573),
        .success = QColor(0x1B7F38),
        .warning = QColor(0xB45309),
        .danger = QColor(0xC01C28),
    };
}

/// Whether the scheme on screen is dark; written only by ThemeApplier on the GUI thread.
bool& darkScheme()
{
    static bool dark = false;
    return dark;
}

// The sheet, with {{token}} placeholders filled from the tokens.
const char* const kSheet = R"qss(
QMainWindow, QDialog { background: {{bg}}; }
QWidget { color: {{text}}; }
QLabel, QCheckBox, QRadioButton { background: transparent; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QToolTip {
    background: {{elevated}}; color: {{text}};
    border: 1px solid {{border}}; border-radius: 6px; padding: 5px 8px;
}
QLabel[muted="true"] { color: {{muted}}; }
QLabel[small="true"] { font-size: 12px; }
QLabel[title="true"] { font-size: 18px; font-weight: 600; }
QLabel[heading="true"] { font-size: 16px; font-weight: 600; }
QLabel[section="true"] { font-size: 11px; font-weight: 700; color: {{accent}}; letter-spacing: 1px; }
QLabel[link="true"] { color: {{link}}; }

QFrame[bar="true"] { background: {{bar}}; border: none; border-bottom: 1px solid {{border}}; }
QFrame[card="true"] { background: {{panel}}; border: 1px solid {{border}}; border-radius: 12px; }
QFrame[separator="true"] { background: {{border}}; max-height: 1px; min-height: 1px; border: none; }
QFrame[sheet="true"] { background: {{panel}}; }
QWidget[dictionaryRow="true"] { border-bottom: 1px solid {{border}}; }
QFrame[sheetFoot="true"] { background: {{panel}}; border: none; border-top: 1px solid {{border}}; }

QLineEdit, QPlainTextEdit, QTextEdit[input="true"] {
    background: {{input}}; color: {{text}};
    border: 1px solid {{border}}; border-radius: 8px; padding: 0 10px;
    selection-background-color: {{accentStrong}}; selection-color: {{accentText}};
}
QLineEdit { min-height: 34px; }
QPlainTextEdit, QTextEdit[input="true"] { padding: 6px 8px; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit[input="true"]:focus { border: 1px solid {{accent}}; }
QLineEdit:disabled { color: {{muted}}; }
QLineEdit#search { min-height: 34px; padding-left: 4px; }

QPushButton {
    background: {{panel}}; color: {{text}};
    border: 1px solid {{border}}; border-radius: 8px;
    min-height: 32px; padding: 0 14px;
}
QPushButton:hover { background: {{hover}}; }
QPushButton:pressed { background: {{accentSoft}}; border-color: {{accent}}; }
QPushButton:focus { border-color: {{accent}}; }
QPushButton:disabled { color: {{muted}}; }
QPushButton[primary="true"] { background: {{accentStrong}}; border-color: {{accentStrong}}; color: {{accentText}}; }
QPushButton[primary="true"]:hover { background: {{accentHover}}; border-color: {{accentHover}}; }
QPushButton[danger="true"] { color: {{danger}}; }
QPushButton[flat="true"] { background: transparent; border-color: transparent; }
QPushButton[flat="true"]:hover { background: {{hover}}; }
QPushButton[linkButton="true"] { background: transparent; border: none; color: {{link}}; padding: 0 2px; min-height: 0; }

QToolButton {
    background: transparent; border: 1px solid transparent; border-radius: 8px;
    min-width: 32px; min-height: 32px; padding: 0 2px;
}
QToolButton:hover { background: {{hover}}; }
QToolButton:pressed, QToolButton:checked { background: {{accentSoft}}; }
QToolButton:focus { border-color: {{accent}}; }
QToolButton::menu-indicator { image: none; width: 0; }
/* Segmented choice (mock.css .seg): one frame, hairlines between the options. */
QFrame[segmented="true"] { background: {{input}}; border: 1px solid {{border}}; border-radius: 8px; }
QFrame[segmented="true"] QToolButton {
    background: transparent; color: {{text}}; border: none; border-right: 1px solid {{border}};
    border-radius: 0; min-width: 0; min-height: 0; padding: 6px 14px;
}
QFrame[segmented="true"] QToolButton[segmentEdge="first"] { border-top-left-radius: 7px; border-bottom-left-radius: 7px; }
QFrame[segmented="true"] QToolButton[segmentEdge="last"] {
    border-right: none; border-top-right-radius: 7px; border-bottom-right-radius: 7px;
}
QFrame[segmented="true"] QToolButton:hover { background: {{hover}}; }
QFrame[segmented="true"] QToolButton:checked { background: {{accentSoft}}; color: {{accent}}; font-weight: 600; }
QFrame[segmented="true"] QToolButton:focus { color: {{accent}}; }
QPushButton[compact="true"] { padding: 0 4px; min-width: 32px; }

QToolButton[chip="true"] {
    background: {{input}}; border: 1px solid {{border}};
    min-height: 34px; padding: 0 10px 0 10px;
}
QToolButton[chip="true"]:hover { background: {{hover}}; }
QToolButton[favorite="true"]:checked { background: transparent; }

QMenu {
    background: {{elevated}}; color: {{text}};
    border: 1px solid {{border}}; border-radius: 8px; padding: 6px;
}
QMenu::item { padding: 7px 28px 7px 10px; border-radius: 6px; }
QMenu::item:selected { background: {{hover}}; }
QMenu::item:disabled { color: {{muted}}; }
QMenu::icon { padding-left: 10px; }
QMenu::separator { height: 1px; background: {{border}}; margin: 5px 4px; }
QMenu::indicator { width: 0; }

QListView#results { background: {{panel}}; border: none; border-right: 1px solid {{border}}; outline: 0; }
QTextBrowser#entry { background: {{bg}}; border: none; }
QSplitter::handle { background: {{border}}; }

QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent; color: {{muted}};
    padding: 8px 12px; border: none; border-bottom: 2px solid transparent;
}
QTabBar::tab:selected { color: {{text}}; border-bottom: 2px solid {{accent}}; }
QTabBar::tab:hover { color: {{text}}; }
QTabWidget::pane { border: none; border-top: 1px solid {{border}}; }

/* Drop-downs look like the header's filter chip (mocks/dictionaries-available.html). */
QComboBox {
    background: {{input}}; color: {{text}};
    border: 1px solid {{border}}; border-radius: 8px; min-height: 32px; padding: 0 30px 0 12px;
}
QComboBox:hover { background: {{hover}}; }
QComboBox:focus { border-color: {{accent}}; }
QComboBox::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 26px; border: none; background: transparent;
}
QComboBox::down-arrow { image: url({{chevron}}); width: 14px; height: 14px; }
QComboBox::down-arrow:on { top: 1px; }
QComboBox QAbstractItemView {
    background: {{elevated}}; color: {{text}}; border: 1px solid {{border}}; border-radius: 8px;
    padding: 4px; outline: 0;
    selection-background-color: {{hover}}; selection-color: {{text}};
}

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: {{border}}; border-radius: 3px; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: {{muted}}; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: {{border}}; border-radius: 3px; min-width: 32px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QFrame#toast { background: {{elevated}}; border: 1px solid {{border}}; border-radius: 8px; }
)qss";

} // namespace

Tokens Tokens::forScheme(bool dark)
{
    return dark ? darkTokens() : lightTokens();
}

const Tokens& Tokens::current()
{
    static const Tokens kDark = darkTokens();
    static const Tokens kLight = lightTokens();
    return darkScheme() ? kDark : kLight;
}

bool Tokens::currentIsDark()
{
    return darkScheme();
}

void Tokens::setCurrentScheme(bool dark)
{
    darkScheme() = dark;
}

QPalette paletteFor(const Tokens& t)
{
    QPalette palette;
    palette.setColor(QPalette::Window, t.bg);
    palette.setColor(QPalette::WindowText, t.text);
    palette.setColor(QPalette::Base, t.input);
    palette.setColor(QPalette::AlternateBase, t.panel);
    palette.setColor(QPalette::Text, t.text);
    palette.setColor(QPalette::PlaceholderText, t.muted);
    palette.setColor(QPalette::Button, t.panel);
    palette.setColor(QPalette::ButtonText, t.text);
    palette.setColor(QPalette::ToolTipBase, t.elevated);
    palette.setColor(QPalette::ToolTipText, t.text);
    palette.setColor(QPalette::Highlight, t.accentStrong);
    palette.setColor(QPalette::HighlightedText, t.accentText);
    palette.setColor(QPalette::Link, t.link);
    palette.setColor(QPalette::LinkVisited, t.link);
    palette.setColor(QPalette::Mid, t.border);
    palette.setColor(QPalette::Midlight, t.hover);
    palette.setColor(QPalette::Dark, t.border);
    palette.setColor(QPalette::Light, t.elevated);
    palette.setColor(QPalette::Accent, t.accent);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, t.muted);
    }
    return palette;
}

void makeSectionLabel(QLabel* label)
{
    constexpr qreal kSpacingPercent = 106.0; // letter-spacing .06em
    label->setProperty("section", true);
    QFont font = label->font();
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::PercentageSpacing, kSpacingPercent);
    label->setFont(font);
}

QString styleSheetFor(const Tokens& t)
{
    const QHash<QString, QColor> values = {
        {u"accent"_s, t.accent},
        {u"accentStrong"_s, t.accentStrong},
        {u"accentHover"_s, t.accentHover},
        {u"accentText"_s, t.accentText},
        {u"accentSoft"_s, t.accentSoft},
        {u"bg"_s, t.bg},
        {u"bar"_s, t.bar},
        {u"panel"_s, t.panel},
        {u"elevated"_s, t.elevated},
        {u"hover"_s, t.hover},
        {u"input"_s, t.input},
        {u"border"_s, t.border},
        {u"text"_s, t.text},
        {u"muted"_s, t.muted},
        {u"link"_s, t.link},
        {u"danger"_s, t.danger},
    };
    QString sheet = QString::fromUtf8(kSheet);
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        sheet.replace(u"{{"_s + it.key() + u"}}"_s, it.value().name());
    }
    sheet.replace(u"{{chevron}}"_s, icons::tintedFile(u"down"_s, t.muted));
    return sheet;
}

} // namespace omnidict::ui
