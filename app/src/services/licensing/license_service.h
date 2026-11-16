// SPDX-FileCopyrightText: 2026 Keshav Bhatt (Ktechpit)
// SPDX-License-Identifier: LicenseRef-Ktechpit-Licensing-Module

#pragma once

#include "AccountLicenseManager.h"
#include "services/pro_status.h"

namespace omnidict::services {

/// Omnidict Pro through the Ktechpit licensing module, for the Microsoft Store build (ADR-024):
/// the Account ID, the 14-day trial every new install starts with, and the server's last
/// answer, checked on start and weekly, so Omnidict keeps working offline with it.
class LicenseService final : public ProStatus
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LicenseService)

public:
    explicit LicenseService(QObject* parent = nullptr);
    ~LicenseService() override;

    /// Loads or makes the Account ID and reads the last answer; with `online`, asks the server
    /// when a check is due.
    void start(bool online = true);

    [[nodiscard]] Tier tier() const override;
    [[nodiscard]] int trialDaysLeft() const override;
    [[nodiscard]] int trialDays() const override;
    [[nodiscard]] QDateTime trialEnd() const override;
    [[nodiscard]] bool isForever() const override;
    [[nodiscard]] QDateTime paidUntil() const override;
    [[nodiscard]] QDateTime lastChecked() const override;
    [[nodiscard]] QString accountId() const override { return m_accountId; }
    [[nodiscard]] bool checking() const override { return m_checking; }
    void refresh() override;

    [[nodiscard]] QUrl checkoutUrl(Source source) const override;
    [[nodiscard]] QUrl portalUrl() const override;
    [[nodiscard]] QUrl passUrl() const override;
    [[nodiscard]] QUrl passCheckoutUrl() const override;

private:
    AccountLicenseManager m_manager;
    AccountLicenseManager::LicenseStatus m_status;
    QString m_accountId;
    bool m_checking = false;
};

} // namespace omnidict::services
