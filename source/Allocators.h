#pragma once

#include "nvrhi/nvrhi.h"

class GrowableGPUVirtualBuffer
{
public:
    uint64_t QueueAppend(const void* srcData, uint64_t sizeInBytes);
    void CommitPendingUploads();

    nvrhi::BufferHandle m_Buffer;
    uint64_t m_CurrentBytesOffset = 0;
    uint64_t m_UploadedBytesOffset = 0;

private:
    std::mutex m_QueuedUploadBatchesLck;
    std::vector<std::vector<std::byte>> m_QueuedUploadBatches;
};

class SimpleResizeableGPUBuffer
{
public:
    void Write(nvrhi::CommandListHandle commandList, void* srcData, size_t nbBytes);
    void ClearBuffer(nvrhi::CommandListHandle commandList, size_t nbBytes);
    void GrowBufferIfNeeded(size_t nbElements);

    operator bool() const { return m_Buffer && (m_BufferDesc.byteSize > 0); };

    nvrhi::BufferHandle m_Buffer;
    nvrhi::BufferDesc m_BufferDesc;
};
