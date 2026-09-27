#include "ui/about_dialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLocale>
#include <QTextBrowser>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kWidth = 560;
constexpr int kHeight = 480;
} // namespace

AboutDialog::AboutDialog(const QList<services::DictionaryInfo>& dictionaries, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About Omnidict"));
    auto* text = new QTextBrowser;
    text->setObjectName(u"aboutText"_s);
    text->setOpenExternalLinks(true);
    text->setHtml(aboutHtml(dictionaries));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(text, 1);
    layout->addWidget(buttons);
    resize(kWidth, kHeight);
}

QString AboutDialog::aboutHtml(const QList<services::DictionaryInfo>& dictionaries)
{
    // Identity comes from the application object, which main() sets from app/version.h.
    QString html = u"<h2>%1</h2><p>%2 %3</p><p>%4</p>"_s.arg(
        QGuiApplication::applicationDisplayName().toHtmlEscaped(), tr("Version"),
        QCoreApplication::applicationVersion().toHtmlEscaped(),
        tr("An offline dictionary with downloadable dictionaries."));
    html += u"<h3>"_s + tr("Dictionaries") + u"</h3>"_s;
    if (dictionaries.isEmpty()) {
        html += u"<p>"_s + tr("No dictionaries are installed.") + u"</p>"_s;
    }
    const QLocale locale;
    for (const services::DictionaryInfo& dictionary : dictionaries) {
        const QString license = dictionary.licenseUrl.isEmpty()
                                    ? dictionary.license.toHtmlEscaped()
                                    : u"<a href=\"%1\">%2</a>"_s.arg(dictionary.licenseUrl.toHtmlEscaped(),
                                                                     dictionary.license.toHtmlEscaped());
        html += u"<p><b>%1</b><br>%2<br>%3: %4. %5 %6, %7 %8.</p>"_s.arg(
            dictionary.name.toHtmlEscaped(), dictionary.attribution.toHtmlEscaped(), tr("Licence"), license,
            tr("Version"), dictionary.version.toHtmlEscaped(), locale.toString(dictionary.entryCount),
            tr("entries"));
    }
    return html;
}

} // namespace omnidict::ui
