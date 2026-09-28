#include "models/results_model.h"

using namespace Qt::StringLiterals;

namespace omnidict::models {

namespace {

/// "Spanish-English (Wiktionary)" as the name and the muted part after it.
std::pair<QString, QString> splitName(const QString& name)
{
    const qsizetype open = name.lastIndexOf(u" ("_s);
    if (open > 0 && name.endsWith(u')')) {
        return {name.left(open), name.mid(open + 1)};
    }
    return {name, QString()};
}

} // namespace

ResultsModel::ResultsModel(QObject* parent)
    : QAbstractListModel(parent)
{}

void ResultsModel::setDictionaryNames(const QHash<QString, QString>& names)
{
    m_names = names;
}

void ResultsModel::setResults(const core::SearchResults& results, const QStringList& favorites,
                              int dictionaryCount)
{
    m_results = results;
    m_favorites = QSet<QString>(favorites.cbegin(), favorites.cend());
    m_dictionaryCount = dictionaryCount;
    m_definitionsExpanded = false;
    rebuild();
}

void ResultsModel::setFavorite(const QString& dictId, const QString& headword, bool favorite)
{
    const QString key = core::favoriteKey(dictId, headword);
    if (favorite) {
        m_favorites.insert(key);
    } else {
        m_favorites.remove(key);
    }
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (row.kind == RowKind::Entry && row.dictId == dictId &&
            (row.headword.isEmpty() ? row.text : row.headword) == headword && row.favorite != favorite) {
            row.favorite = favorite;
            const QModelIndex changed = index(static_cast<int>(i));
            Q_EMIT dataChanged(changed, changed, {FavoriteRole});
        }
    }
}

void ResultsModel::showAllDefinitions()
{
    if (!m_definitionsExpanded) {
        m_definitionsExpanded = true;
        rebuild();
    }
}

void ResultsModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    m_noHeadwordMatches = m_results.headwords.isEmpty();
    if (m_noHeadwordMatches) {
        m_rows.append({.kind = RowKind::Note,
                       .text = m_dictionaryCount == 1
                                   ? tr("No headword matches in 1 dictionary")
                                   : tr("No headword matches in %1 dictionaries").arg(m_dictionaryCount)});
        appendSuggestions();
    } else {
        appendGroups(m_results.headwords);
    }
    appendDefinitions();
    endResetModel();
}

void ResultsModel::setSaved(const QList<core::SavedEntry>& recent, const QList<core::SavedEntry>& favorites)
{
    beginResetModel();
    m_rows.clear();
    m_results = {};
    m_noHeadwordMatches = false;
    const auto appendSection = [this](const QString& title, const QList<core::SavedEntry>& entries,
                                      bool starred, Action action, const QString& actionText) {
        if (entries.isEmpty()) {
            return;
        }
        m_rows.append({.kind = RowKind::Section, .text = title, .action = action, .actionText = actionText});
        for (const core::SavedEntry& entry : entries) {
            m_rows.append({.kind = RowKind::Entry,
                           .text = entry.headword,
                           .dictId = entry.dictId,
                           .preview = entry.preview,
                           .favorite = starred,
                           .sideText = splitName(nameOf(entry.dictId)).first});
        }
    };
    appendSection(tr("Favourites"), favorites, true, Action::None, {});
    appendSection(tr("Recent"), recent, false, Action::ClearHistory, tr("Clear"));
    endResetModel();
}

void ResultsModel::clear()
{
    beginResetModel();
    m_rows.clear();
    m_results = {};
    m_noHeadwordMatches = false;
    endResetModel();
}

ResultsModel::Row ResultsModel::entryRow(const QString& dictId, const core::EntryPreview& entry,
                                         const QString& preview) const
{
    Row row{.kind = RowKind::Entry,
            .text = entry.headword,
            .dictId = dictId,
            .entryId = entry.id,
            .preview = preview,
            .favorite = isFavorite(dictId, entry.headword)};
    if (!entry.matchedForm.isEmpty()) {
        // mocks/main.html: the form that matched, then "from <headword>".
        row.text = entry.matchedForm;
        row.headword = entry.headword;
        row.detail = tr("from %1").arg(entry.headword);
    }
    return row;
}

void ResultsModel::appendGroups(const QList<core::ResultGroup>& groups)
{
    for (const core::ResultGroup& group : groups) {
        const auto [name, detail] = splitName(group.dictName);
        m_rows.append({.kind = RowKind::Dictionary, .text = name, .dictId = group.dictId, .detail = detail});
        for (const core::EntryPreview& entry : group.rows) {
            m_rows.append(entryRow(group.dictId, entry, entry.preview));
        }
    }
}

void ResultsModel::appendSuggestions()
{
    if (m_results.suggestions.isEmpty()) {
        return;
    }
    m_rows.append({.kind = RowKind::Section, .text = tr("Did you mean")});
    for (const core::SuggestedWord& word : m_results.suggestions) {
        m_rows.append({.kind = RowKind::Entry,
                       .text = word.entry.headword,
                       .dictId = word.dictId,
                       .entryId = word.entry.id,
                       .preview = word.dictName + u": "_s + word.entry.preview,
                       .favorite = isFavorite(word.dictId, word.entry.headword)});
    }
}

void ResultsModel::appendDefinitions()
{
    qsizetype total = 0;
    for (const core::ResultGroup& group : std::as_const(m_results.definitions)) {
        total += group.rows.size();
    }
    if (total == 0) {
        if (m_noHeadwordMatches) {
            m_rows.append({.kind = RowKind::Section, .text = tr("Also found in definitions")});
            m_rows.append({.kind = RowKind::Note, .text = tr("None")});
        }
        return;
    }
    const bool collapse = !m_definitionsExpanded && total > kCollapsedDefinitions;
    m_rows.append({.kind = RowKind::Section,
                   .text = tr("Also found in definitions"),
                   .action = collapse ? Action::ShowAllDefinitions : Action::None,
                   .actionText = collapse ? tr("Show %1").arg(total) : QString()});
    qsizetype shown = 0;
    for (const core::ResultGroup& group : std::as_const(m_results.definitions)) {
        const QString name = splitName(group.dictName).first;
        for (const core::EntryPreview& entry : group.rows) {
            if (collapse && shown == kCollapsedDefinitions) {
                return;
            }
            m_rows.append(entryRow(group.dictId, entry, name + u": "_s + entry.preview));
            ++shown;
        }
    }
}

QString ResultsModel::nameOf(const QString& dictId) const
{
    return m_names.value(dictId, dictId);
}

bool ResultsModel::isFavorite(const QString& dictId, const QString& headword) const
{
    return m_favorites.contains(core::favoriteKey(dictId, headword));
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
    case Qt::AccessibleTextRole:
        return row.kind == RowKind::Entry ? QVariant(row.text + u", "_s + row.preview) : QVariant(row.text);
    case KindRole:
        return QVariant::fromValue(row.kind);
    case DictIdRole:
        return row.dictId;
    case EntryIdRole:
        return row.entryId;
    case PreviewRole:
        return row.preview;
    case FavoriteRole:
        return row.favorite;
    case SideTextRole:
        return row.sideText;
    case ActionRole:
        return QVariant::fromValue(row.action);
    case ActionTextRole:
        return row.actionText;
    case HeadwordRole:
        return row.headword.isEmpty() ? row.text : row.headword;
    case DetailRole:
        return row.detail;
    default:
        return {};
    }
}

Qt::ItemFlags ResultsModel::flags(const QModelIndex& index) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return Qt::NoItemFlags;
    }
    const Row& row = m_rows.at(index.row());
    if (row.kind == RowKind::Entry) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }
    // Headings with a link take clicks, but are never selected.
    return row.action != Action::None ? Qt::ItemIsEnabled : Qt::NoItemFlags;
}

QHash<int, QByteArray> ResultsModel::roleNames() const
{
    QHash<int, QByteArray> names = QAbstractListModel::roleNames();
    names.insert(KindRole, "kind");
    names.insert(DictIdRole, "dictId");
    names.insert(EntryIdRole, "entryId");
    names.insert(PreviewRole, "preview");
    names.insert(FavoriteRole, "favorite");
    names.insert(SideTextRole, "sideText");
    names.insert(ActionRole, "action");
    names.insert(ActionTextRole, "actionText");
    names.insert(DetailRole, "detail");
    names.insert(HeadwordRole, "headword");
    return names;
}

} // namespace omnidict::models
