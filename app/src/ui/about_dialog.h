#pragma once

#include "services/lookup_service.h"

#include <QDialog>

namespace omnidict::ui {

/// Who made Omnidict and every dictionary in it. Dictionary attribution and
/// licences are shown for each open dictionary as its bundle states them
/// (PLAN D10): the open licences of the content require it.
class AboutDialog : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AboutDialog)

public:
    explicit AboutDialog(const QList<services::DictionaryInfo>& dictionaries, QWidget* parent = nullptr);
    ~AboutDialog() override = default;

    /// The dialog's text as HTML. Pure, tested.
    [[nodiscard]] static QString aboutHtml(const QList<services::DictionaryInfo>& dictionaries);
};

} // namespace omnidict::ui
