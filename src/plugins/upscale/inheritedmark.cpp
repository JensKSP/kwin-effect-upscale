/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// How a game's page shows a value it inherits against one it states, shared by
// its settings and its methods, and kept apart from settingcontrols.cpp only
// because that file would otherwise pass the size limit.

#include "settingcontrols.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFont>
#include <QWidget>

namespace KWin
{

// Qt Designer's property editor rule, which a game's page follows: a property
// the object sets itself has its name in bold, and a reset button at the right
// end of its value, enabled only then (QtPropertyEditorDelegate::paint() and
// ResetDecorator in qttools). Here what the game states is the property set,
// and what the reset returns to is the value it inherits. On top of that, as
// Jens laid down on 2026-09-21, an inherited value is shown in italic and a
// stated one upright, so that which one a row shows is seen at a glance.
void upscaleMarkInherited(QWidget *name, QWidget *value, QWidget *reset, bool own)
{
    if (name && name != value) {
        QFont font = name->font();
        font.setBold(own);
        name->setFont(font);
    }
    if (value) {
        // A switch's text is its name and its value at once, and takes both.
        QFont font = value->font();
        font.setItalic(!own);
        if (value == name) {
            font.setBold(own);
        }
        value->setFont(font);
        // What a list offers are choices, not the inherited value, so the list
        // it opens stays upright.
        if (auto *box = qobject_cast<QComboBox *>(value); box && box->view()) {
            QFont upright = box->view()->font();
            upright.setItalic(false);
            box->view()->setFont(upright);
        }
    }
    if (reset) {
        reset->setEnabled(own);
    }
}

} // namespace KWin
