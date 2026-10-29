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

### Snapdragon Game Super Resolution 1

The Snapdragon GSR 1 scaling filter is Qualcomm's single-pass shader,
`sgsr/v1/include/glsl/sgsr1_shader_mobile.frag` at commit `d926f074`, in its
RGBA operation mode. It is carried as `src/plugins/upscale/shaders/gsr.frag`,
a shader resource inside the plugin.

Changed from Qualcomm's reference: the texels its `textureGather` calls and its
bilinear base tap read are fetched one by one, because the effect's capture is
sampled nearest and OpenGL ES 3.0 has no gather; its edge sharpness is set by
the effect's sharpening strength; and for a linear destination it filters the
effect's bounded working encoding, shifted into -1 to 1, with its result kept
in that range.

- Copyright holder: Qualcomm Innovation Center, Inc.
- License: BSD 3-Clause (`BSD-3-Clause`)

```text
Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

### NVIDIA Image Scaling

The NVIDIA Image Scaling filter is `NVScaler` from NVIDIA's Image Scaling SDK,
version 1.0.3 (`NIS/NIS_Scaler.h` and `NIS/NIS_Config.h` at commit
`35e13ba3`). It is carried as `src/plugins/upscale/shaders/nis.frag` and
`nis_coefficients.glsl`, shader resources inside the plugin, and the part of
`NVScalerUpdateConfig` that shapes the filter as
`src/plugins/upscale/nisconfig.h`.

Changed from NVIDIA's reference: the compute shader, which shares a tile of
luma and edge maps between a block of output pixels, is a fragment program in
which each output pixel reads its own six by six neighbourhood and derives the
four edge maps it interpolates; its two filter banks are constants rather than
a texture; its bilinear colour tap is computed from four texel fetches; an
encoded destination's output is clamped to zero to one; a linear destination is
filtered in NVIDIA's unit of 80 nits, with its luma kept from going negative;
and a sharpness of zero leaves no unsharp mask.

- Copyright holder: NVIDIA Corporation & Affiliates
- License: MIT (`MIT`)

```text
The MIT License(MIT)

Copyright(c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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
