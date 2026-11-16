#pragma once

#include "services/lookup_service.h"

#include <QStringList>

namespace omnidict::ui::free_limit {

/// How many dictionaries Omnidict Free searches at a time (ADR-024, mocks/pro.html note 3).
inline constexpr int kDictionaries = 1;

/// The installed dictionaries that are switched on, in the user's order.
[[nodiscard]] QStringList switchedOn(const QList<services::DictionaryInfo>& installed,
                                     const QStringList& order, const QStringList& disabled);

/// The switched-off list that leaves only the first kDictionaries of `installed` switched on, in
/// the user's order; the others stay installed (mocks/pro-prompts.html note 3).
[[nodiscard]] QStringList disabledForFree(const QList<services::DictionaryInfo>& installed,
                                          const QStringList& order, const QStringList& disabled);

} // namespace omnidict::ui::free_limit
