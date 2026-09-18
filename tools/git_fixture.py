# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep fixture Git repositories out of the repository a check is running for."""

import os

# Git exports these when it runs a hook, and they name the repository the hook
# is running for. A `git -C <fixture>` subprocess inherits them and works on
# that repository instead of the fixture: it stages fixture files into the real
# index and its commits fail against the real hooks. In an ordinary checkout
# GIT_DIR is the relative ".git", which `-C` makes harmless; in a linked
# worktree it is absolute, so a push from one corrupts its own index.
LOCATIONS = (
    "GIT_DIR",
    "GIT_WORK_TREE",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_PREFIX",
)


def without_repository(environment: dict[str, str]) -> dict[str, str]:
    """Copy an environment with every inherited repository location removed."""
    detached = dict(environment)
    for name in LOCATIONS:
        detached.pop(name, None)
    return detached


def detach() -> None:
    """Drop those locations here, so every subprocess inherits none of them."""
    for name in LOCATIONS:
        os.environ.pop(name, None)
