<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Contributing

The effect is experimental and does not yet have verified working upscaling on
real hardware. Read the [current state](README.md#state) before testing it.
Reports, documentation improvements and patches are welcome through
[GitHub issues and pull requests](https://github.com/JensKSP/kwin-effect-upscale).
Search existing issues first and link related reports rather than duplicating them.

## Reporting a problem

Use the rendering or build/install form, according to where the failure occurs.
Report the exact candidate, reproduction steps, expected result and actual
result. A successful build or a loaded effect is not proof that it scaled a
frame. Say when a value is unknown; do not infer an effective setting from a
requested one.

| Information | Where to obtain it |
| --- | --- |
| Running plugin version and revision | The [startup log](README.md#which-build-am-i-running) identifies the loaded binary. Include its complete version and any dirty suffix. A source checkout's `git rev-parse HEAD` identifies only that checkout. |
| Distribution, architecture, Plasma and Qt | Plasma's Info Center, About this System. For a build in a container, also name its Containerfile and image. |
| KWin, GPU, graphics driver and backend | KWin's support information, using the command below. On systems where it is unavailable, copy the corresponding Info Center entries and say what is missing. |
| Output resolution, refresh rate, scale, HDR and VRR | System Settings, Display Configuration. Name the display model and connection. Distinguish enabled settings from observed behavior. |
| Input size, application and scaling mode | The application's settings and version, its Wayland/Xwayland backend, and the effect's settings. Record actual buffer size only if measured. |
| Build/install failure | Exact commands, source revision, compiler version and the first failing diagnostic with surrounding context. Attach the relevant build or package log. |

[KDE's debugging guide](https://community.kde.org/KWin/Debugging) describes
KWin's support information. Run its read-only query in the affected session:

```sh
qdbus-qt6 org.kde.KWin /KWin supportInformation
```

Some distributions name the Qt 6 utility `qdbus6` or install it as
`/usr/lib/qt6/bin/qdbus`. For systemd sessions, a bounded log excerpt is:

```sh
journalctl --user -b -u plasma-kwin_wayland --since '10 minutes ago'
```

On other systems, use the session's KWin log. Review logs and screenshots before
posting: remove credentials, unrelated window titles, usernames, private paths
and personal display identifiers. Keep versions, error messages and relevant
timing. Do not attach a complete environment dump or unrelated session history.

Use the hardware acceptance form to record tests against a specific candidate.
Follow the handbook's [validation requirements](doc/upscaling.md#validation-requirements)
and [benchmark comparisons](doc/upscaling.md#benchmark-performance-comparisons).
Record pass, fail, not tested or not applicable for each condition, with evidence.
One successful SDR test does not establish HDR or VRR support.

## Building and checking

Build dependencies are maintained in `debian/control`. Use the maintained
container definitions to reproduce CI; a native installation is needed only
for testing in a real desktop session. From a regular repository checkout:

```sh
podman build --pull -t upscale-check:trixie -f containers/trixie/Containerfile .
podman run --rm -v "$PWD:/src" -w /src upscale-check:trixie \
    python3 -B tools/run-checks.py lint
podman run --rm -v "$PWD:/src" -w /src upscale-check:trixie \
    python3 -B tools/run-checks.py gcc
podman run --rm -v "$PWD:/src" -w /src upscale-check:trixie \
    python3 -B tools/run-checks.py clang
```

Docker can run the same commands in place of Podman. Build outputs and check
caches stay under `build/`. For KWin master compatibility, build the
`containers/neon-unstable/Containerfile` image and run both compiler modes there
as well. The handbook describes the [complete check matrix and native resource
limits](doc/upscaling.md#build-and-release-pipeline), including the extra container
permission required for ThreadSanitizer. Ninja and the other tools schedule
their own workers; do not wrap independent full-machine builds in another
parallel launcher.

`.pre-commit-config.yaml` is the single check configuration. With pre-commit
installed (for example, `pipx install pre-commit==4.6.2`), install both hooks
once, then run both stages before submitting:

```sh
pre-commit install --hook-type pre-commit --hook-type pre-push
pre-commit run --all-files
pre-commit run --all-files --hook-stage pre-push
```

The container's `lint` mode runs both stages. For a Markdown-only change, its
`docs --base <base-commit>` mode runs the applicable hooks and rejects mixed
code changes. CI uses the same entry points. Source changes require the compiler
and relevant runtime checks; hardware claims require observed native results.

## Submitting a patch

Keep changes focused and explain the problem, resulting behavior and observed
validation. Link an existing issue when relevant; opening one is not mandatory.
Record failed or unperformed checks honestly. Tests should cover behavior and
meaningful failure boundaries rather than repeat the implementation.

Follow [KWin's contribution conventions](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/CONTRIBUTING.md)
and KDE Frameworks style. Commit subjects normally use `component: Do a thing`.
Keep `src/plugins/upscale/` suitable for copying into KWin; project-specific
packaging and tooling belong outside it. Own files use GPL-2.0-or-later SPDX
headers; preserve third-party notices and keep REUSE checks passing.

Watch CI and review feedback after each push. Investigate findings, fix valid
issues and explain disagreements with evidence. Both `Quality gate` and
`CodeRabbit approval` must pass for the current revision. The maintainer reviews
contributions and chooses when to merge or enable auto-merge; passing automation
does not authorize an autonomous merge. See the handbook's
[review policy](doc/upscaling.md#pull-request-reviews).
