<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Building and checking

These rules say where a build runs, which checks exist and when each one runs,
and what may be reported as passed. They complete the
[repository rules](../AGENTS.md) and bind in the same way. Read them before
building, before running checks or calling anything built, tested or done, and
before touching `.pre-commit-config.yaml`, `.clang-tidy`, `containers/`,
`tools/check-*.py` or a workflow.
[Contributing](../CONTRIBUTING.md#building-and-checking) has the commands for
building the container images and running each check group, and the handbook
describes the [complete check matrix](upscaling.md#build-and-release-pipeline).

## Agents build in the containers

Laid down by Jens, 2026-09-17.

- **Build and check in the containers under `containers/`**, not against
  whatever happens to be installed on the machine the agent runs on.
  `containers/trixie` is the minimum supported environment (KWin 6.3.6),
  `containers/neon-unstable` tracks KWin master. CI builds Trixie on every push
  and leaves KWin master to the nightly, because master is not this repository's
  to keep green; an agent still has to build in both before calling a change
  built.
- A change counts as built once it builds in both containers, with GCC and with
  Clang, warnings as errors.
- **Build natively when the effect has to run.** A container has no session, no
  output and no TV. Runtime checks against KWin's virtual backend happen in the
  container; running the effect in a real session happens in a native build on
  wzpc, and so does acceptance.
- **In-source builds are forbidden.** One build directory beside the sources,
  named `build`, and nothing else: no generated files, no build artefacts, no
  `compile_commands.json`, no editor caches anywhere in the source tree. The
  top-level `CMakeLists.txt` refuses an in-source configure, and `build/` is in
  `.gitignore`.

**Why:** the supported target is the KWin that Debian Trixie ships, not the one
on the machine an agent happens to sit on, and a warning only one of the two
compilers emits is still a warning. Generated files in the tree end up in a
commit sooner or later, and what this repository contains has to be the plugin
and nothing else.

## One configuration defines every check

Laid down by Jens, 2026-09-17.

- **Run both pre-commit stages:** `pre-commit run --all-files` and
  `pre-commit run --all-files --hook-stage pre-push`. The default stage alone
  omits whole-tree checks, strict type checking and regression tests. Inside
  the maintained container, `python3 -B tools/run-checks.py lint` runs both with
  one command, as CI does. Install both hooks once with
  `pre-commit install --hook-type pre-commit --hook-type pre-push`.
- For a documentation-only change, the maintained container also supports
  `python3 -B tools/run-checks.py docs --base <base-commit>`. It verifies the
  changed paths before using native pre-commit file filtering for both stages.
  CI uses this path for proven documentation-only PRs and master pushes; code,
  unknown inputs, nightly, release and manual full runs retain full validation.
- `.pre-commit-config.yaml` is the only list of checks. Do not add a linter to CI
  that is not in it, and do not add a check that CI cannot run.

### Which check runs when

Four levels, each doing what the one before it could not afford. A check belongs
at the earliest level that can carry it, and moves up only when it cannot.

| Level | What runs | Why there |
| --- | --- | --- |
| commit | formatters and linters, on the changed files | ~1s, and they fix rather than complain |
| push | whole-tree checks and the regression tests | they scale with the repository, not with the commit |
| pull request | both of the above over the whole tree, plus a build with GCC and with Clang, an arm64 GCC build, clang-tidy and the plugin metadata schema | needs a toolchain and KDE Frameworks installed |
| nightly | every package: two Debian-family distributions on two architectures, Fedora, openSUSE, Arch and FreeBSD, each installed and tested afterwards, and a build against KWin master | expensive, or a moving target nobody pushing can be blamed for |

- A commit hook that takes noticeable time gets skipped with `--no-verify`, and a
  check that is skipped is not a check. Keep the commit level to what a commit
  can actually break.
- Anything scanning the whole tree - `reuse lint`, the agent leftover rule -
  belongs at push or later. The leftover rule runs at both: the staged files on
  commit, everything on push, because `--no-verify` exists.
- A build against KWin master never gates a pull request. It tracks something
  outside this repository, so its failures are not the author's, and a day is
  soon enough to hear about them.
- Hook versions are pinned. clang-format is pinned to **19** because that is what
  Trixie ships; a different major version formats differently and would put the
  hook, the container and CI at odds.
- clang-tidy is not in the hook. It needs a configured build for its
  `compile_commands.json`, so it runs in the container and in CI:
  `clang-tidy -p build src/plugins/upscale/*.cpp`. Its config is static analysis
  and naming, never formatting; formatting is clang-format's alone.
- The file size limit and its check are described with the
  [code conventions](conventions.md#how-big-a-file-may-get).

### What may touch the plugin folder

- A formatter runs inside `src/plugins/upscale/` only if its output is what KWin
  itself writes. clang-format qualifies, because `.clang-format` is KWin's own
  file. **gersemi does not** and is excluded there: it rewrites KWin's CMake
  style, collapsing source lists onto one line and breaking
  `target_link_libraries(target PRIVATE` apart. Checked against KWin's
  `src/plugins/blur/CMakeLists.txt`, not assumed.
- `.clang-tidy` lives at the top level and nowhere else. KWin has no
  `.clang-tidy`; a config file KWin does not have must not travel upstream with
  the plugin folder. For the same reason every check enabled in it has to agree
  with code KWin already writes — a check that would flag KWin's own effects is
  one we switch off, not a style we adopt.

**Why:** a check nobody can run locally is a check that fails in CI, and a second
list of linters somewhere else is a list that drifts.

## Building for the developer to try is not a release

Laid down by Jens, 2026-09-18.

- **When the point is to put the current state in front of Jens**, build,
  install, and run only the checks that the change itself calls for. A focused
  test of what was touched is welcome; the full suite is not. He is waiting at
  a keyboard, and the minutes it takes are his.
- The full obligation — both pre-commit stages, both containers, both
  compilers, clang-tidy, the whole test suite — belongs to work that is being
  finished: before a commit, before a pull request, and before anything is
  called done. Say which of the two a run was, so that "it passes" is never
  read as more than it is.
- Nothing here weakens the rule to
  [never guess a result](../AGENTS.md#never-guess-a-result). A check that did
  not run is reported as not run, never as passed.
