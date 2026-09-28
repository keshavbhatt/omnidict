#include "ui/results_delegate.h"

#include "models/results_model.h"
#include "ui/icons.h"
#include "ui/style.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

using Model = models::ResultsModel;
using Kind = Model::RowKind;

// Geometry from mocks/mock.css (.results).
constexpr int kRowMarginX = 8; // around an entry row
constexpr int kRowPadX = 10;   // inside it
constexpr int kRowPadY = 7;
constexpr int kRowGap = 2; // between entry rows
constexpr int kHeadingPadX = 16;
constexpr int kHeadingTop = 14;
constexpr int kHeadingBottom = 4;
constexpr int kNotePadY = 8;
constexpr int kRadius = 8;
constexpr int kStarSize = 15;
constexpr int kSideGap = 8;
constexpr qreal kHeadingLetterSpacing = 6.0; // percent
constexpr int kHeadingPixelSize = 11;
constexpr int kPreviewPixelSize = 13;
constexpr int kSidePixelSize = 11;

Kind kindOf(const QModelIndex& index)
{
    return index.data(Model::KindRole).value<Kind>();
}

QFont headingFont(const QFont& base)
{
    QFont font = base;
    font.setPixelSize(kHeadingPixelSize);
    font.setWeight(QFont::Bold);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::PercentageSpacing, 100.0 + kHeadingLetterSpacing);
    return font;
}

QFont detailFont(const QFont& base)
{
    QFont font = base;
    font.setPixelSize(kHeadingPixelSize);
    font.setWeight(QFont::Medium);
    return font;
}

QFont headwordFont(const QFont& base)
{
    QFont font = base;
    font.setWeight(QFont::DemiBold);
    return font;
}

QFont pixelFont(const QFont& base, int pixels)
{
    QFont font = base;
    font.setPixelSize(pixels);
    return font;
}

/// Where a heading's link is drawn, for painting and for clicks.
QRect actionRect(const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const QString text = index.data(Model::ActionTextRole).toString();
    if (text.isEmpty()) {
        return {};
    }
    const QFontMetrics metrics(detailFont(option.font));
    const int width = metrics.horizontalAdvance(text);
    const QRect area = option.rect.adjusted(kHeadingPadX, kHeadingTop, -kHeadingPadX, -kHeadingBottom);
    return {area.right() - width, area.top(), width, area.height()};
}

void paintHeading(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const Tokens& t = Tokens::current();
    const QRect area = option.rect.adjusted(kHeadingPadX, kHeadingTop, -kHeadingPadX, -kHeadingBottom);
    const QRect link = actionRect(option, index);
    const int right = link.isNull() ? area.right() : link.left() - kSideGap;

    const QFont font = headingFont(option.font);
    painter->setFont(font);
    painter->setPen(t.muted);
    const QString text = index.data(Qt::DisplayRole).toString();
    const QFontMetrics metrics(font);
    const QString shown = metrics.elidedText(text, Qt::ElideRight, right - area.left());
    painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter, shown);

    const QString detail = index.data(Model::DetailRole).toString();
    const int detailLeft = area.left() + metrics.horizontalAdvance(shown) + (kSideGap / 2);
    if (!detail.isEmpty() && detailLeft < right) {
        painter->setFont(detailFont(option.font));
        painter->drawText(QRect(detailLeft, area.top(), right - detailLeft, area.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(detail, Qt::ElideRight, right - detailLeft));
    }
    if (!link.isNull()) {
        painter->setFont(detailFont(option.font));
        painter->setPen(t.link);
        painter->drawText(link, Qt::AlignRight | Qt::AlignVCenter,
                          index.data(Model::ActionTextRole).toString());
    }
}

/// The right-hand side of an entry row: the star, then a short muted label
/// left of it. Returns what is left of `content` for the headword and preview.
QRect paintTrailing(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
                    QRect content)
{
    const Tokens& t = Tokens::current();
    if (index.data(Model::FavoriteRole).toBool()) {
        const qreal dpr = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap star = icons::pixmap(u"star-filled"_s, t.warm, kStarSize, dpr);
        const QPoint at(content.right() - kStarSize + 1, content.center().y() - (kStarSize / 2));
        painter->drawPixmap(at, star);
        content.setRight(at.x() - kSideGap);
    }
    const QString side = index.data(Model::SideTextRole).toString();
    if (!side.isEmpty()) {
        const QFont font = pixelFont(option.font, kSidePixelSize);
        const QFontMetrics metrics(font);
        // Measured in fractional pixels and rounded up: an integer advance can be a
        // hair short of the drawn text, and eliding at it cut the name (mocks show it whole).
        const int needed = qCeil(QFontMetricsF(font).horizontalAdvance(side)) + 1;
        const int width = std::min(needed, content.width() / 2);
        painter->setFont(font);
        painter->setPen(t.muted);
        painter->drawText(QRect(content.right() - width, content.top(), width, content.height()),
                          Qt::AlignRight | Qt::AlignVCenter, metrics.elidedText(side, Qt::ElideRight, width));
        content.setRight(content.right() - width - kSideGap);
    }
    return content;
}

void paintEntry(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const Tokens& t = Tokens::current();
    const QRect row = option.rect.adjusted(kRowMarginX, kRowGap / 2, -kRowMarginX, -kRowGap / 2);
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    if (selected || hovered) {
        QPainterPath path;
        path.addRoundedRect(QRectF(row), kRadius, kRadius);
        painter->fillPath(path, selected ? t.accentSoft : t.hover);
    }
    const QRect content =
        paintTrailing(painter, option, index, row.adjusted(kRowPadX, kRowPadY, -kRowPadX, -kRowPadY));

    const QFont hwFont = headwordFont(option.font);
    const QFontMetrics hwMetrics(hwFont);
    const QRect top(content.left(), content.top(), content.width(), hwMetrics.height());
    painter->setFont(hwFont);
    painter->setPen(selected ? t.accent : t.text);
    const QString shown =
        hwMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, top.width());
    painter->drawText(top, Qt::AlignLeft | Qt::AlignVCenter, shown);
    // A form found in place of its headword: "ran from run" (mocks/main.html .via).
    const QString via = index.data(Model::DetailRole).toString();
    const int viaLeft = top.left() + hwMetrics.horizontalAdvance(shown) + (kSideGap / 2);
    if (!via.isEmpty() && viaLeft < top.right()) {
        const QFont viaFont = pixelFont(option.font, kSidePixelSize + 1);
        painter->setFont(viaFont);
        painter->setPen(t.muted);
        const QRect viaRect(viaLeft, top.top(), top.right() - viaLeft, top.height());
        painter->drawText(viaRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(viaFont).elidedText(via, Qt::ElideRight, viaRect.width()));
    }

    const QFont previewFont = pixelFont(option.font, kPreviewPixelSize);
    const QFontMetrics previewMetrics(previewFont);
    const QRect bottom(content.left(), top.bottom() + 1, content.width(), previewMetrics.height());
    painter->setFont(previewFont);
    painter->setPen(t.muted);
    painter->drawText(
        bottom, Qt::AlignLeft | Qt::AlignVCenter,
        previewMetrics.elidedText(index.data(Model::PreviewRole).toString(), Qt::ElideRight, bottom.width()));
}

void paintNote(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    painter->setFont(pixelFont(option.font, kPreviewPixelSize - 1));
    painter->setPen(Tokens::current().muted);
    const QRect area = option.rect.adjusted(kHeadingPadX, 0, -kHeadingPadX, 0);
    painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(),
                                                        Qt::ElideRight, area.width()));
}

} // namespace

ResultsDelegate::ResultsDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{}

void ResultsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    switch (kindOf(index)) {
    case Kind::Section:
    case Kind::Dictionary:
        paintHeading(painter, option, index);
        break;
    case Kind::Note:
        paintNote(painter, option, index);
        break;
    case Kind::Entry:
        paintEntry(painter, option, index);
        break;
    }
    painter->restore();
}

QSize ResultsDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    switch (kindOf(index)) {
    case Kind::Section:
    case Kind::Dictionary:
        return {option.rect.width(),
                QFontMetrics(headingFont(option.font)).height() + kHeadingTop + kHeadingBottom};
    case Kind::Note:
        return {option.rect.width(),
                QFontMetrics(pixelFont(option.font, kPreviewPixelSize)).height() + (2 * kNotePadY)};
    case Kind::Entry:
        break;
    }
    const int height = QFontMetrics(headwordFont(option.font)).height() + 1 +
                       QFontMetrics(pixelFont(option.font, kPreviewPixelSize)).height() + (2 * kRowPadY) +
                       kRowGap;
    return {option.rect.width(), height};
}

bool ResultsDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                  const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease && kindOf(index) != Kind::Entry) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton &&
            actionRect(option, index).contains(mouse->position().toPoint())) {
            Q_EMIT actionActivated(index);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace omnidict::ui
