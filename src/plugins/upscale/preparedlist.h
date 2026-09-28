/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QGroupBox>

class QDBusMessage;
class QVBoxLayout;

namespace KWin
{

/**
 * The games an optional helper prepared to render smaller (see
 * org.kde.KWin.Upscale.Helper1.xml), each with a button that undoes it.
 *
 * What the helper changed outlives the effect, so undoing it has to be one
 * click away on the page where the effect is configured. The box stays hidden
 * while no helper answers or it prepared nothing, so a system without one
 * shows nothing of it.
 */
class UpscalePreparedList : public QGroupBox
{
    Q_OBJECT

public:
    explicit UpscalePreparedList(QWidget *parent = nullptr);

    /** Asks the helper again; the answer arrives later. */
    void refresh();

private:
    void fill(const QDBusMessage &reply);
    void reset(const QString &id);

    QVBoxLayout *m_rows;
};

} // namespace KWin
