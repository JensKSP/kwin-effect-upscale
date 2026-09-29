/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "upscale_config.h"
#include "upscaleconfig.h"
#include <KConfigGroup>
#include <KLocalizedString>
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QTimer>
namespace KWin
{
void UpscaleEffectConfig::addProxyControls(QFormLayout *layout)
{
    m_x11Proxy = new QCheckBox(i18n("Enable the X11 proxy at login"), widget());
    m_x11Proxy->setObjectName(QStringLiteral("x11Proxy"));
    m_x11Proxy->setToolTip(i18n("Tell recognized X11 games the smaller screen before they create a window. "
                                "Switching this takes effect at the next login."));
    m_proxyStatus = new QLabel(widget());
    m_proxyStatus->setWordWrap(true);
    m_proxyStatus->setObjectName(QStringLiteral("x11ProxyStatus"));
    layout->addRow(m_x11Proxy);
    layout->addRow(m_proxyStatus);
    connect(m_x11Proxy, &QCheckBox::toggled, this, [this]() {
        updateProxyStatus();
        setNeedsSave(true);
    });
    // The overall Desktop Effects switch belongs to KWin's enclosing page.
    // Refresh its saved state too, without changing this page's pending edits.
    auto *timer = new QTimer(this);
    timer->setInterval(2000);
    connect(timer, &QTimer::timeout, this, &UpscaleEffectConfig::updateProxyStatus);
    timer->start();
}
void UpscaleEffectConfig::updateProxyStatus()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->reparseConfiguration();
    const bool effectEnabled = KConfigGroup(config, QStringLiteral("Plugins")).readEntry("upscaleEnabled", true);
    const bool desired = effectEnabled && m_x11Proxy->isChecked();
    const QString session = qEnvironmentVariable("UPSCALE_X11_SESSION_ROUTED");
    if (session.isEmpty()) {
        m_proxyStatus->setText(desired ? i18n("Log out required: apply, then log out and back in to use the X11 proxy.")
                                       : i18n("X11 routing is disabled for the next login."));
    } else if ((session == QLatin1String("1")) != desired) {
        m_proxyStatus->setText(desired ? i18n("Log out required: apply, then log out and back in to use the X11 proxy.")
                                       : i18n("Log out required: apply, then log out and back in to stop using the X11 proxy. "
                                              "X11 programs already running keep using it until then."));
    } else {
        m_proxyStatus->setText(desired ? i18n("This session was configured to use the X11 proxy.")
                                       : i18n("This session uses Xwayland directly, without the proxy."));
    }
}
}
