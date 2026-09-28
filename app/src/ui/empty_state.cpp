#include "ui/empty_state.h"

#include "ui/icons.h"
#include "ui/style.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kTileSize = 64;
constexpr int kTileRadius = 18;
constexpr int kGlyphSize = 32;
constexpr int kMaxTextWidth = 420;
} // namespace

EmptyState::EmptyState(QWidget* parent)
    : QWidget(parent)
    , m_tile(new QLabel(this))
    , m_title(new QLabel(this))
    , m_text(new QLabel(this))
    , m_tips(new QVBoxLayout)
    , m_button(new QPushButton(this))
{
    m_tile->setFixedSize(kTileSize, kTileSize);
    m_title->setProperty("heading", true);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setWordWrap(true);
    m_text->setProperty("muted", true);
    m_text->setAlignment(Qt::AlignCenter);
    m_text->setWordWrap(true);
    m_tips->setSpacing(4);
    m_button->hide();
    connect(m_button, &QPushButton::clicked, this, &EmptyState::buttonClicked);

    // Word-wrapped labels only get their height right at a known width
    // (DOCS/LESSONS.md): they fill a fixed-width column, centred by stretches.
    auto* column = new QWidget(this);
    column->setFixedWidth(kMaxTextWidth);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(m_tile, 0, Qt::AlignHCenter);
    layout->addWidget(m_title);
    layout->addWidget(m_text);
    layout->addLayout(m_tips);
    layout->addWidget(m_button, 0, Qt::AlignHCenter);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(column);
    row->addStretch(1);
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);
}

void EmptyState::setContent(const QString& glyph, const QString& title, const QString& text,
                            const QStringList& tips, const QString& buttonText)
{
    m_glyph = glyph;
    paintGlyph();
    m_title->setText(title);
    m_text->setText(text);
    m_text->setVisible(!text.isEmpty());
    while (const QLayoutItem* item = m_tips->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const QString& tip : tips) {
        auto* label = new QLabel(tip, this);
        label->setProperty("muted", true);
        label->setProperty("small", true);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        m_tips->addWidget(label);
    }
    m_button->setText(buttonText);
    m_button->setVisible(!buttonText.isEmpty());
}

void EmptyState::setButtonPrimary(bool primary)
{
    m_button->setProperty("primary", primary);
    m_button->style()->unpolish(m_button);
    m_button->style()->polish(m_button);
}

void EmptyState::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        paintGlyph();
    }
}

void EmptyState::paintGlyph()
{
    const Tokens& t = Tokens::current();
    const qreal dpr = devicePixelRatioF();
    QPixmap tile(QSize(kTileSize, kTileSize) * dpr);
    tile.setDevicePixelRatio(dpr);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, kTileSize, kTileSize), kTileRadius, kTileRadius);
    painter.fillPath(path, t.accentSoft);
    if (!m_glyph.isEmpty()) {
        constexpr int kInset = (kTileSize - kGlyphSize) / 2;
        painter.drawPixmap(kInset, kInset, icons::pixmap(m_glyph, t.accent, kGlyphSize, dpr));
    }
    painter.end();
    m_tile->setPixmap(tile);
}

} // namespace omnidict::ui
