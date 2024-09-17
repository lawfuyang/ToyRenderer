#pragma once

#include "MathUtilities.h"
#include "CriticalSection.h"

class OctTree
{
public:
    struct Node
    {
        uint32_t m_Data;
        AABB m_AABB;

        uint32_t m_ArrayIdx = (uint32_t)-1;
    };

    OctTree();

    void Insert(Node* obj, uint32_t nodeIdx);
    void Remove(Node* obj);
    void Update(Node* obj, uint32_t nodeIdx);
    void GetObjectsInBound(const Frustum& frustum, std::vector<Node*>& foundObjects, bool bFineGrainCulling);
    void GetObjectsInBound(const OBB& obb, std::vector<Node*>& foundObjects, bool bFineGrainCulling);
    uint32_t TotalObjects() const;
    void Clear();
    void Subdivide();
    void DiscardEmptyBuckets();

    // Returns child that contains the provided boundary
    OctTree* GetChild(const AABB& bound) const;

    bool IsLeaf() const;

    void ForEachOctTree(void(*func)(const OctTree&));

    static const uint32_t kNbChildren = 8;

    AABB m_AABB;
    uint32_t m_Level = 0;
    
    uint32_t m_CurrentIdx = UINT32_MAX;
    uint32_t m_ParentIdx = UINT32_MAX;

    // nodes are in this order:
        // -x, -y, -z
        // -x, -y, +z
        // -x, +y, -z
        // -x, +y, +z
        // +x, -y, -z
        // +x, -y, +z
        // +x, +y, -z
        // +x, +y, +z
    uint32_t m_ChildrenIndices[kNbChildren];

    std::vector<uint32_t> m_NodeIndices;
};

class OctTreeRoot
{
public:
    OctTree m_Root;
    std::vector<OctTree> m_OctTrees;
    std::vector<OctTree::Node> m_OctTreeNodes;
};
