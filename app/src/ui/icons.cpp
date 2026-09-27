#include "ui/icons.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

using namespace Qt::StringLiterals;

namespace omnidict::ui::icons {

namespace {

QByteArray tintedSvg(const QString& name, const QColor& color)
{
    QFile file(u":/icons/ui/"_s + name + u".svg"_s);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svg = file.readAll();
    svg.replace("#000000", color.name(QColor::HexRgb).toLatin1());
    return svg;
}

QPixmap render(const QByteArray& svg, int size, qreal dpr, qreal opacity)
{
    QSvgRenderer renderer(svg);
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    if (renderer.isValid()) {
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(opacity);
        renderer.render(&painter, QRectF(0, 0, size, size));
    }
    return pm;
}

} // namespace

QPixmap pixmap(const QString& name, const QColor& color, int size, qreal devicePixelRatio)
{
    static QHash<QString, QPixmap> cache;
    const QString key = name + u'|' + color.name(QColor::HexArgb) + u'|' + QString::number(size) + u'|' +
                        QString::number(devicePixelRatio);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }
    const QPixmap pm =
        render(tintedSvg(name, color), size, devicePixelRatio, static_cast<qreal>(color.alphaF()));
    cache.insert(key, pm);
    return pm;
}

QIcon themed(const QString& name, const QColor& color, const QColor& disabledColor)
{
    constexpr float kDisabledAlpha = 0.35F;
    QColor disabled = disabledColor;
    if (!disabled.isValid()) {
        disabled = color;
        disabled.setAlphaF(kDisabledAlpha);
    }
    QIcon icon;
    for (const int size : {16, 18, 20, 24, 32}) {
        for (const qreal dpr : {1.0, 2.0}) {
            icon.addPixmap(pixmap(name, color, size, dpr), QIcon::Normal);
            icon.addPixmap(pixmap(name, disabled, size, dpr), QIcon::Disabled);
        }
    }
    return icon;
}

QIcon brand()
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(u":/icons/hicolor/%1x%1/apps/com.ktechpit.omnidict.png"_s.arg(size));
    }
    return icon;
}

} // namespace omnidict::ui::icons
