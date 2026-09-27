#pragma once

#include <QTextBrowser>

namespace omnidict::ui {

/// Shows one entry's HTML (DOCS/entry-html.md) with the desktop stylesheet.
/// `lex:` links become headwordActivated(); no other link is followed.
class EntryView : public QTextBrowser
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(EntryView)

public:
    explicit EntryView(QWidget* parent = nullptr);
    ~EntryView() override = default;

    void showEntry(const QString& html);
    /// A short centred note in place of an entry ("No dictionaries yet", ...).
    void showMessage(const QString& text);

Q_SIGNALS:
    void headwordActivated(const QString& headword);

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyStyleSheet();
    void onAnchorClicked(const QUrl& url);
};

} // namespace omnidict::ui
