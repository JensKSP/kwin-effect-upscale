/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Direct3D 11 on a flip-model swap chain, as current games present; exclusive
// is DXGI's fullscreen state. The frame is copied from a texture filled once
// for its size, so that no shader is needed.

#include "probe.h"

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <vector>

namespace
{

class Direct3D11 : public Renderer
{
public:
    bool start(HWND window, int width, int height, bool exclusive, std::string &error) override
    {
        DXGI_SWAP_CHAIN_DESC description = {};
        description.BufferDesc.Width = UINT(width);
        description.BufferDesc.Height = UINT(height);
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.OutputWindow = window;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        const HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                                             &description, &m_swapChain, &m_device, nullptr, &m_context);
        if (FAILED(result)) {
            error = "D3D11CreateDeviceAndSwapChain answered " + hresult(result);
            return false;
        }
        if (exclusive) {
            const HRESULT fullscreen = m_swapChain->SetFullscreenState(TRUE, nullptr);
            if (FAILED(fullscreen)) {
                error = "SetFullscreenState answered " + hresult(fullscreen);
                return false;
            }
        }
        m_width = width;
        m_height = height;
        return fill();
    }

    void frame(int width, int height) override
    {
        if (width != m_width || height != m_height) {
            if (m_source) {
                m_source->Release();
                m_source = nullptr;
            }
            if (FAILED(m_swapChain->ResizeBuffers(0, UINT(width), UINT(height), DXGI_FORMAT_UNKNOWN, 0))) {
                return;
            }
            m_width = width;
            m_height = height;
            if (!fill()) {
                return;
            }
        }
        ID3D11Texture2D *back = nullptr;
        if (SUCCEEDED(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back)))) {
            m_context->CopyResource(back, m_source);
            back->Release();
        }
        m_swapChain->Present(1, 0);
    }

    std::string device() const override
    {
        IDXGIDevice *device = nullptr;
        IDXGIAdapter *adapter = nullptr;
        DXGI_ADAPTER_DESC description = {};
        if (SUCCEEDED(m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&device)))) {
            if (SUCCEEDED(device->GetAdapter(&adapter))) {
                adapter->GetDesc(&description);
                adapter->Release();
            }
            device->Release();
        }
        char name[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name), nullptr, nullptr);
        return name;
    }

private:
    // The texture the frame is copied from, red in its left half and blue in
    // its right, at the swap chain's size.
    bool fill()
    {
        std::vector<std::uint32_t> pixels(std::size_t(m_width) * std::size_t(m_height));
        for (int y = 0; y < m_height; ++y) {
            for (int x = 0; x < m_width; ++x) {
                // R8G8B8A8 in memory, read as one little-endian word.
                pixels[std::size_t(y) * std::size_t(m_width) + std::size_t(x)] = x < leftHalf(m_width) ? 0xff0000ffU : 0xffff0000U;
            }
        }
        D3D11_TEXTURE2D_DESC description = {};
        description.Width = UINT(m_width);
        description.Height = UINT(m_height);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = pixels.data();
        data.SysMemPitch = UINT(m_width) * 4;
        return SUCCEEDED(m_device->CreateTexture2D(&description, &data, &m_source));
    }

    IDXGISwapChain *m_swapChain = nullptr;
    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_context = nullptr;
    ID3D11Texture2D *m_source = nullptr;
    int m_width = 0;
    int m_height = 0;
};

}

std::unique_ptr<Renderer> makeDirect3D11()
{
    return std::make_unique<Direct3D11>();
}
