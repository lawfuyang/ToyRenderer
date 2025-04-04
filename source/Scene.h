#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

#include "MathUtilities.h"
#include "Utilities.h"
#include "Visual.h"

class Primitive;
class RenderGraph;

struct Animation
{
    struct Channel
    {
        enum class PathType { Translation, Rotation, Scale };

        uint32_t m_TargetNodeIdx;
        std::vector<float> m_KeyFrames; // in seconds
        std::vector<Vector4> m_Data;
        PathType m_PathType;

        Vector4 Evaluate(float time) const;
    };

    float m_TimeStart;
    float m_TimeEnd;

    std::string m_Name;
    std::vector<Channel> m_Channels;
};

struct GPUCullingCounters
{
    uint32_t m_EarlyInstances;
    uint32_t m_EarlyMeshlets;
    uint32_t m_LateInstances;
    uint32_t m_LateMeshlets;
};

class View
{
public:
    void Update();
    void UpdateVectors(float yaw, float pitch);

    float m_ZNearP = 0.1f;

    float m_FOV = ConvertToRadians(45.0f);
    float m_AspectRatio = 16.0f / 9.0f;
    Vector3 m_Eye;
    Quaternion m_Orientation;

    Matrix m_CullingWorldToView;
    Matrix m_CullingPrevWorldToView;

    Matrix m_WorldToView;
    Matrix m_ViewToClip;
    Matrix m_WorldToClip;
    Matrix m_ViewToWorld;
    Matrix m_ClipToWorld;

    Matrix m_PrevWorldToView;
    Matrix m_PrevViewToClip;
    Matrix m_PrevWorldToClip;

    Frustum m_Frustum;

    GPUCullingCounters m_GPUCullingCounters;
};

class Scene
{
public:
    struct Camera
    {
        std::string m_Name;
        Vector3 m_Position;
        Quaternion m_Orientation;
    };

    void Initialize();
    void Update();
    void Shutdown();
    void UpdateIMGUIPropertyGrid();
    void PostSceneLoad();
    void SetCamera(uint32_t idx);

    bool IsShadowsEnabled() const;

    std::shared_ptr<RenderGraph> m_RenderGraph;

    View m_View;

    double m_AnimationTimeSeconds = 0.0;
    float m_SunOrientation = 270.0f;
    float m_SunInclination = 30.0f;
    Vector3 m_DirLightVec = Vector3{ 0.5773502691896258f, 0.5773502691896258f, -0.5773502691896258f };
    Vector3 m_DirLightColor = Vector3::One;
    float m_DirLightStrength = 1.0f;
    float m_LastFrameExposure = 1.0f;

    AABB m_AABB;
    Sphere m_BoundingSphere;
    OBB m_OBB;

    std::vector<Node> m_Nodes;
    std::vector<Primitive> m_Primitives;
    std::vector<uint32_t> m_OpaquePrimitiveIDs;
    std::vector<uint32_t> m_AlphaMaskPrimitiveIDs;
    std::vector<uint32_t> m_TransparentPrimitiveIDs;
    std::vector<Camera> m_Cameras;
    std::vector<Animation> m_Animations;

    // because I really dont want to include ShaderInterop.h in a header file...
    struct NodeLocalTransformBytes { std::byte m_Bytes[48]; };
    std::vector<NodeLocalTransformBytes> m_NodeLocalTransforms;

    nvrhi::TextureHandle m_HZB;
    nvrhi::BufferHandle m_LuminanceBuffer;
    nvrhi::BufferHandle m_InstanceConstsBuffer;
    nvrhi::BufferHandle m_OpaqueInstanceIDsBuffer;
    nvrhi::BufferHandle m_AlphaMaskInstanceIDsBuffer;
    nvrhi::BufferHandle m_TransparentInstanceIDsBuffer;
    nvrhi::BufferHandle m_NodeLocalTransformsBuffer;
    nvrhi::BufferHandle m_PrimitiveIDToNodeIDBuffer;
    nvrhi::BufferHandle m_TLASInstanceDescsBuffer;
    nvrhi::rt::AccelStructHandle m_TLAS;

private:
    void UpdateMainViewCameraControls();
    void UpdateInstanceIDsBuffers();
    void UpdateDirectionalLightVector();
    void UpdateAnimations();
    void CreateAccelerationStructures();

    // TODO: move this shit to some sort of camera class
    Vector2 m_CurrentMousePos;
    Vector2 m_MouseLastPos;
    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
};
#define g_Scene g_Graphic.m_Scene
