#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/taskflow/taskflow/taskflow.hpp"

#include "SmallVector.h"

class IRenderer;

class RenderGraph
{
public:
	using PassID = uint8_t;

	static const PassID kInvalidPassID = std::numeric_limits<PassID>::max();

	enum class Phase { Setup, Execute };

	struct ResourceHandle
	{
		enum class Type : uint8_t { Texture, Buffer };
		enum class AccessType : uint8_t { Read, Write };

		nvrhi::ResourceHandle m_Resource;

		bool m_bAllocated = false;

		uint32_t m_AllocatedFrameIdx = UINT32_MAX;
		uint32_t m_DescIdx = UINT32_MAX;
		Type m_Type = (Type)UINT32_MAX;

		// Compile-time data
		PassID m_FirstAccess = kInvalidPassID; // First pass that accesses this resource
		PassID m_LastAccess = kInvalidPassID;  // Last pass that accesses this resource
		PassID m_LastWrite = kInvalidPassID;   // Last pass that wrote to this resource. Used for pass culling
	};

	struct ResourceDesc
	{
		nvrhi::TextureDesc m_TextureDesc;
		nvrhi::BufferDesc m_BufferDesc;
	};

	struct ResourceAccess
	{
		ResourceHandle* m_ResourceHandle;
		ResourceHandle::AccessType m_AccessType;
	};

	using ResourceAccessesArray = SmallVector<ResourceAccess, 8>;

	struct Pass
	{
		IRenderer* m_Renderer;
		ResourceAccessesArray m_ResourceAccesses;
		nvrhi::CommandListHandle m_CommandList;
	};

	struct Heap
	{
	public:
		uint64_t Allocate(uint64_t size);
		void Free(uint64_t heapOffset);
		void FindBest(uint64_t size, uint32_t& foundIdx, uint64_t& heapOffset);
		void FindFirst(uint64_t size, uint32_t& foundIdx, uint64_t& heapOffset);

		nvrhi::HeapHandle m_Heap;

		struct Block
		{
			uint64_t m_Size;
			bool m_Allocated;
		};
		std::vector<Block> m_Blocks;

		uint64_t m_Used = 0;
		uint64_t m_Peak = 0;
	};
	
	void Initialize();
	void InitializeForFrame(tf::Taskflow& taskFlow);
	void Shutdown();
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
	void AddDependencyInternal(ResourceHandle& resourceHandle, ResourceHandle::AccessType accessType);

	void CreateTransientResourceInternal(ResourceHandle& resourceHandle, ResourceHandle::Type resourceType);
	nvrhi::IResource* GetResourceInternal(const ResourceHandle& resourceHandle, ResourceHandle::Type resourceType) const;

	tf::Taskflow* m_TaskFlow;
	
	std::vector<tf::Task> m_CommandListQueueTasks;
	std::vector<Pass> m_Passes;

	std::vector<ResourceHandle*> m_ResourceHandles;
	std::vector<ResourceDesc> m_ResourceDescs;

	std::vector<nvrhi::HeapHandle> m_FreeHeaps;
	std::vector<nvrhi::HeapHandle> m_UsedHeaps;

	Phase m_CurrentPhase = Phase::Setup;

	Heap m_Heap;
};
