#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

class GraphicRHI
{
public:
    virtual ~GraphicRHI() = default;

    static GraphicRHI* Create(nvrhi::GraphicsAPI api);
    
    virtual nvrhi::DeviceHandle CreateDevice() = 0;
    virtual void InitSwapChainTextureHandles() = 0;
    virtual uint32_t GetCurrentBackBufferIndex() = 0;
    virtual void SwapChainPresent() = 0;
    virtual void* GetNativeCommandList(nvrhi::CommandListHandle commandList) = 0;

    virtual void SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName) = 0;
    virtual void SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName) = 0;
};
