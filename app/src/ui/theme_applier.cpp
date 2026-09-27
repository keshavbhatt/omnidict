#include "ui/theme_applier.h"

#include "core/settings.h"
#include "ui/logging.h"
#include "ui/style.h"

#include <QApplication>
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
