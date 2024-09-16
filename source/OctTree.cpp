#include "OctTree.h"

#include "Engine.h"
#include "GraphicPropertyGrid.h"

void OctTree::Insert(OctTree::Node* obj)
{
    assert(obj->m_ArrayIdx == (uint32_t)-1);
    SCOPED_MULTITHREAD_DETECTOR(m_MultithreadDetector);

    // insert object into leaf
    if (OctTree* child = GetChild(obj->m_AABB))
    {
        child->Insert(obj);
        return;
    }

    obj->m_ArrayIdx = (uint32_t)m_Objects.size();
    m_Objects.push_back(obj);

    // TODO: expose these?
    static const uint32_t kMaxLevel = UINT_MAX;// 4; // TODO: enforce max tree depth?
    static const uint32_t kCapacity = 16;

    // Subdivide if required
    if (m_Level < kMaxLevel)
    {
        if (IsLeaf() && (m_Objects.size() >= kCapacity))
        {
            Subdivide();
            Update(obj);
        }
    }
}

void OctTree::Remove(OctTree::Node* obj)
{
    assert(obj->m_ArrayIdx < m_Objects.size());
    SCOPED_MULTITHREAD_DETECTOR(m_MultithreadDetector);

    // swap with last element and pop
    OctTree::Node* lastNode = m_Objects.back();
    std::swap(m_Objects[obj->m_ArrayIdx], m_Objects.back());
    lastNode->m_ArrayIdx = obj->m_ArrayIdx;
    m_Objects.pop_back();

    obj->m_ArrayIdx = (uint32_t)-1;

    DiscardEmptyBuckets();
}

void OctTree::Update(OctTree::Node* obj)
{
    Remove(obj);

    // Not contained in this node -- insert into parent
    if (m_Parent && m_AABB.Contains(obj->m_AABB) != DirectX::ContainmentType::CONTAINS)
    {
        m_Parent->Insert(obj);
        return;
    }

    // Still within current node -- insert into leaf
    if (OctTree* child = GetChild(obj->m_AABB))
    {
        child->Insert(obj);
        return;
    }

    Insert(obj);
}

template <typename BoundingVolumeT>
static void GetObjectsInBoundInternal(const OctTree& octTree, const BoundingVolumeT& boundingVolume, std::vector<OctTree::Node*>& foundObjects, bool bFineGrainCulling)
{
    bool bGatherNodes = boundingVolume.Contains(octTree.m_AABB) != DirectX::ContainmentType::DISJOINT;
    bGatherNodes |= !g_GraphicPropertyGrid.m_DebugControllables.m_bEnableCPUOctTreeFrustumCulling;

    // TODO: bFineGrainCulling
    if (bGatherNodes)
    {
        foundObjects.insert(foundObjects.end(), octTree.m_Objects.begin(), octTree.m_Objects.end());
    }

    for (OctTree* leaf : octTree.m_Children)
    {
        if (!leaf)
        {
            continue;
        }

        GetObjectsInBoundInternal(*leaf, boundingVolume, foundObjects, bFineGrainCulling);
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
    uint32_t total = (uint32_t)m_Objects.size();

    for (OctTree* child : m_Children)
    {
        total += child ? child->TotalObjects() : 0;
    }

    return total;
}

void OctTree::Clear()
{
    m_Objects.clear();

    for (OctTree*& child : m_Children)
    {
        if (child)
        {
            child->Clear();
        }

        child = nullptr;
    }
}

void OctTree::Subdivide()
{
    assert(m_Allocator);

    for (uint32_t i = 0; i < kNbChildren; ++i)
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

        Vector3 newOctExtents = m_AABB.Extents * Vector3{ 0.5f };
        Vector3 newOctCenter = m_AABB.Center + newOctExtents * Vector3{ kOctExtentsMultiplier[i] };

        m_Children[i] = m_Allocator->NewObject();
        m_Children[i]->m_Allocator = m_Allocator;
        m_Children[i]->m_AABB = AABB{ newOctCenter, newOctExtents };
        m_Children[i]->m_Level = m_Level + 1;
        m_Children[i]->m_Parent = this;
    }
}

void OctTree::DiscardEmptyBuckets()
{
    if (!m_Objects.empty())
    {
        return;
    }

    for (OctTree* child : m_Children)
    {
        if (child && (!child->IsLeaf() || !child->m_Objects.empty()))
        {
            return;
        }
    }

    Clear();

    if (m_Parent)
    {
        m_Parent->DiscardEmptyBuckets();
    }
}

OctTree* OctTree::GetChild(const AABB& bound) const
{
    for (OctTree* child : m_Children)
    {
        if (child && child->m_AABB.Contains(bound) == DirectX::ContainmentType::CONTAINS)
        {
            return child;
        }
    }

    return nullptr; // Cannot contain boundary -- too large
}

bool OctTree::IsLeaf() const
{
    bool bIsLeaf = true;
    for (OctTree* leaf : m_Children)
    {
        if (leaf)
        {
            bIsLeaf = false;
            break;
        }
    }
    return bIsLeaf;
}

void OctTree::ForEachOctTree(void(*func)(const OctTree&))
{
    func(*this);

    for (OctTree* leaf : m_Children)
    {
        if (leaf)
        {
            leaf->ForEachOctTree(func);
        }
    }
}
