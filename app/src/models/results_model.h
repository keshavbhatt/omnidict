#pragma once

#include "core/search_engine.h"

#include <QAbstractListModel>
#include <QList>

namespace omnidict::models {

/// Search results as one list: a header row per dictionary with its entries
/// under it, and after them an "Also found in definitions" section grouped the
/// same way (PLAN 7.2). Header rows cannot be selected.
class ResultsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ResultsModel)

public:
    enum class RowKind
    {
        Section,    ///< a heading over several dictionaries' results
        Dictionary, ///< the name of the dictionary whose entries follow
        Entry,
    };
    Q_ENUM(RowKind)

    enum Role
    {
        KindRole = Qt::UserRole + 1,
        DictIdRole,
        EntryIdRole,
        PreviewRole,
    };

    explicit ResultsModel(QObject* parent = nullptr);
    ~ResultsModel() override = default;

    void setResults(const core::SearchResults& results);
    void clear();

    /// The row of the first entry, or -1.
    [[nodiscard]] int firstEntryRow() const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    struct Row
    {
        RowKind kind = RowKind::Entry;
        QString text; ///< heading, dictionary name or headword
        QString dictId;
        qint64 entryId = 0;
        QString preview;
    };

    void appendGroups(const QList<core::ResultGroup>& groups);

    QList<Row> m_rows;
};

} // namespace omnidict::models
