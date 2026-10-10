/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A Windows program that draws as a game does, through the graphics API it is
// given, into a window over its whole screen: borderless, a popup window of
// the screen's size, or exclusive, which asks the API for exclusive
// fullscreen or, for OpenGL and Vulkan, sets the display mode first. The left
// half is red and the right half blue. tools/package_wine.py runs it under
// Wine with each of Wine's display drivers. It prints the screen Wine reports,
// the device that draws, and the client area it got whenever that changes,
// and ends after the seconds it is given, 60 by default.
//
//     probe.exe opengl|d3d9|d3d11|d3d12|vulkan borderless|exclusive [SECONDS]
//
// Built by tools/package_wine.py with MinGW-w64; see its build_probe().

#include "probe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static LRESULT CALLBACK handle(HWND window, UINT message, WPARAM word, LPARAM value)
{
    if (message == WM_CLOSE || message == WM_DESTROY || (message == WM_KEYDOWN && word == VK_ESCAPE)) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, word, value);
}

std::string hresult(long result)
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08lx", static_cast<unsigned long>(result));
    return text;
}

bool setDisplayMode(int width, int height, std::string &error)
{
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    mode.dmPelsWidth = DWORD(width);
    mode.dmPelsHeight = DWORD(height);
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    const LONG result = ChangeDisplaySettingsExW(nullptr, &mode, nullptr, CDS_FULLSCREEN, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL) {
        error = "ChangeDisplaySettingsEx answered " + std::to_string(result);
        return false;
    }
    return true;
}

static std::unique_ptr<Renderer> renderer(const char *api)
{
    if (!std::strcmp(api, "opengl")) {
        return makeOpenGL();
    }
    if (!std::strcmp(api, "d3d9")) {
        return makeDirect3D9();
    }
    if (!std::strcmp(api, "d3d11")) {
        return makeDirect3D11();
    }
    if (!std::strcmp(api, "d3d12")) {
        return makeDirect3D12();
    }
    if (!std::strcmp(api, "vulkan")) {
        return makeVulkan();
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    std::unique_ptr<Renderer> drawing = argc > 2 ? renderer(argv[1]) : nullptr;
    const bool exclusive = argc > 2 && !std::strcmp(argv[2], "exclusive");
    if (!drawing || (!exclusive && std::strcmp(argv[2], "borderless"))) {
        std::printf("usage: probe opengl|d3d9|d3d11|d3d12|vulkan borderless|exclusive [seconds]\n");
        return 2;
    }
    const DWORD seconds = argc > 3 ? DWORD(std::strtoul(argv[3], nullptr, 10)) : 60;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    WNDCLASSW kind = {};
    kind.style = CS_OWNDC;
    kind.lpfnWndProc = handle;
    kind.hInstance = instance;
    kind.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    kind.lpszClassName = L"UpscaleProbe";
    RegisterClassW(&kind);
    const HWND window = CreateWindowExW(0, kind.lpszClassName, L"Upscale probe", WS_POPUP | WS_VISIBLE, 0, 0, width, height,
                                        nullptr, nullptr, instance, nullptr);
    std::string error;
    if (!window || !drawing->start(window, width, height, exclusive, error)) {
        std::printf("unavailable: %s\n", window ? error.c_str() : "no window");
        return 3;
    }
    std::printf("screen %dx%d, %s %s on %s\n", width, height, argv[1], argv[2], drawing->device().c_str());
    std::fflush(stdout);
    RECT shown = {};
    const DWORD end = GetTickCount() + seconds * 1000;
    MSG message;
    while (GetTickCount() < end) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return 0;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        RECT client;
        GetClientRect(window, &client);
        if (client.right != shown.right || client.bottom != shown.bottom) {
            shown = client;
            std::printf("client %ldx%ld\n", client.right, client.bottom);
            std::fflush(stdout);
        }
        if (client.right > 0 && client.bottom > 0) {
            drawing->frame(client.right, client.bottom);
        }
    }
    return 0;
}
