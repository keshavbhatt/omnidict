#pragma once

#include "services/pro_status.h"

#include <QUrlQuery>

namespace omnidict::tests {

/// Stands in for the Ktechpit licensing module (Microsoft Store build only): each test sets the
/// tier and dates it needs, and the links carry the source so tests can check them.
class FakePro final : public services::ProStatus
{
public:
    explicit FakePro(Tier tier = Tier::Free)
        : m_tier(tier)
    {}

    void set(Tier tier, const QDateTime& paidUntil = {})
    {
        m_tier = tier;
        m_paidUntil = paidUntil;
        Q_EMIT statusChanged();
    }

    [[nodiscard]] Tier tier() const override { return m_tier; }
    [[nodiscard]] int trialDaysLeft() const override { return m_tier == Tier::Trial ? 12 : 0; }
    [[nodiscard]] int trialDays() const override { return 14; }
    [[nodiscard]] QDateTime trialEnd() const override { return QDateTime::currentDateTime().addDays(12); }
    [[nodiscard]] bool isForever() const override { return !m_paidUntil.isValid(); }
    [[nodiscard]] QDateTime paidUntil() const override { return m_paidUntil; }
    [[nodiscard]] QDateTime lastChecked() const override
    {
        return QDateTime::currentDateTime().addSecs(-120);
    }
    [[nodiscard]] QString accountId() const override { return QStringLiteral("WINTESTACCOUNT0001"); }
    [[nodiscard]] bool checking() const override { return false; }
    void refresh() override { ++refreshes; }

    [[nodiscard]] QUrl checkoutUrl(Source source) const override
    {
        QUrl url(QStringLiteral("https://example.invalid/checkout"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("source"), sourceName(source));
        url.setQuery(query);
        return url;
    }
    [[nodiscard]] QUrl portalUrl() const override
    {
        return QUrl(QStringLiteral("https://example.invalid/portal"));
    }
    [[nodiscard]] QUrl passUrl() const override
    {
        return QUrl(QStringLiteral("https://example.invalid/pass"));
    }
    [[nodiscard]] QUrl passCheckoutUrl() const override
    {
        return QUrl(QStringLiteral("https://example.invalid/pass-checkout"));
    }

    int refreshes = 0;

private:
    Tier m_tier;
    QDateTime m_paidUntil;
};

} // namespace omnidict::tests
