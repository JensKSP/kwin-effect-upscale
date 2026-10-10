/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <windows.h>

#include <memory>
#include <string>

// One graphics API, as a game uses it. It makes its device for the window and
// then, each frame, draws the left half of the window's client area red and
// the right half blue, at the size the client area has then, and shows it.
class Renderer
{
public:
    virtual ~Renderer() = default;
    // False, with what failed in @p error, where the API cannot be used here.
    virtual bool start(HWND window, int width, int height, bool exclusive, std::string &error) = 0;
    virtual void frame(int width, int height) = 0;
    // The device that draws, as the API names it.
    virtual std::string device() const = 0;
};

std::unique_ptr<Renderer> makeOpenGL();
std::unique_ptr<Renderer> makeDirect3D9();
std::unique_ptr<Renderer> makeDirect3D11();
std::unique_ptr<Renderer> makeDirect3D12();
std::unique_ptr<Renderer> makeVulkan();

// Set the display mode a game sets for exclusive fullscreen where its API has
// no fullscreen of its own, OpenGL's and Vulkan's: the screen's own size.
bool setDisplayMode(int width, int height, std::string &error);

// A failed call's result, as the API documentation lists it.
std::string hresult(long result);

// The first half of a frame of @p width, and the second, as the probe splits it.
inline int leftHalf(int width)
{
    return width / 2;
}
