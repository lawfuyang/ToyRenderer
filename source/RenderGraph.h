#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/taskflow/taskflow/taskflow.hpp"

#include "SmallVector.h"

class IRenderer;

class RenderGraph
{
public:
	using PassID = uint8_t;

	static const uint32_t kInvalidResourceHandle = UINT_MAX;
	static const PassID kInvalidPassID = UINT8_MAX;

	enum class Phase { Setup, Execute };

	struct ResourceHandle
	{
		uint32_t m_ID = kInvalidResourceHandle;
		uint32_t m_AllocatedFrameIdx = -1;
	};

	struct Resource
	{
		enum class Type { Texture, Buffer };
		enum class AccessType { Read, Write };

		nvrhi::ResourceHandle m_Resource;

		Type m_Type = (Type)-1;

		uint16_t m_DescIdx = -1;

		// Compile-time data
		PassID m_FirstAccess = kInvalidPassID; // First pass that accesses this resource
		PassID m_LastAccess = kInvalidPassID;  // Last pass that accesses this resource
		PassID m_LastWrite = kInvalidPassID;   // Last pass that wrote to this resource. Used for pass culling
	};

	struct ResourceAccess
	{
		ResourceHandle* m_ResourceHandle;
		Resource::AccessType m_AccessType;
	};

	using ResourceAccessesArray = SmallVector<ResourceAccess, 8>;

	struct Pass
	{
		IRenderer* m_Renderer;
		ResourceAccessesArray m_ResourceAccesses;
		nvrhi::CommandListHandle m_CommandList;
	};
	
	void InitializeForFrame(tf::Taskflow& taskFlow);
	void PostRender();
	void DrawIMGUI();

	void Compile();
	void AddRenderer(IRenderer* renderer, tf::Task* taskToSucceed = nullptr);

	// Setup Phase funcs
	void CreateTransientResource(ResourceHandle& resourceHandle, const nvrhi::TextureDesc& desc);
	void CreateTransientResource(ResourceHandle& resourceHandle, const nvrhi::BufferDesc& desc);
	void AddReadDependency(ResourceHandle& resourceHandle);
	void AddWriteDependency(ResourceHandle& resourceHandle);

	// Execute Phase funcs
	[[nodiscard]] nvrhi::TextureHandle GetTexture(const ResourceHandle& resourceHandle) const;
	[[nodiscard]] nvrhi::BufferHandle GetBuffer(const ResourceHandle& resourceHandle) const;

private:
	void AddDependencyInternal(ResourceHandle& resourceHandle, Resource::AccessType accessType);

	Resource& CreateTransientResourceInternal(ResourceHandle& resourceHandle, Resource::Type resourceType);
	nvrhi::IResource* GetResourceInternal(const ResourceHandle& resourceHandle, Resource::Type resourceType) const;

	tf::Taskflow* m_TaskFlow;
	
	std::vector<tf::Task> m_CommandListQueueTasks;
	std::vector<Pass> m_Passes;
	std::vector<Resource> m_Resources;
	std::vector<nvrhi::TextureDesc> m_TextureCreationDescs;
	std::vector<nvrhi::BufferDesc> m_BufferCreationDescs;
	std::vector<ResourceHandle*> m_ResourceHandles;

	std::vector<nvrhi::HeapHandle> m_FreeHeaps;
	std::vector<nvrhi::HeapHandle> m_UsedHeaps;

	Phase m_CurrentPhase = Phase::Setup;
};
