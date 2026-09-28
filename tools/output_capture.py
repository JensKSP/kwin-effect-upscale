# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pictures of what a running KWin shows, and how two of them compare.

KWin's ScreenShot2 interface hands a picture over through a pipe it is given,
which only a D-Bus binding that passes file descriptors can do, hence
python3-dbus. The session has to allow it: KWin refuses programs without a
desktop file that grants the interface, unless
KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 is set for the compositor, as a test
session does. Only the game test image carries dbus, numpy and Pillow.
"""

from __future__ import annotations

import os
from typing import Any

import dbus  # type: ignore[import-not-found]
import numpy as np
from PIL import Image

# QImage formats KWin hands over, and where red, green and blue lie in each
# pixel's four bytes. RGB32 and the ARGB32 kinds are native-endian words, which
# on a little-endian machine lie in memory as blue, green, red, alpha.
CHANNELS = {
    4: (2, 1, 0) if np.little_endian else (1, 2, 3),
    5: (2, 1, 0) if np.little_endian else (1, 2, 3),
    6: (2, 1, 0) if np.little_endian else (1, 2, 3),
    16: (0, 1, 2),
    17: (0, 1, 2),
    18: (0, 1, 2),
}


def capture(
    method: str, *arguments: str, timeout: int = 120
) -> np.ndarray[Any, np.dtype[np.uint8]]:
    """Take one picture through ScreenShot2 and return its red, green and blue."""
    bus = dbus.SessionBus()
    screenshot = dbus.Interface(
        bus.get_object("org.kde.KWin", "/org/kde/KWin/ScreenShot2"), "org.kde.KWin.ScreenShot2"
    )
    reading, writing = os.pipe()
    try:
        options = dbus.Dictionary(
            {"include-cursor": False, "native-resolution": True}, signature="sv"
        )
        # A picture waits for KWin's next frame, which llvmpipe may take a
        # while to draw at 3840 x 2160; far beyond D-Bus's default 25 seconds
        # is a compositor that is not drawing.
        call = getattr(screenshot, method)
        results = call(*arguments, options, dbus.types.UnixFd(writing), timeout=timeout)
    finally:
        # KWin has its own copy; the pipe ends when KWin has written it all.
        os.close(writing)
    with os.fdopen(reading, "rb") as stream:
        data = stream.read()
    width, height, stride = int(results["width"]), int(results["height"]), int(results["stride"])
    order = CHANNELS.get(int(results["format"]))
    if order is None or len(data) < stride * height:
        message = f"unexpected picture: format {int(results['format'])}, {len(data)} bytes"
        raise ValueError(message)
    rows = np.frombuffer(data, dtype=np.uint8, count=stride * height).reshape(height, stride)
    return rows[:, : width * 4].reshape(height, width, 4)[:, :, list(order)].copy()


def save(picture: np.ndarray[Any, np.dtype[np.uint8]], path: str) -> None:
    """Keep a picture for a person to look at."""
    Image.fromarray(picture, "RGB").save(path)


def enlarged(
    picture: np.ndarray[Any, np.dtype[np.uint8]], part: tuple[int, int], size: tuple[int, int]
) -> np.ndarray[Any, np.dtype[np.uint8]]:
    """Enlarge the top left part of a picture to a size, bilinearly, as a plain stretch does."""
    cropped = Image.fromarray(picture[: part[1], : part[0]], "RGB")
    return np.asarray(cropped.resize(size, Image.Resampling.BILINEAR), dtype=np.uint8)


def sharpness(picture: np.ndarray[Any, np.dtype[np.uint8]]) -> float:
    """Measure detail: the mean size of the Laplacian over the picture's light."""
    light = picture.astype(np.float64) @ np.array([0.299, 0.587, 0.114])
    laplacian = (
        4 * light[1:-1, 1:-1]
        - light[:-2, 1:-1]
        - light[2:, 1:-1]
        - light[1:-1, :-2]
        - light[1:-1, 2:]
    )
    return float(np.abs(laplacian).mean())


def likeness(
    first: np.ndarray[Any, np.dtype[np.uint8]], second: np.ndarray[Any, np.dtype[np.uint8]]
) -> float:
    """Compare what two pictures show, not their detail: mean difference of thumbnails.

    Both are shrunk to 64 by 36 first, so that sharpening and one frame's
    motion hardly count, while a picture in the wrong place, the wrong way up
    or in the wrong colours counts fully. 0 is the same, 255 the opposite.
    """
    size = (64, 36)
    small = [
        np.asarray(Image.fromarray(picture, "RGB").resize(size, Image.Resampling.BOX), np.float64)
        for picture in (first, second)
    ]
    return float(np.abs(small[0] - small[1]).mean())
