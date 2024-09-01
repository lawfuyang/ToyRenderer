#include "Allocators.h"

#include "Engine.h"
#include "Graphic.h"

uint64_t GrowableGPUVirtualBuffer::QueueAppend(const void* srcData, uint64_t sizeInBytes)
{
    PROFILE_FUNCTION();

    std::vector<std::byte> bytesCopy;
    bytesCopy.resize(sizeInBytes);
    memcpy(bytesCopy.data(), srcData, sizeInBytes);

    // queue bytes to upload
    uint64_t destOffsetBytes;
    {
        AUTO_LOCK(m_QueuedUploadBatchesLck);
        m_QueuedUploadBatches.push_back(std::move(bytesCopy));
        destOffsetBytes = m_CurrentBytesOffset;
        m_CurrentBytesOffset += sizeInBytes;
    }
    assert(bytesCopy.empty());

    return destOffsetBytes;
}

void GrowableGPUVirtualBuffer::CommitPendingUploads()
{
    if (m_QueuedUploadBatches.empty())
    {
        return;
    }

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "GrowableGPUVirtualBuffer::CommitPendingUploads");

    const nvrhi::BufferDesc& bufferDesc = m_Buffer->getDesc();

    // grow buffer if needed
    if (m_CurrentBytesOffset > bufferDesc.byteSize)
    {
        const uint64_t newSizeInBytes = std::max(bufferDesc.byteSize * 2, m_CurrentBytesOffset);

        // create new buffer
        nvrhi::BufferDesc desc = bufferDesc;
        desc.byteSize = newSizeInBytes;
        nvrhi::BufferHandle newBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);

        // copy contents of old buffer over
        const uint64_t kDestOffsetBytes = 0;
        const uint64_t kSrcOffsetBytes = 0;
        commandList->copyBuffer(newBuffer, kDestOffsetBytes, m_Buffer, kSrcOffsetBytes, bufferDesc.byteSize);

        LOG_DEBUG("Virtual Buffer: [%s], Grow: [%f -> %f] MB", bufferDesc.debugName.c_str(), BYTES_TO_MB((float)bufferDesc.byteSize), BYTES_TO_MB((float)newSizeInBytes));

        m_Buffer = newBuffer;
    }

    const uint64_t offsetStart = m_UploadedBytesOffset;
    for (const std::vector<std::byte>& uploadBatch : m_QueuedUploadBatches)
    {
        commandList->writeBuffer(m_Buffer, uploadBatch.data(), uploadBatch.size(), m_UploadedBytesOffset);
        m_UploadedBytesOffset += uploadBatch.size();
    }

    LOG_DEBUG("Virtual Buffer: [%s], Commit: [%f] MB", bufferDesc.debugName.c_str(), BYTES_TO_MB(m_UploadedBytesOffset - offsetStart));

    AUTO_LOCK(m_QueuedUploadBatchesLck);
    m_QueuedUploadBatches.clear();
    m_QueuedUploadBatches.shrink_to_fit();
}

void SimpleResizeableGPUBuffer::Write(nvrhi::CommandListHandle commandList, void* srcData, size_t nbBytes)
{
    //PROFILE_SCOPED(StringFormat("SimpleResizeableGPUBuffer::Write [%s]", m_BufferDesc.debugName.c_str()));

    GrowBufferIfNeeded(nbBytes);

    PROFILE_SCOPED("SimpleResizeableGPUBuffer Write");

    commandList->writeBuffer(m_Buffer, srcData, nbBytes);
}

void SimpleResizeableGPUBuffer::ClearBuffer(nvrhi::CommandListHandle commandList, size_t nbBytes)
{
    GrowBufferIfNeeded(nbBytes);

    PROFILE_SCOPED("SimpleResizeableGPUBuffer Clear");

    commandList->clearBufferUInt(m_Buffer, 0);
}

void SimpleResizeableGPUBuffer::GrowBufferIfNeeded(size_t nbBytes)
{
    // simply check if debug name is empty for uninitialized desc
    assert(!m_BufferDesc.debugName.empty());

    if (!m_Buffer || (nbBytes > m_BufferDesc.byteSize))
    {
        PROFILE_SCOPED("SimpleResizeableGPUBuffer Create");

        m_BufferDesc.byteSize = nbBytes;
        m_Buffer = g_Graphic.m_NVRHIDevice->createBuffer(m_BufferDesc);
    }
}
