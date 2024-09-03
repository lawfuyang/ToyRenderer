#include "RenderGraph.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"

// NOTE: jank solution to access the correct ResourceAccess array index via PassID of the currently executing thread
thread_local RenderGraph::PassID tl_CurrentThreadPassID = RenderGraph::kInvalidPassID;

struct RenderGraphIMGUIData
{
	// essentially copy-paste of RenderGraph::ResourceAccess, but with a deep copy of the ResourceHandle
	struct ResourceAccess
	{
		RenderGraph::ResourceHandle m_ResourceHandle;
		RenderGraph::Resource::AccessType m_AccessType;
	};

	struct Pass
	{
		IRenderer* m_Renderer;
		std::vector<ResourceAccess> m_ResourceAccesses;
	};

	std::vector<Pass> m_Passes;
	std::vector<RenderGraph::Resource> m_Resources;
	std::vector<nvrhi::TextureDesc> m_TextureCreationDescs;
	std::vector<nvrhi::BufferDesc> m_BufferCreationDescs;
};
static RenderGraphIMGUIData gs_RenderGraphIMGUIData;

void RenderGraph::InitializeForFrame(tf::Taskflow& taskFlow)
{
	assert(!m_TaskFlow);
	m_TaskFlow = &taskFlow;
	m_CommandListQueueTasks.clear();
}

void RenderGraph::PostRender()
{
	PROFILE_FUNCTION();

	// cache resource for reuse in next frame
	for (const Resource& resource : m_Resources)
	{
		assert(resource.m_Resource);
		if (resource.m_Type == Resource::Type::Texture)
		{
			nvrhi::TextureHandle texture = (nvrhi::ITexture*)resource.m_Resource.Get();
			const std::size_t textureDescHash = HashResourceDesc(texture->getDesc());
			m_CachedTextures[textureDescHash].push_back(texture);
		}
		else
		{
			nvrhi::BufferHandle buffer = (nvrhi::IBuffer*)resource.m_Resource.Get();
			const std::size_t bufferDescHash = HashResourceDesc(buffer->getDesc());
			m_CachedBuffers[bufferDescHash].push_back(buffer);
		}
	}

	gs_RenderGraphIMGUIData.m_Passes.clear();
	gs_RenderGraphIMGUIData.m_Resources.clear();
	gs_RenderGraphIMGUIData.m_TextureCreationDescs.clear();
	gs_RenderGraphIMGUIData.m_BufferCreationDescs.clear();
	
	auto& renderGraphControllables = g_GraphicPropertyGrid.m_RenderGraphControllables;
	if (renderGraphControllables.m_bUpdateIMGUI)
	{
		// deep copy data for IMGUI
		for (const Pass& pass : m_Passes)
        {
			RenderGraphIMGUIData::Pass& newPass = gs_RenderGraphIMGUIData.m_Passes.emplace_back();

			newPass.m_Renderer = pass.m_Renderer;

            for (const ResourceAccess& resourceAccess : pass.m_ResourceAccesses)
            {
				newPass.m_ResourceAccesses.push_back({ *resourceAccess.m_ResourceHandle, resourceAccess.m_AccessType });
            }
        }

		gs_RenderGraphIMGUIData.m_Resources = m_Resources;

		// don't extend the lifetime of the resource handles
		for (Resource& resource : gs_RenderGraphIMGUIData.m_Resources)
        {
            resource.m_Resource = nullptr;
        }

		gs_RenderGraphIMGUIData.m_TextureCreationDescs = m_TextureCreationDescs;
		gs_RenderGraphIMGUIData.m_BufferCreationDescs = m_BufferCreationDescs;

		renderGraphControllables.m_bUpdateIMGUI = false;
	}

	m_TaskFlow = nullptr;
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

void RenderGraph::DrawIMGUI()
{
    static bool s_ShowPassViewWindow = false;
    if (ImGui::Button("Show Pass View Window"))
    {
		s_ShowPassViewWindow = !s_ShowPassViewWindow;
    }

    if (s_ShowPassViewWindow && ImGui::Begin("Pass View Window"))
    {
        if (ImGui::BeginTable("Passes", 2, ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Renderer");
            ImGui::TableSetupColumn("Resources");
            ImGui::TableHeadersRow();

            for (uint32_t i = 0; i < gs_RenderGraphIMGUIData.m_Passes.size(); i++)
            {
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                const RenderGraphIMGUIData::Pass& pass = gs_RenderGraphIMGUIData.m_Passes.at(i);
                ImGui::PushID(i);

				// TODO: compute queue
				const char* passName = StringFormat("%s (Graphics)", pass.m_Renderer->m_Name.c_str());

                if (ImGui::TreeNodeEx(passName, flags))
                {
                    for (const RenderGraphIMGUIData::ResourceAccess& access : pass.m_ResourceAccesses)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        const Resource& resource = gs_RenderGraphIMGUIData.m_Resources.at(access.m_ResourceHandle.m_ID);

                        const char* resourceName =
                            (resource.m_Type == Resource::Type::Texture) ?
                            gs_RenderGraphIMGUIData.m_TextureCreationDescs.at(resource.m_DescIdx).debugName.c_str() :
                            gs_RenderGraphIMGUIData.m_BufferCreationDescs.at(resource.m_DescIdx).debugName.c_str();

                        const char* accessType = (access.m_AccessType == Resource::AccessType::Read) ? "Read" : "Write";

                        ImGui::TreeNodeEx(resourceName, flags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                        ImGui::TableNextColumn();
                        ImGui::Text(accessType);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
        ImGui::End();
    }

	static bool s_ShowResourceUsageWindow = false;
	if (ImGui::Button("Show Resource Usage Window"))
	{
		s_ShowResourceUsageWindow = !s_ShowResourceUsageWindow;
	}

	if (s_ShowResourceUsageWindow && ImGui::Begin("Resource Usage Window"))
	{


		ImGui::End();
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

	// allocate resources
	for (Resource& resource : m_Resources)
    {
		assert(resource.m_DescIdx != -1);

		const char* resourceDebugName = "";

		if (resource.m_Type == Resource::Type::Texture)
		{
			const nvrhi::TextureDesc& desc = m_TextureCreationDescs.at(resource.m_DescIdx);
			resourceDebugName = desc.debugName.c_str();

			const std::size_t textureDescHash = HashResourceDesc(desc);

			// check if resource is cached
			if (auto it = m_CachedTextures.find(textureDescHash); 
				it != m_CachedTextures.end())
			{
				std::vector<nvrhi::TextureHandle>& textureCache = it->second;
				resource.m_Resource = textureCache.back();
				textureCache.pop_back();
			}
			else
			{
				resource.m_Resource = g_Graphic.m_NVRHIDevice->createTexture(desc);
			}
		}
		else
		{
			const nvrhi::BufferDesc& desc = m_BufferCreationDescs.at(resource.m_DescIdx);
			resourceDebugName = desc.debugName.c_str();

            const std::size_t bufferDescHash = HashResourceDesc(desc);

            // check if resource is cached
            if (auto it = m_CachedBuffers.find(bufferDescHash); 
				it != m_CachedBuffers.end())
            {
				std::vector<nvrhi::BufferHandle>& bufferCache = it->second;
                resource.m_Resource = bufferCache.back();
                bufferCache.pop_back();
            }
            else
            {
                resource.m_Resource = g_Graphic.m_NVRHIDevice->createBuffer(desc);
            }
		}

		// set resource debug name to desc debug name
		Graphic::UpdateResourceDebugName(resource.m_Resource, resourceDebugName);
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

	const ResourceAccessesArray& accesses = m_Passes.at(tl_CurrentThreadPassID).m_ResourceAccesses;

	// check if resource is requested by the current Pass
	auto it = std::ranges::find_if(accesses, [&resourceHandle](const ResourceAccess& access) { return access.m_ResourceHandle == &resourceHandle; });
	assert(it != accesses.end());

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
