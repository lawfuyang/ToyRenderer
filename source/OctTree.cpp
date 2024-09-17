#include "OctTree.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"
#include "GraphicPropertyGrid.h"

OctTree::OctTree()
{
    for (uint32_t& i : m_ChildrenIndices)
    {
        i = UINT_MAX;
    }
}

void OctTree::Insert(OctTree::Node* obj, uint32_t nodeIdx)
{
    assert(obj->m_ArrayIdx == (uint32_t)-1);

    // insert object into leaf
    if (OctTree* child = GetChild(obj->m_AABB))
    {
        child->Insert(obj, nodeIdx);
        return;
    }

    obj->m_ArrayIdx = m_NodeIndices.size();
    m_NodeIndices.push_back(nodeIdx);

    // TODO: expose these?
    static const uint32_t kMaxLevel = UINT_MAX;// 4; // TODO: enforce max tree depth?
    static const uint32_t kCapacity = 16;

    // Subdivide if required
    if (m_Level < kMaxLevel)
    {
        if (IsLeaf() && (m_NodeIndices.size() >= kCapacity))
        {
            Subdivide();
            Update(obj, nodeIdx);
        }
    }
}

void OctTree::Remove(OctTree::Node* obj)
{
    assert(obj->m_ArrayIdx < m_NodeIndices.size());

    // swap with last element and pop
    OctTree::Node& lastNode = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTreeNodes.at(m_NodeIndices.back());
    lastNode.m_ArrayIdx = obj->m_ArrayIdx;

    std::swap(m_NodeIndices[obj->m_ArrayIdx], m_NodeIndices.back());
    m_NodeIndices.pop_back();

    obj->m_ArrayIdx = (uint32_t)-1;

    DiscardEmptyBuckets();
}

void OctTree::Update(OctTree::Node* obj, uint32_t nodeIdx)
{
    Remove(obj);

    // Not contained in this node -- insert into parent
    if (m_ParentIdx != UINT_MAX && m_AABB.Contains(obj->m_AABB) != DirectX::ContainmentType::CONTAINS)
    {
        OctTree& parentTree = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.at(m_ParentIdx);
        parentTree.Insert(obj, nodeIdx);
        return;
    }

    // Still within current node -- insert into leaf
    if (OctTree* child = GetChild(obj->m_AABB))
    {
        child->Insert(obj, nodeIdx);
        return;
    }

    Insert(obj, nodeIdx);
}

template <typename BoundingVolumeT>
static void GetObjectsInBoundInternal(const OctTree& octTree, const BoundingVolumeT& boundingVolume, std::vector<OctTree::Node*>& foundObjects, bool bFineGrainCulling)
{
    bool bGatherNodes = boundingVolume.Contains(octTree.m_AABB) != DirectX::ContainmentType::DISJOINT;
    bGatherNodes |= !g_GraphicPropertyGrid.m_DebugControllables.m_bEnableCPUOctTreeFrustumCulling;

    // TODO: bFineGrainCulling
    if (bGatherNodes)
    {
        foundObjects.reserve(foundObjects.size() + octTree.m_NodeIndices.size());

        for (uint32_t i : octTree.m_NodeIndices)
        {
            OctTree::Node& node = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTreeNodes.at(i);
            foundObjects.push_back(&node);
        }
    }

    for (uint32_t childIdx : octTree.m_ChildrenIndices)
    {
        if (childIdx == UINT_MAX)
        {
            continue;
        }

        const OctTree& childTree = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.at(childIdx);

        GetObjectsInBoundInternal(childTree, boundingVolume, foundObjects, bFineGrainCulling);
    }
}

void OctTree::GetObjectsInBound(const Frustum& frustum, std::vector<Node*>& foundObjects, bool bFineGrainCulling)
{
    GetObjectsInBoundInternal(*this, frustum, foundObjects, bFineGrainCulling);
}

void OctTree::GetObjectsInBound(const OBB& obb, std::vector<Node*>& foundObjects, bool bFineGrainCulling)
{
    GetObjectsInBoundInternal(*this, obb, foundObjects, bFineGrainCulling);
}

uint32_t OctTree::TotalObjects() const
{
    uint32_t total = m_NodeIndices.size();

    for (uint32_t childIdx : m_ChildrenIndices)
    {
        if (childIdx == UINT_MAX)
        {
            continue;
        }

        total += g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.at(childIdx).TotalObjects();
    }

    return total;
}

void OctTree::Clear()
{
    m_NodeIndices.clear();

    for (uint32_t& childIdx : m_ChildrenIndices)
    {
        if (childIdx == UINT_MAX)
        {
            continue;
        }

        g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.at(childIdx).Clear();

        childIdx = UINT_MAX;
    }
}

void OctTree::Subdivide()
{
    static const Vector3 kOctExtentsMultiplier[kNbChildren] =
    {
        { -1.0f, -1.0f, -1.0f }, // -x, -y, -z
        { -1.0f, -1.0f,  1.0f }, // -x, -y, +z
        { -1.0f,  1.0f, -1.0f }, // -x, +y, -z
        { -1.0f,  1.0f,  1.0f }, // -x, +y, +z
        {  1.0f, -1.0f, -1.0f }, // +x, -y, -z
        {  1.0f, -1.0f,  1.0f }, // +x, -y, +z
        {  1.0f,  1.0f, -1.0f }, // +x, +y, -z
        {  1.0f,  1.0f,  1.0f }, // +x, +y, +z
    };

    for (uint32_t i = 0; i < kNbChildren; ++i)
    {
        assert(m_ChildrenIndices[i] == UINT_MAX);

        Vector3 newOctExtents = m_AABB.Extents * Vector3{ 0.5f };
        Vector3 newOctCenter = m_AABB.Center + newOctExtents * Vector3{ kOctExtentsMultiplier[i] };

        const uint32_t newTreeIdx = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.size();
        OctTree& newTree = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.emplace_back();
        newTree.m_CurrentIdx = newTreeIdx;

        newTree.m_AABB = AABB{ newOctCenter, newOctExtents };
        newTree.m_Level = m_Level + 1;
        newTree.m_ParentIdx = m_CurrentIdx;
    }
}

void OctTree::DiscardEmptyBuckets()
{
    if (!m_NodeIndices.empty())
    {
        return;
    }

    for (uint32_t& childIdx : m_ChildrenIndices)
    {
        if (childIdx == UINT_MAX)
        {
            continue;
        }

        OctTree& childTree = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.at(childIdx);

        if (!childTree.IsLeaf() || !childTree.m_NodeIndices.empty())
        {
            return;
        }
    }

    Clear();

    if (m_ParentIdx != UINT_MAX)
    {
        OctTree& parentTree = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.at(m_ParentIdx);

        parentTree.DiscardEmptyBuckets();
    }
}

OctTree* OctTree::GetChild(const AABB& bound) const
{
    for (uint32_t childIdx : m_ChildrenIndices)
    {
        if (childIdx == UINT_MAX)
        {
            continue;
        }

        OctTree& childTree = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.at(childIdx);

        if (childTree.m_AABB.Contains(bound) == DirectX::ContainmentType::CONTAINS)
        {
            return &childTree;
        }
    }

    return nullptr; // Cannot contain boundary -- too large
}

bool OctTree::IsLeaf() const
{
    return std::any_of(std::begin(m_ChildrenIndices), std::end(m_ChildrenIndices), [](uint32_t childIdx) { return childIdx != UINT_MAX; });
}

void OctTree::ForEachOctTree(void(*func)(const OctTree&))
{
    func(*this);

    for (uint32_t childIdx : m_ChildrenIndices)
    {
        if (childIdx != UINT_MAX)
        {
            OctTree& childTree = g_Graphic.m_Scene->m_OctTreeRoot.m_OctTrees.at(childIdx);
            childTree.ForEachOctTree(func);
        }
    }
}
