<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: every package in a standard installation of its system

## Start and end state

Asked for by Jens on 2026-09-28 (item 2f of the open list): install every
package in a standard installation of every system it is built for, and show
that the effect works without further configuration. The nightly builds the
packages and tests each in a clean container of its distribution, and FreeBSD
in the machine that built it, emptied first: the package installs, both
plugins load, and it is reinstalled and removed. No run had ever logged in to a
Plasma session with a package installed, started a game in it or seen the X11
proxy take over a session's Xwayland; that was left to Jens's own machine.

The end state is a machine per system and architecture, each the system's own
cloud image with the Plasma desktop its installer offers, logged in by SDDM to
Plasma's Wayland session, in which one command installs the package with the
system's package manager, logs in again, checks what the package is for and
removes it again, and reports each step.

## Scope, dependencies and gates

Agreed with Jens on 2026-09-28: Debian Trixie, Kubuntu 26.04, Fedora and
openSUSE Tumbleweed on amd64 and arm64, Arch on amd64 and FreeBSD on amd64.
OpenGL comes from Mesa's llvmpipe in the guest, never from the host's GPU; amd64
runs under KVM and arm64 under full system emulation before releases, since
binfmt boots no system. The machines are made by `tools/virtual_machine.py`,
which the [conformance machine](slice-wayland-conformance.md) shares.

Excluded: hardware (the physical checks stay in their own slices), the games'
own behaviour beyond what the effect does with them, and upgrades from an
earlier published version, which the pipeline owns.

**Supported scope.** Debian Trixie on amd64, the supported target, passes every
step with the package the nightly or a release builds.

**Full acceptance.** Every system and architecture above passes every step.

## Approach

`tools/package-vm.py <system>` makes the machine from the system's cloud image
and a cloud-init template under `containers/vm-host` (`plasma-debian.in`,
`plasma-kubuntu.in`, `plasma-fedora.in`, `plasma-opensuse.in`,
`plasma-arch.in`), each image checked against the sums file its distribution
publishes beside it, with a 3840 × 2160 screen on QEMU's VGA, or virtio-gpu on
arm64, since the effect acts from 1920 × 1080 up by default. `check <package>` copies
the package into the machine and runs `tools/package_check.py` in the guest as
root, which finds the system's package manager and checks, in order:

1. a Plasma Wayland session before installing;
2. the package installed with the system's package manager;
3. a new session after logging out and in again, the way a logout does it;
4. the effect loaded and supported, with nothing of it configured;
5. the settings module opening;
6. X11 routed through the session proxy, and the proxy started;
7. SuperTuxKart on native Wayland, set up as a player at a 4K screen has it,
   drawn smaller and enlarged by the effect, through its shipped entry;
8. Extreme Tux Racer's X11 connection answered by the proxy through its
   shipped entry;
9. the package removed with the package manager, and KWin still running.

A step whose premise failed is reported as not run.

## Progress

Debian Trixie amd64, 2026-09-29, developed on a machine that was reset between
runs. Found on the way, each by running it:

- `loginctl terminate-user` ends the check's own SSH session with the Plasma
  one, and ending only SDDM's session left SDDM saying its helper had crashed,
  after which it logged nobody in again.
- Plasma runs as units of the user's service manager, which the SSH session
  keeps alive, so a new SDDM login met the old KWin. The check now does what a
  logout does: SDDM stops, the user's service manager restarts, SDDM starts.
- Debian puts games in `/usr/games`, which root's PATH lacks; programs start
  with the environment the session publishes, as from its launcher.
- A fresh SuperTuxKart configuration draws 1024 × 768 in fullscreen, which the
  effect rightly leaves alone as under half the screen; the check sets the game
  up at 3840 × 2160, the effect not at all.
- Plasma chose a scale of 1.05 for the 4K screen from its EDID, and the check
  leaves it, since that is what a standard installation does.

With the nightly's package of `5c117bd`, all nine steps passed: SuperTuxKart
supplied 2560 × 1440, enlarged to 3840 × 2160, and Extreme Tux Racer's
connection was told 2560 × 1440 by the proxy.

**Supported scope met, 2026-09-29:** on a machine made afresh with
`create --replace`, the check passed all nine steps again with the same
package. The effect was loaded and supported after logging in again with
nothing configured, and the settings module opened.

Kubuntu 26.04 amd64, 2026-09-29, on a machine made afresh from Ubuntu's
`resolute` cloud image with `kubuntu-desktop`, Plasma 6.6.6 and KWin 6.6.6. With
the nightly's package of `d801766`, eight steps passed and SuperTuxKart was
never enlarged. Plasma chose a scale of 2.7 for the same 4K screen, at which the
output is 1422.22 logical pixels wide; the game's fullscreen window can be 1422
at most, which KWin places at 3839 device pixels of 3840. The effect accepts a
window within one device pixel of its output's edges, and this is one pixel
exactly, but the arithmetic returned 1.0000000000002 and the effect refused the
window as not covering its output. The same hair over one pixel comes out at
many common scales (1.35, 1.4, 1.55, 1.8, 2.25, 3.0 on common sizes), so this is
a defect of the effect, not of the machine: the comparison now allows a
millionth of a pixel for floating point, and `upscale-resolution-test` checks
it at 2.7, 1.35 and 1.8. With a package built from that tree, the same machine
passed all nine steps: SuperTuxKart supplied 2560 × 1440, enlarged to
3840 × 2160, and Extreme Tux Racer was told 2560 × 1440 by the proxy.

The machine also locks its screen after five idle minutes, as Plasma does by
default, and nothing moves the pointer in it; the effect then reports itself
inactive, which a game started by hand an hour after the check showed. The
check's own login starts the idle time afresh, and in the first run
SuperTuxKart started 23 seconds after it.

Fedora 43, openSUSE Tumbleweed and Arch have a template and a profile since
2026-09-29, and their machines came up logged in to Plasma's Wayland session.
All three passed the nine steps the same day with nightly 36545995686's
packages of `7ff0fe6`: SuperTuxKart drawn at 2560 × 1440 and enlarged on
Wayland in each, the X11 game answered by the proxy through its shipped
entry (Extreme Tux Racer on Fedora; SuperTuxKart on openSUSE and Arch), and
KWin still running after the package was removed.

arm64 has profiles for Debian, Kubuntu, Fedora and openSUSE since 2026-09-29:
the amd64 profile with the system's arm64 image, `qemu-system-aarch64` on the
virt machine with every host core translating, UEFI from Debian's
`qemu-efi-aarch64`, and virtio-gpu at 3840 × 2160 in place of VGA, which the
virt machine lacks; openSUSE's template loads virtio_gpu there instead of
bochs. Arch publishes no arm64 image. The Debian arm64 machine finished its
first boot on 2026-09-29 in 38 minutes under emulation, logged in to Plasma's
Wayland session with KWin running, and virtio-gpu offering 3840 × 2160. Its check passed all nine steps the same day
with the nightly's arm64 package, at the second run. In the first, step 8
failed: the proxy asked the effect about Extreme Tux Racer's connection while
the emulated KWin was still busy with the SuperTuxKart killed a moment
before, and the answer came later than the half second the proxy holds a
program (D-Bus `NoReply` in the journal), so the game was passed on
unanswered and the effect resized its window instead. The check now waits
before the X11 game until KWin uses under a tenth of a core over two
seconds, at most two minutes; the proxy's bound is the product's and stays.

Kubuntu 26.04, Fedora 43 and openSUSE Tumbleweed arm64 were made and checked
under emulation the same day, all nine steps passing with nightly
36545995686's arm64 packages. Every system and architecture but FreeBSD has
passed (item 2f of the open list).

PR #23 review follow-up, 2026-09-29: the recoverable-cloud-init diagnostic
still ran outside the boot deadline. It now has a timeout from the remaining
boot time and is best effort: cloud-init's warning exit code does not abort
readiness. The regression covers both a warning and a diagnostic that hangs.
Both hook stages and the Python regression suite passed in the maintained
Trixie container (`build/wayland-auto-check/review-lint2.log`). No new
cloud-image boot was needed for this diagnostic-only correction.

Two more checks, 2026-10-07, for limitations the nine steps do not reach
([known limitations](slice-known-limitations.md#progress), K3 and K9).
`sandboxed PACKAGE` (`tools/package_sandboxed.py`) runs SuperTuxKart and
Extreme Tux Racer as the system packages them, from Flathub and from the Snap
Store, each on Wayland and through X11, and records whether the shipped entry
claims the game, the size it supplies, the name the effect gives its program,
the proxy's answer with the names it offered, and the effect's status. `wine
PACKAGE` (`tools/package_wine.py`) builds `tools/wine-opengl-probe.c` with
MinGW, runs it under the system's Wine on Wine's Wayland driver with All
applications checked, and checks that it is enlarged and that KWin's picture
of the screen splits its red half from its blue one at the middle; it pictures
the desktop before the game as well. Found on the way, each by running it:

- A session locked its screen after five idle minutes, half an hour into a
  run, and the effect rightly refused everything; the relogin now turns the
  tester's automatic locking off.
- KWin answers long before Plasma's splash screen has gone, and a game started
  then lies under it; the relogin waits for the splash.
- `flatpak kill` run as root does not see the tester's instances, and ending
  the command that started a game ends neither the sandbox nor the game; a
  game left running made the next case read the first one's window. Games are
  ended by name between cases, and a reading counts only from the case's own
  route.
- Wine leaves its server and desktop running with the pipes they were started
  with, so reading a Wine command's output never ended, and Wine's own shutdown
  left its services running; Wine runs without pipes, and the prefix's server
  is ended.

## Remaining work

- FreeBSD cannot run Plasma's Wayland session in a machine: KWin needs a
  DRM/KMS driver, FreeBSD's drm-kmod drives Intel, AMD and NVIDIA hardware
  only, and virtio-gpu KMS exists only as an open pull request. The first,
  freebsd/drm-kmod#499, closed unmerged on 2026-10-01, superseded by #517,
  which rebuilds it on a LinuxKPI virtio layer and needs changes to FreeBSD's
  base system that are in review on Phabricator; both were open on
  2026-10-08. Trying it before then means a custom kernel and world as well as
  the driver from an unmerged branch, and the nightly's stock machine could
  not use it, so the session check waits for it to be merged.
  Decided by Jens on 2026-09-29: FreeBSD keeps the nightly's install, load and
  removal test in an emptied machine, and its session check waits for that
  driver.
- Plasma started without systemd, as startplasma does on the BSDs (item 8 of
  the open list, moved here on 2026-10-03), is checked by FreeBSD's session
  check once that can run. On Linux, every machine that passed its nine steps
  saw the proxy start at step 6 after an SDDM login.
- Whether GitHub's arm64 runners offer KVM, which would let the arm64
  machines run there rather than under full emulation (item 2f of the open
  list); unchecked.
- Container identity for the X11 proxy on the BSDs
  ([resolution control](slice-resolution-control.md#what-the-proxy-costs-and-what-goes-through-it-2026-09-29));
  Flatpak and Snap on Linux ran on 2026-10-07 with the `sandboxed` check, and
  what is left of them is K9 in the
  [known limitations](slice-known-limitations.md).
