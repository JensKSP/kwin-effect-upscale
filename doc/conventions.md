<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Code conventions

These are the rules for the code in this repository. They complete the
[repository rules](../AGENTS.md) and bind in the same way. Read them before
editing C++, GLSL, CMake or Python, and before adding a source file.

## Style

- KDE Frameworks Coding Style with KWin's `.clang-format`: four spaces, braces
  always, `m_`/`s_` prefixes, no abbreviations, grouped and sorted includes.
- KWin's own conventions: `auto` only to avoid repeating a type and for
  iterators; avoid `QRect::right()`/`bottom()`. They come from KWin's
  `doc/coding-conventions.md`, which is still in the supported target v6.3.6 but
  was dropped from master on 2026-07-22 as "kind of outdated". KWin's
  `CONTRIBUTING.md` still links to it. Take the rules from the tagged file, and
  take anything newer from the code.
- C++23. Qt built without keywords (`QT_NO_KEYWORDS` and the other Qt
  definitions KWin sets).
- Licensing: own code is `GPL-2.0-or-later`, third-party shaders keep their own
  licence. REUSE compliant: an SPDX header in every file, licence texts in
  `LICENSES/`.
- Commit messages follow KWin's `CONTRIBUTING.md`; see
  [commits, branches and pull requests](pull-requests.md).

## The plugin folder stays upstreamable

The [repository rule](../AGENTS.md#the-plugin-folder-stays-upstreamable) keeps
`src/plugins/upscale/` a folder KDE could copy into KWin unchanged, with
everything project-specific outside it. [Building and checking](checks.md#what-may-touch-the-plugin-folder)
says which formatters and configuration files may reach into that folder.

**Why:** the goal is that upstreaming is a copy, not a port.

## The code runs wherever KDE runs

Laid down by Jens, 2026-09-17.

- **Write portable code.** The effect has to build and run on every platform KDE
  supports, not only on the machine or the distribution it is developed on. That
  means Linux on amd64 and on arm64, and it means the BSDs, which KWin supports
  and KDE's own CI builds for.
- No Linux-only interfaces where Qt, KWin or POSIX offer the same thing: no
  `/proc`, no `/sys`, no Linux-specific system calls, no glibc extensions, no
  `#include <linux/...>`.
- No assumption about word size, endianness, or that a pointer is 64 bits. On
  arm64 `char` is unsigned; write code that does not care.
- Graphics goes through KWin's own abstractions and through libepoxy, never
  through a driver or a windowing system directly. Shaders stay GLSL ES
  compatible, because that is what KWin's OpenGL ES backend compiles them as.
- Paths, processes and time come from Qt, not from the shell.

**Why:** an effect that only works on the author's distribution cannot be
upstreamed, and arm64 is not hypothetical here - packages are built for it.

## How big a file may get

- **400 code lines is the limit, 300 raises a warning.** Comments, blank lines and
  the licence header do not count, so documenting a function never costs budget.
  `tools/check-file-size.py` measures it; KDE has no rule of this kind, so this
  one is ours.
- Its counterpart is `readability-function-size` in `.clang-tidy`: a file can sit
  at 399 lines and still hold one function nobody can follow.
- When a file has to exceed the limit, split it. If it truly cannot be split, say
  why in the commit message rather than raising the number.
