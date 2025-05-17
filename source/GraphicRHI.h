#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

class GraphicRHI
{
public:
    virtual ~GraphicRHI() = default;

    virtual nvrhi::DeviceHandle CreateDevice() = 0;
    virtual void InitSwapChainTextureHandles() = 0;
    virtual uint32_t GetCurrentBackBufferIndex() = 0;
    virtual void SwapChainPresent() = 0;
    virtual void* GetNativeCommandList(nvrhi::CommandListHandle commandList) = 0;

    virtual void SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName) = 0;
    virtual void SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName) = 0;
};

class D3D12RHI : public GraphicRHI
{
public:
    nvrhi::DeviceHandle CreateDevice() override;
    void InitSwapChainTextureHandles() override;
    uint32_t GetCurrentBackBufferIndex() override { return m_SwapChain->GetCurrentBackBufferIndex(); }
    void SwapChainPresent() override;
    void* GetNativeCommandList(nvrhi::CommandListHandle commandList) override;

    void SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName) override;
    void SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName) override;

    bool m_bTearingSupported = false;

    ComPtr<ID3D12CommandQueue> m_ComputeQueue;
    ComPtr<ID3D12CommandQueue> m_CopyQueue;
    ComPtr<ID3D12CommandQueue> m_GraphicsQueue;
    ComPtr<ID3D12Device> m_D3DDevice;
    ComPtr<ID3D12Resource> m_SwapChainD3D12Resources[2];
    ComPtr<IDXGIAdapter1> m_DXGIAdapter;
    ComPtr<IDXGIFactory6> m_DXGIFactory;
    ComPtr<IDXGISwapChain3> m_SwapChain;
};

class VulkanRHI : public GraphicRHI
{
public:
    nvrhi::DeviceHandle CreateDevice() override { assert(false && "Not Implemented!"); return {}; }
    void InitSwapChainTextureHandles() override { assert(false && "Not Implemented!"); }
    uint32_t GetCurrentBackBufferIndex() override { assert(false && "Not Implemented!"); return UINT32_MAX; }
    void SwapChainPresent() override { assert(false && "Not Implemented!"); }
    void* GetNativeCommandList(nvrhi::CommandListHandle commandList) override { assert(false && "Not Implemented!"); return nullptr; }

    void SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName) override { assert(false && "Not Implemented!"); }
    void SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName) override { assert(false && "Not Implemented!"); }
};
