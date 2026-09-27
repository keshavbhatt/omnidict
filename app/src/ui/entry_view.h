#pragma once

#include <QTextBrowser>

namespace omnidict::ui {

/// Shows one entry's HTML (DOCS/entry-html.md) with the desktop style sheet
/// built from the design tokens, followed by the dictionary's credit line.
/// `lex:` links become headwordActivated(); no other link is followed.
class EntryView : public QTextBrowser
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(EntryView)

public:
    explicit EntryView(QWidget* parent = nullptr);
    ~EntryView() override = default;

    /// `credit` is plain text, shown under a rule at the end (the content
    /// licence asks for it on every entry).
    void showEntry(const QString& html, const QString& credit);
    /// Body text size in pixels; headings scale with it.
    void setTextSize(int pixels);
    /// The entry as plain text, for the clipboard.
    [[nodiscard]] QString plainText() const { return toPlainText(); }

Q_SIGNALS:
    void headwordActivated(const QString& headword);

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyStyleSheet();
    void render();
    void onAnchorClicked(const QUrl& url);

    QString m_html;
    QString m_credit;
    int m_textSize = 15;
};

} // namespace omnidict::ui
