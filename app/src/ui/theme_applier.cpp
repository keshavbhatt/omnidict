#include "ui/theme_applier.h"

#include "core/settings.h"
#include "ui/logging.h"
#include "ui/style.h"

#include <QApplication>
#include <QFont>
#include <QFontInfo>
#include <QStyleFactory>
#include <QStyleHints>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

ThemeApplier::ThemeApplier(core::Settings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (QStyle* fusion = QStyleFactory::create(u"Fusion"_s)) {
        QApplication::setStyle(fusion);
    } else {
        qCWarning(lcUi) << "Fusion style unavailable; using the platform default";
    }
    // Chinese, Japanese and Korean text falls back to sans-serif faces when the
    // desktop has them; left to itself fontconfig may pick a serif one (DOCS/DESIGN.md: one UI face).
    QFont font = QApplication::font();
    // The resolved face, not an alias such as "Sans Serif": an alias in a family
    // list would hand Latin text to the first CJK face too.
    QStringList families = {QFontInfo(font).family()};
    families << u"Noto Sans CJK SC"_s << u"Noto Sans CJK JP"_s << u"Noto Sans CJK TC"_s
             << u"Noto Sans CJK KR"_s << u"Source Han Sans SC"_s << u"Source Han Sans CN"_s
             << u"Source Han Sans JP"_s << u"Source Han Sans"_s << u"WenQuanYi Micro Hei"_s
             << u"Noto Sans Devanagari"_s;
    font.setFamilies(families);
    QApplication::setFont(font);
    apply();
    connect(&m_settings, &core::Settings::themeChanged, this, &ThemeApplier::apply);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_settings.theme() == core::Theme::System) {
            apply();
        }
    });
}

void ThemeApplier::apply()
{
    const core::Theme theme = m_settings.theme();
    QStyleHints* hints = QGuiApplication::styleHints();
    if (theme == core::Theme::System) {
        hints->unsetColorScheme();
    } else {
        hints->setColorScheme(theme == core::Theme::Dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    }
    const bool dark = theme == core::Theme::System ? hints->colorScheme() == Qt::ColorScheme::Dark
                                                   : theme == core::Theme::Dark;
    Tokens::setCurrentScheme(dark);
    const Tokens& tokens = Tokens::current();
    QApplication::setPalette(paletteFor(tokens));
    qApp->setStyleSheet(styleSheetFor(tokens));
    Q_EMIT schemeChanged(dark);
}

} // namespace omnidict::ui
