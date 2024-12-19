#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

#include "MathUtilities.h"
#include "Utilities.h"
#include "Visual.h"

class Primitive;
class RenderGraph;

struct GPUCullingCounters
{
    uint32_t m_Early;
    uint32_t m_Late;
};

class View
{
public:
    void Initialize();
    void Update();
    void UpdateVectors(float yaw, float pitch);

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

    enum EView { Main, CSM0, CSM1, CSM2, CSM3 };

    void Initialize();
    void Update();
    void Shutdown();
    void UpdateIMGUIPropertyGrid();
    void OnSceneLoad();
    void CalculateCSMSplitDistances();
    void SetCamera(uint32_t idx);

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
    std::vector<Primitive> m_Primitives;
    std::vector<uint32_t> m_OpaquePrimitiveIDs;
    std::vector<uint32_t> m_AlphaMaskPrimitiveIDs;
    std::vector<uint32_t> m_TransparentPrimitiveIDs;

    nvrhi::BufferHandle m_LuminanceBuffer;

    nvrhi::BufferHandle m_InstanceConstsBuffer;
    nvrhi::BufferHandle m_OpaqueInstanceIDsBuffer;
    nvrhi::BufferHandle m_AlphaMaskInstanceIDsBuffer;
    nvrhi::BufferHandle m_TransparentInstanceIDsBuffer;

    nvrhi::TextureHandle m_HZB;

    std::vector<Camera> m_Cameras;

private:
    void UpdateMainViewCameraControls();
    void UpdateCSMViews();
    void UpdateInstanceConstsBuffer();
    void UpdateInstanceIDsBuffers();

    // TODO: move this shit to some sort of camera class
    Vector2 m_CurrentMousePos;
    Vector2 m_MouseLastPos;
    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
};
