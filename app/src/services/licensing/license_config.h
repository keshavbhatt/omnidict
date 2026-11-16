// SPDX-FileCopyrightText: 2026 Keshav Bhatt (Ktechpit)
// SPDX-License-Identifier: LicenseRef-Ktechpit-Licensing-Module

#pragma once

#include "AccountLicenseConfig.h"

#include <QCoreApplication>

namespace omnidict::services {

/// The Ktechpit server Omnidict checks with and sells through.
inline constexpr auto kKtechpitServer = "https://ktechpit.com/USS/public/";

/// The app's AccountLicenseConfig: the shared Ktechpit server, app code OMNIDICT, checkout slug
/// omnidict, a 14-day evaluation (ADR-024). Settings follow the application name.
inline AccountLicenseConfig appLicenseConfig()
{
    const QString server = QString::fromLatin1(kKtechpitServer);
    AccountLicenseConfig cfg;
    cfg.appCode = QStringLiteral("OMNIDICT");
    cfg.appName = QStringLiteral("Omnidict");
    cfg.settingsOrgName = QCoreApplication::organizationName();
    cfg.settingsAppName = QCoreApplication::applicationName();
    cfg.checkStatusEndpoint = server + QStringLiteral("check/status.php");
    cfg.checkoutUrlTemplate = server + QStringLiteral("checkout.php?slug=omnidict&accountId=%1");
    cfg.selfServicePortalUrl = server + QStringLiteral("user/");
    cfg.evaluationDurationSecs = 14LL * 24LL * 60LL * 60LL;
    return cfg;
}

} // namespace omnidict::services
