#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Print one distribution's build dependencies, translated from debian/control.

    distribution-packages.py <distribution>

The output is a single line for the distribution's package manager to install.
"""

import argparse
from pathlib import Path

from distribution_packages import DISTRIBUTIONS, translate


def main() -> None:
    """Print the translated dependencies for one package manager to install."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("distribution", choices=sorted(DISTRIBUTIONS))
    arguments = parser.parse_args()
    control = Path(__file__).resolve().parents[1] / "debian" / "control"
    print(" ".join(translate(control.read_text(), arguments.distribution)))


if __name__ == "__main__":
    main()
