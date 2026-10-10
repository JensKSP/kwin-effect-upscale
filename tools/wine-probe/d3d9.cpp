/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Direct3D 9, which Wine translates itself; exclusive is its own fullscreen,
// a device that is not windowed.

#include "probe.h"

#include <d3d9.h>

namespace
{

class Direct3D9 : public Renderer
{
public:
    bool start(HWND window, int width, int height, bool exclusive, std::string &error) override
    {
        m_direct3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!m_direct3d) {
            error = "Direct3DCreate9 failed";
            return false;
        }
        D3DADAPTER_IDENTIFIER9 adapter = {};
        if (SUCCEEDED(m_direct3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &adapter))) {
            m_name = adapter.Description;
        }
        m_parameters.Windowed = !exclusive;
        m_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        m_parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
        m_parameters.BackBufferWidth = UINT(width);
        m_parameters.BackBufferHeight = UINT(height);
        m_parameters.BackBufferCount = 1;
        m_parameters.hDeviceWindow = window;
        m_parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        const HRESULT result = m_direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &m_parameters, &m_device);
        if (FAILED(result)) {
            error = "CreateDevice answered " + hresult(result);
            return false;
        }
        return true;
    }

    void frame(int width, int height) override
    {
        // A windowed device draws at its window's size; a fullscreen one at
        // the mode it set.
        if (m_parameters.Windowed && (UINT(width) != m_parameters.BackBufferWidth || UINT(height) != m_parameters.BackBufferHeight)) {
            m_parameters.BackBufferWidth = UINT(width);
            m_parameters.BackBufferHeight = UINT(height);
            if (FAILED(m_device->Reset(&m_parameters))) {
                return;
            }
        }
        const LONG right = LONG(m_parameters.BackBufferWidth);
        const LONG bottom = LONG(m_parameters.BackBufferHeight);
        const D3DRECT first = {0, 0, leftHalf(right), bottom};
        const D3DRECT second = {leftHalf(right), 0, right, bottom};
        m_device->Clear(1, &first, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 0), 1.0F, 0);
        m_device->Clear(1, &second, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0F, 0);
        m_device->Present(nullptr, nullptr, nullptr, nullptr);
    }

    std::string device() const override
    {
        return m_name;
    }

private:
    IDirect3D9 *m_direct3d = nullptr;
    IDirect3DDevice9 *m_device = nullptr;
    D3DPRESENT_PARAMETERS m_parameters = {};
    std::string m_name;
};

}

std::unique_ptr<Renderer> makeDirect3D9()
{
    return std::make_unique<Direct3D9>();
}
