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
    /// Without the main pane's margins, for the Quick Lookup popup, whose own padding
    /// lines the entry up with its dictionary name (mocks/quick-lookup.html).
    void setCompact(bool compact);
    /// The entry as plain text, for the clipboard (labels included, though they are drawn as images).
    [[nodiscard]] QString plainText() const;

Q_SIGNALS:
    void headwordActivated(const QString& headword);

protected:
    void changeEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void applyStyleSheet();
    void render();
    /// The HTML with every label replaced by an image of it in its bordered box.
    [[nodiscard]] QString withLabelImages(const QString& html);
    void onAnchorClicked(const QUrl& url);

    QString m_html;
    QString m_credit;
    int m_textSize = 15;
};

} // namespace omnidict::ui
