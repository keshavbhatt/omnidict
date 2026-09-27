#include "ui/switch_button.h"

#include "ui/style.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>

namespace omnidict::ui {

namespace {
constexpr int kTrackWidth = 36;
constexpr int kTrackHeight = 20;
constexpr int kThumbMargin = 2;
constexpr int kThumbDiameter = kTrackHeight - (2 * kThumbMargin);
constexpr int kFocusHalo = 3;
constexpr qreal kDisabledOpacity = 0.4;
} // namespace

SwitchButton::SwitchButton(QWidget* parent)
    : QAbstractButton(parent)
{
    init();
}

SwitchButton::SwitchButton(const QString& text, QWidget* parent)
    : QAbstractButton(parent)
{
    init();
    setAccessibleName(text);
    setToolTip(text);
}

void SwitchButton::init()
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QSize SwitchButton::sizeHint() const
{
    const int margin = 2 * kFocusHalo;
    return {kTrackWidth + margin, kTrackHeight + margin};
}

void SwitchButton::paintEvent(QPaintEvent* /*event*/)
{
    const Tokens& t = Tokens::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) {
        painter.setOpacity(kDisabledOpacity);
    }

    const QRectF track((width() - kTrackWidth) / 2.0, (height() - kTrackHeight) / 2.0, kTrackWidth,
                       kTrackHeight);

    if (hasFocus()) {
        QPainterPath halo;
        halo.addRoundedRect(track.adjusted(-kFocusHalo, -kFocusHalo, kFocusHalo, kFocusHalo),
                            (kTrackHeight / 2.0) + kFocusHalo, (kTrackHeight / 2.0) + kFocusHalo);
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.accentSoft);
        painter.drawPath(halo);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(isChecked() ? t.accentStrong : t.border);
    painter.drawRoundedRect(track, kTrackHeight / 2.0, kTrackHeight / 2.0);

    const qreal thumbX =
        isChecked() ? track.right() - kThumbMargin - kThumbDiameter : track.left() + kThumbMargin;
    const QRectF thumb(thumbX, track.top() + kThumbMargin, kThumbDiameter, kThumbDiameter);
    painter.setBrush(Qt::white);
    painter.drawEllipse(thumb);
}

} // namespace omnidict::ui
