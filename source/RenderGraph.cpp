#include "RenderGraph.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"

// NOTE: jank solution to access the correct ResourceAccess array index via PassID of the currently executing thread
thread_local RenderGraph::PassID tl_CurrentThreadPassID = RenderGraph::kInvalidPassID;

static const bool kDoDebugLogging = false;
static const uint32_t kDefaultHeapBlockSize = MB_TO_BYTES(16);
static const uint32_t kMaxHeapBlockSize = GB_TO_BYTES(1);
static const uint32_t kHeapAlignment = KB_TO_BYTES(64);
static const uint32_t kMaxTransientResourceAge = 2;

static std::size_t HashResourceDesc(const nvrhi::TextureDesc& desc)
{
	std::size_t seed = 0;
	HashCombine(seed, desc.width);
	HashCombine(seed, desc.height);
	HashCombine(seed, desc.depth);
	HashCombine(seed, desc.arraySize);
	HashCombine(seed, desc.mipLevels);
	HashCombine(seed, desc.sampleCount);
	HashCombine(seed, desc.sampleQuality);
	HashCombine(seed, desc.format);
	HashCombine(seed, desc.dimension);
	HashCombine(seed, desc.isRenderTarget);
	HashCombine(seed, desc.isUAV);
	HashCombine(seed, desc.isTypeless);
	HashCombine(seed, desc.isShadingRateSurface);
	HashCombine(seed, HashRawMem(desc.clearValue));
	HashCombine(seed, desc.useClearValue);
	return seed;
}

static std::size_t HashResourceDesc(const nvrhi::BufferDesc& desc)
{
	std::size_t seed = 0;
	HashCombine(seed, desc.byteSize);
	HashCombine(seed, desc.structStride);
	HashCombine(seed, desc.format);
	HashCombine(seed, desc.canHaveUAVs);
	HashCombine(seed, desc.canHaveTypedViews);
	HashCombine(seed, desc.canHaveRawViews);
	HashCombine(seed, desc.isVertexBuffer);
	HashCombine(seed, desc.isIndexBuffer);
	HashCombine(seed, desc.isConstantBuffer);
	HashCombine(seed, desc.isDrawIndirectArgs);
	HashCombine(seed, desc.isAccelStructBuildInput);
	HashCombine(seed, desc.isAccelStructStorage);
	HashCombine(seed, desc.isShaderBindingTable);
	return seed;
}

void RenderGraph::Initialize()
{
    CreateNewHeap(kDefaultHeapBlockSize);
}

void RenderGraph::InitializeForFrame(tf::Taskflow& taskFlow)
{
	PROFILE_FUNCTION();

	m_TaskFlow = &taskFlow;
	m_CommandListQueueTasks.clear();

	m_Passes.clear();

	// get ready for next frame
	m_CurrentPhase = Phase::Setup;
}

void RenderGraph::Shutdown()
{
	for (ResourceHandle* resourceHandle : m_ResourceHandles)
	{
        resourceHandle->m_Resource = nullptr;
	}
}

void RenderGraph::Compile()
{
	PROFILE_FUNCTION();

	m_CurrentPhase = Phase::Execute;

    // set ordered execution of command list queuing tasks
	for (uint32_t i = 1; i < m_CommandListQueueTasks.size(); ++i)
	{
        m_CommandListQueueTasks[i].succeed(m_CommandListQueueTasks[i - 1]);
	}

	// Track first/last Renderer access
	for (size_t i = 0; i < m_Passes.size(); i++)
	{
		const Pass& pass = m_Passes.at(i);
		const ResourceAccessesArray& resourceAccesses = pass.m_ResourceAccesses;

		const PassID passID = i;
		for (const ResourceAccess& resourceAccess : resourceAccesses)
		{
			assert(resourceAccess.m_ResourceHandle);

			ResourceHandle& resource = *resourceAccess.m_ResourceHandle;

			// update first and last access
			if (resource.m_FirstAccess == kInvalidPassID)
			{
				// first access to resource must always be a write
				assert(resourceAccess.m_AccessType == ResourceHandle::AccessType::Write);

				resource.m_FirstAccess = passID;
			}
			resource.m_LastAccess = passID;

			if (resourceAccess.m_AccessType == ResourceHandle::AccessType::Write)
			{
				//resource.m_LastWrite = passID;
			}
		}
	}

	for (ResourceHandle* resourceHandle : m_ResourceHandles)
	{
		assert(resourceHandle->m_AllocatedFrameIdx != UINT32_MAX);

		const int32_t resourceAge = g_Graphic.m_FrameCounter - resourceHandle->m_AllocatedFrameIdx;
		assert(resourceAge >= 0);

		// free transient resources that are too old
		if (resourceHandle->m_Resource && (resourceAge > kMaxTransientResourceAge))
		{
			FreeResource(*resourceHandle);
		}
	}

	nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

	// allocate resources
	for (ResourceHandle* resource : m_ResourcesToAlloc)
	{
		assert(resource->m_DescIdx != UINT32_MAX);

		uint64_t memReq = 0;

		if (resource->m_Type == ResourceHandle::Type::Texture)
		{
			resource->m_Resource = device->createTexture(m_ResourceDescs.at(resource->m_DescIdx).m_TextureDesc);
			memReq = device->getTextureMemoryRequirements((nvrhi::ITexture*)resource->m_Resource.Get()).size;
		}
		else
		{
			resource->m_Resource = device->createBuffer(m_ResourceDescs.at(resource->m_DescIdx).m_BufferDesc);
			memReq = device->getBufferMemoryRequirements((nvrhi::IBuffer*)resource->m_Resource.Get()).size;
		}

		assert(memReq != 0);
		assert(memReq <= kMaxHeapBlockSize);
		
		uint32_t foundHeapIdx = UINT32_MAX;
        uint32_t foundHeapOffset = UINT32_MAX;
		for (uint32_t i = 0; i < m_Heaps.size(); ++i)
		{
			if (m_Heaps[i].m_Heap->getDesc().capacity < memReq)
			{
				continue;
			}

			foundHeapOffset = m_Heaps[i].Allocate(memReq);

            if (foundHeapOffset != UINT32_MAX)
            {
                foundHeapIdx = i;
                break;
            }
		}

		// create new heap
		if (foundHeapIdx == UINT32_MAX)
        {
            CreateNewHeap(std::max((uint32_t)memReq, kDefaultHeapBlockSize));

            foundHeapIdx = m_Heaps.size() - 1;
            foundHeapOffset = m_Heaps.back().Allocate(memReq);
        }

        assert(foundHeapIdx != UINT32_MAX);
        assert(foundHeapOffset != UINT32_MAX);

        resource->m_HeapIdx = foundHeapIdx;
        resource->m_HeapOffset = foundHeapOffset;
        {
            PROFILE_SCOPED("Bind Resource Memory");

            if (resource->m_Type == ResourceHandle::Type::Texture)
            {
                verify(g_Graphic.m_NVRHIDevice->bindTextureMemory((nvrhi::ITexture*)resource->m_Resource.Get(), m_Heaps[foundHeapIdx].m_Heap, foundHeapOffset));
            }
            else
            {
                verify(g_Graphic.m_NVRHIDevice->bindBufferMemory((nvrhi::IBuffer*)resource->m_Resource.Get(), m_Heaps[foundHeapIdx].m_Heap, foundHeapOffset));
            }
        }

		if constexpr (kDoDebugLogging)
		{
			LOG_DEBUG("Bind Heap: resource: %s, memReq: %d, heapIdx: %d, heapOffset: %d", GetResourceName(*resource), memReq, foundHeapIdx, foundHeapOffset);
		}
    }
	m_ResourcesToAlloc.clear();

	for (HeapToFree elem : m_HeapsToFree)
	{
		if constexpr (kDoDebugLogging)
		{
			LOG_DEBUG("Free Heap: heapIdx: %d, heapOffset: %d", elem.m_Idx, elem.m_Offset);
		}
        m_Heaps.at(elem.m_Idx).Free(elem.m_Offset);
	}
	m_HeapsToFree.clear();
}

void RenderGraph::AddRenderer(IRenderer* renderer, tf::Task* taskToSucceed)
{
	STATIC_MULTITHREAD_DETECTOR();

	assert(renderer);

	// increase PassID type size if needed
	assert(m_Passes.size() < std::numeric_limits<PassID>::max());

	const PassID passIdx = m_Passes.size();

	// just append a a new Pass. will pop it if the renderer is not used
	Pass& newPass = m_Passes.emplace_back();

	if (!renderer->Setup(*this))
	{
		// ensure that no read/write dependencies were requested
		// allocating a transient resource will implicitly add a write dependency as well
		assert(newPass.m_ResourceAccesses.empty());
		m_Passes.pop_back();

		return;
	}

	newPass.m_Renderer = renderer;
	newPass.m_CommandList = g_Graphic.AllocateCommandList(); // TODO: compute queue

    // main Renderer task
	tf::Task renderTask = m_TaskFlow->emplace([this, passIdx]
		{
			Pass& pass = m_Passes.at(passIdx);
			IRenderer* renderer = pass.m_Renderer;
			assert(renderer);
			assert(pass.m_CommandList);

			PROFILE_SCOPED(renderer->m_Name.c_str());

			SCOPED_COMMAND_LIST(pass.m_CommandList, renderer->m_Name.c_str());

			// NOTE: see comment in declaration of this threadlocal variable
			tl_CurrentThreadPassID = passIdx;

			renderer->Render(pass.m_CommandList, *this);

			tl_CurrentThreadPassID = RenderGraph::kInvalidPassID;
		});

	// command list queuing task
	tf::Task queueCommandListTask = m_TaskFlow->emplace([this, passIdx]
		{
			Pass& pass = m_Passes.at(passIdx);
			assert(pass.m_CommandList);

			g_Graphic.QueueCommandList(pass.m_CommandList);
		});

    m_CommandListQueueTasks.push_back(queueCommandListTask);

    // schedule dependency for both CPU & GPU, if needed
	if (taskToSucceed)
	{
		renderTask.succeed(*taskToSucceed);
        queueCommandListTask.succeed(*taskToSucceed);
	}
}

template <typename ResourceDescT>
void RenderGraph::CreateTransientResource(ResourceHandle& resourceHandle, const ResourceDescT& inputDesc)
{
	assert(m_CurrentPhase == Phase::Setup);

    constexpr ResourceHandle::Type resourceType = std::is_same_v<ResourceDescT, nvrhi::TextureDesc> ? ResourceHandle::Type::Texture : ResourceHandle::Type::Buffer;

	// insert into array for 1st time registration
    // NOTE: at 200fps, it takes 248 days to overflow this, so we're good
	if (resourceHandle.m_AllocatedFrameIdx == UINT32_MAX)
	{
		m_ResourceHandles.push_back(&resourceHandle);

		// "reserve' the desc slot. both types
		resourceHandle.m_DescIdx = m_ResourceDescs.size();
		m_ResourceDescs.emplace_back();
	}

	bool bReallocResource = false;
    bReallocResource |= resourceType != resourceHandle.m_Type;
	bReallocResource |= (g_Graphic.m_FrameCounter - resourceHandle.m_AllocatedFrameIdx) > kMaxTransientResourceAge;

	if constexpr (resourceType == ResourceHandle::Type::Texture)
	{
		bReallocResource |= HashResourceDesc(m_ResourceDescs.at(resourceHandle.m_DescIdx).m_TextureDesc) != HashResourceDesc(inputDesc);
	}
	else
	{
        bReallocResource |= HashResourceDesc(m_ResourceDescs.at(resourceHandle.m_DescIdx).m_BufferDesc) != HashResourceDesc(inputDesc);
	}

    if (bReallocResource)
    {
        FreeResource(resourceHandle);
		m_ResourcesToAlloc.push_back(&resourceHandle);
    }

	resourceHandle.m_AllocatedFrameIdx = g_Graphic.m_FrameCounter;
    resourceHandle.m_Type = resourceType;

    if constexpr (resourceType == ResourceHandle::Type::Texture)
    {
		nvrhi::TextureDesc& desc = m_ResourceDescs.at(resourceHandle.m_DescIdx).m_TextureDesc;
        desc = inputDesc;
		desc.isVirtual = true;
    }
    else
    {
        nvrhi::BufferDesc& desc = m_ResourceDescs.at(resourceHandle.m_DescIdx).m_BufferDesc;
        desc = inputDesc;
		desc.isVirtual = true;
    }

	// creator implicitly has a write dependency on the resource
	AddWriteDependency(resourceHandle);
}
template void RenderGraph::CreateTransientResource(ResourceHandle& resourceHandle, const nvrhi::TextureDesc& inputDesc);
template void RenderGraph::CreateTransientResource(ResourceHandle& resourceHandle, const nvrhi::BufferDesc& inputDesc);

void RenderGraph::AddDependencyInternal(ResourceHandle& resourceHandle, ResourceHandle::AccessType accessType)
{
	assert(m_CurrentPhase == Phase::Setup);

	ResourceAccessesArray& accesses = m_Passes.back().m_ResourceAccesses;

	// check if resource already requested a dependency
#if _DEBUG
	for (const ResourceAccess& access : accesses)
	{
		assert(access.m_ResourceHandle != &resourceHandle);
	}
#endif // _DEBUG

	accesses.push_back(ResourceAccess{ &resourceHandle, accessType });
}

nvrhi::IResource* RenderGraph::GetResourceInternal(const ResourceHandle& resourceHandle, ResourceHandle::Type resourceType) const
{
	assert(m_CurrentPhase == Phase::Execute);
	assert(resourceHandle.m_AllocatedFrameIdx != UINT32_MAX); // un-allocated transient resource
    assert(resourceHandle.m_AllocatedFrameIdx == g_Graphic.m_FrameCounter); // resource is too old
	assert(tl_CurrentThreadPassID != kInvalidPassID);

#if _DEBUG
	const ResourceAccessesArray& accesses = m_Passes.at(tl_CurrentThreadPassID).m_ResourceAccesses;

	// check if resource is requested by the current Pass
	auto it = std::ranges::find_if(accesses, [&resourceHandle](const ResourceAccess& access) { return access.m_ResourceHandle == &resourceHandle; });
	assert(it != accesses.end());
#endif // _DEBUG

	assert(resourceHandle.m_Resource);
	assert(resourceHandle.m_Type == resourceType);

	return resourceHandle.m_Resource.Get();
}

void RenderGraph::FreeResource(ResourceHandle& resourceHandle)
{
	resourceHandle.m_Resource = nullptr;
	resourceHandle.m_FirstAccess = kInvalidPassID;
	resourceHandle.m_LastAccess = kInvalidPassID;
	//resourceHandle.m_LastWrite = kInvalidPassID;

	if (resourceHandle.m_HeapIdx != UINT32_MAX)
	{
        assert(resourceHandle.m_HeapOffset != UINT32_MAX);

		m_HeapsToFree.push_back({ resourceHandle.m_HeapIdx, resourceHandle.m_HeapOffset });

		if constexpr (kDoDebugLogging)
		{
			LOG_DEBUG("Free resource: %s, heapOffset: %d", GetResourceName(resourceHandle), resourceHandle.m_HeapOffset);
		}
	}

    resourceHandle.m_HeapIdx = UINT32_MAX;
	resourceHandle.m_HeapOffset = UINT32_MAX;
}

const char* RenderGraph::GetResourceName(const ResourceHandle& resourceHandle) const
{
    return resourceHandle.m_Type == ResourceHandle::Type::Texture ?
        m_ResourceDescs.at(resourceHandle.m_DescIdx).m_TextureDesc.debugName.c_str() :
        m_ResourceDescs.at(resourceHandle.m_DescIdx).m_BufferDesc.debugName.c_str();
}

void RenderGraph::CreateNewHeap(uint32_t size)
{
	Heap& newHeap = m_Heaps.emplace_back();
	newHeap.m_Blocks.push_back({ size, false });
	newHeap.m_Heap = g_Graphic.m_NVRHIDevice->createHeap(nvrhi::HeapDesc{ size, nvrhi::HeapType::DeviceLocal, "RDG Heap" });

	if constexpr (kDoDebugLogging)
	{
		LOG_DEBUG("New Heap size: %d", size);
	}
}

uint32_t RenderGraph::Heap::Allocate(uint32_t size)
{
	// sanity check
    assert(!m_Blocks.empty());
    assert(size % kHeapAlignment == 0);

	// Search through the free list for a free block that has enough space to allocate our data
	uint32_t foundIdx = UINT32_MAX;
	uint32_t heapOffset = 0;

	if constexpr (1)
	{
		FindBest(size, foundIdx, heapOffset);
	}
	else
	{
        FindFirst(size, foundIdx, heapOffset);
	}

	// no free block found
    if (foundIdx == UINT32_MAX)
    {
        return UINT32_MAX;
    }

    assert(m_Blocks[foundIdx].m_Allocated == false);
    assert(m_Blocks[foundIdx].m_Size % kHeapAlignment == 0);
    assert(heapOffset != UINT32_MAX);
	assert(heapOffset % kHeapAlignment == 0);

	// split the block 
	if (const uint32_t remainingSize = m_Blocks[foundIdx].m_Size - size;
		remainingSize > 0)
	{
		m_Blocks.insert(m_Blocks.begin() + foundIdx + 1, { remainingSize, false });
	}

    // update block
    m_Blocks[foundIdx].m_Size = size;
    m_Blocks[foundIdx].m_Allocated = true;

	m_Used += size;
	m_Peak = std::max(m_Peak, m_Used);

	// NOTE: we dont return block idx because the "Free" function will merge consecutive free blocks, "invalidating" all indices
	return heapOffset;
}

void RenderGraph::Heap::Free(uint32_t heapOffset)
{
	assert(heapOffset != UINT32_MAX);
    assert(heapOffset % kHeapAlignment == 0);

	uint32_t foundIdx = 0;
	for (uint32_t searchHeapOffset = 0; foundIdx < m_Blocks.size(); ++foundIdx)
	{
        if (searchHeapOffset == heapOffset)
        {
            assert(m_Blocks[foundIdx].m_Allocated == true);
			break;
        }

        searchHeapOffset += m_Blocks[foundIdx].m_Size;
	}
    assert(foundIdx < m_Blocks.size());

    m_Blocks[foundIdx].m_Allocated = false;

	// merge next block if possible
    if (foundIdx < m_Blocks.size() - 1)
    {
        if (!m_Blocks[foundIdx + 1].m_Allocated)
        {
            m_Blocks[foundIdx].m_Size += m_Blocks[foundIdx + 1].m_Size;
            assert(m_Blocks[foundIdx].m_Size % kHeapAlignment == 0);

            m_Blocks.erase(m_Blocks.begin() + foundIdx + 1);
        }
    }

	// merge previous block if possible
	if (foundIdx > 0)
	{
        if (!m_Blocks[foundIdx - 1].m_Allocated)
        {
            m_Blocks[foundIdx - 1].m_Size += m_Blocks[foundIdx].m_Size;
			assert(m_Blocks[foundIdx - 1].m_Size % kHeapAlignment == 0);

            m_Blocks.erase(m_Blocks.begin() + foundIdx);
        }
    }

	// sanity check
    assert(!m_Blocks.empty());
	
    m_Used -= m_Blocks[foundIdx].m_Size;
}

void RenderGraph::Heap::FindBest(uint32_t size, uint32_t& foundIdx, uint32_t& heapOffset)
{
	// Iterate all blocks to find best fit
	uint32_t smallestDiff = kDefaultHeapBlockSize;

	for (uint32_t i = 0, heapOffsetSearch = 0; i < m_Blocks.size(); heapOffsetSearch += m_Blocks[i].m_Size, ++i)
	{
        if (m_Blocks[i].m_Allocated)
        {
            continue;
        }

		const uint32_t remainingSize = m_Blocks[i].m_Size - size;

		if (m_Blocks[i].m_Size >= size && (remainingSize < smallestDiff))
		{
			foundIdx = i;
			heapOffset = heapOffsetSearch;
            smallestDiff = remainingSize;
		}
	}
}

void RenderGraph::Heap::FindFirst(uint32_t size, uint32_t& foundIdx, uint32_t& heapOffset)
{
    // Iterate all blocks to find first fit
    for (uint32_t i = 0; i < m_Blocks.size(); heapOffset += m_Blocks[i].m_Size, ++i)
    {
        if (m_Blocks[i].m_Allocated)
        {
            continue;
        }

        if (m_Blocks[i].m_Size >= size)
        {
            foundIdx = i;
            break;
        }
    }
}
