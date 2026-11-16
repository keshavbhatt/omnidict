#pragma once

#include "services/pro_status.h"

#include <QDialog>

namespace omnidict::ui {

/// Search more than one dictionary with Pro (mocks/pro-prompts.html note 1): asked on Free when
/// a second dictionary is switched on. Use ... instead swaps the two in one step (the free way
/// out); Choose a plan opens the checkout; What Pro adds asks for the Pro sheet.
class DictionaryProPrompt : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DictionaryProPrompt)

public:
    enum class Choice
    {
        Cancel,
        Swap,
        Plan,
        WhatProAdds,
    };

    DictionaryProPrompt(services::ProStatus& pro, const QString& onName, const QString& newName,
                        QWidget* parent = nullptr);
    ~DictionaryProPrompt() override = default;

    [[nodiscard]] Choice choice() const { return m_choice; }

private:
    QWidget* buildFooter(services::ProStatus& pro, const QString& newName);

    Choice m_choice = Choice::Cancel;
};

} // namespace omnidict::ui
