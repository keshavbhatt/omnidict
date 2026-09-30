#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

class QAbstractScrollArea;
class QComboBox;
class QLabel;
class QWidget;

namespace omnidict::ui {

/// The brand tokens of DOCS/DESIGN.md section 1, per scheme: the one source of
/// every colour the app paints. DOCS/mocks/mock.css holds the same values; the
/// two must never disagree.
struct Tokens
{
    QColor accent;
    QColor accentStrong;
    QColor accentHover;
    QColor accentText;
    QColor accentSoft;
    QColor warm;
    QColor warmSoft;
    QColor bg;
    QColor bar;
    QColor panel;
    QColor elevated;
    QColor hover;
    QColor input;
    QColor border;
    QColor text;
    QColor muted;
    QColor link;
    QColor headword;
    QColor pattern;
    QColor example;
    QColor success;
    QColor warning;
    QColor danger;

    [[nodiscard]] static Tokens forScheme(bool dark);
    /// The tokens of the scheme on screen now (set by ThemeApplier).
    [[nodiscard]] static const Tokens& current();
    [[nodiscard]] static bool currentIsDark();
    static void setCurrentScheme(bool dark);
};

/// A Fusion palette built from the tokens, for everything the sheet leaves alone.
[[nodiscard]] QPalette paletteFor(const Tokens& tokens);

/// Makes `label` a group title (mock.css .group-title): the sheet colours and
/// sizes it; capitals and spacing are font settings a style sheet cannot make.
void makeSectionLabel(QLabel* label);

/// A window title ending in the app's name, "Settings - Omnidict". Without the
/// name at the end, Qt appends it itself with an em dash, which our text never uses.
[[nodiscard]] QString titleWithApp(const QString& title);

/// Key caps for a key sequence as the desktop writes it ("Ctrl+Alt+D"), one label
/// per key (the app sheet's `keycap` look), in a row.
[[nodiscard]] QWidget* makeKeyCaps(const QString& keys, QWidget* parent);

/// Gives a drop-down's list the app's item style (row height, padding, hover) and
/// the box's own font: Qt's default combo delegate ignores the sheet's `::item` rules
/// and paints in the smaller menu font.
void styleComboPopup(QComboBox* combo);

/// Turns off the Ctrl+wheel zoom Qt's text views have built in: it has no limits
/// and rescales only part of the text. Text size is Ctrl+Plus/Minus in the entry.
void disableWheelZoom(QAbstractScrollArea* view);

/// The application style sheet. Widgets opt into the looks with dynamic
/// properties: `primary` and `flat` buttons, `chip` tool buttons, `muted`,
/// `section` and `title` labels, `bar` and `card` frames.
[[nodiscard]] QString styleSheetFor(const Tokens& tokens);

} // namespace omnidict::ui
