#include "ui/entry_view.h"

#include "ui/style.h"

#include <QEvent>
#include <QScrollBar>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kMarginX = 8; // with the document margin, mocks/mock.css .entry padding
constexpr double kHeadwordScale = 2.0;
constexpr double kSmallScale = 0.87;
} // namespace

EntryView::EntryView(QWidget* parent)
    : QTextBrowser(parent)
{
    setObjectName(u"entry"_s);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setFrameShape(QFrame::NoFrame);
    setViewportMargins(kMarginX, 0, kMarginX, 0);
    document()->setDocumentMargin(24);
    connect(this, &QTextBrowser::anchorClicked, this, &EntryView::onAnchorClicked);
    applyStyleSheet();
}

void EntryView::showEntry(const QString& html, const QString& credit)
{
    m_html = html;
    m_credit = credit;
    render();
    verticalScrollBar()->setValue(0);
}

void EntryView::setTextSize(int pixels)
{
    if (pixels != m_textSize) {
        m_textSize = pixels;
        applyStyleSheet();
    }
}

void EntryView::render()
{
    QString html = m_html;
    if (!m_credit.isEmpty()) {
        html += u"<hr><p class=\"credit\">"_s + m_credit.toHtmlEscaped() + u"</p>"_s;
    }
    setHtml(html);
}

void EntryView::changeEvent(QEvent* event)
{
    QTextBrowser::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        applyStyleSheet();
    }
}

/// The contract fixes the structure; the look is ours (mocks/main.html): the
/// headword in the headword colour, patterns green, labels on a tint,
/// examples muted, links in the link colour.
void EntryView::applyStyleSheet()
{
    const Tokens& t = Tokens::current();
    QFont font = document()->defaultFont();
    font.setPixelSize(m_textSize);
    document()->setDefaultFont(font);
    const auto px = [this](double scale) { return QString::number(qRound(m_textSize * scale)); };
    const QString css = uR"(
        .hw { font-size: %6px; font-weight: 600; color: %1; }
        .roman { color: %2; font-style: italic; }
        .freq { color: %5; }
        .prons { color: %2; margin-top: 4px; }
        .region { font-size: %7px; }
        .sense { margin-top: 18px; }
        .sense-head { margin-bottom: 2px; font-size: %7px; }
        .num { font-weight: 700; }
        .pos { font-weight: 600; font-variant: small-caps; color: %2; }
        .pattern { color: %3; }
        .label { background-color: %4; color: %2; font-size: %7px; }
        .def { margin-top: 2px; }
        .examples { color: %8; margin-top: 4px; }
        .tr { font-style: italic; }
        .rel, .forms { color: %2; margin-top: 10px; }
        .rel-type { font-weight: 600; }
        .credit { color: %2; font-size: %9px; }
        a { color: %10; text-decoration: none; }
    )"_s.arg(t.headword.name(), t.muted.name(), t.pattern.name(), t.hover.name(), t.warm.name(),
             px(kHeadwordScale), px(kSmallScale), t.example.name(), px(kSmallScale * kSmallScale),
             t.link.name());
    document()->setDefaultStyleSheet(css);
    // The default sheet applies to HTML set after it: render again.
    if (!m_html.isEmpty()) {
        render();
    }
}

void EntryView::onAnchorClicked(const QUrl& url)
{
    if (url.scheme() == u"lex"_s) {
        Q_EMIT headwordActivated(url.path());
    }
}

} // namespace omnidict::ui
