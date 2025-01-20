#include "Scene.h"

#include "extern/imgui/imgui.h"
#include "extern/SDL/SDL3/SDL_keyboard.h"
#include "extern/SDL/SDL3/SDL_mouse.h"

#include "CommonResources.h"
#include "Engine.h"
#include "Graphic.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Visual.h"

#include "shaders/shared/BasePassStructs.h"
#include "shaders/shared/DeferredLightingStructs.h"
#include "shaders/shared/IndirectArguments.h"

extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_ShadowMapArrayRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;

class ClearBuffersRenderer : public IRenderer
{
public:
    ClearBuffersRenderer() : IRenderer{ "ClearBuffersRenderer" } {}

    bool Setup(RenderGraph& renderGraph) override
    {
        renderGraph.AddWriteDependency(g_GBufferARDGTextureHandle);
        renderGraph.AddWriteDependency(g_LightingOutputRDGTextureHandle);
        renderGraph.AddWriteDependency(g_DepthStencilBufferRDGTextureHandle);

        const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;
        if (shadowControllables.m_bEnabled)
        {
            renderGraph.AddWriteDependency(g_ShadowMapArrayRDGTextureHandle);
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        static bool s_ClearBackBufferEveryFrame = true; // clearing every frame makes things easier to debug
        static bool s_ClearLightingOutputEveryFrame = true; // clearing every frame makes things easier to debug

        Scene* scene = g_Graphic.m_Scene.get();

        if (s_ClearBackBufferEveryFrame)
        {
            PROFILE_GPU_SCOPED(commandList, "Clear Back Buffer");

            commandList->clearTextureFloat(g_Graphic.GetCurrentBackBuffer(), nvrhi::AllSubresources, nvrhi::Color{ 0.4f, 0.4f, 0.4f, 1.0f });
        }

        static bool s_ClearGBuffersEveryFrame = true;
        if (s_ClearGBuffersEveryFrame)
        {
            PROFILE_GPU_SCOPED(commandList, "Clear GBuffers");

            nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);

            commandList->clearTextureUInt(GBufferATexture, nvrhi::AllSubresources, 0);
        }

        if (s_ClearLightingOutputEveryFrame)
        {
            PROFILE_GPU_SCOPED(commandList, "Clear Lighting Output");

            nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);

            commandList->clearTextureFloat(lightingOutputTexture, nvrhi::AllSubresources, nvrhi::Color{ 0.0f });
        }

        // clear depth buffer
        {
            PROFILE_GPU_SCOPED(commandList, "Clear Depth Buffer");

            nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

            const bool kClearStencil = true;
            const uint8_t kClearStencilValue = Graphic::kStencilBit_Sky;
            commandList->clearDepthStencilTexture(depthStencilBuffer, nvrhi::AllSubresources, true, Graphic::kFarDepth, kClearStencil, kClearStencilValue);
        }

        // clear shadow map array
        {
            const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;
            if (shadowControllables.m_bEnabled)
            {
                PROFILE_GPU_SCOPED(commandList, "Clear Shadow Map Array");

                nvrhi::TextureHandle shadowMapArray = renderGraph.GetTexture(g_ShadowMapArrayRDGTextureHandle);

                commandList->clearDepthStencilTexture(shadowMapArray, nvrhi::AllSubresources, true, Graphic::kFarShadowMapDepth, false, 0);
            }
        }
    }
};
static ClearBuffersRenderer gs_ClearBuffersRenderer;
IRenderer* g_ClearBuffersRenderer = &gs_ClearBuffersRenderer;

void View::Initialize()
{
}

void View::Update()
{
    PROFILE_FUNCTION();

    // update prev frame matrices
    m_PrevFrameViewMatrix = m_ViewMatrix;
    m_PrevFrameProjectionMatrix = m_ProjectionMatrix;
    m_PrevFrameViewProjectionMatrix = m_ViewProjectionMatrix;
    m_PrevFrameInvViewMatrix = m_InvViewMatrix;
    m_PrevFrameInvProjectionMatrix = m_InvProjectionMatrix;
    m_PrevFrameInvViewProjectionMatrix = m_InvViewProjectionMatrix;

    m_ViewMatrix = Matrix::CreateLookAt(m_Eye, m_Eye + m_LookAt, Vector3::Up);

    if (m_bIsPerspective)
    {
        m_ProjectionMatrix = Matrix::CreatePerspectiveFieldOfView(m_FOV, m_AspectRatio, m_ZNearP, m_ZFarP);
        ModifyPerspectiveMatrix(m_ProjectionMatrix, m_ZNearP, m_ZFarP, Graphic::kInversedDepthBuffer, Graphic::kInfiniteDepthBuffer);
    }
    else
    {
        m_ProjectionMatrix = Matrix::CreateOrthographic(m_Width, m_Height, m_ZNearP, m_ZFarP);
    }

    m_ViewProjectionMatrix = m_ViewMatrix * m_ProjectionMatrix;
    m_InvViewMatrix = m_ViewMatrix.Invert();
    m_InvProjectionMatrix = m_ProjectionMatrix.Invert();
    m_InvViewProjectionMatrix = m_ViewProjectionMatrix.Invert();

    Frustum::CreateFromMatrix(m_Frustum, m_ProjectionMatrix);
    m_Frustum.Transform(m_Frustum, m_InvViewMatrix);

	const bool bFreezeCullingCamera = g_GraphicPropertyGrid.m_InstanceRenderingControllables.m_bFreezeCullingCamera;
    if (!bFreezeCullingCamera)
    {
        m_CullingPrevFrameViewMatrix = m_PrevFrameViewMatrix;
		m_CullingViewMatrix = m_ViewMatrix;
    }
}

void View::UpdateVectors(float yaw, float pitch)
{
    const float PIBy2 = std::numbers::pi * 0.5f;

    const float r = std::cos(pitch);
    m_LookAt =
    {
        r * std::sin(yaw),
        std::sin(pitch),
        r * std::cos(yaw),
    };

    m_Right =
    {
        std::sin(yaw - PIBy2),
        0,
        std::cos(yaw - PIBy2),
    };

    m_Up = m_Right.Cross(m_LookAt);
}

void Scene::Initialize()
{
    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Scene Initialize CommandList");

    {
        PROFILE_SCOPED("Init Luminance Buffer");

        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(float);
        desc.structStride = sizeof(float);
        desc.debugName = "Exposure Buffer";
        desc.canHaveTypedViews = true;
        desc.canHaveUAVs = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        m_LuminanceBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);

        const float kInitialExposure = 1.0f;
        commandList->writeBuffer(m_LuminanceBuffer, &kInitialExposure, sizeof(float));
    }

    {
        nvrhi::TextureDesc desc;
        desc.width = GetNextPow2(g_Graphic.m_RenderResolution.x) >> 1;
        desc.height = GetNextPow2(g_Graphic.m_RenderResolution.y) >> 1;
        desc.format = Graphic::kHZBFormat;
        desc.isUAV = true;
        desc.debugName = "HZB";
        desc.mipLevels = ComputeNbMips(desc.width, desc.height);
        desc.useClearValue = false;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        m_HZB = g_Graphic.m_NVRHIDevice->createTexture(desc);

        commandList->clearTextureFloat(m_HZB, nvrhi::AllSubresources, nvrhi::Color{ Graphic::kFarDepth });
    }

    m_Views[Main].m_ZNearP = Graphic::kDefaultCameraNearPlane;
    m_Views[Main].m_AspectRatio = (float)g_Graphic.m_RenderResolution.x / g_Graphic.m_RenderResolution.y;
    m_Views[Main].m_Eye = Vector3{ 0.0f, 10.0f, -10.0f };
    m_Views[Main].m_LookAt = Vector3{ 0.0f, 0.0f, 1.0f };
    m_Views[Main].m_Right = Vector3{ 1.0f, 0.0f, 0.0f };
    m_Views[Main].Update();

    for (size_t i = 0; i < Graphic::kNbCSMCascades; i++)
    {
        m_Views[CSM0 + i].m_bIsPerspective = false;
    }

    for (View& view : m_Views)
    {
        view.Initialize();
    }

    m_RenderGraph = std::make_shared<RenderGraph>();
    m_RenderGraph->Initialize();

    CalculateCSMSplitDistances();

    UpdateDirectionalLightVector();
}

void Scene::SetCamera(uint32_t idx)
{
    const Camera& camera = m_Cameras.at(idx);

    View& view = m_Views[EView::Main];

    view.m_Eye = camera.m_Position;

    const Matrix matrix = Matrix::CreateFromQuaternion(camera.m_Orientation);
    const Vector3 forwardVector = matrix.Forward();

    m_Yaw = atan2f(forwardVector.x, forwardVector.z);
    m_Pitch = asinf(forwardVector.y);

    view.UpdateVectors(m_Yaw, m_Pitch);
}

void Scene::UpdateMainViewCameraControls()
{
    // disable camera controls if imgui keyboard input is active... so we don't move the camera when inputing values to imgui
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        return;
    }

    const bool* keyboardStates = SDL_GetKeyboardState(nullptr);

    float mouseX, mouseY;
	const SDL_MouseButtonFlags mouseButtonFlags = SDL_GetMouseState(&mouseX, &mouseY);

    View& mainView = m_Views[EView::Main];

    // right click + mouse wheel changes camera movement speed, like UE
    static float s_CameraMoveSpeed = 0.1f;

    if ((mouseButtonFlags & SDL_BUTTON_RMASK) && (g_Engine.m_MouseWheelY != 0.0f))
    {
        s_CameraMoveSpeed *= (g_Engine.m_MouseWheelY > 0.0f) ? 2.0f : 0.5f;
        s_CameraMoveSpeed = std::max(kKindaSmallNumber, s_CameraMoveSpeed);
        LOG_DEBUG("CameraMoveSpeed is now: %f", s_CameraMoveSpeed);
    }

    m_MouseLastPos = m_CurrentMousePos;
    m_CurrentMousePos = { mouseX, mouseY };


    // for some weird reason, windows underflows to 65535 when cursor is crosses left/top window
    m_CurrentMousePos.x = m_CurrentMousePos.x > 60000.0f ? 0.0f : m_CurrentMousePos.x;
    m_CurrentMousePos.y = m_CurrentMousePos.y > 60000.0f ? 0.0f : m_CurrentMousePos.y;
    m_CurrentMousePos.Clamp(Vector2::Zero, { (float)g_Graphic.m_DisplayResolution.x, (float)g_Graphic.m_DisplayResolution.y });

    // Calculate the move vector in camera space.
    Vector3 finalMoveVector;

    if (keyboardStates[SDL_SCANCODE_A])
    {
        finalMoveVector -= mainView.m_Right;
    }
	if (keyboardStates[SDL_SCANCODE_D])
    {
        finalMoveVector += mainView.m_Right;
    }
	if (keyboardStates[SDL_SCANCODE_W])
    {
        finalMoveVector += mainView.m_LookAt;
    }
	if (keyboardStates[SDL_SCANCODE_S])
    {
        finalMoveVector -= mainView.m_LookAt;
    }

    if (finalMoveVector.LengthSquared() > 0.1f)
    {
        finalMoveVector.Normalize();
        mainView.m_Eye += finalMoveVector * s_CameraMoveSpeed * g_Engine.m_CPUCappedFrameTimeMs;
    }

    if (mouseButtonFlags & SDL_BUTTON_RMASK)
    {
        const Vector2 mouseDeltaVec = m_CurrentMousePos - m_MouseLastPos;

        // compute new camera angles and vectors based off mouse delta
        static float s_MouseRotationSpeed = 0.002f;
        m_Yaw -= s_MouseRotationSpeed * mouseDeltaVec.x;
        m_Pitch -= s_MouseRotationSpeed * mouseDeltaVec.y;

        mainView.UpdateVectors(m_Yaw, m_Pitch);
    }
}

void Scene::UpdateCSMViews()
{
    PROFILE_FUNCTION();

    View& mainView = m_Views[EView::Main];

    for (size_t i = 0; i < Graphic::kNbCSMCascades; i++)
    {
        View& CSMView = m_Views[EView::CSM0 + i];

        const Matrix cascadeProj = Matrix::CreatePerspectiveFieldOfView(mainView.m_FOV, mainView.m_AspectRatio, i == 0 ? 0.1f : m_CSMSplitDistances[i - 1], m_CSMSplitDistances[i]);

        Frustum cascadeFrustum;
        Frustum::CreateFromMatrix(cascadeFrustum, cascadeProj);
        cascadeFrustum.Transform(cascadeFrustum, mainView.m_InvViewMatrix);

        Vector3 frustumCorners[8];
        cascadeFrustum.GetCorners(frustumCorners);

        Vector3 frustumCenter;
        for (const Vector3& v : frustumCorners)
        {
            frustumCenter += v;
        }
        frustumCenter /= (float)std::size(frustumCorners);

        CSMView.m_Eye = frustumCenter + m_DirLightVec;
        CSMView.m_LookAt = -m_DirLightVec;
        CSMView.m_ViewMatrix = Matrix::CreateLookAt(CSMView.m_Eye, CSMView.m_Eye + CSMView.m_LookAt, Vector3::Up);

        float minX = kKindaBigNumber;
        float maxX = -kKindaBigNumber;
        float minY = kKindaBigNumber;
        float maxY = -kKindaBigNumber;
        float minZ = kKindaBigNumber;
        float maxZ = -kKindaBigNumber;
        for (const Vector3& v : frustumCorners)
        {
            // hack
            static const float kExpansionBuffer = 1.1f;

            const Vector3 trf = Vector3::Transform(v, CSMView.m_ViewMatrix);
            minX = std::min(minX, trf.x) * kExpansionBuffer;
            maxX = std::max(maxX, trf.x) * kExpansionBuffer;
            minY = std::min(minY, trf.y) * kExpansionBuffer;
            maxY = std::max(maxY, trf.y) * kExpansionBuffer;
            minZ = std::min(minZ, trf.z) * kExpansionBuffer;
            maxZ = std::max(maxZ, trf.z) * kExpansionBuffer;
        }

        CSMView.m_Width = std::max(1.0f, maxX - minX);
        CSMView.m_Height = std::max(1.0f, maxY - minY);
        CSMView.m_ZNearP = minZ;
        CSMView.m_ZFarP = (maxZ - minZ);

        // TODO: fix shadow edge shimmering & jitter
#if 0
        // This code removes the shimmering effect along the edges of shadows due to the light changing to fit the camera.
        Vec4 normalizeByBufferSize = Vec4((1.0f / width), (1.0f / width), 0.0f, 0.0f);

        // We calculate the offsets as a percentage of the bound.
        Vec4 boarderOffset = lightCameraOrthographicMax - lightCameraOrthographicMin;
        boarderOffset = math::mulPerElem(boarderOffset, g_halfVector);
        lightCameraOrthographicMax += boarderOffset;
        lightCameraOrthographicMin -= boarderOffset;

        // The world units per texel are used to snap  the orthographic projection
        // to texel sized increments.
        // Because we're fitting tighly to the cascades, the shimmering shadow edges will still be present when the
        // camera rotates.  However, when zooming in or strafing the shadow edge will not shimmer.
        Vec4 worldUnitsPerTexel = lightCameraOrthographicMax - lightCameraOrthographicMin;
        worldUnitsPerTexel = math::mulPerElem(worldUnitsPerTexel, normalizeByBufferSize);

        // We snap the camera to 1 pixel increments so that moving the camera does not cause the shadows to jitter.
        // This is a matter of integer dividing by the world space size of a texel
        lightCameraOrthographicMin = math::divPerElem(lightCameraOrthographicMin, worldUnitsPerTexel);
        lightCameraOrthographicMin = Vec4(floorf(lightCameraOrthographicMin.getX()),
            floorf(lightCameraOrthographicMin.getY()),
            floorf(lightCameraOrthographicMin.getZ()),
            floorf(lightCameraOrthographicMin.getW()));
        lightCameraOrthographicMin = math::mulPerElem(lightCameraOrthographicMin, worldUnitsPerTexel);

        lightCameraOrthographicMax = math::divPerElem(lightCameraOrthographicMax, worldUnitsPerTexel);
        lightCameraOrthographicMax = Vec4(floorf(lightCameraOrthographicMax.getX()),
            floorf(lightCameraOrthographicMax.getY()),
            floorf(lightCameraOrthographicMax.getZ()),
            floorf(lightCameraOrthographicMax.getW()));
        lightCameraOrthographicMax = math::mulPerElem(lightCameraOrthographicMax, worldUnitsPerTexel);
#endif

        if constexpr (Graphic::kInversedShadowMapDepthBuffer)
        {
            std::swap(CSMView.m_ZNearP, CSMView.m_ZFarP);
        }

        CSMView.Update();
    }
}

void Scene::UpdateInstanceConstsBuffer()
{
    const uint32_t nbPrimitives = m_Primitives.size();
    if (nbPrimitives == 0)
    {
        return;
    }

    PROFILE_FUNCTION();

    // TODO: upload only dirty primitives

    std::vector<BasePassInstanceConstants> instanceConstsBytes;

    for (uint32_t i = 0; i < nbPrimitives; ++i)
    {
        const Primitive& primitive = m_Primitives.at(i);
        assert(primitive.IsValid());

        const Node& node = m_Nodes.at(primitive.m_NodeID);
        const Material& material = primitive.m_Material;
        const Mesh& mesh = g_Graphic.m_Meshes.at(primitive.m_MeshIdx);

        const Matrix worldMatrix = node.MakeLocalToWorldMatrix();

        Sphere instanceBS;
        mesh.m_BoundingSphere.Transform(instanceBS, worldMatrix);

        // instance consts
        BasePassInstanceConstants instanceConsts{};
        instanceConsts.m_WorldMatrix = worldMatrix;
        instanceConsts.m_MeshDataIdx = mesh.m_MeshDataBufferIdx;
        instanceConsts.m_MaterialDataIdx = material.m_MaterialDataBufferIdx;
        instanceConsts.m_BoundingSphere = Vector4{ instanceBS.Center.x, instanceBS.Center.y, instanceBS.Center.z, instanceBS.Radius };

        instanceConstsBytes.push_back(instanceConsts);
    }

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Upload BasePassInstanceConstants");

    {
        nvrhi::BufferDesc desc;
        desc.byteSize = instanceConstsBytes.size() * sizeof(BasePassInstanceConstants);
        desc.structStride = sizeof(BasePassInstanceConstants);
        desc.debugName = "Instance Consts Buffer";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        m_InstanceConstsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        commandList->writeBuffer(m_InstanceConstsBuffer, instanceConstsBytes.data(), instanceConstsBytes.size() * sizeof(BasePassInstanceConstants));
    }
}

void Scene::UpdateInstanceIDsBuffers()
{
    const uint32_t nbPrimitives = m_Primitives.size();
    if (nbPrimitives == 0)
    {
        return;
    }

    PROFILE_FUNCTION();

    m_OpaquePrimitiveIDs.clear();
	m_AlphaMaskPrimitiveIDs.clear();
	m_TransparentPrimitiveIDs.clear();

    for (uint32_t i = 0; i < nbPrimitives; ++i)
    {
        const Primitive& primitive = m_Primitives[i];
        const Material& material = primitive.m_Material;

        switch (material.m_AlphaMode)
        {
        case AlphaMode::Opaque:
            m_OpaquePrimitiveIDs.push_back(i);
            break;
        case AlphaMode::Mask:
            m_AlphaMaskPrimitiveIDs.push_back(i);
            break;
        case AlphaMode::Blend:
            m_TransparentPrimitiveIDs.push_back(i);
            break;
        default:
            assert(0);
            break;
        }
    }

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Upload Instance IDs");

    m_OpaqueInstanceIDsBuffer = g_CommonResources.DummyUIntStructuredBuffer;
    if (!m_OpaquePrimitiveIDs.empty())
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = m_OpaquePrimitiveIDs.size() * sizeof(uint32_t);
        desc.structStride = sizeof(uint32_t);
        desc.debugName = "Opaque Instance IDs Buffer";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        m_OpaqueInstanceIDsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        commandList->writeBuffer(m_OpaqueInstanceIDsBuffer, m_OpaquePrimitiveIDs.data(), m_OpaquePrimitiveIDs.size() * sizeof(uint32_t));
    }

    m_AlphaMaskInstanceIDsBuffer = g_CommonResources.DummyUIntStructuredBuffer;
    if (!m_AlphaMaskPrimitiveIDs.empty())
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = m_AlphaMaskPrimitiveIDs.size() * sizeof(uint32_t);
        desc.structStride = sizeof(uint32_t);
        desc.debugName = "Alpha Mask Instance IDs Buffer";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        m_AlphaMaskInstanceIDsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        commandList->writeBuffer(m_AlphaMaskInstanceIDsBuffer, m_AlphaMaskPrimitiveIDs.data(), m_AlphaMaskPrimitiveIDs.size() * sizeof(uint32_t));
    }

    m_TransparentInstanceIDsBuffer = g_CommonResources.DummyUIntStructuredBuffer;
    if (!m_TransparentPrimitiveIDs.empty())
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = m_TransparentPrimitiveIDs.size() * sizeof(uint32_t);
        desc.structStride = sizeof(uint32_t);
        desc.debugName = "Transparent Instance IDs Buffer";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        m_TransparentInstanceIDsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        commandList->writeBuffer(m_TransparentInstanceIDsBuffer, m_TransparentPrimitiveIDs.data(), m_TransparentPrimitiveIDs.size() * sizeof(uint32_t));
    }
}

void Scene::UpdateDirectionalLightVector()
{
    const float orientationRadians = ConvertToRadians(m_SunOrientation);
    const float costheta = cosf(orientationRadians);
    const float sintheta = sinf(orientationRadians);
    const float inclinationRadians = ConvertToRadians(m_SunInclination);
    const float cosphi = cosf(inclinationRadians);
    const float sinphi = sinf(inclinationRadians);
    m_DirLightVec = Vector3{ costheta * cosphi, sinphi, sintheta * cosphi };

    assert(m_DirLightVec.LengthSquared() <= (1 + kKindaSmallNumber));
}

void Scene::Update()
{
    PROFILE_FUNCTION();

    UpdateMainViewCameraControls();

    m_Views[EView::Main].Update();

    UpdateCSMViews();

    tf::Taskflow tf;

    m_RenderGraph->InitializeForFrame(tf);
    {
        PROFILE_SCOPED("Schedule Renderers");

        extern IRenderer* g_ClearBuffersRenderer;
        extern IRenderer* g_GBufferRenderer;
        extern IRenderer* g_ShadowMaskRenderer;
        extern IRenderer* g_DeferredLightingRenderer;
        extern IRenderer* g_SunCSMBasePassRenderers[Graphic::kNbCSMCascades];
        extern IRenderer* g_TransparentForwardRenderer;
        extern IRenderer* g_IMGUIRenderer;
        extern IRenderer* g_SkyRenderer;
        extern IRenderer* g_PostProcessRenderer;
        extern IRenderer* g_AdaptLuminanceRenderer;
        extern IRenderer* g_AmbientOcclusionRenderer;
        extern IRenderer* g_BloomRenderer;

        m_RenderGraph->AddRenderer(g_ClearBuffersRenderer);
        m_RenderGraph->AddRenderer(g_GBufferRenderer);
        m_RenderGraph->AddRenderer(g_AmbientOcclusionRenderer);

        for (uint32_t i = 0; i < Graphic::kNbCSMCascades; i++)
        {
            m_RenderGraph->AddRenderer(g_SunCSMBasePassRenderers[i]);
        }

        m_RenderGraph->AddRenderer(g_ShadowMaskRenderer);
        m_RenderGraph->AddRenderer(g_DeferredLightingRenderer);
        m_RenderGraph->AddRenderer(g_SkyRenderer);
        m_RenderGraph->AddRenderer(g_BloomRenderer);
        m_RenderGraph->AddRenderer(g_TransparentForwardRenderer);
        m_RenderGraph->AddRenderer(g_AdaptLuminanceRenderer);
        m_RenderGraph->AddRenderer(g_PostProcessRenderer);

        // DisplayResolution Debug Passes
        m_RenderGraph->AddRenderer(g_IMGUIRenderer);
    }
    m_RenderGraph->Compile();

    g_Engine.m_Executor->corun(tf);
}

void Scene::CalculateCSMSplitDistances()
{
    const GraphicPropertyGrid::ShadowControllables& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;

    float tmpCSMSplitDistances[Graphic::kNbCSMCascades + 2]{};

    for (uint32_t i = 0; i < std::size(tmpCSMSplitDistances); ++i)
    {
        const float f = (float)i / std::size(tmpCSMSplitDistances);
        const float l = Graphic::kDefaultCameraNearPlane * pow(shadowControllables.m_MaxShadowDistance / Graphic::kDefaultCameraNearPlane, f);
        const float u = Graphic::kDefaultCameraNearPlane + (shadowControllables.m_MaxShadowDistance - Graphic::kDefaultCameraNearPlane) * f;
        tmpCSMSplitDistances[i] = l * shadowControllables.m_CSMSplitLambda + u * (1.0f - shadowControllables.m_CSMSplitLambda);
    }

    for (uint32_t i = 0; i < Graphic::kNbCSMCascades; ++i)
    {
        m_CSMSplitDistances[i] = tmpCSMSplitDistances[i + 2];
    }
    m_CSMSplitDistances[Graphic::kNbCSMCascades - 1] = shadowControllables.m_MaxShadowDistance;
}

void Scene::Shutdown()
{
    m_RenderGraph->Shutdown();
}

void Scene::UpdateIMGUIPropertyGrid()
{
    if (ImGui::TreeNode("Cameras"))
    {
        std::string cameraComboStr;
        for (const Scene::Camera& camera : m_Cameras)
        {
            cameraComboStr += camera.m_Name + '\0';
        }
        cameraComboStr += '\0';

        static int cameraIdx = 0;
        if (ImGui::Combo("##SceneCameraCombo", &cameraIdx, cameraComboStr.c_str()))
        {
            SetCamera(cameraIdx);
        }

        if (!m_Cameras.empty() && ImGui::Button("Reset"))
        {
            SetCamera(0);
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Lighting"))
    {
        bool bUpdateDirection = false;
        bUpdateDirection |= ImGui::SliderFloat("Sun Orientation", &m_SunOrientation, 0.0f, 360.0f);
        bUpdateDirection |= ImGui::SliderFloat("Sun Inclination", &m_SunInclination, 0.0f, 89.0f);
        if (bUpdateDirection)
        {
            UpdateDirectionalLightVector();
        }

        ImGui::InputFloat3("Directional Light Color", (float*)&m_DirLightColor, "%.1f");
        ImGui::DragFloat("Directional Light Strength", &m_DirLightStrength, 0.1f, 0.0f);

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Culling Stats"))
    {
		const auto& InstanceRenderingControllables = g_GraphicPropertyGrid.m_InstanceRenderingControllables;

        // TODO: support transparent
		const View& mainView = m_Views[EView::Main];

		ImGui::Text("Early:");

		ImGui::Indent();

		ImGui::Text("Instances:[%d]", mainView.m_GPUCullingCounters.m_EarlyInstances);
		ImGui::Text("Meshlets: [%d]", mainView.m_GPUCullingCounters.m_EarlyMeshlets);

		ImGui::Unindent();

        ImGui::Text("Late:");

        ImGui::Indent();

        ImGui::Text("Instances: [%d]", mainView.m_GPUCullingCounters.m_LateInstances);
        ImGui::Text("Meshlets: [%d]", mainView.m_GPUCullingCounters.m_LateMeshlets);

        ImGui::Unindent();

        ImGui::TreePop();
    }
}

void Scene::OnSceneLoad()
{
    PROFILE_FUNCTION();
    SCOPED_TIMER_FUNCTION();

    View& mainView = m_Views[EView::Main];

    // empirically resize CSM distances based on scene BS radius
    g_GraphicPropertyGrid.m_ShadowControllables.m_MaxShadowDistance = std::min(300.0f, std::max(1.0f, m_BoundingSphere.Radius * 2));
    CalculateCSMSplitDistances();

    // empirically set camera near plane based on scene BS radius
    mainView.m_ZNearP = std::max(0.1f, std::min(m_BoundingSphere.Radius * 0.01f, 0.1f));

    LOG_DEBUG("Scene AABB: c:[%f, %f, %f] e:[%f, %f, %f]", m_AABB.Center.x, m_AABB.Center.y, m_AABB.Center.z, m_AABB.Extents.x, m_AABB.Extents.y, m_AABB.Extents.z);
    LOG_DEBUG("Scene Bounding Sphere: [%f, %f, %f][r: %f]", m_BoundingSphere.Center.x, m_BoundingSphere.Center.y, m_BoundingSphere.Center.z, m_BoundingSphere.Radius);
    LOG_DEBUG("CSM Split Distances: [%f, %f, %f, %f]", m_CSMSplitDistances[0], m_CSMSplitDistances[1], m_CSMSplitDistances[2], m_CSMSplitDistances[3]);
    LOG_DEBUG("Camera Near Plane: %f", mainView.m_ZNearP);

    // set to first camera if any
    if (!m_Cameras.empty())
    {
        SetCamera(0);
    }

    UpdateInstanceConstsBuffer();
	UpdateInstanceIDsBuffers();
}
