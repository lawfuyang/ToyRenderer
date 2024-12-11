#include "RenderGraph.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"

// NOTE: jank solution to access the correct ResourceAccess array index via PassID of the currently executing thread
thread_local RenderGraph::PassID tl_CurrentThreadPassID = RenderGraph::kInvalidPassID;

void RenderGraph::InitializeForFrame(tf::Taskflow& taskFlow)
{
	m_TaskFlow = &taskFlow;
	m_CommandListQueueTasks.clear();

	const uint32_t idx = g_Graphic.m_FrameCounter % 2;

	// cache heaps for reuse in next frame
	m_FreeHeaps.insert(m_FreeHeaps.end(), m_UsedHeaps[idx].begin(), m_UsedHeaps[idx].end());
	m_UsedHeaps[idx].clear();

	// sort so that resources can get tightest fit heap
	std::sort(m_FreeHeaps.begin(), m_FreeHeaps.end(), [](const nvrhi::HeapHandle& lhs, const nvrhi::HeapHandle& rhs) { return lhs->getDesc().capacity < rhs->getDesc().capacity; });

	m_Passes.clear();
	m_Resources.clear();
	m_TextureCreationDescs.clear();
	m_BufferCreationDescs.clear();

	// reset resource handles
	for (ResourceHandle* resourceHandle : m_ResourceHandles)
	{
		resourceHandle->m_ID = kInvalidResourceHandle;
	}
	m_ResourceHandles.clear();

	// get ready for next frame
	m_CurrentPhase = Phase::Setup;
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
			assert(resourceAccess.m_ResourceHandle->m_ID != kInvalidResourceHandle);

			Resource& resource = m_Resources.at(resourceAccess.m_ResourceHandle->m_ID);

			// update first and last access
			if (resource.m_FirstAccess == kInvalidPassID)
			{
				// first access to resource must always be a write
				assert(resourceAccess.m_AccessType == Resource::AccessType::Write);

				resource.m_FirstAccess = passID;
			}
			resource.m_LastAccess = passID;

			if (resourceAccess.m_AccessType == Resource::AccessType::Write)
			{
				resource.m_LastWrite = passID;
			}
		}
	}

	nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

	// allocate resources
	for (Resource& resource : m_Resources)
	{
		assert(resource.m_DescIdx != -1);

		const bool bIsTexture = resource.m_Type == Resource::Type::Texture;
		nvrhi::MemoryRequirements memReq;

		if (bIsTexture)
		{
			nvrhi::TextureDesc descCopy = m_TextureCreationDescs.at(resource.m_DescIdx);
			descCopy.isVirtual = true;
			resource.m_Resource = device->createTexture(descCopy);

			memReq = device->getTextureMemoryRequirements((nvrhi::ITexture*)resource.m_Resource.Get());
		}
		else
		{
			nvrhi::BufferDesc descCopy = m_BufferCreationDescs.at(resource.m_DescIdx);
			descCopy.isVirtual = true;
			resource.m_Resource = device->createBuffer(descCopy);

			memReq = device->getBufferMemoryRequirements((nvrhi::IBuffer*)resource.m_Resource.Get());
		}

		nvrhi::HeapHandle heapToUse;

		// find a heap that can fit the resource
		for (auto it = m_FreeHeaps.begin(); it != m_FreeHeaps.end(); ++it)
		{
			const nvrhi::HeapHandle& heap = *it;

			if (heap->getDesc().capacity >= memReq.size)
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
			// sanity check for alignment
			assert(memReq.size % memReq.alignment == 0);

			heapToUse = device->createHeap(nvrhi::HeapDesc{ memReq.size, nvrhi::HeapType::DeviceLocal, "RDG Heap" });
			//LOG_DEBUG("New RDG Heap: bytes: %d, alignment: %d", memReq.size, memReq.alignment);
		}

		assert(heapToUse);

		const uint32_t idx = g_Graphic.m_FrameCounter % 2;
		m_UsedHeaps[idx].push_back(heapToUse);

		if (bIsTexture)
		{
			nvrhi::TextureHandle textureResource = (nvrhi::ITexture*)resource.m_Resource.Get();

			verify(device->bindTextureMemory(textureResource, heapToUse, 0));
			Graphic::UpdateResourceDebugName(textureResource, textureResource->getDesc().debugName);
		}
		else
		{
			nvrhi::BufferHandle bufferResource = (nvrhi::IBuffer*)resource.m_Resource.Get();

			verify(device->bindBufferMemory(bufferResource, heapToUse, 0));
			Graphic::UpdateResourceDebugName(bufferResource, bufferResource->getDesc().debugName);
		}
	}
}

void RenderGraph::AddRenderer(IRenderer* renderer, tf::Task* taskToSucceed)
{
	STATIC_MULTITHREAD_DETECTOR();

	assert(renderer);

	const uint32_t nbResourcesBeforePass = m_Resources.size();

	// increase PassID type size if needed
	assert(m_Passes.size() < std::numeric_limits<PassID>::max());

	const PassID passIdx = m_Passes.size();

	// just append a a new Pass. will pop it if the renderer is not used
	Pass& newPass = m_Passes.emplace_back();

	if (!renderer->Setup(*this))
	{
		// ensure that no transient resources were created by the renderer if it was not used
		assert(nbResourcesBeforePass == m_Resources.size());

		// also ensure that no read/write dependencies were requested
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

RenderGraph::Resource& RenderGraph::CreateTransientResourceInternal(ResourceHandle& resourceHandle, Resource::Type resourceType)
{
	assert(m_CurrentPhase == Phase::Setup);
	assert(resourceHandle.m_ID == kInvalidResourceHandle); // double creation?

	resourceHandle.m_ID = m_ResourceHandles.size();
	m_ResourceHandles.push_back(&resourceHandle);

	resourceHandle.m_AllocatedFrameIdx = g_Graphic.m_FrameCounter;

	Resource& newResource = m_Resources.emplace_back();
	newResource.m_Type = resourceType;

	// creator implicitly has a write dependency on the resource
	AddWriteDependency(resourceHandle);

	return newResource;
}

void RenderGraph::CreateTransientResource(ResourceHandle& resourceHandle, const nvrhi::TextureDesc& desc)
{
	Resource& newResource = CreateTransientResourceInternal(resourceHandle, Resource::Type::Texture);

	newResource.m_DescIdx = m_TextureCreationDescs.size();
	m_TextureCreationDescs.push_back(desc);
}

void RenderGraph::CreateTransientResource(ResourceHandle& resourceHandle, const nvrhi::BufferDesc& desc)
{
	Resource& newResource = CreateTransientResourceInternal(resourceHandle, Resource::Type::Buffer);

	newResource.m_DescIdx = m_BufferCreationDescs.size();
	m_BufferCreationDescs.push_back(desc);
}

void RenderGraph::AddDependencyInternal(ResourceHandle& resourceHandle, Resource::AccessType accessType)
{
	assert(m_CurrentPhase == Phase::Setup);

	ResourceAccessesArray& accesses = m_Passes.back().m_ResourceAccesses;

	// check if resource already requested a dependency
	for (const ResourceAccess& access : accesses)
	{
		assert(access.m_ResourceHandle != &resourceHandle);
	}

	accesses.push_back(ResourceAccess{ &resourceHandle, accessType });
}

void RenderGraph::AddReadDependency(ResourceHandle& resourceHandle)
{
	AddDependencyInternal(resourceHandle, Resource::AccessType::Read);
}

void RenderGraph::AddWriteDependency(ResourceHandle& resourceHandle)
{
	AddDependencyInternal(resourceHandle, Resource::AccessType::Write);
}

nvrhi::IResource* RenderGraph::GetResourceInternal(const ResourceHandle& resourceHandle, Resource::Type resourceType) const
{
	assert(m_CurrentPhase == Phase::Execute);
	assert(resourceHandle.m_ID != kInvalidResourceHandle);
	assert(resourceHandle.m_AllocatedFrameIdx == g_Graphic.m_FrameCounter);
	assert(tl_CurrentThreadPassID != kInvalidPassID);

#if _DEBUG
	const ResourceAccessesArray& accesses = m_Passes.at(tl_CurrentThreadPassID).m_ResourceAccesses;

	// check if resource is requested by the current Pass
	auto it = std::ranges::find_if(accesses, [&resourceHandle](const ResourceAccess& access) { return access.m_ResourceHandle == &resourceHandle; });
	assert(it != accesses.end());
#endif // _DEBUG

	const Resource& resource = m_Resources.at(resourceHandle.m_ID);
	assert(resource.m_Resource);
	assert(resource.m_Type == resourceType);

	return resource.m_Resource.Get();
}

nvrhi::TextureHandle RenderGraph::GetTexture(const ResourceHandle& resourceHandle) const
{
	return (nvrhi::ITexture*)GetResourceInternal(resourceHandle, Resource::Type::Texture);
}

nvrhi::BufferHandle RenderGraph::GetBuffer(const ResourceHandle& resourceHandle) const
{
	return (nvrhi::IBuffer*)GetResourceInternal(resourceHandle, Resource::Type::Buffer);
}
