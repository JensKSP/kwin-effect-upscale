# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

# Every invocation describes this build, even when the sources are unchanged.
# CMake honours SOURCE_DATE_EPOCH for reproducible package timestamps.
string(TIMESTAMP UPSCALE_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
