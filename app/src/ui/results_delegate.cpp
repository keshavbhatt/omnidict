#include "ui/results_delegate.h"

#include "models/results_model.h"

#include <QApplication>
#include <QPainter>

namespace omnidict::ui {

namespace {

constexpr int kPadding = 8;

using Kind = models::ResultsModel::RowKind;

Kind kindOf(const QModelIndex& index)
{
    return index.data(models::ResultsModel::KindRole).value<Kind>();
}

QFont headingFont(const QFont& base)
{
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * 0.85);
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);
    return font;
}

QFont headwordFont(const QFont& base)
{
    QFont font = base;
    font.setBold(true);
    return font;
}

} // namespace

ResultsDelegate::ResultsDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{}

void ResultsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QPalette& pal = opt.palette;
    const QRect area = opt.rect.adjusted(kPadding, 0, -kPadding, 0);
    painter->save();

    const Kind kind = kindOf(index);
    if (kind != Kind::Entry) {
        painter->setFont(headingFont(opt.font));
        painter->setPen(kind == Kind::Section ? pal.color(QPalette::Highlight)
                                              : pal.color(QPalette::PlaceholderText));
        painter->drawText(area.adjusted(0, kPadding / 2, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(opt.text, Qt::ElideRight, area.width()));
        painter->restore();
        return;
    }

    const QStyle* style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
    opt.text.clear();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    const QColor textColor = pal.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const QFont hwFont = headwordFont(opt.font);
    const int lineHeight = QFontMetrics(hwFont).height();

    painter->setFont(hwFont);
    painter->setPen(textColor);
    const QRect top(area.left(), area.top() + (kPadding / 2), area.width(), lineHeight);
    painter->drawText(top, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(),
                                                        Qt::ElideRight, top.width()));

    painter->setFont(opt.font);
    QColor previewColor = textColor;
    previewColor.setAlphaF(selected ? 0.85F : 0.6F);
    painter->setPen(previewColor);
    const QRect bottom(area.left(), top.bottom(), area.width(), opt.fontMetrics.height());
    painter->drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter,
                      opt.fontMetrics.elidedText(index.data(models::ResultsModel::PreviewRole).toString(),
                                                 Qt::ElideRight, bottom.width()));
    painter->restore();
}

QSize ResultsDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (kindOf(index) != Kind::Entry) {
        return {option.rect.width(), QFontMetrics(headingFont(option.font)).height() + (kPadding * 2)};
    }
    return {option.rect.width(),
            QFontMetrics(headwordFont(option.font)).height() + option.fontMetrics.height() + kPadding};
}

} // namespace omnidict::ui
