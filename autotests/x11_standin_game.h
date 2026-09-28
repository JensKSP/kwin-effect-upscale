/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QProcess>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

// A game window made by a process of its own, as x11_game_standin.cpp reports
// it: a test starts it under a Wine loader's name, or as itself. That process
// owns the X11 connection, which is how KWin 6.6 tells whose window it is, and
// names itself in _NET_WM_PID, which is how 6.3 did, unless it is anonymous.
class StandInGame
{
public:
    StandInGame(const QString &program, const QString &when, const QSize &size, bool anonymous = false,
                const QString &windowClass = QStringLiteral("upscale-x11-test"))
    {
        QStringList arguments{windowClass, QString::number(size.width()), QString::number(size.height()), when};
        if (anonymous) {
            arguments.append(QStringLiteral("anonymous"));
        }
        m_process.start(program, arguments);
    }
    ~StandInGame()
    {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(5000)) {
            m_process.kill();
            m_process.waitForFinished();
        }
    }
    bool started()
    {
        return m_process.waitForStarted();
    }
    qint64 processId() const
    {
        return m_process.processId();
    }
    bool isFullscreen()
    {
        read();
        return m_fullscreen;
    }
    QRect geometry()
    {
        read();
        return m_geometry;
    }
    QList<QSize> configuredSizes()
    {
        read();
        return m_configured;
    }
    QSize sizeAtMapping()
    {
        read();
        return m_sizeAtMapping;
    }
    int closeRequests()
    {
        read();
        return m_closes;
    }

private:
    void read()
    {
        while (m_process.canReadLine()) {
            const QList<QByteArray> fields = m_process.readLine().simplified().split(' ');
            if (fields.size() == 2 && fields.first() == "fullscreen") {
                m_fullscreen = fields.at(1) == "1";
            } else if (fields.size() == 5 && fields.first() == "geometry") {
                m_geometry = QRect(fields.at(1).toInt(), fields.at(2).toInt(), fields.at(3).toInt(), fields.at(4).toInt());
            } else if (fields.size() == 3 && fields.first() == "configured") {
                m_configured.append(QSize(fields.at(1).toInt(), fields.at(2).toInt()));
            } else if (fields.size() == 3 && fields.first() == "mapped") {
                m_sizeAtMapping = QSize(fields.at(1).toInt(), fields.at(2).toInt());
            } else if (fields.first() == "close") {
                ++m_closes;
            }
        }
    }

    QProcess m_process;
    bool m_fullscreen = false;
    QRect m_geometry;
    QList<QSize> m_configured;
    QSize m_sizeAtMapping;
    int m_closes = 0;
};
