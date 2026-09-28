<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Third-party notices

Upscale is licensed under the GNU General Public License, version 2 or any
later version (`GPL-2.0-or-later`). This file names what it contains from
others and what it links to, and carries the notices those parts require. It is
installed with every package, with or without the settings module, as
`share/kwin-effect-upscale/third-party-notices.md`.

## Adapted code in the effect

### AMD FidelityFX Super Resolution 1

The effect's scaler is AMD's FidelityFX Super Resolution 1 (FSR 1), version
`v1.20210629`: its edge-adaptive spatial upsampling (EASU) and its robust
contrast-adaptive sharpening (RCAS). They are translated to GLSL in
`src/plugins/upscale/shaders/easu.glsl` and `rcas.glsl`, which the effect
carries as shader resources inside its plugin.

Changed from AMD's reference: the arithmetic is written in GLSL's 32-bit float
and vector types; exact reciprocals replace the reference's bit-level
approximations; flat neighbourhoods, and constant black or white in RCAS, are
guarded explicitly, where an exact reciprocal would produce NaNs; and RCAS's
strength multiplies its lobe, with zero handled by skipping the pass.

- Copyright holder: Advanced Micro Devices, Inc.
- License: MIT (`MIT`)

```text
FidelityFX Super Resolution Sample

Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

Nothing else from a third party is compiled into the packages. The source tree
also holds KWin's `.clang-format` (MIT, Christoph Cullmann and Gernot Gebhard),
which formats the code and is not installed, and CI and tool configuration
dedicated to the public domain under `CC0-1.0`; `debian/copyright` and the
SPDX headers name every file.

## Linked system libraries

The effect, its settings module and the X11 session proxy are linked against
libraries the system provides; none of them is copied into the packages. Their
licenses are the ones their own packages state, which differ between modules of
one project, so they are pointed to rather than restated here. On Debian and
Ubuntu each package's terms are in `/usr/share/doc/<package>/copyright`; the
other distributions install theirs with the package as well.

| Component | Used by | Debian package of the library |
| --- | --- | --- |
| KWin (`libkwin`) | effect | `libkwin6` |
| Qt 6: Core, Gui, Widgets, DBus, OpenGL, Network | all three | `libqt6core6t64`, `libqt6gui6`, `libqt6widgets6`, `libqt6dbus6`, `libqt6opengl6`, `libqt6network6` |
| Qt 6: Qml, Quick and their models | effect and settings, through KWin and KCMUtils | `libqt6qml6`, `libqt6quick6`, `libqt6qmlmodels6`, `libqt6qmlmeta6`, `libqt6qmlworkerscript6` |
| KDE Frameworks: CoreAddons, Config, I18n | all three | `libkf6coreaddons6`, `libkf6configcore6`, `libkf6configgui6`, `libkf6i18n6` |
| KDE Frameworks: WindowSystem | effect | `libkf6windowsystem6` |
| KDE Frameworks: KCMUtils, ConfigWidgets, WidgetsAddons, ColorScheme | settings | `libkf6kcmutils6`, `libkf6kcmutilscore6`, `libkf6kcmutilsquick6`, `libkf6configwidgets6`, `libkf6widgetsaddons6`, `libkf6colorscheme6` |
| Wayland server library | effect | `libwayland-server0` |
| libepoxy | effect | `libepoxy0` |
| libdrm | effect | `libdrm2` |
| libxcb, with its RandR and X-Resource extensions, and Xlib | effect | `libxcb1`, `libxcb-randr0`, `libxcb-res0`, `libx11-6` |
| libglvnd (GLX, OpenGL) | effect and settings | `libglx0`, `libopengl0` |
| The C and C++ runtime | all three | `libc6`, `libstdc++6`, `libgcc-s1` |

The list is what the built binaries name as their dependencies on Debian 13,
read with `readelf -d`; another distribution's build can differ in a library
KWin itself pulls in.
