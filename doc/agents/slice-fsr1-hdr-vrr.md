<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: FSR rendering with HDR and VRR

**Status on 2026-09-28**, above the record: FSR 1 with optional RCAS, its colour
path and the orientation fix are implemented; lifecycle and fallback are
accepted on the OpenGL virtual backend in the VM (item 72). Open, as the open
list numbers them: the scaled frame, active-effect state and fallback on the
physical output and the phase-reversed A0/A1 repeats (71, 73), the B to D cost
matrix with real games (74), and aspect ratio with integer scaling, specified
and not implemented (76). **Decided by Jens on 2026-09-29 (item 75):** HDR and
VRR are postponed to a later version; when they are taken up, VRR is accepted
on the NVIDIA host (pcjensd) and on wzpc. This release's gates therefore leave
out the HDR and VRR parts below. **Taken up by Jens on 2026-10-11**, after
0.4.0, on pcjensd; the [handoff](#handoff-to-the-workstation-2026-10-11)
says where the work starts.

## Handoff to the workstation, 2026-10-11

Work moves from wzpc to pcjensd, the NVIDIA host. The branch is
`fsr/hdr-vrr`, started from `a8ee501`, the merge of 0.4.0. The tag `v0.4.0`
is set on that commit once master's checks on it pass.

**What is there.** The colour path is implemented, not refused: an HDR
destination is a supported case. `supportsUpscaleColors()`
(`compatibility.h`) accepts the sRGB, gamma 2.2, PQ and linear transfer
functions with a finite positive reference luminance and a luminance range;
anything else is `UpscaleRefusal::UnsupportedColors`, remembered for the
window. The capture is taken at buffer size by KWin's item renderer in the
destination's encoding. A non-linear destination, PQ among them, is filtered
directly in that encoding in `GL_RGB10_A2`; a linear destination, KWin's
scRGB-like target, keeps a floating-point capture and filters in a working
encoding in `GL_RGBA32F` (`upscaleFilterFormat()`). The shader and
configuration tests cover these conversions on the virtual backend. A
statement made on 2026-10-10 while K12 was decided, that HDR or PQ buffers
were refused, was wrong; what is open is acceptance on a real HDR output, not
support. On VRR, KWin 6.3.6 selects adaptive presentation independently of
direct scanout, which is the path the effect relies on; that is a reading of
KWin's code, not an observation.

**The host.** [Hardware acceptance hosts](../upscaling.md#hardware-acceptance-hosts)
records pcjensd as found: Debian 13, kernel 7.2.6-zabbly+, RTX 5090 with the
proprietary driver 615.71.09, nvidia_drm modeset=1, KWin 6.3.6, the LG
ULTRAGEAR+ on DP-3 at 3840 × 2160, 240 Hz, scale 1.45, SDR, HDR and wide
colour gamut off, adaptive sync never. Read the versions again before the
first run and record what changed. Turning HDR or adaptive sync on is a
change of the test conditions: record when, how and who turned it on, and
put the settings back as found afterwards unless Jens keeps them.

Read again on 2026-10-11 at the start of the session: kernel 7.2.9-zabbly+,
driver 615.71.09 and KWin `4:6.3.6-1` unchanged. DP-3 at 3840 × 2160, 240 Hz,
scale 1.45, now with **HDR enabled** (SDR brightness 600 nits, peak 604 nits),
**wide colour gamut enabled** and **VRR Automatic**. Jens turned these on
himself before this session and keeps them on; this
host is where HDR and VRR are made to work. The decision on 2026-10-10 is
settled as one document: HDR stays in this slice. Native configure stopped on
the missing `libxcb-res0-dev`, the only unmet build dependency of
`debian/control` here.

Jens confirmed the GPU, HDR and adaptive sync configuration with Left 4 Dead 2
on 2026-10-11; this agent did not observe that run. His order for the session:
find an open-source game that outputs HDR and is affected by VRR, confirm the
effect with it on this host, then check every game we support for how it
behaves with HDR and VRR and fix what needs fixing.

What this host offers, read with `wayland-info` and `vulkaninfo`: KWin
announces `wp_color_manager_v1` and `frog_color_management_factory_v1`, and
the NVIDIA Vulkan driver offers `VK_EXT_swapchain_colorspace` with the
HDR10 ST 2084 and extended sRGB linear colour spaces. A native Wayland Vulkan
client can therefore present PQ and scRGB here. VRR needs only a fullscreen
client with a varying frame rate; HDR output is what narrows the choice.

Candidates found by searching, none of them run yet:

- Quake II RTX, GPL, Vulkan. Reported to present HDR on Wayland with
  `SDL_VIDEODRIVER=wayland`; heavy enough on its ray tracing path to vary its
  frame rate. Its own resolution scaling has to be off for our tests.
- Godot 4.7's `misc/hdr_output` demo from `godot-demo-projects`: HDR on
  Wayland through Vulkan, Forward+ or Mobile renderer, with the display
  driver set to `wayland` and Request HDR Output enabled. A controlled
  reference rather than a game.
- RetroArch with its Vulkan driver, HDR since 1.10.0; an emulator front end.

`libxcb-res0-dev` was installed with Jens's permission, and the native
Release build of `30453cd` then passed (564 steps, no compiler warnings). It
is not installed; the effect is not loaded in the session.

### Baseline without the effect, 2026-10-11

Round 1 establishes HDR and VRR on this host with the effect unloaded
(`isEffectLoaded upscale` false); round 2 repeats each case with it. VRR is
read from the CRTC's `VRR_ENABLED` property with a small read-only libdrm
probe under `build/hdr-apps/`, which opens the card without becoming DRM
master; the actual refresh rate is read by Jens from the monitor.

Godot 4.7.2's `misc/hdr_output` demo, native Wayland, Vulkan, RTX 5090,
assets imported once with `--headless --import`:

| Case | Observed |
| --- | --- |
| Windowed | Jens: the demo reports HDR active, its sweeps exceed desktop white. Probe: `VRR_ENABLED=0`, as Automatic intends |
| `--fullscreen` | Jens: fullscreen, HDR unchanged. Probe: `VRR_ENABLED=1` |
| Demo closed | Probe: `VRR_ENABLED=0` again |
| `--fullscreen --max-fps 90` | Godot: 90 fps for all 54 reported seconds. Probe: `VRR_ENABLED=1`. Jens: the monitor ran at about 90 Hz |

Godot warns that KWin 6.3.6 lacks the FIFO protocol and that its frame
pacing is degraded; it did not show in these runs. Quake II RTX, Steam app 1089130, the native Linux build with the shareware
levels. Started outside Steam its binary needs `libssl.so.1.1`, which
Debian 13 does not ship, so it runs through Steam's soldier runtime:
`SDL_VIDEODRIVER=wayland SteamLinuxRuntime_soldier/run -- ./q2rtx +set vid_hdr 1
+set vid_fullscreen 1 +set vid_vsync 0 +set drs_enable 0`. Its first start
creates only `~/.quake2rtx`. Probe: `VRR_ENABLED=1` while it ran, `0` after it
closed. Jens: HDR and fullscreen worked; in a second run with `scr_fps 1`
the monitor's refresh followed the game at about 120 Hz. It statically links SDL2 with
the Wayland driver and `SDL_VIDEO_WAYLAND_MODE_EMULATION`, and has its own
dynamic resolution (`drs_*`) and FSR (`flt_fsr_*`). Its own scaling keeps the
swapchain at output size, so it never gives the effect a smaller buffer; round
2 has to switch it off and obtain the smaller buffer through a fullscreen mode
instead, which is what its profile has to record.

SuperTuxKart 1.4 (Debian), native Wayland, its configuration set to
fullscreen 3840 × 2160 with the FPS display; the previous file is kept beside
it as `config.xml.before-hdr-vrr-2026-10-11`. Its Vulkan renderer on this
driver could not read a version ("Can not find version for '' ''"), fell
back to the fixed pipeline and warned that OpenGL was too old: a defect of
SuperTuxKart's experimental renderer here, with the effect not loaded. Its
OpenGL renderer started normally (OpenGL 4.3, RTX 5090) and is the one its
profile uses. Jens: it worked on the HDR output, and in a race
the monitor read 120 Hz, the game's own `max_fps` cap, where fixed refresh
would have stayed at 240. One probe sample about 15 s after start read
`VRR_ENABLED=0`, most likely before the race went fullscreen.

Extreme Tux Racer 0.8.4 (Debian), Xwayland, its own settings fullscreen at
automatic resolution. Probe, once a second: one `0` at start, `1` for the
twenty seconds it ran fullscreen, `0` after it closed. Jens: it worked.

### Round 2: with the effect, 2026-10-11

The native build of `30453cd` (plus uncommitted documents) was installed with
Jens's permission and enabled in his session; its startup line and
`supportInformation` name that commit. All games is off on this host
(`UnknownApplications=false`), so only listed games are acted on.

Quake II RTX, started exactly as in round 1. Without an entry it was not
recognized, as All games off intends. With a provisional entry in Jens's
`~/.config/kwinupscalerc` (program `.*/Quake II RTX/q2rtx`, Wayland fullscreen
Auto) the overlay appeared and the effect advertised 1920 × 1080 as a screen
mode. Measured identity: KWin resolves the process to
`…/steamapps/common/Quake II RTX/q2rtx` through the soldier runtime; class,
instance and desktop file `q2rtx`; Steam build 17873149, version 1.8.0.

**The gap.** The game ignored the mode and supplied 3840 × 2162 in every
reading. Its source (NVIDIA/Q2RTX `f2526e9`, `src/unix/video/sdl.c` and
`src/client/refresh.c`) explains why: `vid_fullscreen 1`, the default, selects
`desktop`, which is `SDL_WINDOW_FULLSCREEN_DESKTOP` and reads no mode. KWin's
`getWindowInfo` reported the window at 0,0, fullscreen, 2648.28 × 1491.03
logical, which is 3840 × 2162 device pixels on an output of 2648.28 × 1489.66
(3840 × 2160): exact in width, two device pixels too tall, constant over 26
readings. `upscaleCoversOutput` allows one device pixel, so `placementRefusal`
returns `NotCoveringOutput`, `upscaleWindowAwaitingBuffer` never offers the
window to Auto, and the fractional-scale and smaller-configure steps that
exist for exactly this kind of client never run. Asking the player to change
the game's fullscreen setting is not a fix: obtaining the smaller buffer is
the effect's job. Jens asked for a systematic review of the latest SDL 1.2
(sdl12-compat), SDL2 and SDL3 fullscreen and borderless cases against the
effect's levers before fixing; that review's findings belong to the
[resolution-control slice](slice-resolution-control.md).

During play presented and submitted frames both settled at about 121 per
second with `VRR_ENABLED=1`; the lower early figures were loading inside the
rolling window.

**Fixed and observed, 2026-10-11.** Far edges may now run past the output by
the client's rounding (`upscaleReachesFarEdge()`, `upscaleFrameCovers()`;
also X11 validation and the resized-geometry match). `upscale-resolution`
passed natively. In English locale every native CTest entry passed except
`upscale-x11-integration`, 48 passed, 7 failed, 7 skipped, all seven pointer
and touch input cases; the unchanged `30453cd` built in a detached checkout
failed the same seven, so they belong to running it natively here. Container
builds, clang-tidy and both pre-commit stages have not run yet.

The build was installed and loaded without a logout through
`tools/reload-upscale.py --gui` (running build 11:27:59Z). Quake II RTX, same
start, with `WAYLAND_DEBUG=1`: delivered 1920 × 1080 to a 3840 × 2160
destination, `scaling=1`, about 328 presented frames per second (worst
6.2 ms) against about 121 at native 4K in round 1; its window 1324.14 × 744.83
logical. Jens: it worked, with much higher frame rates.

The Wayland trace shows the ladder: the advertised `wl_output.mode(3, 1920,
1080)` was ignored; KWin configured 2648 × 1490; step one sent
`preferred_scale(87)`, and SDL 2 answered by shrinking its viewport
destination to 960 × 540 rather than its buffer, so the request was
withdrawn; step two configured 1324 × 745 and the buffer followed. The 1491
came from KWin: SDL's 2161 row buffer made the window 1490.34 logical, and the
configure KWin sent at target scale 0.725 (`snapToPixels` in
`xdgshellwindow.cpp:726`) rounded 1080.5 up, giving 1491, which then held at
1.45. Open for Jens: skip step one for a window that answers it by shrinking
its window, and restore the output geometry explicitly after withdrawing it.
Not yet checked: HDR and pointer aim while scaled.

**SuperTuxKart, round 2.** Jens's session had stored the old key `Preset=5`,
which the effect maps to Performance; on his request it now uses the default,
`Resolution=2` (Quality), with the previous file kept as
`kwinrc.before-default-resolution-2026-10-11`. At Performance the game was
told 1920 × 1080, delivered it and was enlarged with VRR on; Jens: it looked
fine. At Quality, after its configuration was set back to 3840 × 2160, it was
told and delivered 2560 × 1440, scaled in all 19 readings while in front, VRR
on during the race.

**Defect found: the advertisement leaks into the game's settings.**
SuperTuxKart saves the screen mode it was told as its own resolution when it
exits: `real_width` and `real_height` read 1920 × 1080 after the Performance
run and 2560 × 1440 after the Quality run. With the effect off it would keep
rendering at that size, stretched by KWin, until the player chose its
resolution again. Open: the advertised-mode method needs a design that leaves
a game's saved choice intact; recorded in its entry's note meanwhile.
SuperTuxKart's own render resolution (`scale_rtts_factor`) and Quake II RTX's
own scaling and FSR shrink and enlarge the 3D scene inside the game and supply
a full-size picture. Jens: the effect is the replacement for a game's own
scaling, with its resolution, filter, sharpening and later modes, so the
user can find the best combination; both entries' notes advise leaving the
game's scaler off and letting the effect replace it.

**Status, 2026-10-11 evening.** Runs 1 to 3 of the plan below have been carried
out and recorded under it: SDR on the HDR output (SuperTuxKart), PQ (Quake II
RTX, including the comparison with the effect off, the in-game HDR switch,
alt-tab and aiming) and scRGB (Godot, measured), each scaled with
`VRR_ENABLED=1`. The handbook's
[HDR and VRR section](../upscaling.md#hdr-and-variable-refresh-rate) now
states the compatibility with that evidence. Still open for this slice: Jens's
visual verdict on the scRGB run, HDR on the television, the integer-geometry
refusal after a told mode, the start-up flash of a told client, and the
remaining matrix rows (image quality, cost matrix, lifecycle on the TV).

**Closing HDR and VRR on this host, plan of 2026-10-11.** Jens asked to run
the tests needed to call HDR and VRR working, and nothing else. Each run is
proposed, confirmed, run and judged before the next one. The effect's
`supportInformation` shows the destination transfer function and
`scaling=1`, the probe shows `VRR_ENABLED`, and Jens reads the picture and
the monitor's refresh rate. Scaling always blocks direct scanout, so each scaled
run also covers that matrix condition.

1. SDR game on the HDR output, scaled, with VRR: SuperTuxKart at 3840 × 2160,
   Quality. Colours and brightness as with the effect off, toggled during the
   run; monitor at the game's 120 Hz cap, then at a second cap. The OSD
   timeout of 5 s is checked in the same run.
2. PQ game, scaled, with VRR: Quake II RTX with `vid_hdr 1` on Wayland.
   Highlights and colours compared with the effect off; monitor following a
   frame cap such as 90; HDR switched off and on in the game while scaled
   (transition); alt-tab out and back (fallback and return); pointer aim.
3. scRGB, scaled: Godot's `hdr_output` demo, if it presents extended linear
   sRGB here (the transfer function in `supportInformation` tells) and takes
   a smaller buffer. Otherwise a minimal scRGB Vulkan client is needed.

**Run 1, 2026-10-11, observed.** New build (`eff90c78f7-dirty`, built
14:36:13Z, with the OSD fix and the 5 s default) loaded through the reload
tool; `OsdTimeout=30` removed from Jens's kwinrc. SuperTuxKart, Wayland,
3840 × 2160 fullscreen, cap 120, Quality: told and delivered 2560 × 1440,
enlarged to 3840 × 2160, `scaling=1` and `scanout=blocked` in 49 of 54
readings (the rest were start and exit), about 110 frames per second
presented during the race, the effect reporting that HDR follows KWin's
colour management. Probe: `VRR_ENABLED=1` in 100 of 111 samples, `0` at start
and after exit. Jens: it worked. The comparison with the effect switched off
did not happen, because the game was closed first; it moves to run 2. The game
again saved 2560 × 1440 as its own resolution (the open leak defect) and was
set back to 3840 × 2160.

**Run 2, 2026-10-11, observed.** Quake II RTX, Wayland, `vid_hdr 1`,
`cl_async 1`, `r_maxfps 90`, same build as run 1. Told 2560 × 1440, delivered
2561 × 1440 (SDL's rounding, accepted since the coverage fix), enlarged to
3840 × 2160 with `scaling=1` in 108 of 113 readings; the others were the
start (2917 × 1434, before the configure arrived) and one reading at exit.
Presented 90.91 frames per second in every reading after loading, exactly
the cap. Probe: `VRR_ENABLED=1` in 220 of 229 samples, `0` at start and after
exit. Jens: it was fine. The comparison with the effect switched off did not
happen, because the game was closed before he asked for it.
Asked afterwards, Jens confirmed: the monitor read about 90 Hz, HDR switched
off and on in the game's menu kept the picture right while scaled, alt-tab out
and back worked, and aiming with the mouse was normal.

**Run 3, 2026-10-11: Quake II RTX not scaled, defect found.** The same start
as run 2, five minutes later, with a script meant to switch the effect off and
on once scaling began. Scaling never began, so the script never acted; the
effect stayed loaded throughout. The game supplied 3840 × 2162 for about 35 s
at about 62 frames per second, and the effect correctly reported that the
buffer was not smaller than the destination. The journal shows the difference
from run 2. In both runs step one (`preferred_scale`) was answered by SDL with
a 2560 × 1441 buffer, and KWin's configure at the lowered target scale moved
the requested geometry to 1490.69 logical rows on an output of 1489.66
(`snapToPixels` at 0.9667 on the 1490.34 the 2161-row buffer had made). In run 2
step two had already been sent by then, and the request was matched by the
resized geometry. In run 3 the answer came first. `upscaleRequestCoversOutput()`
still held the requested geometry to one device pixel on every edge (it is
1.5 here), so `requestedPresentation()` failed, the recheck released the request,
and step two was never sent. The coverage fix of this morning had missed this
fourth comparison. Fixed: it uses `upscaleFrameCovers()` like the committed
frame. Natively built without warnings; `upscale-placement`,
`upscale-resolution` and `upscale-integration` passed. A KWin conformance
case that reproduces the race needs the `containers/wayland-tests` image,
which pcjensd does not have; not written yet. Jens also saw the persistent
statistics and developer view all the time, because his kwinrc sets
`OsdStatistics=true` and `OsdDeveloper=true` explicitly (both off by
default in a release build). Who set them is not known.

**Run 4, 2026-10-11: the fix, repeated starts.** Fixed build (built
15:02:56Z) loaded through the reload tool. Quake II RTX started six times
with the run 2 command line, each closed by this agent with SIGTERM after
about 10 s in the game. One start overlapped another because a helper script
was killed by its own process match. In all six the journal shows step one
answered with 2560 × 1441, step two sent 50 to 57 ms after the scale request,
and the game then supplying 2561 × 1440. The effect reported `scaling=1`
2 to 4 s after start in every non-overlapping run.

**Run 5, 2026-10-11: comparison with the effect off, PQ.** Quake II RTX as in
run 2, scaled (2560 × 1441 to 3840 × 2160) from 17:55:38. At 17:57:08 a timer
unloaded the effect, and the game returned to native size at once. Loading
it again failed, because `tools/reload-upscale.py` deletes its temporary
plugin copy once KWin has loaded it, so no file under that name remained.
That is a property of the test tool. The effect stayed off for two minutes and was loaded
again with the reload tool at 17:59:06. Jens: brightness and colours were the same
with and without the effect. **Open, suspicious:** after loading again into
the running game, step two brought it back to 2561 × 1440 in a 1766 × 993
window, but for the ten seconds until Jens closed it the effect reported the
window as not covering its output and the ladder as exhausted ("choose
2560 × 1440 in the game"), where fresh starts scaled within 2 to 4 s.
Enabling the effect over a running game is to be investigated. Further
off/on comparisons switch the game's entry through the configuration, which
keeps the effect loaded.

**The switch-on defect, found and fixed, 2026-10-11.** KWin takes the size a
client commits as the window's move-resize geometry once no configure is
pending (`XdgSurfaceWindow::maybeUpdateMoveResizeGeometry`, KWin 6.3.6).
After step two Quake II RTX committed 1766.21 × 993.10 for the requested
1766 × 993. `requestedPresentation()` and `restoreGeometry()` compared that
geometry with the request exactly. The first therefore dropped the request the moment it
succeeded. The second, on withdrawal, did not give the full size back, which is
the second half of the open point above. On a fresh start this went unnoticed,
because the told mode of 2560 × 1440 made the smaller window count as
covering its output on its own; over a running game nothing had been told,
so it was neither enlarged nor restored. Both now ask `holdsResize()`, which
allows the client's rounding (`upscaleFrameCovers()`). Regression test
`keepsAResizeTheClientRoundedThroughItsBuffer` in `upscale-integration`:
scale 1.45, Auto switched on over a running client that was told nothing, the
client drawing SDL's rounded buffer; it failed without the fix at the
enlargement check and passed with it, natively (27 passed, 5 skipped). Not yet
installed: installing needs Jens's password. The hardware check over a running
game waits for him, also because of the rule in the Proton slice (item 101)
against reloading while a game runs, which run 5 broke.

**Unattended runs while Jens was away, 2026-10-11 evening**, installed build
of 15:02:56Z (without `holdsResize()`), screen saver and power management
inhibited with `kde-inhibit` for the duration, container checks running
beside them, so frame rates are not results:

- Quake II RTX through its default X11 path (no `SDL_VIDEODRIVER`), started
  fresh, closed after 60 s: told 2560 × 1440 as its X11 window size, supplied
  it, enlarged to 3840 × 2160, `presentation=x11-fullscreen`, `scaling=1` in
  25 readings, pointer input translated; probe `VRR_ENABLED=1` throughout
  play. Whether it presented HDR through Xwayland was not judged.
- glmark2-wayland 2023.01, `--fullscreen`, shipped entry AdvertisedScale: told
  scale 1, supplied 2648 × 1490, the nearest to 2560 × 1440 a whole scale can
  tell, enlarged to 3840 × 2160, `scaling=1`.
- vkmark 2025.01, `--winsys wayland --fullscreen`, shipped entry Auto: told
  and supplied 2560 × 1440, enlarged, `scaling=1`. With glmark2 this closes the
  recommended values left open in the application-profiles slice for a scaled
  desktop.
- Godot 4.7.2's `hdr_output` demo, native Wayland, Vulkan, `--fullscreen
  --max-fps 90`, under a temporary entry in Jens's `kwinupscalerc` (program
  `.*/Godot_v4.7.2-stable_linux.x86_64`, Auto), removed afterwards. Its
  surface's image description, read with `WAYLAND_DEBUG=1`, is
  `set_tf_named(5)`, extended linear, with sRGB primaries: scRGB. It
  follows step one: `preferred_scale(116)`, supplied 2560 × 1441, enlarged to
  3840 × 2160, `scaling=1`, 90 frames per second presented, the description
  unchanged while scaled; probe `VRR_ENABLED=1` in 45 of 58 samples, `0` at
  start and after exit. Measured, not judged: the picture, negative scRGB
  values and highlights wait for Jens. This is run 3 of the plan.

**Native `upscale-x11-integration` on pcjensd, narrowed, 2026-10-11.** Run
again natively after the commits above: this time the second case
(`initialWindowedMapping(windowed-timeout)`) hung until QTest's 300 s limit,
instead of the seven input failures seen in the morning. The log shows why the
X11 side never comes up: the nested KWin starts the X11 proxy session as its
Xwayland, the proxy refuses with "Cannot initialize Upscale X11 session: File
exists", and KWin stops Xwayland after four crashes, so the test's X11 client
blocks in `xcb_connect`. "File exists" is a stale `errno`, most likely left by
`QTemporaryDir`'s name retries; the failing step is one of `Session::listen()`
or `Startup::parse()`'s descriptor and argument checks, and the message does
not say which. The same test passes in the container, so what differs natively
is the real session beside it (shared `/tmp/.X11-unix`, the runtime
directory). Not pursued further: it needs more nested sessions on the desktop
GPU, and the run itself is environmental. Worth fixing either way: the proxy's
start-up error should name the step that failed, not `errno` from an unrelated
call.

**Frame-time attempt, Quake II RTX, 2026-10-11 18:42, not a result.**
`tools/measure-frame-times.py` (now addressing a reloaded build by its name,
`effect_control.effect_name()`), presets native, quality and performance,
two passes, game restarted per run, `r_maxfps 1000`, HDR and VRR on, machine
idle. Supplied 3840 × 2161, 2561 × 1440 and 1920 × 1080 as asked (the
quality and performance runs scaled), but the client committed 62.5 frames per second in
every run: the game holds that rate where it was left, at its menu, so the
runs say nothing about rendering cost. One quality run measured nothing; Jens
used the machine briefly during it. A cost run needs a scene that renders
uncapped, for example a timedemo, chosen first.

**Frame times with uncapped benchmarks, 2026-10-11 19:20, HDR and VRR on.**
`tools/measure-frame-times.py`, two passes each, machine idle, the client's
own FPS read from its output. glmark2-wayland 2023.01 terrain: at Native it
renders 5296 × 2980 by itself (it rounds the desktop's 1.45 up to scale 2 and
KWin scales its picture down), about 1380 frames per second; at Quality and
Performance the effect tells it scale 1, it renders 2648 × 1490, enlarged,
about 2850: +106 %, outside the spread, in both passes. vkmark 2025.01 cube,
immediate presentation: 3840 × 2160, 2560 × 1440 and 1920 × 1080 as asked,
the reduced runs scaled in every reading, the client's rate +9 to +10 %,
within the spread: the cube is not bound by pixels at 4K on this GPU, so it
says nothing about cost. The tool leaves the last preset in `kwinrc`; Jens's
Quality was restored after each run (it had been left at Performance once).

**Found while measuring: a told client shows its small picture before it
is enlarged.** Jens saw vkmark covering only part of the screen at some starts.
Polled every 50 ms: its window appears with the told 2560 × 1440 buffer on
a 1280 × 720 logical surface, and for about 0.2 s, until the effect has
selected it, KWin draws it at that size in the corner; after that it is
enlarged in every reading, and still captures (Spectacle) of vkmark and
glmark2 show the whole output covered. Not intended; cosmetic, not specific to HDR or VRR:
the effect should take over a window it told the smaller mode from that
window's first frame.

What Jens saw for longer was a different thing, found in the native runs:
at Native the effect leaves vkmark alone, and vkmark puts its 3840 × 2160
buffer on a 1920 × 1080 logical surface, reckoning with the scale 2 KWin
announces on `wl_output` for the 1.45 desktop, and ignores the fullscreen
configure. Its window covers 72 % of the output, top left, with the desktop
beside it (captured; `getWindowInfo` 1920 × 1080 fullscreen; the effect
reported it inactive as not covering its output). Not the effect's doing,
by reasoning, since it does not act on the window at Native; not checked with
the effect unloaded. An idea, outside this slice: a fullscreen window whose
buffer already has the output's pixels could be presented over the whole
output one to one.

**Switching on over a running game, on hardware, 2026-10-11 19:45.** Build
`33b9143` (with `holdsResize()`) installed with Jens's password and loaded
through the reload tool. Quake II RTX on Wayland, started with its entry
switched off in Jens's user layer (`MethodWaylandFullScreen=Off`): 3840 × 2161,
not scaled. Entry switched on through the configuration: scaled within one
second (2560 × 1441 to 3840 × 2160) and held for 20 s. Switched off again:
within one second back to its full window (2648.28 × 1491.03 logical,
3840 × 2162), the full size given back. The user layer was restored afterwards,
which switched it on a third time. Jens: aiming and the desktop pointer
worked. Run 5's open point is closed.

**Integer geometry with HDR and VRR, 2026-10-11 19:55.** vkmark,
`--fullscreen -p fifo`, Geometry=1 (integer), captured. At Performance, told
1920 × 1080, enlarged by exactly two to 3840 × 2160 over the whole output;
probe `VRR_ENABLED=1` in 15 of 18 samples. At Quality, told 2560 × 1440,
the effect refused ("the fitting integer factor is not two, the only one FSR 1
enlarges by"), and what the screen showed was vkmark's 1280 × 720 logical
surface in the top left corner over the desktop: the client had already been
told the smaller mode, and the refusal leaves it where KWin puts it. Defect,
outside HDR and VRR: with integer geometry the effect must not tell a
resolution it will then refuse to enlarge, or must present what it told
centred with bars. The settings were restored (Geometry removed, Quality).

KWin's support information
reports only the configured `Adaptive Sync: automatic`, never whether it is
active, which is why the probe exists.

**Order of work**, against the [acceptance matrix](#required-acceptance-matrix):

1. Build natively in `build/` and run the effect in Jens's session in SDR
   first: the scaled frame, `upscale` in `activeEffects`, fallback, and the
   NVIDIA driver's answer to the capture formats. The GPU class KWin reports
   there is `Unknown`, so the GLES and storage paths may differ from wzpc.
2. HDR on: SDR content on the HDR output, then PQ and scRGB clients, then
   switching between SDR and HDR while scaled. Look for clipped highlights,
   banding in gradients, wide-gamut colours, negative scRGB values, and any
   second conversion: the scaled picture must match KWin's own stretch of the
   same buffer in colour, and differ only in detail.
3. Adaptive sync on: presentation with scaling active and direct scanout
   blocked, in SDR and in HDR, across frame rates within the monitor's range.
   A setting that reads enabled is no evidence; the presentation mode and the
   output's refresh have to be observed.
4. Where time allows on the same hardware: K10's real Direct3D, Vulkan, DXVK,
   vkd3d-proton and Proton games, and K13, from a list prepared for that
   session first, as the [known limitations](slice-known-limitations.md)
   record.

Jens decided on 2026-10-10 that HDR-aware FSR is a work package of its own,
accepted on his television, which is wzpc's output. Ask him at the start
whether that means a slice document split from this one, and keep the
television run as the HDR acceptance; pcjensd is where it is developed and
first seen. After HDR, his order stays as recorded there: K16 X11 pen, the
K12 lifts, vkd3d-proton in the Wine check, K10 and K13, sandboxed Steam on
his machine, and the refreshed KWin proposal 29b, which he files.

**Working notes that live on wzpc only**, repeated here because the next
session starts without them:

- Build the check images from `containers/` on pcjensd; wzpc's tags such as
  `upscale-check:trixie-a0` do not exist there. Never run two compositor
  test sessions on the desktop GPU at once; parallel nested sessions froze
  wzpc. `tools/conformance-vm.py` keeps the virtual-backend tests off it.
- `pre-commit run --all-files` lints only files Git knows; mark new files
  with `git add -N` before the final lint, or CI finds what the local lint
  skipped.
- Give every long command a hard limit that also ends its container: `timeout
  -k`, named containers removed with `podman rm -f`. In Wine, `wineserver -k`
  before `wineserver -w`, since the Wine desktop keeps the server alive.
- Wait with relative times; a clock time like `date -d '23:41 UTC'` read
  after midnight waits a day.
- CodeRabbit reviews about once an hour and does not queue a refused request;
  a reply in a thread is not its verdict. A pull request's text is changed
  with `gh api -X PATCH repos/{owner}/{repo}/pulls/N`, since `gh pr edit`
  fails on this repository.
- Commit by explicit path; the checkout may be shared. Temporary files, VMs,
  containers, images and merged branches are cleaned up once a series of
  tasks is done.

## Start state

FSR 1, optional RCAS and global resolution controls are implemented. The dated
shader/configuration builds and tests below passed in their recorded copies;
they are not a new verification of concurrent source changes. Integration,
real-game performance, HDR/VRR and joint TV acceptance remain open.

## End state

The supplied-buffer rendering path passes supported-container, lifecycle,
image-quality, performance and real-device acceptance for the required SDR,
HDR and VRR combinations. Native-size/unsupported cases restore normal KWin
rendering, and measured limitations and design invariants are preserved in
source and the handbook. All required acceptance below has passed.

## Supported scope and full acceptance

The [handbook](../upscaling.md#supported-scope-and-full-acceptance) defines both
gates. For this package they are:

**Supported scope, releasable.** An eligible opaque fullscreen buffer between
half and full destination size, matching aspect ratio, unrotated output, SDR,
on KWin 6.3.6, is scaled with EASU and optional RCAS; every other case falls
back to ordinary KWin rendering and the status names the reason. Closing this
gate requires the real-session eligibility defect to be fixed and one scaled
frame observed on hardware, the lifecycle and fallback checks, and the A0/A1
overhead bounded with phase-reversed repeats. It does not require a game,
Proton, HDR, VRR or the B-to-D cost matrix.

**Full acceptance.** The required acceptance matrix below, unchanged. HDR, VRR,
real games under Valve Proton and standalone Wine, image quality and the
complete cost matrix keep this requirement open after any release.

The recorded wzpc HDMI path reported adaptive sync incapable, blocking VRR
acceptance there. The separately inventoried NVIDIA host has not yet passed
VRR acceptance either; its recorded configuration has VRR disabled.

## The scaler-effective gate

This gate is the package's first milestone and the project's next acceptance
after the [development infrastructure slice](slice-development-infrastructure.md)
in the original sequence. The defect and regression coverage are now addressed,
and on 2026-10-03 the physical display gave a scaled frame, its pixel
comparison, the active-effect state and the fallback, and the loaded effect's
overhead was bounded with phase-reversed repeats once its cause was removed
(see the progress below): the gate is closed.
It was introduced before any real-GPU scaled frame had been observed. Loading
the plugin, reporting it supported, passing the container and headless tests and
installing it on the acceptance host have all been achieved and none of them
establish that the scaler does anything.

The gate closes when the effect demonstrably processes a supplied smaller buffer
on real hardware, and not before:

| Required observation | Why it is not implied by the current evidence |
| --- | --- |
| A named eligibility condition explains every refusal | Each condition now reports itself, and the nested session on the real GPU named one. The real session on the television has not been re-run against it. |
| The refusal recorded on 2026-09-18 is understood and fixed | Isolated to `TransformedRenderTarget` and removed, as recorded below. What remains is observing a scaled frame with that fix in the real session. |
| `activeEffects` lists `upscale` for an eligible window | The effect never entered KWin's active set, so it never took its scanout restriction. |
| The destination pixels differ from ordinary KWin scaling | A covered output proves composition happened, not that EASU ran. |
| An ineligible window still restores ordinary rendering | Fallback has only been exercised where the effect was already inactive. |
| An automated test reproduces the real-backend refusal | The orientation test now covers Normal/FlipY and detects the original refusal; physical-display acceptance remains separate. |

The last row is the durable part. A fix that only repairs the running session
leaves the suites unable to detect the next occurrence, so this gate requires
the automated coverage to be extended to whatever distinguishes the real
graphics backend from the existing fixtures.

The gate needs no game, no Proton, no HDR, no VRR and no resolution control. The
fixture built from `autotests/wayland_client.cpp` already supplies a conforming
buffer on request. Nothing here waits for the joint acceptance session.

Depends on the candidate selection and rejection reporting from the development
infrastructure package. Diagnosing this refusal is the reason that package is
sequenced first.

### The refusing condition, found on 2026-09-18

The condition is `TransformedRenderTarget`: the effect refused every frame
because the render target was not upright. It was found by running the
production plugin against SuperTuxKart in a nested KWin 6.3.6, where the
buffer was eligible and the status named this condition for the frame.

It is not specific to that session. KWin's DRM backend begins every frame with
`m_pipeline->output()->transform().combine(OutputTransform::FlipY)`
(`src/backends/drm/drm_egl_layer.cpp`), so on an upright screen the target
transform is `FlipY` on **every** composed frame of a real session. The check
therefore refused everything on real hardware, which is what "the effect has
never been observed scaling a frame" was recording.

The check was also unnecessary. `RenderViewport`'s projection matrix already
contains `renderTarget.transform().toMatrix()`
(`src/core/renderviewport.cpp`), and the scaler draws through that matrix, as
KWin's own effects do. Nothing had to be compensated for.

Fixed by accepting an upright or flipped target and refusing only orientations
nothing has drawn through; a rotated output is still refused earlier by its own
condition. A render test draws the same image into an upright and a flipped
target and requires the results to be mirror images, and it fails when the
expectation is inverted, so it is not vacuous.

Observed afterwards, with the production plugin, factory defaults, a
3840 × 2160 output at desktop scale 3 and SuperTuxKart configured for
fullscreen at 3840 × 2160: supplied input 2560 × 1440, destination
3840 × 2160, processing **FSR 1, sharpening 0%**. This is the first observation
of the scaler processing a real game's buffer. It is a headless nested session
with a real GPU: it establishes that the path runs and is not image-quality,
performance, HDR, VRR or television acceptance, and the gate's remaining
requirements stand.

### Driving the benchmarks, and why the first numbers are not results

Jens asked for glmark2 and vkmark fullscreen at half resolution with the effect
enlarging, measured against the same run at 3840 × 2160. Both were read in
source and then run; both now work through the plugin, and the measurement
environment does not.

#### What each benchmark actually reads

Neither benchmark behaves like SuperTuxKart, and neither could have been driven
by guessing.

- glmark2 2023.01, `src/native-state-wayland.cpp`: on a granted fullscreen
  configure it sets its size to the configure size multiplied by the output
  scale and calls `wl_surface_set_buffer_scale` with that scale. It reads the
  advertised mode only in the branch where fullscreen was refused, which is why
  an advertised mode alone changed nothing: measured, 3840 × 2160 unchanged.
- vkmark 2025.01, its Wayland window-system source `wayland_native_system.cpp`: its fullscreen extent is
  the advertised mode in pixels, and it separately calls
  `wl_surface_set_buffer_scale` with the advertised scale. An advertised mode
  alone therefore shrank its window to 1920 × 1080 on a 3840 × 2160 screen, and
  an advertised scale alone stretched its surface to twice the screen.
- SuperTuxKart 1.4, `lib/graphics_engine/src/ge_vulkan_driver.cpp`: its Vulkan
  swapchain is `SDL_Vulkan_GetDrawableSize`, which is why the Vulkan renderer
  follows the window rather than a mode and the advertised mode does not reach
  it. Its OpenGL renderer goes through SDL's mode emulation and does.

#### Observed with the production plugin

A 3840 × 2160 virtual output at desktop scale 2, the effect at its Performance
preset, each benchmark started by the session rather than by the effect:

| Client | Committed buffer | Destination | Effect |
| --- | --- | --- | --- |
| glmark2, `--fullscreen -b terrain` | 1920 × 1080 | 3840 × 2160 | FSR 1, sharpening 0% |
| vkmark, `--fullscreen -b cube` | 1920 × 1080 | 3840 × 2160 | eligible |

Exactly half, fullscreen, covering the output, through the effect's own
catalogue and method selection.

#### The numbers from this environment are unusable

glmark2's terrain scene scored 40 at 3840 × 2160 and 42 at 1920 × 1080 **with
the effect disabled in both runs**: a quarter of the pixels bought five per
cent. The client is therefore not what limits this environment, so no
comparison made in it can measure what the effect costs or saves. The run with
the effect enabled scored 40 against the 4K baseline's 39, which says nothing
for the same reason.

Two causes are known and neither is the effect: a nested compositor composites
every frame at the full output size, and a Wayland client is paced by frame
callbacks. Do not record any of these figures as a performance result.

The performance matrix therefore stays where the handbook already puts it: the
real session on the acceptance host, with the physical output, its own
presentation timing and direct scanout in the picture. The one thing these runs
establish is that the matrix can now be driven at all, which it could not be
before: A0 and C differ only in what the client was told.

#### What the benchmarks turned out to be good for, 2026-09-18

The benchmarks were adopted as the instrument for measuring what reducing the
rendering resolution is worth. Read in their own source, glmark2 cannot do
that: its `terrain` scene runs its height, normal, specular and both bloom
passes at fixed 256 × 256 and 512 × 512 resolutions and draws a hundred and
thirty thousand triangle grid, so almost nothing it does follows the window.
Measured windowed, with this effect uninvolved, a quarter of the pixels made it
*slower*: 21 frames per second at 1920 × 1080 against 26 at 3840 × 2160. Every
comparison run through it, including the ones recorded earlier in this
document, was therefore measuring an instrument that cannot move.

vkmark's `effect2d` does follow the window: its render area is the swapchain
extent and its kernel steps are one over that extent. It went from 20.8 ms a
frame at 1920 × 1080 to 27.0 ms at 3840 × 2160, so roughly six milliseconds a
frame is what rendering 4K instead of 1080p costs that workload here. That is
the size of the prize this effect is competing for.

Jens's conclusion, and the one recorded here: the benchmarks stay, for
compatibility and for catching a regression, rather than as the measure of a
gain. They cover ground the two games do not. glmark2 is scale-driven and
vkmark is mode-and-scale-driven, so between them both remaining control methods
are exercised. vkmark commits `DRM_FORMAT_XBGR16161616`, which is how the
scaler's missing 16-bit formats were found: every frame of it was refused, and
nothing else in the test set had shown that. With presentation now measured
from `RenderLoop::framePresented`, "the effect did not make this worse" is
checkable for a client it cannot help.

Anything used to measure a gain has to be shown to respond to resolution first,
windowed and with the effect uninvolved. That check costs two runs and would
have saved every measurement made on 2026-09-18.

#### What a real run needs

- The 4K output at **desktop scale 2** for an exactly half-resolution
  comparison. At scale 3 the reachable step is two thirds, and at scale 1 these
  two clients cannot be reduced at all.
- A scene heavy enough to sit below the refresh rate at 3840 × 2160, or the cap
  hides the difference. vkmark's `-p immediate` for the throughput series.
- Both series from the handbook, and composition versus direct scanout recorded
  for each case: enabling the effect gives up scanout, and that cost belongs in
  the end-to-end number rather than being left out of it.
- SuperTuxKart's own `--profile-time` as the sanity check before the matrix. It
  works at any desktop scale, because its method does not go through the
  integer scale.

## Scope and boundaries

Own original-buffer capture, EASU/RCAS, colour handling, initial geometry,
rendering lifecycle/fallback, global controls and the rendering acceptance
matrix. Profiles, overlays, launching and active resolution control each have
separate packages. Extended aspect ratios and integer filters are owned here too, under
[aspect ratio and integer scaling](#aspect-ratio-and-integer-scaling); they are
outside the supported scope stated above and have their own gate. Optional later
features do not gate this slice's completion.

## Dependencies

Rendering tests can use controlled original buffers and manual game resolution
settings. Where required real games need a helper to supply such buffers, use
the verified [resolution-control method](slice-resolution-control.md) and record
that dependency. Its method development and game-control success criteria are
owned there, not duplicated here. Keep the handbook's staged application order;
a dependency still needed for required rendering acceptance keeps this slice open.

## Approach

1. Retain the source-resolution capture and reversible colour path, verifying
   actual original-buffer mapping through KWin's renderer on both targets.
2. Complete compositor lifecycle/fallback and global configuration checks.
3. Run the staged OpenGL/Vulkan performance comparisons, then real-game image,
   HDR and adaptive-presentation acceptance with Jens on the TV.
4. Fix rendering failures, record observed limits and preserve implementation
   explanations in source before deleting this document after acceptance.

## Rendering design

The handbook's [selected initial approach](../upscaling.md#selected-initial-approach)
specifies the scaler choice, the supported initial geometry, the colour path and
the acceptance this package has to reach. It is the single source; this document
does not restate it.

Within that scope this package owns the global controls: the percentage slider
with its pixel preview, the FSR resolution presets, automatic mode, the optional
RCAS strength and the separate desired and actual resolution status. Resolution
wishes stay guidance here; verified client requests belong to the
[resolution-control package](slice-resolution-control.md). Scaling always follows
the buffer that actually arrived, including games with a fixed size, and a
mismatch with the wish alone must not disable it. The wish is never implemented
by downscaling a completed frame. HDR and VRR remain mandatory throughout.

## Findings

- KWin 6.3.6's `OffscreenEffect` captures at output scale into `GL_RGBA8`
  with an sRGB description. It cannot supply the required input unchanged.
  The implementation renders the surface item at buffer resolution
  through KWin's item renderer, retaining its import and release-fence handling.
- Colour conversion to the current render target happens at input
  resolution. Linear destinations use floating-point intermediates and a
  reversible bounded encoding; non-linear destinations use RGB10_A2 and direct
  filtering. Final composition must not repeat colour conversion. Both paths
  still require physical-display HDR image-quality acceptance.
- KWin 6.3.6 selects adaptive presentation in `compositor_wayland.cpp`
  independently of direct scanout. This establishes an integration path, not
  proof of VRR on a physical display.
- The implementation now contains the source-resolution capture, EASU and
  optional RCAS, eligibility checks, normal-rendering fallback and settings.
  Container shader tests are separate from full compositor and TV acceptance.
- Gamescope's reviewed FSR path scales the base layer with EASU and combines
  RCAS with final composition. Separately composited overlays follow the base
  layer; an in-game HUD is already part of the input buffer.
- Gamescope's reviewed SGSR path uses a modified filter followed by RCAS. It
  cannot be treated as Qualcomm's single-pass reference when comparing costs.
- In the reviewed gamescope code, `update_tmp_images()` allocates an 8-bit
  intermediate image, and the NIS wrapper leaves its HDR modes at their SDR
  defaults. These are not a ready-made HDR integration for KWin.
- FSR's input range and transfer-function assumptions require an explicit HDR
  conversion design. Raw scRGB must not simply be clamped into an SDR buffer.
  Using FP32 shader arithmetic does not by itself solve intermediate-format
  precision or colour management.
- FSR documents a useful range up to four times the input pixel count. The
  1080p/1440p to 4K cases fit that range; 720p to 4K is outside this slice.
- NIS has explicit linear-HDR and PQ modes and remains an alternative if the
  FSR integration cannot meet the required HDR behaviour. HDR and VRR remain
  requirements if the scaler choice changes.

These are source-review findings, not measured quality, timing or power
results for this effect. [Gamescope's reviewed renderer](https://github.com/ValveSoftware/gamescope/blob/c50ddfa9b71a75ec8df94bda8cf31d425dbdda24/src/rendervulkan.cpp),
[AMD's reference](https://github.com/GPUOpen-Effects/FidelityFX-FSR) and
[NVIDIA's reference](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)
document the reviewed pipelines and algorithm inputs. Other sources remain
listed in the [README](../../README.md).

Resolution-method source findings and prototypes are owned by the
[resolution-control slice](slice-resolution-control.md); the current production
effect now sends the profile-specific Wayland/X11 requests documented there.

## Progress and remaining work

- The README distinguishes nested/virtual observations from open physical acceptance;
  build and package availability are explicitly not claims of working upscaling.
- [x] Select and implement FSR 1, optional RCAS and global configuration.
- [x] Implement colour conversions and record automated shader/configuration tests.
- [x] Measure A0 and A1 on the real output; record the unusable aggregate score.
- [x] Isolate and fix the orientation refusal and extend automated coverage.
- [x] Close the physical-display scaler-effective gate with observed pixel
  comparison, active-effect state and fallback on the accepted candidate.
  Needs the physical output and Jens's session (items 71 and 73 of the open
  list, with the A0/A1 phase-reversed repeats): a nested session on the
  desktop GPU is ruled out since parallel compositor sessions froze wzpc.
  **Run 2026-10-03 in Jens's session on the television** (KWin 6.3.6,
  3840 × 2160 at scale 3, `3c574c6`). Scaled frame, active-effect state and
  fallback observed: SuperTuxKart in all six presentations supplied
  2560 × 1440, enlarged with `upscale` in `activeEffects`; with the game
  stopped on one frame, KWin's screenshot of the output matched its plain
  stretch of the same buffer (likeness 0.05 to 0.19 of 255) with 27 to 31 %
  more detail; a windowed game was left to direct scanout with the reason
  named. The overhead is not bounded: in six phase-reversed glmark2 pairs at
  native size, where the loaded effect reported nothing to do and direct
  scanout, throughput with it loaded was 0.82 to 0.84 of throughput without
  it in every scene. The control without any reading of the effect during a
  run gave 0.84 to 0.85 (item 99 of the open list): the cost is the effect's.
  Paired by frame time, the loaded effect added a median of 8.9 µs per frame
  over the 18 scene pairs (mean 8.9 µs; 6.8 to 10.6 µs per pair across its
  three scenes, 2.8 to 15.1 µs per single scene).
  **Bounded the same day (item 102).** Those runs had the Debug build Jens's
  session uses; built as the packages are, RelWithDebInfo, the loaded effect
  still added a median of 4.9 µs per frame (0.88 to 0.90 of glmark2's
  throughput). A profile of the session's KWin showed why: the effect's
  handler of a window's damage, which runs at every client commit, spent 36 %
  of KWin's time, most of it in `upscaleGlobalSettings()` building
  configuration groups for each preference. The effect now keeps the global
  layer as read in `reconfigure()`. Six pairs again, phase-reversed, with
  nothing reading the effect during a run: 0.982 to 0.994 of the unloaded
  throughput, a median of 0.4 µs per frame over the 18 scene pairs; in the
  profile the handler fell to 2.4 % of KWin's time and the configuration
  library dropped out of it.
- [ ] Measure what deciding the full repaint once a frame saved. Most of the
  remaining 2.4 % was the damage handler asking at every commit whether the
  window is the one being enlarged. Since `170e1d7` the effect decides that
  once a frame in `preparePaintArea()`, and the handler repaints a window
  whole only while it is the one enlarged; the integration, output, X11 and
  render suites pass. Remembering the profile per window, the other lever
  considered, was already there. The measurement on the television, with
  `tools/measure-idle-overhead.py --plugin` and six pairs, was spoiled once
  by a build beside it and found the session locked the second time, at
  22:52 on 2026-10-03; it needs Jens's session open and the machine idle.
- [x] Complete original-buffer and lifecycle integration acceptance. On the
  OpenGL virtual backend, 2026-09-28 (item 72 of the open list): the VM
  production test `testUpscaleProduction` (`autotests/kwin_scaling_test.cpp`)
  passed all eight cases on KWin 6.3.6 in the conformance VM (Mesa llvmpipe on
  vgem). `reducesAndScales`, at scales 1, 1.5 and 3, fullscreen and borderless,
  follows the reduced buffer the client commits in answer to the request,
  requires the effect to scale it, and requires the output to differ from
  KWin's own enlargement of the same buffer once the effect is unloaded;
  `unsupportedBufferFallsBack` requires the output to be pixel-identical to
  ordinary KWin for buffers the effect refuses; the rest cover restoration
  after an ignored request, bypass at native size, a windowed client and an
  unplugged output. KWin master is not run there; it stays build-only.
- [ ] Measure physical-output runs B, C and D using the now-implemented
  cooperative resolution paths; nested timing is not acceptance evidence.
- [ ] Complete real-game, HDR/VRR, image-quality and TV acceptance. VRR is
  blocked on this host: HDMI-A-1 reports adaptive sync incapable.

## Findings from validation

### Implementation checks, 2026-09-18

- The production resolution policy passed its compiled regression test:
  exact presets, nearest-integer rounding, percentage bounds, aspect tolerance,
  native-size bypass, unsupported sizes, large integer dimensions and RCAS
  bypass/strength. It runs through the existing tooling-tests hook.
- Mesa llvmpipe executed the actual shader resources through KWin's EGL,
  shader, framebuffer and texture classes on Trixie. Desktop OpenGL and
  OpenGL ES passed data-driven constant-colour tests for sRGB, gamma 2.2,
  linear and PQ, including negative linear values and HDR range. Orientation,
  resizing, sharpening, clipping, scissor-state restoration, rejection of
  oversized intermediate textures and settings tests also passed.
- Runtime testing found and fixed KWin 6.3.6's `_core` shader-resource lookup,
  its GLES allocator silently using 8-bit storage and the default GLES sampler
  precision discarding HDR detail. These are covered by the rendering tests.
- The configuration tests exercise Automatic, exact preset labels and pixel
  previews, keyboard percentage changes, slider bounds, RCAS defaults and
  save/load/defaults against an isolated configuration directory.
- A 3840 × 2160 virtual KWin 6.3.6 session accepted a native Wayland client's
  1920 × 1080 buffer with a fullscreen viewport. No DRM render node is available
  in this environment, so that KWin uses QPainter and correctly refuses the
  OpenGL-only effect. A nested Wayland KWin also requires a DRM device for its
  OpenGL backend. Neither run establishes compositor shader or VRR acceptance.
- A separate headless test exercised KWin 6.3.6's actual item renderer with a
  synthetic surface. Source capture, EASU and EASU with RCAS preserved the test
  image's orientation and constant colour patches. Capturing a 32 × 18 source
  containing a one-pixel pattern and scaling to 64 × 36 produced exactly the
  same 8-bit readback as scaling the original uploaded image directly (maximum
  channel difference zero), both with and without RCAS. This test used the locally
  checked out 6.3.6 source headers because Debian's development package omits
  an internal header needed by `ItemRendererOpenGL`.

Final automated results on amd64, 2026-09-18:

| Environment | Build | Tests |
| --- | --- | --- |
| Trixie, KWin 6.3.6, GCC | Passed, warnings as errors | 4/4 CTest entries passed, including desktop GL, GLES, configuration and AppStream |
| Trixie, KWin 6.3.6, Clang | Passed, warnings as errors | 4/4 CTest entries passed |
| Neon unstable, KWin development snapshot, GCC | Passed, warnings as errors | 3/3 CTest entries passed, including GLES, configuration and AppStream |
| Neon unstable, KWin development snapshot, Clang | Passed, warnings as errors | 3/3 CTest entries passed |

The Neon snapshot was package version
`4:6.7.5+p24.04+vunstable+git20260915.1132-0`. Trixie's initial GLES test run
could not start because the existing build image lacked `libGLESv2.so.2`.
Installing the declared `libgles2` test dependency in the runtime container
resolved this; both compiler builds then passed there. No test was skipped.

`pre-commit run --all-files` and the complete `pre-push` stage passed in the
Trixie container on a working-tree copy under `build/`, including the compiled
resolution-policy regression, tooling tests and REUSE. The manual rendering
hook passed against the configured Trixie GCC build. `clang-tidy` passed for
the plugin sources, and the plugin metadata passed KDE's JSON schema check.
A Trixie staging install placed the effect and configuration modules in KWin's
respective `effects/plugins` and `effects/configs` plugin directories.

Native installation on wzpc, 2026-09-18: the Release build against installed
KWin 6.3.6 passed with warnings as errors, and all four native CTest entries
passed. The missing KCM development dependencies were installed from Trixie.
The effect and its configuration module were installed under `/usr`; SHA-256
checks confirmed that the installed files match the native build. An isolated,
offscreen KDE plugin-discovery probe found `Upscale` in the standard plugin
directory, confirmed its disabled-by-default metadata and instantiated the
installed configuration module successfully. No KWin instance was started or
effect enabled for this installation.

First session check, 2026-09-18: Jens subsequently logged into KDE, found the
settings entry and enabled the effect. In that native KWin session, the D-Bus
query `isEffectLoaded("upscale")` returned true. The effect's status was
`Inactive: no supplied window buffer.` This confirms discovery and loading;
no scaled game frame or image-quality acceptance was observed in this check.

The maintained rendering and settings tests are built under `autotests/` and
run with `pre-commit run upscale-render-tests --all-files --hook-stage manual`
after configuring and building. Set `UPSCALE_BUILD_DIR` when using a build
directory other than `build`. This hook also runs in the Trixie build CI jobs.
It does not replace the virtual-compositor lifecycle or real-device checks.

Open acceptance: full compositor lifecycle and original-buffer mapping with an
OpenGL virtual backend; real Wayland, Valve Proton and standalone Wine games;
Xwayland buffer compatibility; HDR gradients/gamut/transitions; VRR with composition; total
GPU time, frame times and power; joint TV image-quality comparison. Jens has
explicitly deferred the real-device tests to a later joint session.

### Code review follow-up, 2026-09-18

A review of the implemented slice produced the following changes. Each one is
accompanied by the source comment that records the reason next to the code.

- **Direct scanout is no longer blocked for a window the effect cannot draw.**
  Eligibility is decided from the window and its output, but the render
  target's colour description only becomes visible inside a paint pass. A
  candidate whose target this scaler cannot decode is now remembered, which
  takes the effect out of KWin's active set and releases the scanout
  restriction instead of forcing composition for a frame that falls back to
  KWin's own rendering. A different candidate or a reconfiguration clears it.
  This is covered by the integration test, which fails without the change.
- **The version and precision preamble of both shaders now comes from the
  loader.** KWin 6.3.6 hands a shader file to the compiler unchanged, so a
  file starting with `#version 140` reaches an OpenGL ES context with a
  directive that GLSL ES does not define; Mesa accepts it, other drivers need
  not. The preamble is now chosen in `scaler.cpp`, which also declares the
  float, sampler and integer precision that the OpenGL ES fragment language
  leaves unspecified or medium. Newer KWin supplies its own preamble and
  ignores this one.
- **RCAS operates on a full-range signal again.** Its noise limiter is written
  for values that fill 0 to 1, while the working encoding reserves its lower
  half for negative linear values, leaving ordinary content in 0.5 to 1. The
  sharpening pass now works in the `2 * value - 1` domain. The filter itself is
  affine-equivariant, so only the limiter's decision changes. Image quality
  still requires the joint acceptance session; this removes a known mismatch
  with the published filter rather than settling how sharp the result looks.
- **The destination-sized intermediate is released when sharpening is off**,
  and a texture whose framebuffer could not be completed is no longer kept,
  where it would have made every later attempt at that size fail.
- **Smaller changes.** The failure message uses a KWin logging category; the
  configuration module takes its defaults from `upscaleconfig.kcfg` rather
  than repeating them; damage widening tests the damaged window directly
  instead of searching the stacking order on every damage event of every
  window; the transfer-function numbers the shaders use are asserted against
  KWin's enumeration at compile time.

Two review findings were investigated and deliberately not acted on:

- `SurfaceItem::updatePixmap()` in the eligibility check looks like a side
  effect in a query path, but it is required. KWin creates the surface pixmap
  inside `ItemRenderer::renderItem`, which runs after the effect chain, so
  without it the effect is never eligible and never reaches the paint pass
  that would create the pixmap. Removing it made the integration test fail;
  the call now carries a comment saying so.
- `supported()` asking for OpenGL 3.0 on the development API and 3.1 on 6.3.6
  is correct, not an oversight: KWin dropped its desktop OpenGL backend along
  with that API, so 3.0 there means OpenGL ES 3.0 and supplies GLSL ES 3.00.

Observed results for these changes, all on amd64:

| Environment | Build | Tests |
| --- | --- | --- |
| Trixie, KWin 6.3.6, GCC | Passed, warnings as errors | 6/6 CTest entries passed |
| Trixie, KWin 6.3.6, Clang | Passed, warnings as errors | 6/6 CTest entries passed |
| Neon unstable, KWin development snapshot, GCC | Passed, warnings as errors | 4/4 CTest entries passed |
| Neon unstable, KWin development snapshot, Clang | Passed, warnings as errors | 4/4 CTest entries passed |

`clang-tidy` passed for every source under `src/`, and `pre-commit run
--all-files` passed in the Trixie container for both the commit and the
pre-push stage. `.clang-tidy` gained two exceptions for the function that
`Q_LOGGING_CATEGORY` defines, because KWin's own effects declare their
categories in exactly that way.

The review's all-RGBA32F allocation concern has since been addressed for
non-linear destinations: the current scaler uses RGB10_A2 and direct filtering
there, retaining RGBA32F for signed/extended linear colour. Sharpening off releases
the destination intermediate. The handbook describes this implemented path;
physical-display HDR quality and total rendering cost remain open.

The configuration module still derives its pixel
preview from `QScreen::devicePixelRatio()`, which Qt may quantise at
fractional desktop scales; the effect reports the true destination size over
D-Bus and the preview could use it.

### Planned application tests, 2026-09-18

Jens requested a staged application suite: fullscreen OpenGL and Vulkan
benchmarks, a simple Tux Racer family game and an open-source Vulkan game,
followed by games from Steam and Epic Games Store once the initial suite works.
The handbook now makes Valve Proton and standalone Wine mandatory and defines
the [test order and pass criteria](../upscaling.md#test-applications-and-progression).

Jens clarified that the benchmarks' primary purpose is to measure and assess
performance differences. The handbook's
[measurement protocol](../upscaling.md#benchmark-performance-comparisons) therefore
requires native-resolution baselines, native-size bypass, ordinary KWin
scaling, EASU, and EASU with RCAS. Lower-resolution cases cover 1080p and 1440p
on the unchanged 4K output. Uncapped throughput and cost at a fixed frame rate
are separate series, with warm-up, repeated runs, frame-time distributions and
recorded presentation/scanout state. No performance measurements or speedup
claims have been established yet.

The [resolution-control package](slice-resolution-control.md) owns the
open-source game identification/reduction checks in this shared test sequence. The handbook now specifies the
[game-control acceptance cases](../upscaling.md#game-identification-and-resolution-control-tests):
identify and select the correct game, obtain smaller committed buffers through
our control, leave unrelated windows and the physical output unchanged, and
restore normal policy. Manually choosing a smaller in-game resolution only
provides a baseline. At the time of this plan, profiles and active control were unimplemented.
They now exist; their bounded observations and remaining hardware acceptance
are recorded in the resolution-control slice.

Selected applications and the package candidates observed with
`apt-cache policy` on wzpc:

| Order | Application | Trixie package candidates | Status |
| --- | --- | --- | --- |
| 1 | glmark2, native Wayland and Xwayland | `glmark2-wayland`, `glmark2-x11`: `2023.01+dfsg-2` | Planned; not installed or run |
| 2 | vkmark, Wayland and XCB | `vkmark`: `2025.01-1` | Planned; not installed or run |
| 3 | Extreme Tux Racer, OpenGL | `extremetuxracer`: `0.8.4-1` | Planned; not installed or run |
| 4 | SuperTuxKart, explicitly using Vulkan | `supertuxkart`: `1.4+dfsg-5+b1` | Planned; renderer availability and actual backend still to verify |
| 5 | Steam and Epic games with Valve Proton and standalone Wine | Choose titles and record runtime versions after the initial suite passes | Planned; Windows graphics, HDR and VRR coverage still open |

The application choices were checked against their upstream documentation;
package availability is not an execution result. In particular, the benchmarks'
fullscreen option selects the output size and does not establish a smaller
fullscreen buffer. Each backend needs an observed smaller-buffer path while
the physical output stays at 4K. The known Xwayland mismatch on KWin 6.3.6
remains open and must not be bypassed by testing only newer KWin or native Linux
clients. No additional test application was installed or launched for this plan.

### First runs on real hardware, 2026-09-18

Observed in the running KDE Wayland session on wzpc: Debian 13, KWin 6.3.6
(`4:6.3.6-1`), AMD Strix Halo with Mesa 26.1.2 and the `radeonsi` driver.
HDMI-A-1 presents 3840 x 2160 physical pixels at 120 Hz, normally at desktop
scale 3. The installed effect was loaded and reported supported throughout.
These are real-display runs; they are not the joint image-quality session,
which remains deferred.

The output reports `Adaptive Sync: incapable`. VRR acceptance cannot be
performed on this host and display link at all, so it is blocked rather than
merely untested. A second output path is required before VRR can be verified
here; the requirement itself is unchanged.

#### A0 and A1 throughput, glmark2

`glmark2-wayland --fullscreen` supplied a 3840 x 2160 buffer, so A1 exercised
the native-size bypass. Three alternating repeats per case, fixed scenes at
eight seconds each, after one warm-up run. The effect was loaded and unloaded
over `org.kde.kwin.Effects` between runs rather than through saved settings.

| Scene | A0 median FPS, effect unloaded | A1 median FPS, effect loaded |
| --- | --- | --- |
| texture | 20863 | 22603 |
| shading | 23078 | 23957 |
| build | 20421 | 36666, bimodal |

No overhead from the loaded effect was observed, which matches the expectation
that a native-size buffer never becomes a candidate and never blocks direct
scanout. The result does not yet bound that overhead: A0 always ran first, and
both stable scenes favour A1 by four to eight per cent, which is not a physical
outcome and indicates clock or thermal drift across the sequence. Phase-reversed
repeats are required before quoting a number.

The `build` scene alternated between roughly 20000 and 37000 FPS independently
of the effect state, so glmark2's aggregate score is unusable on this host.
Report per-scene medians, as the measurement protocol already requires.

Runs B, C and D were not measured. `--fullscreen` selects the output size, and
no verified route to a smaller fullscreen game buffer exists on KWin 6.3.6, so
the cost of EASU and of losing direct scanout remains unmeasured. This is the
[resolution-control](slice-resolution-control.md) dependency, observed here as
a direct block on this slice's cost acceptance rather than as a future risk.

#### The effect rejects an eligible supplied buffer on this session

A standalone fixture built from this repository's `autotests/wayland_client.cpp`
committed a shared-memory buffer with a fullscreen viewport, which is the case
the container integration test covers. The effect reported the correct supplied
and destination sizes and still refused the window:

| Buffer | Destination | Desktop scale | Reported state |
| --- | --- | --- | --- |
| 1920 x 1080 | 3840 x 2160 | 3 | Ineligible |
| 2560 x 1440 | 3840 x 2160 | 3 | Ineligible |
| 1920 x 1080 | 3840 x 2160 | 1 | Ineligible |

`activeEffects` never listed `upscale`, so no scanout restriction was taken and
no scaled frame was produced. KWin's own scripting interface confirmed that the
window was fullscreen, had frame geometry equal to the output geometry, opacity
one and the expected output, so the window-level conditions in `eligible()` all
held. The rejection is therefore in the surface-level conditions.

Desktop scale is ruled out: the scale 1 run reproduced the container test's
geometry exactly and still failed. Buffer size is ruled out by the two sizes
above. The remaining candidates are the surface child-item, transform, source
box, opaque-region and buffer-format checks, and the difference between this
session's OpenGL and DRM backend and the container fixture's QPainter backend.
At that first observation the cause was not isolated. The later
TransformedRenderTarget diagnosis and fix above supersede this failure state.

This is the first time the installed effect met a supplied buffer it was built
to scale on real hardware, and it did not scale it. The passing container and
headless tests did not predict this, which is a coverage gap in those tests as
well as a defect somewhere in eligibility. Isolating it requires a diagnostic
effect that reports the failing condition, as the resolution-control experiments
used. Until it is resolved, no rendering, image-quality or cost acceptance can
proceed on this host.

### Required acceptance matrix

Record each actual run here with its environment, result and any remaining
limitation. The following are required checks, not claims of success:

| Area | Required evidence |
| --- | --- |
| Repository checks | Both `pre-commit run --all-files` and `pre-commit run --all-files --hook-stage pre-push` in the container; inspect the output. |
| Builds | Trixie and neon unstable, GCC and Clang, warnings as errors; GLSL and GLSL ES shader validation. |
| Runtime | Tests against KWin's virtual backend in the container; actual original-buffer sizes and pixel mapping. |
| Configuration | Percentage-to-pixel conversion at different output and desktop scales; exact preset ratios, rounding, aspect tolerance, 50% and native boundaries, RCAS bypass and increasing strength. |
| Real game | Complete the OpenGL/Vulkan benchmark and open-source game stages first, then Steam/Epic games with Valve Proton and standalone Wine. On the TV, prove smaller actual buffers and a 4K destination, including 1080p and 1440p; cover the required Windows graphics paths and record runtime versions. |
| Image quality | Compare KWin scaling, EASU, and EASU plus RCAS on identical input; inspect text, HUD, fine edges and camera movement with Jens. |
| HDR | SDR to SDR, SDR to HDR, PQ and scRGB to HDR, and transitions; gradients, highlights, wide-gamut colours and negative scRGB values without accidental clipping or duplicate colour conversion. |
| VRR | Verify adaptive presentation with active scaling and blocked direct scanout, both in SDR and HDR, across changing frame rates within the actual output range. An enabled setting alone is insufficient. |
| Cost | Execute the A0–D benchmark matrix for OpenGL and Vulkan: native resolution, native-size bypass, ordinary scaling, EASU, and EASU with RCAS. Report repeated throughput and fixed-frame-rate cost measurements, variation and relative differences, including the loss of direct scanout. Fixed 60 Hz is one baseline; test 120 Hz where the output path supports it. |
| Lifecycle | Native resolution, windowed mode, unsupported geometry, resource failure, output changes and deactivation preserve normal rendering, cursor and overlays; no stale textures or persistent scanout restriction. |

A driver or display-link limitation leaves the corresponding hardware test
open. It does not remove HDR or VRR from the scope. Falling back for every
HDR input is not HDR upscaling support, and virtual tests do not replace
real-display acceptance.

The native scaler build is installed on wzpc but has not yet been timed or
accepted on the TV.
Documentation checks do not constitute completion of the slice.

PR #14 review follow-up: the GLES storage replacement discards prior errors
from KWin’s shared GL context before testing its own allocation. A rendering
regression supplies an earlier GL error and requires valid floating-point
storage. Combined-candidate validation is pending.
Closed 2026-09-28 (item 77 of the open list): the regression is
`UpscaleRenderTest::allocationIgnoresEarlierErrors`, which runs in
`upscale-render` and `upscale-render-gles`, and both passed in every full check
run of the day, among them the one on the tree committed as `f68eab0`, with GCC
and Clang on Trixie.

## Aspect ratio and integer scaling

Merged into this document on 2026-09-20. It is the same subject as the rest of
the package — what the picture is scaled with, and what shape it ends up — and
it was never started, so keeping it apart only split one rendering acceptance
across two documents. Nothing here is closed by the merge, and none of it is
inside the supported scope stated above: every case below still falls back to
ordinary KWin rendering and must be named as unsupported in a release that
ships without it.

Status: required on 2026-09-18; built for 0.3.0 as Jens decided on 2026-09-29,
Fit and Integer both; real-device acceptance open. The permanent
[handbook](../upscaling.md#aspect-ratio-and-integer-scaling) defines the behaviour.
Keep this document until implementation and required testing, including
real-device acceptance, are complete.

### Start state

The current path requires matching aspect ratios and limits enlargement to
two times per axis. No aspect-preserving bars or integer nearest-neighbour
path has been implemented or accepted.

### End state

Fullscreen games can use aspect-preserving fit or integer nearest-neighbour
scaling with correct bars, input mapping and colour/presentation handling.
Global/profile controls report effective geometry, and the acceptance examples,
supported-container checks and real-device tests below have passed.

### Dependencies

Extend the existing renderer and global controls. Integrate sparse settings
through [profiles](slice-application-profiles.md); the
[interactive controls](slice-development-infrastructure.md#interactive-in-game-controls)
can expose the same settings when available but is not required to validate
them through the configuration module. Use verified resolution-control surface
handling for cooperative clients. Already-full-output borderless windows are
eligible today; arbitrary smaller-window fullscreen presentation
and display-specific settings remain excluded as stated below.

### Scope

Support fullscreen games with aspect ratios different from the output, and
pixel art with integer nearest-neighbour scaling. Preserve the whole image,
centre it with black bars and leave the physical output mode unchanged. Provide
global defaults and sparse application overrides, with controls and status in
the configuration module and a shared interface for the planned overlay. HDR and VRR remain requirements.

This is a required extension beyond the initial FSR path's matching-aspect and
two-times-per-axis restrictions. Broader integer factors apply to the nearest
filter; this does not relax FSR's supported range. Display-specific overrides,
fullscreen presentation of windowed games and sharpening at native resolution
remain optional later directions, outside this slice.

### Approach

1. Separate source dimensions, output dimensions and the image destination
   rectangle. Compute aspect-preserving fit and integer geometry in physical
   pixels, including one-times presentation and an explicit unavailable result
   when no positive integer factor fits.
2. Separate geometry from filter choice. Add nearest-neighbour sampling without
   sharpening for pixel replication; retain the documented FSR limits. Resolve
   supported combinations before rendering, report unavailable ones and preserve
   normal rendering on failure.
3. Integrate the new destination with KWin's rendering and input handling on both
   supported targets. Establish how fullscreen coverage, pointer coordinates,
   confinement, relative motion, popups and surface trees stay consistent;
   drawing a smaller rectangle alone is insufficient. Keep KWin unpatched.
4. Draw black bars and filter the image without sampling across its boundary.
   Reuse KWin colour management, preserve separate overlays and the cursor, and
   update damage/cleanup on geometry changes without continuous repainting.
5. Expose global/profile settings and live changes only for verified geometry
   and input paths. Report both image destination and output dimensions. Keep
   comparison mode at the same supplied input and destination geometry.
6. Add appropriate geometry, rendering, configuration and integration coverage,
   then complete real-game and TV acceptance. Preserve durable decisions in
   source comments and the handbook before deleting this slice document.

### Acceptance criteria

Planned checks, not observed results:

- Fit: 1440 × 1080 on 3840 × 2160 gives a 2880 × 2160 image and 480-pixel side
  bars. Cover wider inputs with top/bottom bars, odd dimensions, rounding,
  desktop scaling, resize and output changes; preserve the complete image.
- Integer: 320 × 240 on 3840 × 2160 uses 9×; 1280 × 720 uses 3× and fills that
  output. Cover factors of one, no fitting factor, invalid/large dimensions
  and overflow. No fractional factor or stretching may be labelled integer.
- Rendering: use pixel patterns to prove nearest-neighbour replication,
  orientation, bar placement, image-edge sampling and sharpening bypass.
  Keep supplied letterboxing unchanged. Test permitted FSR combinations and
  clear fallback for unsupported geometry/filter choices.
- Input/lifecycle: native Wayland and Xwayland, including Proton and standalone
  Wine; absolute and relative pointer movement, confinement/locking, focus,
  popups, separate overlays, deactivation, failure and restoring normal rendering.
  The output mode and unrelated windows must remain unchanged.
- Configuration and shared status: global defaults and sparse overrides, live changes,
  actual image/output dimensions, unavailable states and unchanged saved values
  after temporary comparison.
- Run repository checks in Trixie, build with GCC and Clang with warnings as
  errors in both Trixie and neon-unstable, and run relevant rendering and
  virtual-compositor tests. On wzpc, inspect retro/pixel-art and differing-aspect
  games on the TV, verify input, HDR and actual adaptive presentation, and
  measure the added processing cost.

### Progress and remaining work

- [x] Record required scope and distinguish optional later features.
- [x] Specify geometry, filter separation and acceptance examples.
- [x] Establish supported KWin geometry/input integration.
- [x] Implement geometry, nearest filtering, settings and status.
- [ ] Complete automated checks, both compiler/container builds and TV acceptance.

Documentation validation, 2026-09-18: `pre-commit run --all-files` and the complete
pre-push stage passed in Trixie on an isolated copy under
`build/overlay-geometry-check`, containing the committed source and updated
documents. Local documentation links and heading anchors resolved. No
implementation, rendering test or real-device acceptance was performed for
this extension.

### Built, 2026-09-29

Item 76 of the open list. `upscalePicture()` in `picture.h` places the picture
in the output's device pixels for the geometry and filter in force and names
what it cannot honour; the eligibility chain refuses by that answer
(`BufferNoWholeFactor`, `BufferFilterRange` beside the existing sizes), and a
different aspect ratio is no longer refused. The scaler draws up to four black
bars from a one-pixel texture through KWin's texture shader, then the picture:
EASU with optional RCAS into the picture's rectangle, or for Nearest a plain
copy of the capture, which is sampled nearest already, with sharpening forced
to zero. The status names the result: "fitted into W × H with bars", or
"enlarged N times", with the filter.

Input: `UpscalePictureInput`, an input event filter just ahead of KWin's window
actions, gives the seat the transformation from the picture to the surface,
scales relative motion by the same factor, and warps a confined pointer back
onto the picture's edge. It serves native Wayland games and X11 games in a mode
of their own, which Xwayland's emulation maps from the whole frame onto the
drawable; KWin keeps such a window at its mode only from 6.6 on. An X11 window
the effect asked for a size stays with `UpscaleX11Input`, whose presented
mapping and emulated-mode coverage now take the picture instead of the frame.
`presentationScale()` answers "not presented" apart from a scale of one: a
whole factor of one had lost its origin, and the pointer arrived unmapped.

Settings: `Geometry` and `Filter` in `kwinrc`, as numbers, and in an entry by
name (Fit/Integer, FSR/Nearest), on the Scaling tab of both panels, which
replaced the Sharpening tab. `settingtext.cpp` split off `settingcontrols.cpp`,
which the two choices took past the size limit. The test driver now reads its
check pixel at the picture's corner: at the output's corner a centred picture
leaves a bar, and the driver aborted KWin.

Tests: `upscale-picture` covers the handbook's examples, both FSR limits,
factor one, odd bars, no whole factor and the refusals;
`laysThePictureBetweenBars` renders bars and picture on GL and GLES;
`mapsThePointerOntoThePicture` runs a native Wayland client with a 64 × 80
buffer on the 128 × 128 session and checks two positions and the confined clamp
at both edges; `fitsAnEmulatedModeBetweenBars` sets a 1440 × 1080 mode itself
and is skipped before KWin 6.6; `centresAPresentedWindowByAWholeFactor` asks for
2560 × 1440 with Integer and Nearest. Seen failing against the defect each
guards: the Wayland case with the filter's motion handling disabled, (20, 64)
unmapped, and with only the clamp disabled, x −11.3; the own-mode case with X11
excluded again, (254, 100) stretched, on KWin 6.6; the whole-factor case before
the fix, (940, 660) unmapped. The settings page, a game's tab and an entry read
and store both choices.

Left open: relative motion was not driven by a relative-pointer client, and a
locked pointer, popups and subsurfaces of a game with bars, separate overlays,
HDR and VRR are untested; the real games on the TV, retro and differing-aspect
titles, and the added processing cost remain the acceptance above.

## Generic benchmark inputs, 2026-09-28

Jens requires game-specific knowledge to live only in the settings catalogue,
including removal of per-game benchmark code. The effect and proxy already
select behavior through profile settings. The measurement and presentation
tools still embed launch arguments, menu sequences, configuration paths and
log formats for particular titles.

Replace those definitions with explicit command-line inputs: command and
arguments, window identity, startup time, optional keys and optional output
patterns. Remove game configuration writers; the operator supplies a repeatable
scene and initial settings. Record requested protocol and renderer separately
from observations, without injecting toolkit or game-specific arguments.
The presentation runner checks one explicitly described case per invocation.
Update the handbook and regression tests alongside the tools.

Supported-scope acceptance is arbitrary commands preserved as argument vectors,
measurements restricted to the named window, output parsed only with an explicit
pattern, and clean maintained-container Python checks. This does not claim new
performance or hardware results. The existing full physical acceptance matrix
below remains open. The X11 state cleanup and XTS run remain owned by the
[resolution-control slice](slice-resolution-control.md#x11-negotiation-ownership-2026-09-28).

Implemented explicit commands and window identities in both benchmark runners,
and removed `game_settings.py`, `game_output.py` and the remaining per-game
resolution reset. Optional stdout capture patterns replace built-in log formats;
commands, patterns and the expected and observed renderer remain separate in
the record. Process output goes to a run log instead of an undrained pipe.
Both lint stages, strict typing and the checker regression suite passed in the
maintained Trixie container. The added tests cover literal argument boundaries,
duration substitution, unknown observations without patterns, malformed patterns
and rejection of nonfinite rates. No physical benchmark was run. Jens then
directed work back to the proxy simplification; no further benchmark scope is
being added.
