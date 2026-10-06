/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
 * A Windows program drawing with OpenGL into a borderless window over its
 * whole screen, as a game in borderless fullscreen does: the left half red,
 * the right half blue. Run under Wine by tools/package_wine.py, it prints the
 * screen Wine reports and the client area it got whenever that changes, and
 * ends after the seconds it is given, 60 by default.
 *
 *     x86_64-w64-mingw32-gcc -O2 -o probe.exe wine-opengl-probe.c -lopengl32 -lgdi32
 */

#include <windows.h>

#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

static LRESULT CALLBACK handle(HWND window, UINT message, WPARAM word, LPARAM value)
{
    if (message == WM_CLOSE || message == WM_DESTROY || (message == WM_KEYDOWN && word == VK_ESCAPE)) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, word, value);
}

static void draw(int width, int height)
{
    glViewport(0, 0, width, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, width / 2, height);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(width / 2, 0, width - width / 2, height);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

int main(int argc, char **argv)
{
    const DWORD seconds = argc > 1 ? (DWORD)strtoul(argv[1], NULL, 10) : 60;
    const HINSTANCE instance = GetModuleHandleW(NULL);
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    WNDCLASSW kind = {0};
    kind.style = CS_OWNDC;
    kind.lpfnWndProc = handle;
    kind.hInstance = instance;
    kind.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    kind.lpszClassName = L"UpscaleProbe";
    RegisterClassW(&kind);
    const HWND window = CreateWindowExW(0, kind.lpszClassName, L"Upscale probe", WS_POPUP | WS_VISIBLE, 0, 0, width, height, NULL, NULL, instance, NULL);
    const HDC device = GetDC(window);
    PIXELFORMATDESCRIPTOR format = {0};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.iLayerType = PFD_MAIN_PLANE;
    if (!window || !SetPixelFormat(device, ChoosePixelFormat(device, &format), &format)) {
        printf("no window with an OpenGL pixel format\n");
        return 1;
    }
    const HGLRC context = wglCreateContext(device);
    if (!context || !wglMakeCurrent(device, context)) {
        printf("no OpenGL context\n");
        return 1;
    }
    printf("screen %dx%d, renderer %s\n", width, height, (const char *)glGetString(GL_RENDERER));
    fflush(stdout);
    RECT shown = {0};
    const DWORD end = GetTickCount() + seconds * 1000;
    MSG message;
    while (GetTickCount() < end) {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
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
            printf("client %ldx%ld\n", client.right, client.bottom);
            fflush(stdout);
        }
        draw(client.right, client.bottom);
        SwapBuffers(device);
    }
    return 0;
}
