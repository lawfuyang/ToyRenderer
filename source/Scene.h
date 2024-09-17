#pragma once

#include "nvrhi/nvrhi.h"

#include "Allocators.h"
#include "MathUtilities.h"
#include "OctTree.h"
#include "TileRenderingHelper.h"
#include "Utilities.h"
#include "Visual.h"

#include "extern/taskflow/taskflow.hpp"

class Primitive;
class RenderGraph;

enum EVisualBucketType { Opaque, Transparent };

struct GPUCullingCounters
{
    uint32_t m_Frustum;
    uint32_t m_OcclusionPhase1;
    uint32_t m_OcclusionPhase2;
};

class View
{
public:
    void Initialize();
    void Update();
    void GatherVisibleVisualProxies();
    void UpdateVisibleVisualProxiesBuffer(nvrhi::CommandListHandle commandList);

    bool m_bIsMainView = false;

    float m_ZNearP = 0.1f;
    float m_ZFarP = 100.0f;

    bool m_bIsPerspective = true;
    float m_FOV = ConvertToRadians(45.0f);
    float m_AspectRatio = 16.0f / 9.0f;
    float m_Width = 100.0f;
    float m_Height = 100.0f;
    Vector3 m_Eye;
    Vector3 m_LookAt;
    Vector3 m_Right;
    Vector3 m_Up;

    Matrix m_ViewMatrix;
    Matrix m_ProjectionMatrix;
    Matrix m_ViewProjectionMatrix;
    Matrix m_InvViewMatrix;
    Matrix m_InvProjectionMatrix;
    Matrix m_InvViewProjectionMatrix;

    Matrix m_PrevFrameViewMatrix;
    Matrix m_PrevFrameProjectionMatrix;
    Matrix m_PrevFrameViewProjectionMatrix;
    Matrix m_PrevFrameInvViewMatrix;
    Matrix m_PrevFrameInvProjectionMatrix;
    Matrix m_PrevFrameInvViewProjectionMatrix;

    Frustum m_Frustum;

    std::vector<uint32_t> m_FrustumVisibleVisualProxiesIndices;
    SimpleResizeableGPUBuffer m_FrustumVisibleVisualProxiesIndicesBuffer;

    SimpleResizeableGPUBuffer m_InstanceCountBuffer;
    SimpleResizeableGPUBuffer m_DrawIndexedIndirectArgumentsBuffer;
    SimpleResizeableGPUBuffer m_StartInstanceConstsOffsetsBuffer;

    GPUCullingCounters m_GPUCullingCounters;
};

struct VisualProxy
{
    uint32_t m_NodeID;
    const Primitive* m_Primitive;
    Matrix m_WorldMatrix;
    Matrix m_PrevFrameWorldMatrix;
};

class Scene
{
public:
    enum EView { Main, CSM0, CSM1, CSM2, CSM3 };

    void Initialize();
    void Update();
    void Shutdown();
    void UpdateIMGUIPropertyGrid();
    void OnSceneLoad();
    uint32_t InsertPrimitive(Primitive* p, const Matrix& worldMatrix);
    void UpdatePrimitive(Primitive* p, const Matrix& worldMatrix, uint32_t proxyIdx);
    void CalculateCSMSplitDistances();
    void PostRender();

    std::shared_ptr<RenderGraph> m_RenderGraph;

    View m_Views[EnumUtils::Count<EView>()];

    float m_SunOrientation = 0.0f;
    float m_SunInclination = 30.0f;
    Vector3 m_DirLightVec = Vector3{ 0.5773502691896258f, 0.5773502691896258f, -0.5773502691896258f };
    Vector3 m_DirLightColor = Vector3::One;
    float m_DirLightStrength = 1.0f;

    float m_LastFrameExposure = 1.0f;
    float m_CSMSplitDistances[4];

    AABB m_AABB = { Vector3::Zero, Vector3::Zero };
    Sphere m_BoundingSphere = { Vector3::Zero, 0.0f };

    std::vector<Node> m_Nodes;
    std::vector<Visual> m_Visuals;

    OctTree m_OctTree;
    DynamicObjectPool<OctTree> m_OctTreeAllocator;

    std::vector<OctTree::Node*> m_OctTreeNodes;
    DynamicObjectPool<OctTree::Node> m_OctTreeNodeAllocator;

    std::vector<VisualProxy> m_VisualProxies;

    nvrhi::TextureHandle m_HZB;
    nvrhi::BufferHandle m_LuminanceBuffer;

    TileRenderingHelper m_DeferredLightingTileRenderingHelper;

    SimpleResizeableGPUBuffer m_OcclusionCullingPhaseTwoInstanceCountBuffer;
    SimpleResizeableGPUBuffer m_OcclusionCullingPhaseTwoStartInstanceConstsOffsetsBuffer;
    SimpleResizeableGPUBuffer m_OcclusionCullingPhaseTwoDrawIndexedIndirectArgumentsBuffer;

    SimpleResizeableGPUBuffer m_InstanceConstsBuffer;

private:
    void UpdateMainViewCameraControls();
    void UpdateCSMViews();
    void RenderOctTreeDebug();
    void UpdatePicking();
    void CullAndPrepareInstanceDataForViews();
    void UpdateInstanceConstsBuffer(nvrhi::CommandListHandle commandList);

    // TODO: move this shit to some sort of camera class
    Vector2 m_CurrentMousePos;
    Vector2 m_MouseLastPos;
    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
};
