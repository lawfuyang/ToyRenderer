#pragma once

#include "MathUtilities.h"
#include "CriticalSection.h"

class OctTree
{
public:
    struct Node
    {
        // NOTE: can be anything, so long as its <= 8 bytes
        void* m_Data;

        AABB m_AABB;

        uint32_t m_ArrayIdx = (uint32_t)-1;
    };

    void Insert(Node* obj);
    void Remove(Node* obj);
    void Update(Node* obj);
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

    MultithreadDetector m_MultithreadDetector;

    AABB m_AABB;
    uint32_t m_Level = 0;
    OctTree* m_Parent = nullptr;

    // nodes are in this order:
        // -x, -y, -z
        // -x, -y, +z
        // -x, +y, -z
        // -x, +y, +z
        // +x, -y, -z
        // +x, -y, +z
        // +x, +y, -z
        // +x, +y, +z
    std::shared_ptr<OctTree> m_Children[kNbChildren];

    std::vector<Node*> m_Objects;
};
