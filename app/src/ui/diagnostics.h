#pragma once

#include "services/lookup_service.h"

#include <QList>
#include <QString>

namespace omnidict::ui {

/// A plain, copyable block of app, Qt, OS and dictionary versions for a bug
/// report (bug-report.html, about.html): never history, favourites or a path
/// under the home directory.
[[nodiscard]] QString diagnosticsText(const QList<services::DictionaryInfo>& dictionaries);

} // namespace omnidict::ui
