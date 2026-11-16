#include "ui/pro_dialog.h"

#include "ui/icons.h"
#include "ui/style.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

constexpr int kWidth = 560;
constexpr int kIconSize = 18;
constexpr int kMeterHeight = 4;
constexpr int kMarkColumn = 64;
/// The table's first column: the sheet less its margins and the two mark columns.
constexpr int kTextWidth = kWidth - (2 * 20) - (2 * kMarkColumn) - (2 * 8);

QString longDate(const QDateTime& when)
{
    return QLocale().toString(when.date(), u"d MMMM yyyy"_s);
}

/// "1 day", "5 days": Omnidict ships no translations yet, so tr()'s %n plural is not used.
QString days(int count)
{
    return count == 1 ? QObject::tr("1 day") : QObject::tr("%1 days").arg(count);
}

QLabel* mutedSmall(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("muted", true);
    label->setProperty("small", true);
    label->setWordWrap(true);
    return label;
}

/// "2 minutes ago", "3 hours ago", "16 November 2027".
QString sinceWords(const QDateTime& when)
{
    const qint64 seconds = when.secsTo(QDateTime::currentDateTime());
    constexpr qint64 kMinute = 60;
    constexpr qint64 kHour = 60 * kMinute;
    constexpr qint64 kDay = 24 * kHour;
    if (seconds < kMinute) {
        return QObject::tr("just now");
    }
    if (seconds < kHour) {
        const auto minutes = static_cast<int>(seconds / kMinute);
        return minutes == 1 ? QObject::tr("1 minute ago") : QObject::tr("%1 minutes ago").arg(minutes);
    }
    if (seconds < kDay) {
        const auto hours = static_cast<int>(seconds / kHour);
        return hours == 1 ? QObject::tr("1 hour ago") : QObject::tr("%1 hours ago").arg(hours);
    }
    return QObject::tr("on %1").arg(longDate(when));
}

} // namespace

ProDialog::State ProDialog::stateOf(const services::ProStatus& pro)
{
    using Tier = services::ProStatus::Tier;
    switch (pro.tier()) {
    case Tier::Trial:
        return State::Trial;
    case Tier::Pro: {
        if (pro.isForever()) {
            return State::Forever;
        }
        const qint64 days = QDateTime::currentDateTime().daysTo(pro.paidUntil());
        return days <= kEndsSoonDays ? State::EndsSoon : State::Until;
    }
    case Tier::Free:
        return pro.endedPlan().isValid() ? State::Ended : State::Free;
    }
    return State::Free;
}

ProDialog::ProDialog(services::ProStatus& pro, QWidget* parent)
    : QDialog(parent)
    , m_pro(pro)
{
    setObjectName(u"proDialog"_s);
    setWindowTitle(titleWithApp(tr("Omnidict Pro")));
    setWindowModality(Qt::WindowModal);
    setFixedWidth(kWidth);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* body = new QVBoxLayout;
    body->setContentsMargins(20, 20, 20, 16);
    body->setSpacing(14);
    auto* title = new QLabel(tr("Omnidict Pro"), this);
    title->setProperty("title", true);
    body->addWidget(title);
    body->addWidget(buildStatus());
    body->addWidget(buildTable());
    body->addWidget(buildAccount());
    body->addWidget(buildPass());
    root->addLayout(body, 1);
    root->addWidget(buildFooter());

    connect(&m_pro, &services::ProStatus::statusChanged, this, &ProDialog::refresh);
    connect(&m_pro, &services::ProStatus::checkingChanged, this, &ProDialog::refresh);
    refresh();
}

QWidget* ProDialog::buildStatus()
{
    m_status = new QFrame(this);
    m_status->setObjectName(u"proStatus"_s);
    m_status->setProperty("proStatus", true);
    auto* layout = new QHBoxLayout(m_status);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(12);
    m_statusIcon = new QLabel(m_status);
    m_statusIcon->setFixedSize(kIconSize, kIconSize);
    layout->addWidget(m_statusIcon, 0, Qt::AlignTop);
    auto* text = new QVBoxLayout;
    text->setSpacing(3);
    m_statusTitle = new QLabel(m_status);
    m_statusTitle->setObjectName(u"proStatusTitle"_s);
    m_statusTitle->setProperty("strong", true);
    text->addWidget(m_statusTitle);
    m_statusText = mutedSmall({}, m_status);
    m_statusText->setObjectName(u"proStatusText"_s);
    text->addWidget(m_statusText);
    m_meter = new QProgressBar(m_status);
    m_meter->setObjectName(u"proMeter"_s);
    m_meter->setTextVisible(false);
    m_meter->setFixedHeight(kMeterHeight);
    text->addSpacing(5);
    text->addWidget(m_meter);
    layout->addLayout(text, 1);
    return m_status;
}

QWidget* ProDialog::buildTable()
{
    m_table = new QWidget(this);
    m_table->setObjectName(u"proTable"_s);
    auto* grid = new QGridLayout(m_table);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(0);
    grid->setColumnStretch(0, 1);
    grid->setColumnMinimumWidth(1, kMarkColumn);
    grid->setColumnMinimumWidth(2, kMarkColumn);
    const auto head = [this](const QString& text) {
        auto* label = new QLabel(text, m_table);
        label->setProperty("tableHead", true);
        return label;
    };
    grid->addWidget(head(tr("WHAT YOU GET")), 0, 0);
    grid->addWidget(head(tr("FREE")), 0, 1, Qt::AlignHCenter);
    grid->addWidget(head(tr("PRO")), 0, 2, Qt::AlignHCenter);

    const QString yes = u"\u2713"_s;
    const QString no = u"-"_s;
    const QList<TableRow> rows = {
        {.name = tr("Look words up in Omnidict"),
         .detail = tr("Suggestions, wildcards, full text search, favourites"),
         .free = yes,
         .pro = yes},
        {.name = tr("Download dictionaries"),
         .detail = tr("Any of the 600+, and their updates"),
         .free = yes,
         .pro = yes},
        {.name = tr("Dictionaries searched at once"),
         .detail = tr("Free: one at a time, switch between them any time"),
         .free = u"1"_s,
         .pro = tr("All")},
        {.name = tr("Quick lookup from any app"),
         .detail = tr("Select a word anywhere and press Ctrl+Alt+D"),
         .free = no,
         .pro = yes},
    };
    int at = 1;
    for (const TableRow& row : rows) {
        addTableRow(grid, at, row);
        at += 2;
    }
    return m_table;
}

void ProDialog::addTableRow(QGridLayout* grid, int at, const TableRow& row)
{
    auto* rule = new QFrame(m_table);
    rule->setProperty("separator", true);
    grid->addWidget(rule, at, 0, 1, 3);
    auto* cell = new QWidget(m_table);
    auto* cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(0, 8, 0, 8);
    cellLayout->setSpacing(2);
    cellLayout->addWidget(new QLabel(row.name, cell));
    QLabel* detail = mutedSmall(row.detail, cell);
    // A wrapped label needs its width to know its height; the top-level layout does not ask.
    detail->setFixedWidth(kTextWidth);
    detail->setMinimumHeight(detail->heightForWidth(kTextWidth));
    cellLayout->addWidget(detail);
    grid->addWidget(cell, at + 1, 0);
    const auto mark = [this](const QString& text) {
        auto* label = new QLabel(text, m_table);
        const bool included = text != u"-"_s && text != u"1"_s;
        label->setProperty(included ? "tone" : "muted", included ? QVariant(u"success"_s) : QVariant(true));
        label->setToolTip(text == u"-"_s ? tr("Not included") : tr("Included"));
        return label;
    };
    grid->addWidget(mark(row.free), at + 1, 1, Qt::AlignHCenter | Qt::AlignTop);
    grid->addWidget(mark(row.pro), at + 1, 2, Qt::AlignHCenter | Qt::AlignTop);
}

QWidget* ProDialog::buildAccount()
{
    auto* account = new QWidget(this);
    auto* layout = new QVBoxLayout(account);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* heading = new QLabel(tr("Account ID"), account);
    heading->setProperty("strong", true);
    layout->addWidget(heading);
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    m_accountId = new QLabel(account);
    m_accountId->setObjectName(u"proAccountId"_s);
    m_accountId->setProperty("accountId", true);
    m_accountId->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_accountId->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(m_accountId, 1);
    auto* copy = new QPushButton(icons::themed(u"copy"_s, Tokens::current().text), tr("Copy"), account);
    copy->setObjectName(u"proCopy"_s);
    copy->setProperty("small", true);
    copy->setToolTip(tr("Copy the Account ID"));
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(m_pro.accountId()); });
    row->addWidget(copy);
    layout->addLayout(row);
    m_checked = mutedSmall({}, account);
    m_checked->setObjectName(u"proChecked"_s);
    m_checked->setTextFormat(Qt::RichText);
    connect(m_checked, &QLabel::linkActivated, this, [this] { m_pro.refresh(); });
    layout->addWidget(m_checked);
    return account;
}

QWidget* ProDialog::buildPass()
{
    auto* pass = new QFrame(this);
    pass->setObjectName(u"proPass"_s);
    pass->setProperty("proPass", true);
    m_pass = pass;
    auto* layout = new QHBoxLayout(pass);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    auto* name = new QLabel(tr("Omnidict is included in Ktechpit Pass"), pass);
    name->setProperty("strong", true);
    name->setProperty("small", true);
    text->addWidget(name);
    text->addWidget(mutedSmall(tr("One purchase unlocks every Ktechpit app."), pass));
    auto* add = mutedSmall(tr("Have one already? <a href=\"add\">Add Omnidict to it</a>."), pass);
    add->setObjectName(u"proPassAdd"_s);
    add->setTextFormat(Qt::RichText);
    add->setToolTip(tr("Opens your Ktechpit Pass on ktechpit.com with Omnidict filled in"));
    connect(add, &QLabel::linkActivated, this, [this] { QDesktopServices::openUrl(m_pro.passUrl()); });
    text->addWidget(add);
    layout->addLayout(text, 1);
    auto* get =
        new QPushButton(icons::themed(u"external"_s, Tokens::current().text), tr("Get the Pass"), pass);
    get->setObjectName(u"proPassGet"_s);
    get->setProperty("small", true);
    get->setToolTip(tr("Opens the Ktechpit Pass checkout in your browser, with this Account ID"));
    connect(get, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(m_pro.passCheckoutUrl()); });
    layout->addWidget(get, 0, Qt::AlignTop);
    return pass;
}

QWidget* ProDialog::buildFooter()
{
    auto* foot = new QFrame(this);
    foot->setProperty("sheetFoot", true);
    auto* layout = new QHBoxLayout(foot);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->setSpacing(8);
    auto* manage = new QPushButton(tr("Manage purchases"), foot);
    manage->setObjectName(u"proManage"_s);
    manage->setProperty("linkButton", true);
    manage->setCursor(Qt::PointingHandCursor);
    manage->setToolTip(tr("Your purchases on ktechpit.com"));
    connect(manage, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(m_pro.portalUrl()); });
    layout->addWidget(manage);
    layout->addStretch(1);
    auto* close = new QPushButton(tr("Close"), foot);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(close);
    const QIcon external = icons::themed(u"external"_s, Tokens::current().text);
    m_moreTime = new QPushButton(external, tr("Add more time"), foot);
    m_moreTime->setObjectName(u"proMoreTime"_s);
    layout->addWidget(m_moreTime);
    m_choose = new QPushButton(icons::themed(u"external"_s, Tokens::current().accentText),
                               tr("Choose a plan"), foot);
    m_choose->setObjectName(u"proChoose"_s);
    m_choose->setProperty("primary", true);
    m_choose->setDefault(true);
    layout->addWidget(m_choose);
    const auto openCheckout = [this] {
        QDesktopServices::openUrl(m_pro.checkoutUrl(services::ProStatus::Source::Sheet));
    };
    connect(m_moreTime, &QPushButton::clicked, this, openCheckout);
    connect(m_choose, &QPushButton::clicked, this, openCheckout);
    return foot;
}

ProDialog::Status ProDialog::statusFor(State state) const
{
    const Tokens& tokens = Tokens::current();
    switch (state) {
    case State::Trial: {
        const int left = m_pro.trialDaysLeft();
        return {.glyph = u"star"_s,
                .color = tokens.text,
                .title = tr("Pro trial: %1 left").arg(days(left)),
                .text =
                    tr("Everything in Pro is yours to use until %1. After that Omnidict stays free with one "
                       "dictionary at a time.")
                        .arg(longDate(m_pro.trialEnd())),
                .meter = true};
    }
    case State::Free:
        return {.glyph = u"book"_s,
                .color = tokens.muted,
                .title = tr("Free"),
                .text = tr("Look words up in one dictionary at a time, and switch between them freely. Pro "
                           "searches all of them and adds quick lookup from any app."),
                .meter = false};
    case State::Forever:
        return {.glyph = u"check"_s,
                .color = tokens.success,
                .title = tr("Pro, Forever"),
                .text = tr("Thank you. Every Pro feature is yours, in every update."),
                .meter = false};
    case State::Until:
        return {.glyph = u"check"_s,
                .color = tokens.success,
                .title = tr("Pro, until %1").arg(longDate(m_pro.paidUntil())),
                .text =
                    tr("Thank you. Every Pro feature is yours until then. Choose a plan again before it ends "
                       "to add more time."),
                .meter = false};
    case State::EndsSoon: {
        const auto left =
            static_cast<int>(qMax<qint64>(0, QDateTime::currentDateTime().daysTo(m_pro.paidUntil())));
        return {.glyph = u"clock"_s,
                .color = tokens.warning,
                .title = tr("Pro ends in %1").arg(days(left)),
                .text =
                    tr("Your plan runs until %1. After that Omnidict goes back to Free: one dictionary at a "
                       "time, no quick lookup. Choose a plan to add more time.")
                        .arg(longDate(m_pro.paidUntil())),
                .meter = false};
    }
    case State::Ended:
        break;
    }
    return {.glyph = u"book"_s,
            .color = tokens.muted,
            .title = tr("Your Pro plan ended on %1").arg(longDate(m_pro.endedPlan())),
            .text =
                tr("Omnidict is Free again: one dictionary at a time, no quick lookup. Your dictionaries, "
                   "favourites and history are all still here; choose a plan to switch Pro back on."),
            .meter = false};
}

void ProDialog::refresh()
{
    const State state = stateOf(m_pro);
    const Status status = statusFor(state);
    m_statusIcon->setPixmap(
        icons::themed(status.glyph, status.color).pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));
    m_statusTitle->setText(status.title);
    m_statusText->setText(status.text);
    m_meter->setVisible(status.meter);
    if (status.meter) {
        const int used = qMax(0, m_pro.trialDays() - m_pro.trialDaysLeft());
        m_meter->setRange(0, m_pro.trialDays());
        m_meter->setValue(used);
        m_meter->setToolTip(tr("%1 of %2 days used").arg(used).arg(m_pro.trialDays()));
    }

    const bool hasPro = state == State::Forever || state == State::Until;
    m_table->setVisible(!hasPro);
    m_pass->setVisible(!hasPro);
    m_choose->setVisible(!hasPro);
    m_moreTime->setVisible(state == State::Until);
    m_accountId->setText(m_pro.accountId());
    refreshChecked(hasPro || state == State::EndsSoon);

    // As tall as what this state shows: the table and the Pass come and go.
    layout()->activate();
    const int height = heightForWidth(kWidth);
    setFixedHeight(height > 0 ? height : sizeHint().height());
}

void ProDialog::refreshChecked(bool paid)
{
    const QString why = tr("The checkout asks for it, and it is how a purchase finds this copy of Omnidict.");
    if (m_pro.checking()) {
        m_checked->setText(tr("Checking with Ktechpit..."));
    } else if (paid) {
        const QDateTime checked = m_pro.lastChecked();
        m_checked->setText(
            why + u' ' +
            (checked.isValid() ? tr("Checked with Ktechpit %1.").arg(sinceWords(checked)) : QString()));
    } else {
        m_checked->setText(why + u' ' + tr("Bought already? <a href=\"check\">Check again</a>."));
    }
}

} // namespace omnidict::ui
