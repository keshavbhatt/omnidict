#pragma once

#include <QLineEdit>

class QLabel;
class QToolButton;

namespace omnidict::ui {

/// The header's search field (mocks/main.html): a search glyph at the left and
/// a clear button at the right that shows only when there is text. Built from
/// child widgets because QLineEdit's own actions sit off-centre under a style
/// sheet.
class SearchField : public QLineEdit
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SearchField)

public:
    explicit SearchField(QWidget* parent = nullptr);
    ~SearchField() override = default;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void refreshIcons();
    void place();

    QLabel* m_glyph = nullptr;
    QToolButton* m_clear = nullptr;
};

} // namespace omnidict::ui
