/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Direct3D 12, which Wine runs through vkd3d on Vulkan; exclusive is DXGI's
// fullscreen state. d3d12.dll is loaded when asked for, so that the probe
// starts for the other APIs where Wine has no Direct3D 12.

#include "probe.h"

#include <d3d12.h>
#include <dxgi1_4.h>

namespace
{

constexpr UINT bufferCount = 2;

class Direct3D12 : public Renderer
{
public:
    bool start(HWND window, int width, int height, bool exclusive, std::string &error) override
    {
        const HMODULE library = LoadLibraryW(L"d3d12.dll");
        const auto create = library ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(reinterpret_cast<void *>(GetProcAddress(library, "D3D12CreateDevice")))
                                    : nullptr;
        if (!create) {
            error = "no d3d12.dll";
            return false;
        }
        IDXGIFactory4 *factory = nullptr;
        HRESULT result = CreateDXGIFactory2(0, __uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory));
        if (SUCCEEDED(result)) {
            result = create(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&m_device));
        }
        if (FAILED(result)) {
            error = "D3D12CreateDevice answered " + hresult(result);
            return false;
        }
        D3D12_COMMAND_QUEUE_DESC queue = {};
        queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        result = m_device->CreateCommandQueue(&queue, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&m_queue));
        if (FAILED(result)) {
            error = "CreateCommandQueue answered " + hresult(result);
            return false;
        }
        DXGI_SWAP_CHAIN_DESC1 description = {};
        description.Width = UINT(width);
        description.Height = UINT(height);
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = bufferCount;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGISwapChain1 *first = nullptr;
        result = factory->CreateSwapChainForHwnd(m_queue, window, &description, nullptr, nullptr, &first);
        if (FAILED(result)) {
            error = "CreateSwapChainForHwnd answered " + hresult(result);
            return false;
        }
        result = first->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&m_swapChain));
        if (FAILED(result)) {
            error = "IDXGISwapChain3 answered " + hresult(result);
            return false;
        }
        first->Release();
        factory->Release();
        if (exclusive) {
            result = m_swapChain->SetFullscreenState(TRUE, nullptr);
            if (FAILED(result)) {
                error = "SetFullscreenState answered " + hresult(result);
                return false;
            }
        }
        D3D12_DESCRIPTOR_HEAP_DESC heap = {};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = bufferCount;
        m_step = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        if (FAILED(m_device->CreateDescriptorHeap(&heap, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void **>(&m_heap)))
            || FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                       reinterpret_cast<void **>(&m_allocator)))
            || FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator, nullptr, __uuidof(ID3D12GraphicsCommandList),
                                                  reinterpret_cast<void **>(&m_list)))
            || FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&m_fence)))) {
            error = "no descriptor heap, command list or fence";
            return false;
        }
        m_list->Close();
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_width = width;
        m_height = height;
        views();
        return true;
    }

    void frame(int width, int height) override
    {
        if (width != m_width || height != m_height) {
            release();
            if (FAILED(m_swapChain->ResizeBuffers(bufferCount, UINT(width), UINT(height), DXGI_FORMAT_UNKNOWN, 0))) {
                return;
            }
            m_width = width;
            m_height = height;
            views();
        }
        const UINT index = m_swapChain->GetCurrentBackBufferIndex();
        m_allocator->Reset();
        m_list->Reset(m_allocator, nullptr);
        barrier(index, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE view = handle(index);
        const D3D12_RECT first = {0, 0, leftHalf(m_width), m_height};
        const D3D12_RECT second = {leftHalf(m_width), 0, m_width, m_height};
        const float red[4] = {1.0F, 0.0F, 0.0F, 1.0F};
        const float blue[4] = {0.0F, 0.0F, 1.0F, 1.0F};
        m_list->ClearRenderTargetView(view, red, 1, &first);
        m_list->ClearRenderTargetView(view, blue, 1, &second);
        barrier(index, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        m_list->Close();
        ID3D12CommandList *lists[] = {m_list};
        m_queue->ExecuteCommandLists(1, lists);
        m_swapChain->Present(1, 0);
        wait();
    }

    std::string device() const override
    {
        return "Direct3D 12 at feature level 11_0";
    }

private:
    D3D12_CPU_DESCRIPTOR_HANDLE handle(UINT index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE start = m_heap->GetCPUDescriptorHandleForHeapStart();
        start.ptr += SIZE_T(index) * SIZE_T(m_step);
        return start;
    }

    void views()
    {
        for (UINT index = 0; index < bufferCount; ++index) {
            m_swapChain->GetBuffer(index, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&m_buffers[index]));
            m_device->CreateRenderTargetView(m_buffers[index], nullptr, handle(index));
        }
    }

    void release()
    {
        wait();
        for (ID3D12Resource *&buffer : m_buffers) {
            if (buffer) {
                buffer->Release();
                buffer = nullptr;
            }
        }
    }

    void barrier(UINT index, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER transition = {};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = m_buffers[index];
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transition.Transition.StateBefore = before;
        transition.Transition.StateAfter = after;
        m_list->ResourceBarrier(1, &transition);
    }

    // Each frame is finished before the next is recorded, which a probe can
    // afford and which keeps one allocator enough.
    void wait()
    {
        ++m_fenceValue;
        m_queue->Signal(m_fence, m_fenceValue);
        if (m_fence->GetCompletedValue() < m_fenceValue) {
            m_fence->SetEventOnCompletion(m_fenceValue, m_event);
            WaitForSingleObject(m_event, INFINITE);
        }
    }

    ID3D12Device *m_device = nullptr;
    ID3D12CommandQueue *m_queue = nullptr;
    IDXGISwapChain3 *m_swapChain = nullptr;
    ID3D12DescriptorHeap *m_heap = nullptr;
    ID3D12CommandAllocator *m_allocator = nullptr;
    ID3D12GraphicsCommandList *m_list = nullptr;
    ID3D12Fence *m_fence = nullptr;
    ID3D12Resource *m_buffers[bufferCount] = {};
    HANDLE m_event = nullptr;
    UINT64 m_fenceValue = 0;
    UINT m_step = 0;
    int m_width = 0;
    int m_height = 0;
};

}

std::unique_ptr<Renderer> makeDirect3D12()
{
    return std::make_unique<Direct3D12>();
}
