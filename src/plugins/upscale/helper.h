/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

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
 * The optional helper described in org.kde.KWin.Upscale.Helper1.xml.
 *
 * It is asked when a program does not render at the size wanted and cannot be
 * made to while it runs, and whether a window's program was prepared by it.
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

    void offer(EffectWindow *window, const QSize &size, const Offered &reply);
    void answer(const QString &offer, const QString &answer, const Answered &reply);
    void restart(const QString &offer);
    void present(EffectWindow *window, const QSize &wanted, const Prepared &reply);

private:
    void call(const QString &method, const QVariantList &arguments, const std::function<void(const QDBusMessage &)> &reply);
};

} // namespace KWin
