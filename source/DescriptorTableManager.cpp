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

    m_MaxCapacity = layout->getBindlessDesc()->maxCapacity;

    g_Graphic.m_NVRHIDevice->resizeDescriptorTable(m_DescriptorTable, m_MaxCapacity);
    m_AllocatedDescriptors.resize(m_MaxCapacity);
    m_Descriptors.resize(m_MaxCapacity);
    memset(m_Descriptors.data(), 0, sizeof(nvrhi::BindingSetItem) * m_MaxCapacity);
}

uint32_t DescriptorTableManager::CreateDescriptorHandle(nvrhi::BindingSetItem item)
{
    check(m_DescriptorTable);

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    uint32_t index = 0;

    {
        AUTO_LOCK(m_Lock);
        const auto& found = m_DescriptorIndexMap.find(item);
        if (found != m_DescriptorIndexMap.end())
            return found->second;

        bool foundFreeSlot = false;

        for (index = m_SearchStart; index < m_MaxCapacity; index++)
        {
            if (!m_AllocatedDescriptors[index])
            {
                foundFreeSlot = true;
                break;
            }
        }
        check(foundFreeSlot);

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

void DescriptorTableManager::ReleaseDescriptor(uint32_t indexInTable)
{
    AUTO_LOCK(m_Lock);

    nvrhi::BindingSetItem& descriptor = m_Descriptors[indexInTable];

    if (descriptor.resourceHandle)
        descriptor.resourceHandle->Release();

    // Erase the existing descriptor from the index map to prevent its "reuse" later
    const auto indexMapEntry = m_DescriptorIndexMap.find(m_Descriptors[indexInTable]);
    if (indexMapEntry != m_DescriptorIndexMap.end())
        m_DescriptorIndexMap.erase(indexMapEntry);

    descriptor = nvrhi::BindingSetItem::None(indexInTable);

    g_Graphic.m_NVRHIDevice->writeDescriptorTable(m_DescriptorTable, descriptor);

    m_AllocatedDescriptors[indexInTable] = false;
    m_SearchStart = std::min(m_SearchStart, indexInTable);
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
