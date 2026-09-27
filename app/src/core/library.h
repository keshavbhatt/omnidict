#pragma once

#include "core/bundle.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace omnidict::core {

/// The dictionaries available to search: every usable bundle found under a
/// set of directories. Move-only, like the bundles it owns, and it belongs to
/// the thread that built it.
class Library
{
public:
    /// Finds bundles under each root, in either layout: `<root>/<dict_id>/dict.sqlite`
    /// (a pipeline build) or `<root>/<dict_id>/<version>/dict.sqlite` (installed,
    /// PLAN 5.3), keeping the highest version of each. The first root that has a
    /// dictionary wins. Unusable files are skipped and reported in problems().
    [[nodiscard]] static Library discover(const QStringList& roots);

    /// In display order: by name.
    [[nodiscard]] const std::vector<Bundle>& bundles() const { return m_bundles; }
    /// Nothing when the dictionary is not in the library.
    [[nodiscard]] const Bundle* find(const QString& dictId) const;
    [[nodiscard]] const QStringList& problems() const { return m_problems; }

private:
    std::vector<Bundle> m_bundles;
    QStringList m_problems;
};

} // namespace omnidict::core
