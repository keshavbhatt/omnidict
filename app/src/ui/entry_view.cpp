#include "ui/entry_view.h"

#include "ui/style.h"

#include <QEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextDocument>
#include <QUrl>
#include <QWheelEvent>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kMarginX = 8; // with the document margin, mocks/mock.css .entry padding
constexpr int kDocumentMargin = 24;
constexpr int kCompactDocumentMargin = 2;
constexpr double kHeadwordScale = 2.0;
constexpr double kSmallScale = 0.8;    // labels, part of speech, credit
constexpr double kHeadingScale = 0.87; // the sense heading line
constexpr int kLabelPadX = 5;
constexpr qreal kLabelRadius = 4.0;
} // namespace

EntryView::EntryView(QWidget* parent)
    : QTextBrowser(parent)
{
    setObjectName(u"entry"_s);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setFrameShape(QFrame::NoFrame);
    setViewportMargins(kMarginX, 0, kMarginX, 0);
    document()->setDocumentMargin(kDocumentMargin);
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

void EntryView::setCompact(bool compact)
{
    setViewportMargins(compact ? 0 : kMarginX, 0, compact ? 0 : kMarginX, 0);
    document()->setDocumentMargin(compact ? kCompactDocumentMargin : kDocumentMargin);
    // The popup's own background shows through, in either theme, rather than a box of its own.
    viewport()->setAutoFillBackground(!compact);
    setFrameShape(compact ? QFrame::NoFrame : frameShape());
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
    QString html = withLabelImages(m_html);
    if (!m_credit.isEmpty()) {
        // A table cell, because rich text draws a border only on cells: the 1 px rule of mocks/main.html.
        html += u"<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top: 28px;\"><tr>"
                "<td class=\"credit\">"_s +
                m_credit.toHtmlEscaped() + u"</td></tr></table>"_s;
    }
    setHtml(html);
}

QString EntryView::plainText() const
{
    QTextDocument document;
    document.setHtml(m_html + u"<p>"_s + m_credit.toHtmlEscaped() + u"</p>"_s);
    return document.toPlainText();
}

QString EntryView::withLabelImages(const QString& html)
{
    // Rich text draws no border round inline text, and the mock boxes each label.
    static const QRegularExpression kLabel(u"<span class=\"label\">(.*?)</span>"_s);
    const Tokens& t = Tokens::current();
    QFont font = document()->defaultFont();
    font.setPixelSize(qRound(m_textSize * kSmallScale));
    const QFontMetrics metrics(font);
    const qreal dpr = devicePixelRatioF();
    QString out;
    qsizetype from = 0;
    int index = 0;
    for (const QRegularExpressionMatch& match : kLabel.globalMatch(html)) {
        QTextDocument unescape;
        unescape.setHtml(match.captured(1));
        const QString text = unescape.toPlainText();
        const QSize size(metrics.horizontalAdvance(text) + (2 * kLabelPadX), metrics.height() + 2);
        QPixmap box(size * dpr);
        box.setDevicePixelRatio(dpr);
        box.fill(Qt::transparent);
        QPainter painter(&box);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(t.border, 1.0));
        painter.drawRoundedRect(QRectF(0.5, 0.5, size.width() - 1.0, size.height() - 1.0), kLabelRadius,
                                kLabelRadius);
        painter.setFont(font);
        painter.setPen(t.muted);
        painter.drawText(QRect(QPoint(0, 0), size), Qt::AlignCenter, text);
        painter.end();
        const QUrl name(u"omnidict-label:%1"_s.arg(index++));
        document()->addResource(QTextDocument::ImageResource, name, box);
        out += html.mid(from, match.capturedStart() - from);
        out +=
            u"<img src=\"%1\" width=\"%2\" height=\"%3\" style=\"vertical-align: middle;\" alt=\"%4\">"_s.arg(
                name.toString(), QString::number(size.width()), QString::number(size.height()),
                text.toHtmlEscaped());
        out += u"&nbsp;"_s;
        from = match.capturedEnd();
    }
    out += html.mid(from);
    return out;
}

void EntryView::wheelEvent(QWheelEvent* event)
{
    // QTextEdit zooms on Ctrl+wheel: without limits, only the text without a set
    // size, and past the text-size setting. Ctrl+Plus, Ctrl+Minus and Ctrl+0 are
    // the way to change the size, so Ctrl+wheel does nothing here.
    if ((event->modifiers() & Qt::ControlModifier) != 0) {
        event->accept();
        return;
    }
    QTextBrowser::wheelEvent(event);
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
        .sense-head { margin-bottom: 2px; font-size: %11px; }
        .num { font-weight: 700; }
        .pos { font-weight: 600; font-size: %7px; text-transform: uppercase; letter-spacing: 0.6px; color: %2; }
        .pattern { color: %3; }
        .label { color: %2; font-size: %7px; }
        .def { margin-top: 2px; }
        .examples { color: %8; margin-top: 4px; }
        .tr { font-style: italic; }
        .rel, .forms { color: %2; margin-top: 10px; }
        .rel-type { font-weight: 600; }
        .credit { color: %2; font-size: %9px; border-top: 1px solid %4; padding-top: 12px; }
        a { color: %10; text-decoration: none; }
    )"_s.arg(t.headword.name(), t.muted.name(), t.pattern.name(), t.border.name(), t.warm.name(),
             px(kHeadwordScale), px(kSmallScale), t.example.name(), px(kSmallScale), t.link.name(),
             px(kHeadingScale));
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
