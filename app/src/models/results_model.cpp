#include "models/results_model.h"

using namespace Qt::StringLiterals;

namespace omnidict::models {

ResultsModel::ResultsModel(QObject* parent)
    : QAbstractListModel(parent)
{}

void ResultsModel::setResults(const core::SearchResults& results)
{
    beginResetModel();
    m_rows.clear();
    appendGroups(results.headwords);
    if (!results.definitions.isEmpty()) {
        m_rows.append({.kind = RowKind::Section,
                       .text = tr("Also found in definitions"),
                       .dictId = {},
                       .entryId = 0,
                       .preview = {}});
        appendGroups(results.definitions);
    }
    endResetModel();
}

void ResultsModel::setSaved(const QList<core::SavedEntry>& recent, const QList<core::SavedEntry>& favorites)
{
    beginResetModel();
    m_rows.clear();
    const auto appendSection = [this](const QString& title, const QList<core::SavedEntry>& entries) {
        if (entries.isEmpty()) {
            return;
        }
        m_rows.append({.kind = RowKind::Section, .text = title, .dictId = {}, .entryId = 0, .preview = {}});
        for (const core::SavedEntry& entry : entries) {
            m_rows.append({.kind = RowKind::Entry,
                           .text = entry.headword,
                           .dictId = entry.dictId,
                           .entryId = 0,
                           .preview = entry.preview});
        }
    };
    appendSection(tr("Favorites"), favorites);
    appendSection(tr("Recent"), recent);
    endResetModel();
}

void ResultsModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void ResultsModel::appendGroups(const QList<core::ResultGroup>& groups)
{
    for (const core::ResultGroup& group : groups) {
        m_rows.append({.kind = RowKind::Dictionary,
                       .text = group.dictName,
                       .dictId = group.dictId,
                       .entryId = 0,
                       .preview = {}});
        for (const core::EntryPreview& entry : group.rows) {
            m_rows.append({.kind = RowKind::Entry,
                           .text = entry.headword,
                           .dictId = group.dictId,
                           .entryId = entry.id,
                           .preview = entry.preview});
        }
    }
}

int ResultsModel::firstEntryRow() const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).kind == RowKind::Entry) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ResultsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ResultsModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Row& row = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return row.text;
    case Qt::ToolTipRole:
        return row.kind == RowKind::Entry ? QVariant(row.preview) : QVariant();
    case KindRole:
        return QVariant::fromValue(row.kind);
    case DictIdRole:
        return row.dictId;
    case EntryIdRole:
        return row.entryId;
    case PreviewRole:
        return row.preview;
    default:
        return {};
    }
}

Qt::ItemFlags ResultsModel::flags(const QModelIndex& index) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return Qt::NoItemFlags;
    }
    return m_rows.at(index.row()).kind == RowKind::Entry ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                                         : Qt::NoItemFlags;
}

QHash<int, QByteArray> ResultsModel::roleNames() const
{
    QHash<int, QByteArray> names = QAbstractListModel::roleNames();
    names.insert(KindRole, "kind");
    names.insert(DictIdRole, "dictId");
    names.insert(EntryIdRole, "entryId");
    names.insert(PreviewRole, "preview");
    return names;
}

} // namespace omnidict::models
