#pragma once

#include "core/search_engine.h"
#include "core/user_data.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>

namespace omnidict::models {

/// The left-hand list as one flat model (mocks/main*.html): a heading per
/// dictionary with its entries under it; "Did you mean" when nothing matched;
/// "Also found in definitions", collapsed to a few rows; or, for an empty
/// search, favourites and recent entries. Only entry rows can be selected.
class ResultsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ResultsModel)

public:
    enum class RowKind
    {
        Section,    ///< a heading over several dictionaries' rows, maybe with an action
        Dictionary, ///< the name of the dictionary whose entries follow
        Entry,
        Note, ///< a muted line of text, such as "No headword matches"
    };
    Q_ENUM(RowKind)

    /// What a heading's action link does.
    enum class Action
    {
        None,
        ClearHistory,
        ShowAllDefinitions,
    };
    Q_ENUM(Action)

    enum Role
    {
        KindRole = Qt::UserRole + 1,
        DictIdRole,
        EntryIdRole,
        PreviewRole,
        FavoriteRole,   ///< bool: the entry is a favourite (the star)
        SideTextRole,   ///< short muted text at the right, such as the dictionary name
        ActionRole,     ///< Action of a heading
        ActionTextRole, ///< the heading's link text
        DetailRole,     ///< muted text after a heading, such as "(Wiktionary)"
    };

    /// Rows of "Also found in definitions" shown before "Show all".
    static constexpr int kCollapsedDefinitions = 2;

    explicit ResultsModel(QObject* parent = nullptr);
    ~ResultsModel() override = default;

    /// Names shown for saved entries and suggestions, by dictionary id.
    void setDictionaryNames(const QHash<QString, QString>& names);

    /// `favorites` as core::favoriteKey(); `dictionaryCount` for the note
    /// shown when nothing matched.
    void setResults(const core::SearchResults& results, const QStringList& favorites, int dictionaryCount);
    /// What an empty search shows: favourites, then recent entries. Their rows
    /// carry no entry id; they are opened by dictionary and headword.
    void setSaved(const QList<core::SavedEntry>& recent, const QList<core::SavedEntry>& favorites);
    /// Updates the star of every row showing this entry.
    void setFavorite(const QString& dictId, const QString& headword, bool favorite);
    /// Lists every "Also found in definitions" row.
    void showAllDefinitions();
    void clear();

    /// The row of the first entry, or -1.
    [[nodiscard]] int firstEntryRow() const;
    /// True when the list holds search results and none of them is a headword match.
    [[nodiscard]] bool hasNoHeadwordMatches() const { return m_noHeadwordMatches; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    struct Row
    {
        RowKind kind = RowKind::Entry;
        QString text{}; ///< heading, dictionary name or headword
        QString dictId{};
        qint64 entryId = 0;
        QString preview{};
        bool favorite = false;
        QString sideText{};
        Action action = Action::None;
        QString actionText{};
        QString detail{};
    };

    void rebuild();
    void appendGroups(const QList<core::ResultGroup>& groups);
    void appendDefinitions();
    void appendSuggestions();
    [[nodiscard]] QString nameOf(const QString& dictId) const;
    [[nodiscard]] bool isFavorite(const QString& dictId, const QString& headword) const;

    QList<Row> m_rows;
    QHash<QString, QString> m_names;
    core::SearchResults m_results;
    QSet<QString> m_favorites;
    int m_dictionaryCount = 0;
    bool m_definitionsExpanded = false;
    bool m_noHeadwordMatches = false;
};

} // namespace omnidict::models
