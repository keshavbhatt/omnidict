#pragma once

#include <QStyledItemDelegate>

namespace omnidict::ui {

/// Paints the result list as mocks/main.html draws it: headings in small
/// capitals with an optional link at the right, entry rows as a bold headword
/// over a muted preview in a rounded row, with the favourite star.
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

Q_SIGNALS:
    /// The link of a heading (such as Recent's "Clear") was clicked.
    void actionActivated(const QModelIndex& index);

protected:
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
};

} // namespace omnidict::ui
