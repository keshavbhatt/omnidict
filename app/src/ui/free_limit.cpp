#include "ui/free_limit.h"

#include <algorithm>

namespace omnidict::ui::free_limit {

QStringList switchedOn(const QList<services::DictionaryInfo>& installed, const QStringList& order,
                       const QStringList& disabled)
{
    QStringList on;
    for (const services::DictionaryInfo& info : installed) {
        if (!disabled.contains(info.dictId)) {
            on.append(info.dictId);
        }
    }
    std::ranges::stable_sort(on, {}, [&order](const QString& dictId) {
        const qsizetype at = order.indexOf(dictId);
        return at < 0 ? order.size() : at;
    });
    return on;
}

QStringList disabledForFree(const QList<services::DictionaryInfo>& installed, const QStringList& order,
                            const QStringList& disabled)
{
    QStringList result = disabled;
    const QStringList on = switchedOn(installed, order, disabled);
    for (qsizetype i = kDictionaries; i < on.size(); ++i) {
        result.append(on.at(i));
    }
    return result;
}

} // namespace omnidict::ui::free_limit
