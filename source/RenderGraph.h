#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/taskflow/taskflow/taskflow.hpp"

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
		uint32_t m_HeapOffset = UINT32_MAX;
		uint32_t m_HeapIdx = UINT32_MAX;

		uint32_t m_AllocatedFrameIdx = UINT32_MAX;
		uint32_t m_DescIdx = UINT32_MAX;
		Type m_Type;

		// Compile-time data
		PassID m_FirstAccess = kInvalidPassID; // First pass that accesses this resource
		PassID m_LastAccess = kInvalidPassID;  // Last pass that accesses this resource
		//PassID m_LastWrite = kInvalidPassID;   // Last pass that wrote to this resource. Used for pass culling
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

	struct Pass
	{
		IRenderer* m_Renderer;
		std::vector<ResourceAccess> m_ResourceAccesses;
		nvrhi::CommandListHandle m_CommandList;
	};

	struct Heap
	{
	public:
		uint32_t Allocate(uint32_t size);
		void Free(uint32_t heapOffset);
		void FindBest(uint32_t size, uint32_t& foundIdx, uint32_t& heapOffset);
		void FindFirst(uint32_t size, uint32_t& foundIdx, uint32_t& heapOffset);

		nvrhi::HeapHandle m_Heap;

		struct Block
		{
			uint32_t m_Size;
			bool m_Allocated;
		};
		std::vector<Block> m_Blocks;

		uint32_t m_Used = 0;
		uint32_t m_Peak = 0;
	};
	
	void Initialize();
	void InitializeForFrame(tf::Taskflow& taskFlow);
	void Shutdown();
	void Compile();
	tf::Task AddRenderer(IRenderer* renderer);
	void UpdateIMGUI();

	// Setup Phase funcs
	template <typename ResourceDescT>
	void CreateTransientResource(ResourceHandle& resourceHandle, const ResourceDescT& resourceDesc);

	void AddReadDependency(ResourceHandle& resourceHandle) { AddDependencyInternal(resourceHandle, ResourceHandle::AccessType::Read); }
	void AddWriteDependency(ResourceHandle& resourceHandle) { AddDependencyInternal(resourceHandle, ResourceHandle::AccessType::Write); }

	// Execute Phase funcs
	[[nodiscard]] nvrhi::TextureHandle GetTexture(const ResourceHandle& resourceHandle) const { return (nvrhi::ITexture*)GetResourceInternal(resourceHandle, ResourceHandle::Type::Texture); }
	[[nodiscard]] nvrhi::BufferHandle GetBuffer(const ResourceHandle& resourceHandle) const { return (nvrhi::IBuffer*)GetResourceInternal(resourceHandle, ResourceHandle::Type::Buffer); }

private:
	void AddDependencyInternal(ResourceHandle& resourceHandle, ResourceHandle::AccessType accessType);
	nvrhi::IResource* GetResourceInternal(const ResourceHandle& resourceHandle, ResourceHandle::Type resourceType) const;
    void FreeResource(ResourceHandle& resourceHandle);
    const char* GetResourceName(const ResourceHandle& resourceHandle) const;
	void CreateNewHeap(uint32_t size);

	tf::Taskflow* m_TaskFlow;

	bool m_bPassCulling = true;
	bool m_bResourceAliasing = true;
	
	std::vector<tf::Task> m_CommandListQueueTasks;
	std::vector<Pass> m_Passes;

	std::vector<ResourceHandle*> m_ResourceHandles;
	std::vector<ResourceDesc> m_ResourceDescs;

	struct HeapToFree
	{
		uint32_t m_Idx;
		uint32_t m_Offset;
	};

    std::vector<HeapToFree> m_HeapsToFree;
    std::vector<ResourceHandle*> m_ResourcesToAlloc;

	Phase m_CurrentPhase = Phase::Setup;

	std::vector<Heap> m_Heaps;
};
