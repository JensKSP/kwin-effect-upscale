/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A process that stays alive until its standard input closes or it is killed:
// the running game the Wine screen helper's test gives an environment and
// looks for. It stands in for sleep, which the tests avoid; a game with a
// window of its own is x11_game_standin.cpp.

#include <cstdio>

int main()
{
    while (std::getchar() != EOF) {
    }
    return 0;
}
