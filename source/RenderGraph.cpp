#include "RenderGraph.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"

// NOTE: jank solution to access the correct ResourceAccess array index via PassID of the currently executing thread
thread_local RenderGraph::PassID tl_CurrentThreadPassID = RenderGraph::kInvalidPassID;

static const uint64_t kDefaultHeapBlockSize = MB_TO_BYTES(16);
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

static  std::size_t HashResourceDesc(const nvrhi::BufferDesc& desc)
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
    m_Heap.m_Blocks.push_back({ kDefaultHeapBlockSize, false });
	m_Heap.m_Heap = g_Graphic.m_NVRHIDevice->createHeap(nvrhi::HeapDesc{ kDefaultHeapBlockSize, nvrhi::HeapType::DeviceLocal, "RDG Heap" });
}

void RenderGraph::InitializeForFrame(tf::Taskflow& taskFlow)
{
	PROFILE_FUNCTION();

	m_TaskFlow = &taskFlow;
	m_CommandListQueueTasks.clear();

	// cache heaps for reuse in next frame
	m_FreeHeaps.insert(m_FreeHeaps.end(), m_UsedHeaps.begin(), m_UsedHeaps.end());
	m_UsedHeaps.clear();

	// sort so that resources can get tightest fit heap
	std::sort(m_FreeHeaps.begin(), m_FreeHeaps.end(), [](const nvrhi::HeapHandle& lhs, const nvrhi::HeapHandle& rhs) { return lhs->getDesc().capacity < rhs->getDesc().capacity; });

	m_Passes.clear();

	for (ResourceHandle* resourceHandle : m_ResourceHandles)
	{
		assert(resourceHandle->m_AllocatedFrameIdx != UINT32_MAX);
        assert(g_Graphic.m_FrameCounter > resourceHandle->m_AllocatedFrameIdx);

        // free transient resources that are too old
		if (resourceHandle->m_bAllocated && (g_Graphic.m_FrameCounter - resourceHandle->m_AllocatedFrameIdx > kMaxTransientResourceAge))
		{
			resourceHandle->m_bAllocated = false;
			resourceHandle->m_Resource = nullptr;
            resourceHandle->m_FirstAccess = kInvalidPassID;
            resourceHandle->m_LastAccess = kInvalidPassID;
            resourceHandle->m_LastWrite = kInvalidPassID;

            const char* debugName =
				resourceHandle->m_Type == ResourceHandle::Type::Texture ?
				m_ResourceDescs.at(resourceHandle->m_DescIdx).m_TextureDesc.debugName.c_str() :
				m_ResourceDescs.at(resourceHandle->m_DescIdx).m_BufferDesc.debugName.c_str();
            //LOG_DEBUG("Freeing transient resource due to old age: %s", debugName);
		}
	}

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

	// TODO: resource validations:
	// - check if resource has been created in a Pass before requestee
	// - if a read dependency is requested, check if resource was written to in a Pass before requestee

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
				resource.m_LastWrite = passID;
			}
		}
	}

	nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

	// allocate resources
	for (ResourceHandle* resource : m_ResourceHandles)
	{
        if (!resource->m_bAllocated)
        {
            continue;
        }

        // transient resource was allocated in a previous frame, re-use it
		if (resource->m_Resource)
		{
            continue;
		}

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

		nvrhi::HeapHandle heapToUse;

		// find a heap that can fit the resource
		for (auto it = m_FreeHeaps.begin(); it != m_FreeHeaps.end(); ++it)
		{
			const nvrhi::HeapHandle& heap = *it;

			if (heap->getDesc().capacity >= memReq)
			{
				heapToUse = heap;

				// remove heap from free list. NOTE: keep it sorted by using 'erase'
				m_FreeHeaps.erase(it);
				break;
			}
		}

		// create a new heap if none found
		if (!heapToUse)
		{
			heapToUse = device->createHeap(nvrhi::HeapDesc{ memReq, nvrhi::HeapType::DeviceLocal, "RDG Heap" });
			//LOG_DEBUG("New RDG Heap: bytes: %d, alignment: %d", memReq.size, memReq.alignment);
		}

		m_UsedHeaps.push_back(heapToUse);

		assert(heapToUse);

        {
            PROFILE_SCOPED("Bind Resource Memory");

            if (resource->m_Type == ResourceHandle::Type::Texture)
            {
                verify(g_Graphic.m_NVRHIDevice->bindTextureMemory((nvrhi::ITexture*)resource->m_Resource.Get(), heapToUse, 0));
            }
            else
            {
                verify(g_Graphic.m_NVRHIDevice->bindBufferMemory((nvrhi::IBuffer*)resource->m_Resource.Get(), heapToUse, 0));
            }
        }
    }
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

void RenderGraph::CreateTransientResourceInternal(ResourceHandle& resourceHandle, ResourceHandle::Type resourceType)
{
	assert(m_CurrentPhase == Phase::Setup);

	// insert into array for 1st time registration
    // NOTE: at 200fps, it takes 248 days to overflow this, so we're good
	if (resourceHandle.m_AllocatedFrameIdx == UINT32_MAX)
	{
		m_ResourceHandles.push_back(&resourceHandle);

		// "reserve' the desc slot. both types
		resourceHandle.m_DescIdx = m_ResourceDescs.size();
		m_ResourceDescs.emplace_back();
	}
	else
	{
		// dont support changing transient resource type, once allocated. i dont want to handle that shit
		assert(resourceHandle.m_Type == resourceType);
		assert(resourceHandle.m_DescIdx != UINT32_MAX);
	}

	resourceHandle.m_bAllocated = true;
	resourceHandle.m_AllocatedFrameIdx = g_Graphic.m_FrameCounter;
    resourceHandle.m_Type = resourceType;

	// creator implicitly has a write dependency on the resource
	AddWriteDependency(resourceHandle);
}

void RenderGraph::CreateTransientResource(ResourceHandle& resourceHandle, const nvrhi::TextureDesc& desc)
{
	CreateTransientResourceInternal(resourceHandle, ResourceHandle::Type::Texture);

	nvrhi::TextureDesc& texDesc = m_ResourceDescs.at(resourceHandle.m_DescIdx).m_TextureDesc;
	texDesc = desc;
	texDesc.isVirtual = true;
}

void RenderGraph::CreateTransientResource(ResourceHandle& resourceHandle, const nvrhi::BufferDesc& desc)
{
	CreateTransientResourceInternal(resourceHandle, ResourceHandle::Type::Buffer);

	nvrhi::BufferDesc& bufferDesc = m_ResourceDescs.at(resourceHandle.m_DescIdx).m_BufferDesc;
	bufferDesc = desc;
	bufferDesc.isVirtual = true;
}

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

void RenderGraph::AddReadDependency(ResourceHandle& resourceHandle)
{
	AddDependencyInternal(resourceHandle, ResourceHandle::AccessType::Read);
}

void RenderGraph::AddWriteDependency(ResourceHandle& resourceHandle)
{
	AddDependencyInternal(resourceHandle, ResourceHandle::AccessType::Write);
}

nvrhi::IResource* RenderGraph::GetResourceInternal(const ResourceHandle& resourceHandle, ResourceHandle::Type resourceType) const
{
	assert(m_CurrentPhase == Phase::Execute);
	assert(resourceHandle.m_bAllocated);
	assert(resourceHandle.m_AllocatedFrameIdx == g_Graphic.m_FrameCounter);
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

nvrhi::TextureHandle RenderGraph::GetTexture(const ResourceHandle& resourceHandle) const
{
	return (nvrhi::ITexture*)GetResourceInternal(resourceHandle, ResourceHandle::Type::Texture);
}

nvrhi::BufferHandle RenderGraph::GetBuffer(const ResourceHandle& resourceHandle) const
{
	return (nvrhi::IBuffer*)GetResourceInternal(resourceHandle, ResourceHandle::Type::Buffer);
}

uint64_t RenderGraph::Heap::Allocate(uint64_t size)
{
	// NOTE: we dont return block idx because the "Free" function will merge consecutive free blocks, "invalidating" all indices

	const bool bFindBest = true;

	// sanity check
    assert(!m_Blocks.empty());
    assert(size % kHeapAlignment == 0);

	// Search through the free list for a free block that has enough space to allocate our data
	uint32_t foundIdx = UINT32_MAX;
	uint64_t heapOffset = 0;

	if constexpr (bFindBest)
	{
		FindBest(size, foundIdx, heapOffset);
	}
	else
	{
        FindFirst(size, foundIdx, heapOffset);
	}
	assert(foundIdx != UINT32_MAX);
    assert(m_Blocks[foundIdx].m_Allocated == false);
    assert(m_Blocks[foundIdx].m_Size % kHeapAlignment == 0);
	assert(heapOffset % kHeapAlignment == 0);

	// split the block 
	if (const uint64_t remainingSize = m_Blocks[foundIdx].m_Size - size;
		remainingSize > 0)
	{
		m_Blocks.insert(m_Blocks.begin() + foundIdx + 1, { remainingSize, false });
	}

    // update block
    m_Blocks[foundIdx].m_Size = size;
    m_Blocks[foundIdx].m_Allocated = true;

	m_Used += size;
	m_Peak = std::max(m_Peak, m_Used);

	return heapOffset;
}

void RenderGraph::Heap::Free(uint64_t heapOffset)
{
    assert(heapOffset % kHeapAlignment == 0);

	uint32_t foundIdx = 0;
	for (uint64_t searchHeapOffset = 0; foundIdx < m_Blocks.size(); ++foundIdx)
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

void RenderGraph::Heap::FindBest(uint64_t size, uint32_t& foundIdx, uint64_t& heapOffset)
{
	// Iterate all blocks to find best fit
	uint64_t smallestDiff = kDefaultHeapBlockSize;

	for (uint32_t i = 0; i < m_Blocks.size(); ++i)
	{
        if (m_Blocks[i].m_Allocated)
        {
            continue;
        }

		const uint64_t remainingSize = m_Blocks[i].m_Size - size;

		if (m_Blocks[i].m_Size >= size && (remainingSize < smallestDiff))
		{
			foundIdx = i;
            smallestDiff = remainingSize;
		}
		else
		{
			heapOffset += m_Blocks[i].m_Size;
		}
	}
}

void RenderGraph::Heap::FindFirst(uint64_t size, uint32_t& foundIdx, uint64_t& heapOffset)
{
    // Iterate all blocks to find first fit
    for (uint32_t i = 0; i < m_Blocks.size(); ++i)
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
        else
        {
            heapOffset += m_Blocks[i].m_Size;
        }
    }
}
