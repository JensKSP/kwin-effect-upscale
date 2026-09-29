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
`plasma-kubuntu.in`), each image checked against the sums file its
distribution publishes beside it, with a 3840 × 2160 screen on QEMU's VGA,
since the effect acts from 1920 × 1080 up by default. `check <package>` copies
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

## Remaining work

- Fedora 43, openSUSE Tumbleweed and Arch have a template and a profile since
  2026-09-29, and their machines came up logged in to Plasma's Wayland session.
  All three passed the nine steps the same day with nightly 36545995686's
  packages of `7ff0fe6`: SuperTuxKart drawn at 2560 × 1440 and enlarged on
  Wayland in each, the X11 game answered by the proxy through its shipped
  entry (Extreme Tux Racer on Fedora; SuperTuxKart on openSUSE and Arch), and
  KWin still running after the package was removed.
- arm64 has profiles for Debian, Kubuntu, Fedora and openSUSE since 2026-09-29:
  the amd64 profile with the system's arm64 image, `qemu-system-aarch64` on the
  virt machine with every host core translating, UEFI from Debian's
  `qemu-efi-aarch64`, and virtio-gpu at 3840 × 2160 in place of VGA, which the
  virt machine lacks; openSUSE's template loads virtio_gpu there instead of
  bochs. Arch publishes no arm64 image. The Debian arm64 machine finished its
  first boot on 2026-09-29 in 38 minutes under emulation, logged in to Plasma's
  Wayland session with KWin running, and virtio-gpu offering 3840 × 2160; the
  others have not been made yet.
- FreeBSD cannot run Plasma's Wayland session in a machine: KWin needs a
  DRM/KMS driver, FreeBSD's drm-kmod drives Intel, AMD and NVIDIA hardware
  only, and virtio-gpu KMS exists only as the open pull request
  freebsd/drm-kmod#499 (aimed at FreeBSD 15.1, in review on 2026-09-29).
  Whether FreeBSD's check waits for that driver, runs on real hardware, or
  builds the module is Jens's decision (item 2f).
- Container identity (Flatpak, Snap) and the BSDs for the X11 proxy
  ([resolution control](slice-resolution-control.md#what-the-proxy-costs-and-what-goes-through-it-2026-09-29)).
