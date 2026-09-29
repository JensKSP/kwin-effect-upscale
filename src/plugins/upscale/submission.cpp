/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Copy Report: KWin picks the window, the effect says what it observed of it,
// and the page turns that into the text a person sends with a new entry.

#include "submission.h"

#include "identitycontrols.h"
#include "upscale_config.h"

#include <KLocalizedString>

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QMessageBox>
#include <QSysInfo>

namespace KWin
{

static QString shownSize(QString size)
{
    return size.replace(QLatin1Char('x'), QStringLiteral(" x "));
}

// What the method in force told or asked the program, and how that ended.
static QString methodLine(const QVariantMap &facts)
{
    const auto fact = [&facts](const char *key) {
        return facts.value(QLatin1String(key)).toString();
    };
    QString line = QStringLiteral("# Method: ") + fact("method");
    if (!fact("advertised").isEmpty()) {
        line += QStringLiteral("; told ") + shownSize(fact("advertised"));
    }
    if (!fact("requested").isEmpty()) {
        line += QStringLiteral("; asked for ") + shownSize(fact("requested"));
    }
    if (const double scale = facts.value(QStringLiteral("scaleRequested")).toDouble(); scale > 0) {
        line += QStringLiteral("; asked for a surface scale of %1").arg(scale);
    }
    if (!fact("requestFailure").isEmpty()) {
        line += QStringLiteral("; failed: ") + fact("requestFailure");
    }
    return line;
}

QString upscaleSubmissionReport(const QVariantMap &facts, const QString &system)
{
    const auto fact = [&facts](const char *key) {
        return facts.value(QLatin1String(key)).toString();
    };
    QStringList lines{
        QStringLiteral("# An application entry for KWin Upscale, as the effect observed the window."),
        QStringLiteral("# Replace every <...> and send it with the project's application form."),
        QStringLiteral("[Application-<name>]"),
        QStringLiteral("Name=<the application's name>"),
        QStringLiteral("MeasuredVersion=<the version you ran>"),
    };
    // A runtime many programs share names none of them, and the window's
    // identity is what names the game, as Add from Window does it.
    const QString executable = fact("executable");
    if (upscaleIdentifiesOneProgram(executable)) {
        lines << QStringLiteral("Executable=") + upscalePortableExecutable(executable)
              << QStringLiteral("ExecutableMatch=RegularExpression");
    } else if (executable.isEmpty()) {
        lines << QStringLiteral("# Program: not known for this window.");
    } else {
        lines << QStringLiteral("# Program: a runtime many programs share; the window names this one.");
    }
    if (!fact("windowClass").isEmpty()) {
        lines << QStringLiteral("WindowClass=") + fact("windowClass");
    }
    if (!fact("instance").isEmpty()) {
        lines << QStringLiteral("Instance=") + fact("instance");
    }
    lines << fact("presentation") + QLatin1Char('=') + fact("method")
          << QStringLiteral("Note=<one or two sentences: what the application follows, and what it refuses>")
          << QString()
          << QStringLiteral("# Observed:")
          << QStringLiteral("# Window system: ")
            + (facts.value(QStringLiteral("x11")).toBool() ? QStringLiteral("X11, through Xwayland") : QStringLiteral("Wayland"))
          << methodLine(facts)
          << QStringLiteral("# Buffer: %1 supplied, for an output of %2 pixels at scale %3")
                 .arg(shownSize(fact("supplied")), shownSize(fact("destination")), fact("outputScale"))
          << QStringLiteral("# Effect: %1; KWin %2; %3").arg(fact("build"), fact("kwin"), fact("graphics"))
          << QStringLiteral("# Graphics: %1; %2").arg(fact("renderer"), fact("driver"))
          << QStringLiteral("# System: ") + system
          << QStringLiteral("# Started by: <how you started it: Steam, a launcher, a terminal>")
          << QStringLiteral("# The image covers the screen: <yes or no>")
          << QStringLiteral("# The pointer lands where it looks: <yes or no>")
          << QStringLiteral("# Frame times before and after: <optional, from the heads-up display>");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

void UpscaleEffectConfig::copyReport()
{
    if (m_reporting) {
        return;
    }
    m_reporting = true;
    const QDBusMessage pick = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                                             QStringLiteral("org.kde.KWin"), QStringLiteral("queryWindowInfo"));
    auto *picked = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(pick, 60000), this);
    connect(picked, &QDBusPendingCallWatcher::finished, this, [this, picked]() {
        const QDBusPendingReply<QVariantMap> window = *picked;
        picked->deleteLater();
        if (!window.isValid()) {
            // Cancelled, or KWin could not pick: nothing to say either way.
            m_reporting = false;
            return;
        }
        askForReport(window.value().value(QStringLiteral("uuid")).toString());
    });
}

void UpscaleEffectConfig::askForReport(const QString &window)
{
    QDBusMessage ask = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                                                      QStringLiteral("org.kde.KWin.Effect.Upscale1"), QStringLiteral("reportFacts"));
    ask << window;
    auto *answered = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(ask), this);
    connect(answered, &QDBusPendingCallWatcher::finished, this, [this, answered]() {
        const QDBusPendingReply<QVariantMap> facts = *answered;
        answered->deleteLater();
        m_reporting = false;
        if (!facts.isValid() || facts.value().isEmpty()) {
            QMessageBox::warning(widget(), i18n("Copy Report"),
                                 i18n("The upscaler observed nothing of that window. It has to be running and the window open."));
            return;
        }
        QGuiApplication::clipboard()->setText(upscaleSubmissionReport(facts.value(), QSysInfo::prettyProductName()));
        QMessageBox::information(widget(), i18n("Copy Report"),
                                 i18n("The report is on the clipboard. Fill in the marked lines and send it with the "
                                      "application form."));
    });
    // The watcher belongs to this page, its parent, and deletes itself once
    // the reply is in. The static analyzer does not model a QObject parent and
    // reports it as leaked at this brace.
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

} // namespace KWin
