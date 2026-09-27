#include "ui/search_field.h"

#include "ui/icons.h"
#include "ui/style.h"

#include <QEvent>
#include <QLabel>
#include <QToolButton>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kGlyphSize = 18;
constexpr int kClearSize = 24;
constexpr int kClearGlyph = 15;
constexpr int kInset = 10;
constexpr int kTextGap = 8;
} // namespace

SearchField::SearchField(QWidget* parent)
    : QLineEdit(parent)
    , m_glyph(new QLabel(this))
    , m_clear(new QToolButton(this))
{
    m_glyph->setFixedSize(kGlyphSize, kGlyphSize);
    m_glyph->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_clear->setFixedSize(kClearSize, kClearSize);
    m_clear->setIconSize(QSize(kClearGlyph, kClearGlyph));
    m_clear->setCursor(Qt::ArrowCursor);
    m_clear->setFocusPolicy(Qt::NoFocus);
    m_clear->setToolTip(tr("Clear (Esc)"));
    m_clear->setAccessibleName(tr("Clear the search"));
    m_clear->setStyleSheet(
        u"QToolButton { min-width: 0; min-height: 0; padding: 0; border-radius: 12px; }"_s);
    m_clear->hide();
    setTextMargins(kInset + kGlyphSize + kTextGap - 10, 0, kClearSize + 4, 0);
    connect(m_clear, &QToolButton::clicked, this, &QLineEdit::clear);
    connect(this, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_clear->setVisible(!text.isEmpty()); });
    refreshIcons();
}

void SearchField::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    place();
}

void SearchField::changeEvent(QEvent* event)
{
    QLineEdit::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        refreshIcons();
    }
}

void SearchField::refreshIcons()
{
    const Tokens& t = Tokens::current();
    m_glyph->setPixmap(icons::pixmap(u"search"_s, t.muted, kGlyphSize, devicePixelRatioF()));
    m_clear->setIcon(icons::themed(u"x"_s, t.muted));
}

void SearchField::place()
{
    m_glyph->move(kInset, (height() - kGlyphSize) / 2);
    m_clear->move(width() - kClearSize - 6, (height() - kClearSize) / 2);
}

} // namespace omnidict::ui
