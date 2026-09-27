#include "ui/entry_view.h"

#include <QEvent>
#include <QPalette>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

/// Mixes two colours: `amount` 0 gives `a`, 1 gives `b`.
QColor mix(const QColor& a, const QColor& b, float amount)
{
    return QColor::fromRgbF(a.redF() + ((b.redF() - a.redF()) * amount),
                            a.greenF() + ((b.greenF() - a.greenF()) * amount),
                            a.blueF() + ((b.blueF() - a.blueF()) * amount));
}

} // namespace

EntryView::EntryView(QWidget* parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setFrameShape(QFrame::NoFrame);
    document()->setDocumentMargin(18);
    connect(this, &QTextBrowser::anchorClicked, this, &EntryView::onAnchorClicked);
    applyStyleSheet();
}

void EntryView::showEntry(const QString& html)
{
    setHtml(html);
}

void EntryView::showMessage(const QString& text)
{
    setHtml(u"<p class=\"message\">"_s + text.toHtmlEscaped() + u"</p>"_s);
}

void EntryView::changeEvent(QEvent* event)
{
    QTextBrowser::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        applyStyleSheet();
    }
}

/// Colours follow the palette so the entry reads well in light and dark
/// themes; the structure is the contract's, only the look is ours.
void EntryView::applyStyleSheet()
{
    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::Text);
    const QColor base = pal.color(QPalette::Base);
    const QColor accent = pal.color(QPalette::Highlight);
    const QColor muted = mix(text, base, 0.45F);
    const QColor green = mix(QColor(0x26, 0xa2, 0x69), text, 0.15F);
    const QColor labelBack = mix(base, text, 0.08F);
    const QString css = uR"(
        .hw { font-size: x-large; font-weight: bold; color: %1; }
        .roman { color: %2; font-style: italic; }
        .freq { color: %1; }
        .prons { color: %2; margin-top: 2px; }
        .region { font-size: small; }
        .sense { margin-top: 12px; }
        .sense-head { margin-bottom: 2px; }
        .num { font-weight: bold; }
        .pos { font-weight: bold; font-variant: small-caps; }
        .pattern { color: %3; }
        .label { background-color: %4; color: %2; font-size: small; }
        .def { margin-top: 0px; }
        .examples { color: %2; margin-top: 2px; }
        .tr { font-style: italic; }
        .rel, .forms { color: %2; margin-top: 4px; }
        .rel-type { font-weight: bold; }
        a { color: %1; text-decoration: none; }
        .message { color: %2; }
    )"_s.arg(accent.name(), muted.name(), green.name(), labelBack.name());
    document()->setDefaultStyleSheet(css);
    // The default stylesheet applies to HTML set after it: re-render what is shown.
    if (!document()->isEmpty()) {
        setHtml(toHtml());
    }
}

void EntryView::onAnchorClicked(const QUrl& url)
{
    if (url.scheme() == u"lex"_s) {
        Q_EMIT headwordActivated(url.path());
    }
}

} // namespace omnidict::ui
