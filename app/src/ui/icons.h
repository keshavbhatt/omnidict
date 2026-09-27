#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

// Themed monochrome glyphs (DOCS/DESIGN.md): the Lucide SVGs under
// :/icons/ui are drawn in #000000 and tinted at runtime, so one file serves
// both schemes and every state. Rendered pixmaps are cached per name, colour,
// size and device pixel ratio.
namespace omnidict::ui::icons {

/// A QIcon whose normal and disabled modes are tinted `color` and `disabledColor`
/// (by default `color` at a third of its opacity).
[[nodiscard]] QIcon themed(const QString& name, const QColor& color, const QColor& disabledColor = {});
/// One pixmap at `size` logical pixels for the given device pixel ratio.
[[nodiscard]] QPixmap pixmap(const QString& name, const QColor& color, int size,
                             qreal devicePixelRatio = 1.0);
/// Path of a tinted copy of the glyph on disk, for style-sheet url() references.
[[nodiscard]] QString tintedFile(const QString& name, const QColor& color);
/// The app icon (hicolor set).
[[nodiscard]] QIcon brand();

} // namespace omnidict::ui::icons
