#pragma once

#include <QStyledItemDelegate>

namespace omnidict::ui {

/// Paints result rows: a headword over a one-line preview, and the section and
/// dictionary headings between them.
class ResultsDelegate : public QStyledItemDelegate
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ResultsDelegate)

public:
    explicit ResultsDelegate(QObject* parent = nullptr);
    ~ResultsDelegate() override = default;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace omnidict::ui
