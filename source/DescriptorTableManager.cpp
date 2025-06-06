/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <DescriptorTableManager.h>

#include "CriticalSection.h"
#include "Graphic.h"

DescriptorTableManager::DescriptorTableManager(nvrhi::IBindingLayout* layout)
{
    m_DescriptorTable = g_Graphic.m_NVRHIDevice->createDescriptorTable(layout);

    const size_t capacity = m_DescriptorTable->getCapacity();
    m_AllocatedDescriptors.resize(capacity);
    m_Descriptors.resize(capacity);
    memset(m_Descriptors.data(), 0, sizeof(nvrhi::BindingSetItem) * capacity);
}

uint32_t DescriptorTableManager::CreateDescriptorHandle(nvrhi::BindingSetItem item)
{
    assert(m_DescriptorTable);

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    uint32_t index = 0;

    {
        AUTO_LOCK(m_Lock);
        const auto& found = m_DescriptorIndexMap.find(item);
        if (found != m_DescriptorIndexMap.end())
            return found->second;

        uint32_t capacity = m_DescriptorTable->getCapacity();
        bool foundFreeSlot = false;

        for (index = m_SearchStart; index < capacity; index++)
        {
            if (!m_AllocatedDescriptors[index])
            {
                foundFreeSlot = true;
                break;
            }
        }

        if (!foundFreeSlot)
        {
            uint32_t newCapacity = std::max(64u, capacity * 2); // handle the initial case when capacity == 0
            device->resizeDescriptorTable(m_DescriptorTable, newCapacity);
            m_AllocatedDescriptors.resize(newCapacity);
            m_Descriptors.resize(newCapacity);

            // zero-fill the new descriptors
            memset(&m_Descriptors[capacity], 0, sizeof(nvrhi::BindingSetItem) * (newCapacity - capacity));

            index = capacity;
            capacity = newCapacity;
        }

        item.slot = index;
        m_SearchStart = index + 1;
        m_AllocatedDescriptors[index] = true;
        m_Descriptors[index] = item;
        m_DescriptorIndexMap[item] = index;
    }

    device->writeDescriptorTable(m_DescriptorTable, item);

    if (item.resourceHandle)
        item.resourceHandle->AddRef();

    return index;
}

void DescriptorTableManager::ReplaceDescriptor(uint32_t index, nvrhi::BindingSetItem item)
{
    SCOPED_MULTITHREAD_DETECTOR(m_MultithreadDetector);

    assert(index < m_Descriptors.size());
    assert(m_AllocatedDescriptors[index]);
    assert(m_Descriptors[index].slot == index); // ensure that the slot matches the index

    if (m_Descriptors[index].resourceHandle)
    {
        // Release the old resource handle before replacing it
        m_Descriptors[index].resourceHandle->Release();
    }

    m_Descriptors[index] = item;
    m_DescriptorIndexMap[item] = index;

    g_Graphic.m_NVRHIDevice->writeDescriptorTable(m_DescriptorTable, item);

    if (item.resourceHandle)
        item.resourceHandle->AddRef();
}

void DescriptorTableManager::ReleaseDescriptor(uint32_t index)
{
    SCOPED_MULTITHREAD_DETECTOR(m_MultithreadDetector);

    nvrhi::BindingSetItem& descriptor = m_Descriptors[index];

    if (descriptor.resourceHandle)
        descriptor.resourceHandle->Release();

    // Erase the existing descriptor from the index map to prevent its "reuse" later
    const auto indexMapEntry = m_DescriptorIndexMap.find(m_Descriptors[index]);
    if (indexMapEntry != m_DescriptorIndexMap.end())
        m_DescriptorIndexMap.erase(indexMapEntry);

    descriptor = nvrhi::BindingSetItem::None(index);

    g_Graphic.m_NVRHIDevice->writeDescriptorTable(m_DescriptorTable, descriptor);

    m_AllocatedDescriptors[index] = false;
    m_SearchStart = std::min(m_SearchStart, index);
}

DescriptorTableManager::~DescriptorTableManager()
{
    for (nvrhi::BindingSetItem& descriptor : m_Descriptors)
    {
        if (descriptor.resourceHandle)
        {
            descriptor.resourceHandle->Release();
        }
    }
}
