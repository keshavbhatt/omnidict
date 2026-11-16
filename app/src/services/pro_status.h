#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

namespace omnidict::services {

/// Omnidict Pro as the UI sees it (ADR-024, mocks/pro.html): the tier, the trial's and the
/// plan's dates, the Account ID and the Ktechpit links. The Microsoft Store build implements it
/// with the Ktechpit licensing module (services/licensing/license_service.h); every other build
/// has none, and a null ProStatus means everything is included. Tests stand in for it.
class ProStatus : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ProStatus)

public:
    enum class Tier
    {
        Pro,   ///< a purchase or a Ktechpit Pass covers this Account ID
        Trial, ///< inside the 14-day trial every new install starts with
        Free,  ///< the trial or a plan for a time is over and nothing covers it
    };
    Q_ENUM(Tier)

    /// Where a Choose a plan came from, so the checkout can count which prompt sells.
    enum class Source
    {
        Sheet,
        Dictionary,
        QuickLookup,
        TrialEnd,
        PlanEnd,
        EndsSoon,
    };

    explicit ProStatus(QObject* parent = nullptr)
        : QObject(parent)
    {}
    ~ProStatus() override = default;

    [[nodiscard]] virtual Tier tier() const = 0;
    /// Pro features may run: bought, or inside the trial.
    [[nodiscard]] bool hasPro() const { return tier() != Tier::Free; }
    /// Days of the trial left, a part day counting as one; 0 outside the trial.
    [[nodiscard]] virtual int trialDaysLeft() const = 0;
    [[nodiscard]] virtual int trialDays() const = 0;
    /// The trial's last moment; invalid when unknown.
    [[nodiscard]] virtual QDateTime trialEnd() const = 0;
    /// A purchase for good rather than until a date (also true with no purchase at all).
    [[nodiscard]] virtual bool isForever() const = 0;
    /// When a plan for a time ends (or ended); invalid for Forever and with no plan.
    [[nodiscard]] virtual QDateTime paidUntil() const = 0;
    /// When the server last answered; invalid before its first answer.
    [[nodiscard]] virtual QDateTime lastChecked() const = 0;
    [[nodiscard]] virtual QString accountId() const = 0;
    [[nodiscard]] virtual bool checking() const = 0;
    /// Asks the server now (Check again).
    virtual void refresh() = 0;

    [[nodiscard]] virtual QUrl checkoutUrl(Source source) const = 0;
    /// The self-service portal (purchases, receipts).
    [[nodiscard]] virtual QUrl portalUrl() const = 0;
    /// The portal's Ktechpit Pass page with Omnidict and this Account ID filled in.
    [[nodiscard]] virtual QUrl passUrl() const = 0;
    /// The Ktechpit Pass checkout with this Account ID.
    [[nodiscard]] virtual QUrl passCheckoutUrl() const = 0;

    /// A plan for a time that has ended and left Omnidict Free; invalid otherwise.
    [[nodiscard]] QDateTime endedPlan() const
    {
        const QDateTime until = paidUntil();
        return tier() == Tier::Free && until.isValid() && until <= QDateTime::currentDateTime() ? until
                                                                                                : QDateTime();
    }

    [[nodiscard]] static QString sourceName(Source source)
    {
        switch (source) {
        case Source::Sheet:
            return QStringLiteral("sheet");
        case Source::Dictionary:
            return QStringLiteral("dictionary");
        case Source::QuickLookup:
            return QStringLiteral("quick_lookup");
        case Source::TrialEnd:
            return QStringLiteral("trial_end");
        case Source::PlanEnd:
            return QStringLiteral("plan_end");
        case Source::EndsSoon:
            return QStringLiteral("ends_soon");
        }
        return {};
    }

Q_SIGNALS:
    void statusChanged();
    void checkingChanged(bool checking);
};

} // namespace omnidict::services
