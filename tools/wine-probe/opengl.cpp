/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// OpenGL through WGL, which Wine's display driver serves itself.

#include "probe.h"

#include <GL/gl.h>

namespace
{

class OpenGL : public Renderer
{
public:
    bool start(HWND window, int width, int height, bool exclusive, std::string &error) override
    {
        if (exclusive && !setDisplayMode(width, height, error)) {
            return false;
        }
        m_device = GetDC(window);
        PIXELFORMATDESCRIPTOR format = {};
        format.nSize = sizeof(format);
        format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32;
        format.iLayerType = PFD_MAIN_PLANE;
        if (!SetPixelFormat(m_device, ChoosePixelFormat(m_device, &format), &format)) {
            error = "no OpenGL pixel format";
            return false;
        }
        const HGLRC context = wglCreateContext(m_device);
        if (!context || !wglMakeCurrent(m_device, context)) {
            error = "no OpenGL context";
            return false;
        }
        return true;
    }

    void frame(int width, int height) override
    {
        glViewport(0, 0, width, height);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, leftHalf(width), height);
        glClearColor(1.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(leftHalf(width), 0, width - leftHalf(width), height);
        glClearColor(0.0F, 0.0F, 1.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        SwapBuffers(m_device);
    }

    std::string device() const override
    {
        return reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    }

private:
    HDC m_device = nullptr;
};

}

std::unique_ptr<Renderer> makeOpenGL()
{
    return std::make_unique<OpenGL>();
}
