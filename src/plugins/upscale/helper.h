/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusArgument>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantList>

#include <functional>

class QDBusMessage;

namespace KWin
{

class EffectWindow;

/**
 * One screen a program is to see, as the interface counts them: where it lies in
 * the pixels X11 counts, how large, and how often it refreshes. The first of them
 * is the screen the program's window is on, at the size the effect wants.
 */
struct UpscaleProgramScreen
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int rate = 0;
};

/**
 * The optional helper described in org.kde.KWin.Upscale.Helper1.xml.
 *
 * It is asked when a program does not render at the size wanted and cannot be
 * made to while it runs, or before trying a resize for a recognized runtime,
 * and whether a window's program was prepared by it.
 * Every call is asynchronous: the compositor never waits for the helper. A
 * session without one gets no answer, which leaves every window as it would be
 * without this class.
 */
class UpscaleHelper : public QObject
{
    Q_OBJECT

public:
    using Offered = std::function<void(const QString &offer, const QString &question)>;
    using Answered = std::function<void(const QString &restart)>;
    using Prepared = std::function<void(const QSize &size)>;

    UpscaleHelper();

    void offer(EffectWindow *window, const QSize &size, const Offered &reply, bool afterFailure = true);
    void answer(const QString &offer, const QString &answer, const Answered &reply);
    void restart(const QString &offer);
    void present(EffectWindow *window, const QSize &wanted, const Prepared &reply);

private:
    void call(const QString &method, const QVariantList &arguments, const std::function<void(const QDBusMessage &)> &reply);
};

} // namespace KWin

Q_DECLARE_METATYPE(KWin::UpscaleProgramScreen)

QDBusArgument &operator<<(QDBusArgument &argument, const KWin::UpscaleProgramScreen &screen);
const QDBusArgument &operator>>(const QDBusArgument &argument, KWin::UpscaleProgramScreen &screen);
