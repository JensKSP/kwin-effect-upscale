/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "pattern.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <array>

class QComboBox;
class QDBusPendingCallWatcher;
class QFormLayout;
class QLabel;
class QLineEdit;
class QWidget;

namespace KWin
{

struct UpscaleApplication;

/**
 * The fields a profile is found by: the program's path, and the window's
 * class and instance, each with how it is compared.
 *
 * The comparison is chosen the way KWin's own Window Rules offer it, beside
 * the value, with the same three names. An empty value constrains nothing,
 * which is what Window Rules calls "unimportant", so there is no fourth
 * choice.
 *
 * Below the fields it says which open windows the entry would match, as it is
 * written: the narrow and the broad extreme are both the person's to choose,
 * and seeing what an entry catches is how one chooses on purpose. The effect
 * answers, over org.kde.KWin.Effect.Upscale1, because it holds the windows
 * and the matching; without it loaded nothing is said.
 */
class UpscaleIdentityControls : public QObject
{
    Q_OBJECT

public:
    explicit UpscaleIdentityControls(QObject *parent = nullptr);

    /** Add the three rows to @p form. */
    void build(QFormLayout *form, QWidget *parent);
    void show(const UpscaleApplication &application);
    void store(UpscaleApplication &application) const;
    void setEnabled(bool enabled);

Q_SIGNALS:
    void changed();

private:
    UpscaleStringMatch matchOf(std::size_t field) const;
    /** The fields, named as the configuration file names them. */
    QVariantMap entry() const;
    void askForMatches();

    struct Field
    {
        QLineEdit *value = nullptr;
        QComboBox *match = nullptr;
    };
    // Program, window class, window instance: the order of the form.
    std::array<Field, 3> m_fields{};
    QLabel *m_matches = nullptr;
    // Asked a moment after typing stops rather than on every key, and one
    // question at a time: the answer to an older one is replaced by the next.
    QTimer m_asking;
    QPointer<QDBusPendingCallWatcher> m_query;
};

/**
 * Whether a program at @p executable is one game rather than a runtime many
 * games share.
 *
 * Proton and Wine run every game as the same loader, and an interpreter runs
 * every script as itself, so a path to one of those would match all of them.
 * Add from Window leaves the path out for such a program and states the
 * window's identity instead, which is where such a game's name is: Steam
 * gives each Proton game the window class steam_app_<id>.
 */
bool upscaleIdentifiesOneProgram(const QString &executable);

} // namespace KWin
