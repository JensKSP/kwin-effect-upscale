/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "application.h"
#include "compatibility.h"
#include "effect/effecthandler.h"
#include "runtime.h"
#include "settings.h"
#include "upscaleconfig.h"
#include "utils/executable_path.h"
#include "windowidentity.h"
#include "x11geometry.h"
#include <QRegularExpression>
#include <algorithm>
#include <limits>
namespace KWin
{
namespace
{
// The processes the transport answered with a smaller screen. Bounded and in
// arrival order, because a connection need never open a window and nothing
// here would then hear that it ended; the oldest is forgotten first, long
// after the window that asked about it was presented.
constexpr int servedLimit = 128;
QHash<uint, QSize> served;
QList<uint> servedOrder;
} // namespace

void upscaleRecordServed(uint pid, const QSize &screen)
{
    if (!pid || screen.isEmpty() || served.contains(pid)) {
        return;
    }
    served.insert(pid, screen);
    servedOrder.append(pid);
    if (servedOrder.size() > servedLimit) {
        served.remove(servedOrder.takeFirst());
    }
}

bool upscaleServed(pid_t pid)
{
    return pid > 0 && served.contains(static_cast<uint>(pid));
}

QSize upscaleServedScreen(pid_t pid)
{
    return pid > 0 ? served.value(static_cast<uint>(pid)) : QSize();
}

static const UpscaleApplication *connectionApplication(const QStringList &candidates, QVariantMap &answer)
{
    for (const UpscaleApplication &application : upscaleApplications()) {
        // A separate connection identity is explicit permission to select a
        // profile before its WM_CLASS exists. Never discard a window gate.
        const UpscalePattern pattern(application.x11ConnectionExecutable, UpscaleStringMatch::RegularExpression);
        // Any candidate may carry the identity: the program a shared runtime
        // runs is the one a profile is written for, while the runtime itself
        // is what a native program is known by.
        const auto matched = [&pattern](const QString &candidate) {
            return pattern.matches(candidate);
        };
        if (!application.x11ConnectionExecutable.isEmpty() && std::ranges::any_of(candidates, matched)) {
            if (!application.enabled) {
                answer[QStringLiteral("reason")] = QStringLiteral("profile disabled");
                return nullptr;
            }
            return &application;
        }
    }
    return nullptr;
}

static QSize connectionSize(const UpscaleApplication *selected, EffectsHandler *handler, QVariantMap &answer)
{
    // Display queries precede the first window. Refuse a conflicting Off slot
    // rather than promise that a connection-wide advertisement is per-window.
    for (const UpscalePresentation presentation : {UpscalePresentation::X11FullScreen, UpscalePresentation::X11Borderless}) {
        const UpscaleMethod method = upscaleMethodFor(selected, presentation);
        if (method != UpscaleMethod::Auto && method != UpscaleMethod::X11Resize) {
            answer[QStringLiteral("reason")] = QStringLiteral("X11 presentation settings prevent early advertisement");
            return {};
        }
    }
    const UpscaleSettings settings = upscaleResolveSettings(selected);
    const auto screens = handler->screens();
    if (!settings.acts() || screens.size() != 1 || screens.first()->geometry().topLeft() != QPointF(0, 0)) {
        answer[QStringLiteral("reason")] = QStringLiteral("requires one enabled output at the desktop origin");
        return {};
    }
    const QSize pixels = screens.first()->pixelSize();
    const UpscaleSize output{pixels.width(), pixels.height()};
    const UpscaleSize desired = desiredResolution(output, settings.resolution(), settings.value(UpscaleSetting::Percentage));
    if (!exceedsMinimumPixels(output, settings.value(UpscaleSetting::MinimumPixels)) || !canUpscale(desired, output)) {
        answer[QStringLiteral("reason")] = QStringLiteral("native resolution or output below threshold");
        return {};
    }
    return QSize(desired.width, desired.height);
}

bool UpscaleIdentityService::x11PrefixMayMatch(const QString &prefix, const QStringList &candidates) const
{
    if (!UpscaleConfig::x11Proxy() || !m_handler || prefix.isEmpty()) {
        return false;
    }
    const QString identity = QStringLiteral("wine://") + prefix + QLatin1Char('/');
    for (const UpscaleApplication &application : upscaleApplications()) {
        if (!application.enabled || application.x11ConnectionExecutable.isEmpty()) {
            continue;
        }
        QVariantMap answer;
        if (!connectionSize(&application, m_handler, answer).isValid()) {
            continue;
        }
        const QRegularExpression expression(QRegularExpression::anchoredPattern(application.x11ConnectionExecutable));
        if (!expression.isValid() || expression.match(QString()).hasMatch()) {
            continue;
        }
        const QRegularExpressionMatch match = expression.match(identity, 0, QRegularExpression::PartialPreferCompleteMatch);
        const auto matches = [&expression](const QString &candidate) {
            return expression.match(candidate).hasMatch();
        };
        if (match.hasMatch() || match.hasPartialMatch() || std::ranges::any_of(candidates, matches)) {
            return true;
        }
    }
    return false;
}

QVariantMap UpscaleIdentityService::x11ConnectionPolicy(uint pid, const QStringList &candidates) const
{
    QVariantMap answer{{QStringLiteral("reason"), QStringLiteral("unidentified client")}};
    if (!UpscaleConfig::x11Proxy()) {
        answer[QStringLiteral("reason")] = QStringLiteral("proxy disabled; restart required to remove transport");
        return answer;
    }
    if (!pid || pid > static_cast<uint>(std::numeric_limits<pid_t>::max()) || !m_handler) {
        return answer;
    }
    // Without a resolved identity the peer is known only by its own
    // executable, which is what every platform can establish.
    const QStringList identities = candidates.isEmpty()
        ? QStringList{executablePathFromPid(static_cast<pid_t>(pid))}
        : candidates;
    const UpscaleApplication *selected = connectionApplication(identities, answer);
    if (!selected) {
        return answer;
    }
    answer[QStringLiteral("profile")] = selected->id;
    const QSize desired = connectionSize(selected, m_handler, answer);
    if (!desired.isValid()) {
        return answer;
    }
#if KWIN_BUILD_X11
    const QByteArray timing = upscaleX11ModeTiming(QPoint(0, 0), desired);
    if (timing.isEmpty()) {
        answer[QStringLiteral("reason")] = QStringLiteral("requested size absent from Xwayland modes");
        // The first X11 connection may arrive while KWin is still setting up
        // its Xwayland connection. Let the proxy retry within its existing
        // bounded decision interval before treating a missing mode as final.
        answer[QStringLiteral("retry")] = true;
        return answer;
    }
    answer[QStringLiteral("timing")] = timing;
#else
    answer[QStringLiteral("reason")] = QStringLiteral("X11 support unavailable");
    return answer;
#endif
    answer[QStringLiteral("width")] = desired.width();
    answer[QStringLiteral("height")] = desired.height();
    answer[QStringLiteral("reason")] = QStringLiteral("connection display advertisement");
    // This process now renders at the size wanted, so its window is presented
    // across the output instead of being asked to resize itself.
    upscaleRecordServed(pid, desired);
    return answer;
}
}
