#include "Scene.h"

#include "extern/debug_draw/debug_draw.hpp"
#include "extern/imgui/imgui.h"
#include "extern/portable-file-dialogs/portable-file-dialogs.h"

#include "Engine.h"
#include "Graphic.h"
#include "GraphicPropertyGrid.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "RenderGraph.h"
#include "Visual.h"

#include "shaders/shared/BasePassStructs.h"
#include "shaders/shared/DeferredLightingStructs.h"
#include "shaders/shared/IndirectArguments.h"

extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferBRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferCRDGTextureHandle;
extern RenderGraph::ResourceHandle g_ShadowMapArrayRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;

class ClearBuffersRenderer : public IRenderer
{
public:
    ClearBuffersRenderer() : IRenderer{ "ClearBuffersRenderer" } {}

    bool Setup(RenderGraph& renderGraph) override
    {
        renderGraph.AddWriteDependency(g_ShadowMapArrayRDGTextureHandle);
        renderGraph.AddWriteDependency(g_GBufferARDGTextureHandle);
        renderGraph.AddWriteDependency(g_GBufferBRDGTextureHandle);
        renderGraph.AddWriteDependency(g_GBufferCRDGTextureHandle);
        renderGraph.AddWriteDependency(g_LightingOutputRDGTextureHandle);
        renderGraph.AddWriteDependency(g_DepthStencilBufferRDGTextureHandle);

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
            nvrhi::TextureHandle GBufferBTexture = renderGraph.GetTexture(g_GBufferBRDGTextureHandle);
            nvrhi::TextureHandle GBufferCTexture = renderGraph.GetTexture(g_GBufferCRDGTextureHandle);

            commandList->clearTextureFloat(GBufferATexture, nvrhi::AllSubresources, nvrhi::Color{ 0.0f });
            commandList->clearTextureFloat(GBufferBTexture, nvrhi::AllSubresources, nvrhi::Color{ 0.0f });
            commandList->clearTextureFloat(GBufferCTexture, nvrhi::AllSubresources, nvrhi::Color{ 0.0f });
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
            PROFILE_GPU_SCOPED(commandList, "Clear Shadow Map Array");

            nvrhi::TextureHandle shadowMapArray = renderGraph.GetTexture(g_ShadowMapArrayRDGTextureHandle); 

            commandList->clearDepthStencilTexture(shadowMapArray, nvrhi::AllSubresources, true, Graphic::kFarShadowMapDepth, false, 0);
        }
    }
};
static ClearBuffersRenderer gs_ClearBuffersRenderer;
IRenderer* g_ClearBuffersRenderer = &gs_ClearBuffersRenderer;

void View::Initialize()
{
    PROFILE_FUNCTION();

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Init View GPUCulling Indirect Buffers");

    m_FrustumVisibleVisualProxiesIndicesBuffer.m_BufferDesc.structStride = sizeof(uint32_t);
    m_FrustumVisibleVisualProxiesIndicesBuffer.m_BufferDesc.debugName = "Visible Instance Consts Indices";
    m_FrustumVisibleVisualProxiesIndicesBuffer.m_BufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;

    m_InstanceCountBuffer.m_BufferDesc.structStride = sizeof(uint32_t);
    m_InstanceCountBuffer.m_BufferDesc.debugName = "InstanceIndexCounter";
    m_InstanceCountBuffer.m_BufferDesc.canHaveUAVs = true;
    m_InstanceCountBuffer.m_BufferDesc.isDrawIndirectArgs = true;
    m_InstanceCountBuffer.m_BufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    m_InstanceCountBuffer.ClearBuffer(commandList, sizeof(uint32_t));

    m_DrawIndexedIndirectArgumentsBuffer.m_BufferDesc.structStride = sizeof(nvrhi::DrawIndexedIndirectArguments);
    m_DrawIndexedIndirectArgumentsBuffer.m_BufferDesc.debugName = "DrawIndexedIndirectArguments Buffer";
    m_DrawIndexedIndirectArgumentsBuffer.m_BufferDesc.canHaveUAVs = true;
    m_DrawIndexedIndirectArgumentsBuffer.m_BufferDesc.isDrawIndirectArgs = true;
    m_DrawIndexedIndirectArgumentsBuffer.m_BufferDesc.initialState = nvrhi::ResourceStates::IndirectArgument;
    m_DrawIndexedIndirectArgumentsBuffer.ClearBuffer(commandList, sizeof(nvrhi::DrawIndexedIndirectArguments));

    m_StartInstanceConstsOffsetsBuffer.m_BufferDesc.structStride = sizeof(uint32_t);
    m_StartInstanceConstsOffsetsBuffer.m_BufferDesc.debugName = "StartInstanceConstsOffsets Buffer";
    m_StartInstanceConstsOffsetsBuffer.m_BufferDesc.canHaveUAVs = true;
    m_StartInstanceConstsOffsetsBuffer.m_BufferDesc.isVertexBuffer = true;
    m_StartInstanceConstsOffsetsBuffer.m_BufferDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
    m_StartInstanceConstsOffsetsBuffer.ClearBuffer(commandList, sizeof(uint32_t));
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
}

void View::GatherVisibleVisualProxies()
{
    PROFILE_FUNCTION();

    Scene* scene = g_Graphic.m_Scene.get();

    // clear visual proxies
    m_FrustumVisibleVisualProxiesIndices.clear();

    std::vector<OctTree::Node*> visibleNodes;

    // GPU will fine-grain cull in CS, so do only coarse octtree culling
    const bool bFineGrainCulling = false;

    if (m_bIsPerspective)
    {
        scene->m_OctTree.GetObjectsInBound(m_Frustum, visibleNodes, bFineGrainCulling);
    }
    else
    {
        // construct OBB from frustum points to perform OBB-AABB culling, because 'Frustum::Contains' doesn't work with orthographic projection
        Vector3 cascadeCorners[Frustum::CORNER_COUNT]{};
        m_Frustum.GetCorners((DirectX::XMFLOAT3*)&cascadeCorners);

        OBB cascadeOBB;
        OBB::CreateFromPoints(cascadeOBB, std::size(cascadeCorners), (DirectX::XMFLOAT3*)cascadeCorners, sizeof(Vector3));

        scene->m_OctTree.GetObjectsInBound(cascadeOBB, visibleNodes, bFineGrainCulling);
    }

    // get visible proxy indices
    for (OctTree::Node* node : visibleNodes)
    {
        m_FrustumVisibleVisualProxiesIndices.push_back((uint32_t)node->m_Data);
    }
}

void View::UpdateVisibleVisualProxiesBuffer(nvrhi::CommandListHandle commandList)
{
    PROFILE_FUNCTION();

    if (m_FrustumVisibleVisualProxiesIndices.empty())
    {
        return;
    }

    m_FrustumVisibleVisualProxiesIndicesBuffer.Write(commandList, m_FrustumVisibleVisualProxiesIndices.data(), m_FrustumVisibleVisualProxiesIndices.size() * sizeof(uint32_t));
}

void Scene::Initialize()
{
    tf::Taskflow tf;

    tf.emplace([this]
        {
            PROFILE_SCOPED("Init HZB");

            // NOTE: HZB needs to be a nice square with pow2 dims
            nvrhi::TextureDesc desc;
            desc.width = GetNextPow2(g_Graphic.m_RenderResolution.x) >> 1;
            desc.height = GetNextPow2(g_Graphic.m_RenderResolution.y) >> 1;
            desc.format = Graphic::kHZBFormat;
            desc.isRenderTarget = false;
            desc.isUAV = true;
            desc.debugName = "HZB";
            desc.mipLevels = ComputeNbMips(desc.width, desc.height);
            desc.useClearValue = false;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            m_HZB = g_Graphic.m_NVRHIDevice->createTexture(desc);

            nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
            SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Clear Hi-Z");

            commandList->clearTextureFloat(m_HZB, nvrhi::AllSubresources, Graphic::kFarDepth);
        });

    tf.emplace([this]
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

            nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
            SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Init Exposure Buffer");

            const float kInitialExposure = 1.0f;
            commandList->writeBuffer(m_LuminanceBuffer, &kInitialExposure, sizeof(float));
        });

    tf.emplace([this]
        {
            PROFILE_SCOPED("Init Views");

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
            m_Views[Main].m_bIsMainView = true;

            m_OcclusionCullingPhaseTwoInstanceCountBuffer.m_BufferDesc = m_Views[Main].m_InstanceCountBuffer.m_BufferDesc;
            m_OcclusionCullingPhaseTwoInstanceCountBuffer.m_BufferDesc.debugName = "PhaseTwoInstanceCountBuffer";
            m_OcclusionCullingPhaseTwoStartInstanceConstsOffsetsBuffer.m_BufferDesc = m_Views[Main].m_StartInstanceConstsOffsetsBuffer.m_BufferDesc;
            m_OcclusionCullingPhaseTwoStartInstanceConstsOffsetsBuffer.m_BufferDesc.debugName = "PhaseTwoStartInstanceConstsOffsetsBuffer";
            m_OcclusionCullingPhaseTwoDrawIndexedIndirectArgumentsBuffer.m_BufferDesc = m_Views[Main].m_DrawIndexedIndirectArgumentsBuffer.m_BufferDesc;
            m_OcclusionCullingPhaseTwoDrawIndexedIndirectArgumentsBuffer.m_BufferDesc.debugName = "PhaseTwoDrawIndexedIndirectArgumentsBuffer";
        });

    tf.emplace([this]
        {
            PROFILE_SCOPED("Init InstanceConstsBuffer stuff");

            m_InstanceConstsBuffer.m_BufferDesc.structStride = sizeof(BasePassInstanceConstants);
            m_InstanceConstsBuffer.m_BufferDesc.debugName = "Instance Consts Buffer";
            m_InstanceConstsBuffer.m_BufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        });

    tf.emplace([this]
        {
            PROFILE_SCOPED("Init DeferredLightingTileRenderer");

            m_DeferredLightingTileRenderingHelper.Initialize(g_Graphic.m_RenderResolution, Tile_ID_Count);
        });

    g_Engine.m_Executor->corun(tf);

    m_RenderGraph = std::make_shared<RenderGraph>();

    CalculateCSMSplitDistances();
}

void Scene::PostRender()
{
    PROFILE_FUNCTION();

    m_RenderGraph->PostRender();
}

void Scene::UpdateMainViewCameraControls()
{
    // disable camera controls if imgui keyboard input is active... so we don't move the camera when inputing values to imgui
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        return;
    }

    View& mainView = m_Views[EView::Main];

    // right click + mouse wheel changes camera movement speed, like UE
    static float s_CameraMoveSpeed = 0.1f;
    if (Mouse::IsButtonPressed(Mouse::Right) && (Mouse::GetWheel() != 0.0f))
    {
        s_CameraMoveSpeed *= (Mouse::GetWheel() > 0.0f) ? 2.0f : 0.5f;
        s_CameraMoveSpeed = std::max(KINDA_SMALL_NUMBER, s_CameraMoveSpeed);
        LOG_DEBUG("CameraMoveSpeed is now: %f", s_CameraMoveSpeed);
    }

    m_MouseLastPos = m_CurrentMousePos;
    m_CurrentMousePos = { Mouse::GetX(), Mouse::GetY() };

    // for some weird reason, windows underflows to 65535 when cursor is crosses left/top window
    m_CurrentMousePos.x = m_CurrentMousePos.x > 60000.0f ? 0.0f : m_CurrentMousePos.x;
    m_CurrentMousePos.y = m_CurrentMousePos.y > 60000.0f ? 0.0f : m_CurrentMousePos.y;
    m_CurrentMousePos.Clamp(Vector2::Zero, { (float)g_Graphic.m_DisplayResolution.x, (float)g_Graphic.m_DisplayResolution.y });

    // Calculate the move vector in camera space.
    Vector3 finalMoveVector;

    if (Keyboard::IsKeyPressed(Keyboard::KEY_A))
    {
        finalMoveVector -= mainView.m_Right;
    }
    if (Keyboard::IsKeyPressed(Keyboard::KEY_D))
    {
        finalMoveVector += mainView.m_Right;
    }
    if (Keyboard::IsKeyPressed(Keyboard::KEY_W))
    {
        finalMoveVector += mainView.m_LookAt;
    }
    if (Keyboard::IsKeyPressed(Keyboard::KEY_S))
    {
        finalMoveVector -= mainView.m_LookAt;
    }

    if (finalMoveVector.LengthSquared() > 0.1f)
    {
        finalMoveVector.Normalize();
        mainView.m_Eye += finalMoveVector * s_CameraMoveSpeed * g_Engine.m_CPUCappedFrameTimeMs;
    }

    if (Mouse::IsButtonPressed(Mouse::Right))
    {
        const Vector2 mouseDeltaVec = m_CurrentMousePos - m_MouseLastPos;

        // compute new camera angles and vectors based off mouse delta
        static float s_MouseRotationSpeed = 0.002f;
        m_Yaw -= s_MouseRotationSpeed * mouseDeltaVec.x;
        m_Pitch -= s_MouseRotationSpeed * mouseDeltaVec.y;

        const float PIBy2 = std::numbers::pi * 0.5f;

        // Prevent looking too far up or down.
        m_Pitch = std::clamp(m_Pitch, -PIBy2, PIBy2);

        const float r = std::cos(m_Pitch);
        mainView.m_LookAt =
        {
            r * std::sin(m_Yaw),
            std::sin(m_Pitch),
            r * std::cos(m_Yaw),
        };

        mainView.m_Right =
        {
            std::sin(m_Yaw - PIBy2),
            0,
            std::cos(m_Yaw - PIBy2),
        };

        mainView.m_Up = mainView.m_Right.Cross(mainView.m_LookAt);
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

        Vector3 frustumCorners[8];
        GetFrustumCornersWorldSpace(mainView.m_ViewMatrix * cascadeProj, frustumCorners);

        Vector3 frustumCenter;
        for (const Vector3& v : frustumCorners)
        {
            frustumCenter += v;
        }
        frustumCenter /= (float)std::size(frustumCorners);

        CSMView.m_Eye = frustumCenter + m_DirLightVec;
        CSMView.m_LookAt = -m_DirLightVec;
        CSMView.m_ViewMatrix = Matrix::CreateLookAt(CSMView.m_Eye, CSMView.m_Eye + CSMView.m_LookAt, Vector3::Up);

        float minX = KINDA_BIG_NUMBER;
        float maxX = -KINDA_BIG_NUMBER;
        float minY = KINDA_BIG_NUMBER;
        float maxY = -KINDA_BIG_NUMBER;
        float minZ = KINDA_BIG_NUMBER;
        float maxZ = -KINDA_BIG_NUMBER;
        for (const Vector3& v : frustumCorners)
        {
            const Vector3 trf = Vector3::Transform(v, CSMView.m_ViewMatrix);
            minX = std::min(minX, trf.x);
            maxX = std::max(maxX, trf.x);
            minY = std::min(minY, trf.y);
            maxY = std::max(maxY, trf.y);
            minZ = std::min(minZ, trf.z);
            maxZ = std::max(maxZ, trf.z);
        }

        CSMView.m_Width = std::max(1.0f, maxX - minX);
        CSMView.m_Height = std::max(1.0f, maxY - minY);
        CSMView.m_ZNearP = minZ;
        CSMView.m_ZFarP = (maxZ - minZ);

        CSMView.Update();

        if constexpr (Graphic::kInversedShadowMapDepthBuffer)
        {
            CSMView.m_ProjectionMatrix._33 *= -1.0f;
            CSMView.m_ProjectionMatrix._34 *= -1.0f;
        }
    }
}

void Scene::RenderOctTreeDebug()
{
    if (!g_GraphicPropertyGrid.m_DebugControllables.m_bRenderSceneOctTree)
    {
        return;
    }

	PROFILE_FUNCTION();

    auto DebugDrawOctTree = [](const OctTree& OctTree)
        {
            View& mainView = g_Graphic.m_Scene->m_Views[EView::Main];

            const AABB& aabb = OctTree.m_AABB;
            const ddVec3 center{ aabb.Center.x, aabb.Center.y, aabb.Center.z };

            const bool bDepthEnabled = false;
            dd::box(center, dd::colors::White, aabb.Extents.x * 2, aabb.Extents.y * 2, aabb.Extents.z * 2, 0, bDepthEnabled);

            const Vector2 strViewportPos = ProjectWorldPositionToViewport(aabb.Center, mainView.m_ViewProjectionMatrix, g_Graphic.m_DisplayResolution);
            const ddVec3 textScreenCenter{ strViewportPos.x, strViewportPos.y, 0.0f };

            // Level : Nb Primitives
            dd::screenText(StringFormat("[%d]:[%d]", OctTree.m_Level, OctTree.m_Objects.size()), textScreenCenter, dd::colors::White);
        };

    m_OctTree.ForEachOctTree(DebugDrawOctTree);
}

void Scene::UpdatePicking()
{
    PROFILE_FUNCTION();

    Graphic::PickingContext& context = g_Graphic.m_PickingContext;

    if (context.m_State == Graphic::PickingContext::RESULT_READY)
    {
        if (context.m_Result != UINT_MAX)
        {
            Scene* scene = g_Graphic.m_Scene.get();

            const uint32_t pickedNodeID = context.m_Result;

            extern uint32_t g_CurrentlySelectedNodeID;
            g_CurrentlySelectedNodeID = pickedNodeID;

            assert(g_CurrentlySelectedNodeID < scene->m_Nodes.size());
        }

        context.m_State = Graphic::PickingContext::NONE;
    }

    if (context.m_State == Graphic::PickingContext::NONE && Mouse::WasButtonReleased(Mouse::Left) && !ImGui::GetIO().WantCaptureMouse)
    {
		// request picking next frame for thread safety
        g_Engine.AddCommand([&]
            {
                // TODO: properly scale mouse pos when we have upscaling
                const Vector2U clickPos{ std::min(g_Graphic.m_RenderResolution.x - 1, (uint32_t)Mouse::GetX()), std::min(g_Graphic.m_RenderResolution.y - 1, (uint32_t)Mouse::GetY()) };
                context.m_PickingLocation = clickPos;
                context.m_State = Graphic::PickingContext::REQUESTED;

                //LOG_DEBUG("Requested Picking: [%d, %d]", clickPos.x, clickPos.y);
            });
    }
}

void Scene::CullAndPrepareInstanceDataForViews()
{
    PROFILE_FUNCTION();

    // MT gather visible proxies
    for (View& view : m_Views)
    {
        view.GatherVisibleVisualProxies();
    }

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "CullAndPrepareInstanceDataForViews");

    {
        PROFILE_GPU_SCOPED(commandList, "Update Visual Proxies Buffers");

        for (View& view : m_Views)
        {
            view.UpdateVisibleVisualProxiesBuffer(commandList);

            // NOTE: it's VERY important that the state of these 2 gpu culling indirect buffers are persistent throughout the frame
            //       all GPUCulling & BasePass renderers will access their Views' buffers multi-threaded
            const uint32_t nbInstances = (uint32_t)view.m_FrustumVisibleVisualProxiesIndices.size();
            if (nbInstances > 0)
            {
                view.m_InstanceCountBuffer.GrowBufferIfNeeded(nbInstances * sizeof(uint32_t));
                view.m_StartInstanceConstsOffsetsBuffer.GrowBufferIfNeeded(nbInstances * sizeof(uint32_t));
                view.m_DrawIndexedIndirectArgumentsBuffer.GrowBufferIfNeeded(nbInstances * sizeof(DrawIndexedIndirectArguments));

                if (view.m_bIsMainView)
                {
                    m_OcclusionCullingPhaseTwoInstanceCountBuffer.GrowBufferIfNeeded(nbInstances * sizeof(uint32_t));
                    m_OcclusionCullingPhaseTwoStartInstanceConstsOffsetsBuffer.GrowBufferIfNeeded(nbInstances * sizeof(uint32_t));
                    m_OcclusionCullingPhaseTwoDrawIndexedIndirectArgumentsBuffer.GrowBufferIfNeeded(nbInstances * sizeof(DrawIndexedIndirectArguments));
                }
            }
        }
    }

    UpdateInstanceConstsBuffer(commandList);
}

void Scene::Update()
{
    PROFILE_FUNCTION();

    UpdatePicking();

    UpdateMainViewCameraControls();

    m_Views[EView::Main].Update();

    UpdateCSMViews();

    RenderOctTreeDebug();

    tf::Taskflow tf;

    tf::Task prepareInstancesDataTask = tf.emplace([this] { CullAndPrepareInstanceDataForViews(); });
    
    extern IRenderer* g_ClearBuffersRenderer;
    extern IRenderer* g_GPUCullingRenderer[EnumUtils::Count<Scene::EView>()];
    extern IRenderer* g_OpaqueBasePassRenderer;
    extern IRenderer* g_ShadowMaskRenderer;
    extern IRenderer* g_TileClassificationRenderer;
    extern IRenderer* g_DeferredLightingRenderer;
    extern IRenderer* g_TileClassificationDebugRenderer;
    extern IRenderer* g_SunCSMBasePassRenderers[Graphic::kNbCSMCascades];
    extern IRenderer* g_TransparentBasePassRenderer;
    extern IRenderer* g_DebugDrawRenderer;
    extern IRenderer* g_IMGUIRenderer;
    extern IRenderer* g_PickingRenderer;
    extern IRenderer* g_SkyRenderer;
    extern IRenderer* g_PostProcessRenderer;
    extern IRenderer* g_AdaptLuminanceRenderer;
    extern IRenderer* g_AmbientOcclusionRenderer;
    extern IRenderer* g_BloomRenderer;

    m_RenderGraph->InitializeForFrame(tf);

    m_RenderGraph->AddRenderer(g_ClearBuffersRenderer);

    m_RenderGraph->AddRenderer(g_GPUCullingRenderer[Scene::EView::Main], &prepareInstancesDataTask);
    for (size_t i = Scene::EView::CSM0; i < EnumUtils::Count<Scene::EView>(); i++)
    {
        m_RenderGraph->AddRenderer(g_GPUCullingRenderer[i], &prepareInstancesDataTask);
    }

    m_RenderGraph->AddRenderer(g_OpaqueBasePassRenderer, &prepareInstancesDataTask);
    m_RenderGraph->AddRenderer(g_AmbientOcclusionRenderer);

    for (uint32_t i = 0; i < Graphic::kNbCSMCascades; i++)
    {
        m_RenderGraph->AddRenderer(g_SunCSMBasePassRenderers[i], &prepareInstancesDataTask);
    }

    m_RenderGraph->AddRenderer(g_ShadowMaskRenderer);
    m_RenderGraph->AddRenderer(g_TileClassificationRenderer);
    m_RenderGraph->AddRenderer(g_DeferredLightingRenderer);
    m_RenderGraph->AddRenderer(g_SkyRenderer);
    m_RenderGraph->AddRenderer(g_BloomRenderer);
    // m_RenderGraph->AddRenderer(g_TransparentBasePassRenderer).succeed(prepareInstancesDataTask); // TODO: support transparent
    m_RenderGraph->AddRenderer(g_AdaptLuminanceRenderer);

    // TODO: this is supposed to be after PostProcessRenderer, but it currently writes to the BackBuffer as we don't have any uspcaling Renderer yet
    // RenderResolution Debug passes
    m_RenderGraph->AddRenderer(g_TileClassificationDebugRenderer);

    m_RenderGraph->AddRenderer(g_PostProcessRenderer);

    // DisplayResolution Debug Passes
    m_RenderGraph->AddRenderer(g_PickingRenderer, &prepareInstancesDataTask);
    m_RenderGraph->AddRenderer(g_DebugDrawRenderer);
    m_RenderGraph->AddRenderer(g_IMGUIRenderer);

    m_RenderGraph->Compile();

    g_Engine.m_Executor->corun(tf);
}

uint32_t Scene::InsertPrimitive(Primitive* p, const Matrix& worldMatrix)
{
    assert(p->m_SceneOctTreeNodeIdx == UINT_MAX);

    const uint32_t proxyIdx = (uint32_t)m_VisualProxies.size();
    VisualProxy& newProxy = m_VisualProxies.emplace_back();

    newProxy.m_NodeID = m_Visuals.at(p->m_VisualIdx).m_NodeID;
    newProxy.m_Primitive = p;
    newProxy.m_WorldMatrix = worldMatrix;
    newProxy.m_PrevFrameWorldMatrix = worldMatrix;

    const uint32_t nodeIdx = m_OctTreeNodes.size();
    OctTree::Node* newOctTreeNode = m_OctTreeNodeAllocator.NewObject();
	m_OctTreeNodes.push_back(newOctTreeNode);
    p->m_SceneOctTreeNodeIdx = nodeIdx;

    newOctTreeNode->m_Data = (void*)proxyIdx;
    newOctTreeNode->m_AABB = MakeLocalToWorldAABB(g_Graphic.m_Meshes.at(p->m_MeshIdx).m_AABB, worldMatrix);

    m_OctTree.Insert(newOctTreeNode);

    return proxyIdx;
}

void Scene::UpdatePrimitive(Primitive* p, const Matrix& worldMatrix, uint32_t proxyIdx)
{
    VisualProxy& visualProxy = m_VisualProxies.at(proxyIdx);
    visualProxy.m_WorldMatrix = worldMatrix;

	OctTree::Node* octTreeNode = m_OctTreeNodes.at(p->m_SceneOctTreeNodeIdx);

    octTreeNode->m_AABB = MakeLocalToWorldAABB(g_Graphic.m_Meshes.at(p->m_MeshIdx).m_AABB, worldMatrix);

    m_OctTree.Update(octTreeNode);
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

void Scene::UpdateInstanceConstsBuffer(nvrhi::CommandListHandle commandList)
{
    // TODO: upload only dirty proxies

    if (m_VisualProxies.empty())
    {
        return;
    }

    PROFILE_FUNCTION();
    PROFILE_GPU_SCOPED(commandList, "UpdateInstanceConstsBuffer");

    std::vector<BasePassInstanceConstants> instanceConstsBytes;

    {
        PROFILE_SCOPED("Gather Instance Consts Bytes");

        for (VisualProxy& visualProxy : m_VisualProxies)
        {
            // TODO: perhaps this is not the place to do it?
            visualProxy.m_PrevFrameWorldMatrix = visualProxy.m_WorldMatrix;

            const Primitive* primitive = visualProxy.m_Primitive;
            assert(primitive->IsValid());

            const Material& material = primitive->m_Material;
            assert(material.IsValid());

            const Mesh& mesh = g_Graphic.m_Meshes.at(primitive->m_MeshIdx);

            Matrix inverseTransposeMatrix = visualProxy.m_WorldMatrix;
            inverseTransposeMatrix.Translation(Vector3::Zero);
            inverseTransposeMatrix = inverseTransposeMatrix.Invert().Transpose();

            // instance consts
            BasePassInstanceConstants instanceConsts{};
            instanceConsts.m_NodeID = visualProxy.m_NodeID;
            instanceConsts.m_WorldMatrix = visualProxy.m_WorldMatrix;
            instanceConsts.m_PrevFrameWorldMatrix = visualProxy.m_PrevFrameWorldMatrix;
            instanceConsts.m_InverseTransposeWorldMatrix = inverseTransposeMatrix;
            instanceConsts.m_MeshDataIdx = mesh.m_MeshDataBufferIdx;
            instanceConsts.m_MaterialDataIdx = material.m_MaterialDataBufferIdx;

            instanceConstsBytes.push_back(instanceConsts);
        }
    }

    m_InstanceConstsBuffer.Write(commandList, instanceConstsBytes.data(), instanceConstsBytes.size() * sizeof(BasePassInstanceConstants));
}

void Scene::Shutdown()
{
}

void Scene::UpdateIMGUIPropertyGrid()
{
    if (ImGui::TreeNode("Lighting"))
    {
        bool bUpdateDirection = false;
        bUpdateDirection |= ImGui::SliderFloat("Sun Orientation", &m_SunOrientation, 0.0f, 360.0f);
        bUpdateDirection |= ImGui::SliderFloat("Sun Inclination", &m_SunInclination, 0.0f, 89.0f);
        if (bUpdateDirection)
        {
            const float orientationRadians = ConvertToRadians(m_SunOrientation);
            const float costheta = cosf(orientationRadians);
            const float sintheta = sinf(orientationRadians);
            const float inclinationRadians = ConvertToRadians(m_SunInclination);
            const float cosphi = cosf(inclinationRadians);
            const float sinphi = sinf(inclinationRadians);
            m_DirLightVec = Vector3{ costheta * cosphi, sinphi, sintheta * cosphi };

            assert(m_DirLightVec.LengthSquared() <= (1 + KINDA_SMALL_NUMBER));
        }

        ImGui::InputFloat3("Directional Light Color", (float*)&m_DirLightColor, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::DragFloat("Directional Light Strength", &m_DirLightStrength, 0.1f, 0.0f);

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Culling Stats"))
    {
        // TODO: support transparent
        for (size_t i = 0; i < EnumUtils::Count<EView>(); i++)
        {
            const View& view = m_Views[i];

            const uint32_t nbCPUVisible = (uint32_t)m_Views[i].m_FrustumVisibleVisualProxiesIndices.size();

            ImGui::Text("[%s]:", EnumUtils::ToString((EView)i));

            ImGui::Indent();
            ImGui::Text("CPU Visible:[%d]", nbCPUVisible);
            if (nbCPUVisible)
            {
                ImGui::Text("GPU Visible: Frustum:[%d]", view.m_GPUCullingCounters.m_Frustum);

                if (view.m_bIsMainView)
                {
                    ImGui::SameLine();
                    ImGui::Text("Occlusion: Phase 1:[%d]", view.m_GPUCullingCounters.m_OcclusionPhase1);
                    ImGui::SameLine();
                    ImGui::Text("Phase 2:[%d]", view.m_GPUCullingCounters.m_OcclusionPhase2);
                }
            }
            ImGui::Unindent();
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Oct Tree Stats"))
    {
        auto OctTreeStats = [](const OctTree& OctTree)
            {
                std::string levelIndentation;
                levelIndentation.reserve(OctTree.m_Level * 2);
                for (size_t i = 0; i < OctTree.m_Level; i++)
                {
                    levelIndentation += "  ";
                }
                ImGui::Text("Level:[%d] %s Prims:[%d]", OctTree.m_Level, levelIndentation.c_str(), OctTree.m_Objects.size());
            };

        m_OctTree.ForEachOctTree(OctTreeStats);

        ImGui::TreePop();
    }
}

void Scene::OnSceneLoad()
{
    PROFILE_FUNCTION();
    SCOPED_TIMER_FUNCTION();

    View& mainView = m_Views[EView::Main];

    // max extents of every Node
    m_OctTree.m_AABB.Center = m_AABB.Center;
    m_OctTree.m_AABB.Extents = m_AABB.Extents;

    for (Node& node : m_Nodes)
    {
        if (node.m_VisualIdx != UINT_MAX)
        {
            m_Visuals.at(node.m_VisualIdx).OnSceneLoad();
        }
    }

    // empirically resize CSM distances based on scene BS radius
    g_GraphicPropertyGrid.m_ShadowControllables.m_MaxShadowDistance = std::min(300.0f, std::max(1.0f, m_BoundingSphere.Radius * 2));
    CalculateCSMSplitDistances();

    // empirically set camera near plane based on scene BS radius
    mainView.m_ZNearP = std::max(0.1f, std::min(m_BoundingSphere.Radius * 0.01f, 0.1f));

    LOG_DEBUG("Scene AABB: c:[%f, %f, %f] e:[%f, %f, %f]", m_AABB.Center.x, m_AABB.Center.y, m_AABB.Center.z, m_AABB.Extents.x, m_AABB.Extents.y, m_AABB.Extents.z);
    LOG_DEBUG("Scene Bounding Sphere: [%f, %f, %f][r: %f]", m_BoundingSphere.Center.x, m_BoundingSphere.Center.y, m_BoundingSphere.Center.z, m_BoundingSphere.Radius);
    LOG_DEBUG("CSM Split Distances: [%f, %f, %f, %f]", m_CSMSplitDistances[0], m_CSMSplitDistances[1], m_CSMSplitDistances[2], m_CSMSplitDistances[3]);
    LOG_DEBUG("Camera Near Plane: %f", mainView.m_ZNearP);
}

// referenced in imguimanager
bool s_bToggleOpenMapFileDialog = false;

void UpdateSceneIMGUI()
{
    if (s_bToggleOpenMapFileDialog)
    {
        std::vector<std::string> result = pfd::open_file{ "Select a map file", GetResourceDirectory(), { "All Files", "*" }, pfd::opt::force_path }.result();
        if (!result.empty())
        {
			extern void LoadScene(std::string_view filePath);
			LoadScene(result[0]);

			g_Graphic.m_Scene->OnSceneLoad();
		}
        s_bToggleOpenMapFileDialog = false;
    }
}
