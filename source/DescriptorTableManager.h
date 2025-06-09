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

#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

#include "CriticalSection.h"

class DescriptorTableManager
{
protected:
    // Custom hasher that doesn't look at the binding slot
    struct BindingSetItemHasher
    {
        std::size_t operator()(const nvrhi::BindingSetItem& item) const
        {
            size_t hash = 0;
            nvrhi::hash_combine(hash, item.resourceHandle);
            nvrhi::hash_combine(hash, item.type);
            nvrhi::hash_combine(hash, item.format);
            nvrhi::hash_combine(hash, item.dimension);
            nvrhi::hash_combine(hash, item.rawData[0]);
            nvrhi::hash_combine(hash, item.rawData[1]);
            return hash;
        }
    };

    // Custom equality tester that doesn't look at the binding slot
    struct BindingSetItemsEqual
    {
        bool operator()(const nvrhi::BindingSetItem& a, const nvrhi::BindingSetItem& b) const
        {
            return a.resourceHandle == b.resourceHandle
                && a.type == b.type
                && a.format == b.format
                && a.dimension == b.dimension
                && a.subresources == b.subresources;
        }
    };

    nvrhi::DescriptorTableHandle m_DescriptorTable;

    std::vector<nvrhi::BindingSetItem> m_Descriptors;
    std::unordered_map<nvrhi::BindingSetItem, uint32_t, BindingSetItemHasher, BindingSetItemsEqual> m_DescriptorIndexMap;
    std::vector<bool> m_AllocatedDescriptors;
    uint32_t m_SearchStart = 0;
    std::mutex m_Lock;

    MultithreadDetector m_MultithreadDetector;

public:
    DescriptorTableManager(nvrhi::IBindingLayout* layout);
    ~DescriptorTableManager();

    nvrhi::IDescriptorTable* GetDescriptorTable() const { return m_DescriptorTable; }

    uint32_t CreateDescriptorHandle(nvrhi::BindingSetItem item);
    void ReleaseDescriptor(uint32_t index);
};
