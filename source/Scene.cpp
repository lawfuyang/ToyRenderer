#include "Scene.h"

#include "extern/imgui/imgui.h"
#include "SDL3/SDL_keyboard.h"
#include "SDL3/SDL_mouse.h"

#include "CommonResources.h"
#include "Engine.h"
#include "Graphic.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Visual.h"

#include "shaders/ShaderInterop.h"

static_assert(sizeof(Scene::NodeLocalTransformBytes) == sizeof(NodeLocalTransform));

extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferMotionRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;

class ClearBuffersRenderer : public IRenderer
{
public:
    ClearBuffersRenderer() : IRenderer{ "ClearBuffersRenderer" } {}

    bool Setup(RenderGraph& renderGraph) override
    {
        renderGraph.AddWriteDependency(g_GBufferARDGTextureHandle);
        renderGraph.AddWriteDependency(g_GBufferMotionRDGTextureHandle);
        renderGraph.AddWriteDependency(g_LightingOutputRDGTextureHandle);
        renderGraph.AddWriteDependency(g_DepthStencilBufferRDGTextureHandle);

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        static bool s_ClearBackBufferEveryFrame = true; // clearing every frame makes things easier to debug
        static bool s_ClearLightingOutputEveryFrame = true; // clearing every frame makes things easier to debug

        if (s_ClearBackBufferEveryFrame)
        {
            PROFILE_GPU_SCOPED(commandList, "Clear Back Buffer");

            nvrhi::TextureHandle backBuffer = g_Graphic.GetCurrentBackBuffer();

            commandList->clearTextureFloat(backBuffer, nvrhi::AllSubresources, backBuffer->getDesc().clearValue);
        }

        static bool s_ClearGBuffersEveryFrame = true;
        if (s_ClearGBuffersEveryFrame)
        {
            PROFILE_GPU_SCOPED(commandList, "Clear GBuffers");

            nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
            nvrhi::TextureHandle GBufferMotionTexture = renderGraph.GetTexture(g_GBufferMotionRDGTextureHandle);

            commandList->clearTextureUInt(GBufferATexture, nvrhi::AllSubresources, 0);
            commandList->clearTextureFloat(GBufferMotionTexture, nvrhi::AllSubresources, GBufferMotionTexture->getDesc().clearValue);
        }

        if (s_ClearLightingOutputEveryFrame)
        {
            PROFILE_GPU_SCOPED(commandList, "Clear Lighting Output");

            nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);

            commandList->clearTextureFloat(lightingOutputTexture, nvrhi::AllSubresources, lightingOutputTexture->getDesc().clearValue);
        }

        // clear depth buffer
        {
            PROFILE_GPU_SCOPED(commandList, "Clear Depth Buffer");

            nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

            const bool kClearStencil = true;
            const uint8_t kClearStencilValue = Graphic::kStencilBit_Sky;
            commandList->clearDepthStencilTexture(depthStencilBuffer, nvrhi::AllSubresources, true, Graphic::kFarDepth, kClearStencil, kClearStencilValue);
        }
    }
};
static ClearBuffersRenderer gs_ClearBuffersRenderer;
IRenderer* g_ClearBuffersRenderer = &gs_ClearBuffersRenderer;

Vector4 Animation::Channel::Evaluate(float time) const
{
    const auto it = std::lower_bound(m_KeyFrames.begin(), m_KeyFrames.end(), time);
    if (it == m_KeyFrames.begin())
        return m_Data.front();
    if (it == m_KeyFrames.end())
        return m_Data.back();

    const uint32_t i = it - m_KeyFrames.begin();

    const float t = Normalize(time, m_KeyFrames.at(i - 1), m_KeyFrames.at(i));

    if (m_PathType == PathType::Rotation)
    {
        return Quaternion::Slerp(m_Data.at(i - 1), m_Data.at(i), t);
    }

    return Vector4::Lerp(m_Data.at(i - 1), m_Data.at(i), t);
}

void View::Update()
{
    PROFILE_FUNCTION();

    // update prev frame matrices
    m_PrevWorldToView = m_WorldToView;
    m_PrevViewToClip = m_ViewToClip;
    m_PrevWorldToClip = m_WorldToClip;

    m_ViewToWorld = Matrix::CreateFromQuaternion(m_Orientation) * Matrix::CreateTranslation(m_Eye);
    m_WorldToView = m_ViewToWorld.Invert();

    m_ViewToClip = Matrix::CreatePerspectiveFieldOfView(m_FOV, m_AspectRatio, m_ZNearP, kKindaBigNumber);
    ModifyPerspectiveMatrix(m_ViewToClip, m_ZNearP, kKindaBigNumber, Graphic::kInversedDepthBuffer, Graphic::kInfiniteDepthBuffer);

    m_WorldToClip = m_WorldToView * m_ViewToClip;
    m_ClipToWorld = m_WorldToClip.Invert();

    Frustum::CreateFromMatrix(m_Frustum, m_ViewToClip);
    m_Frustum.Transform(m_Frustum, m_ViewToWorld);

	const bool bFreezeCullingCamera = g_GraphicPropertyGrid.m_InstanceRenderingControllables.m_bFreezeCullingCamera;
    if (!bFreezeCullingCamera)
    {
        m_CullingPrevWorldToView = m_PrevWorldToView;
		m_CullingWorldToView = m_WorldToView;
    }
}

void View::UpdateVectors(float yaw, float pitch)
{
    const float PIBy2 = std::numbers::pi * 0.5f;

    const float r = std::cos(pitch);
    Vector3 lookAt { r * std::sin(yaw), std::sin(pitch), r * std::cos(yaw) };
    Vector3 right = { std::sin(yaw - PIBy2), 0, std::cos(yaw - PIBy2) };
    Vector3 up = right.Cross(lookAt);

    m_Orientation = Quaternion::CreateFromRotationMatrix(Matrix::CreateWorld(Vector3::Zero, lookAt, up));
}

void Scene::Initialize()
{
    m_View.m_ZNearP = Graphic::kDefaultCameraNearPlane;
    m_View.m_AspectRatio = (float)g_Graphic.m_RenderResolution.x / g_Graphic.m_RenderResolution.y;
    m_View.m_Eye = Vector3{ 0.0f, 10.0f, -10.0f };
    m_View.Update();

    m_RenderGraph = std::make_shared<RenderGraph>();
    m_RenderGraph->Initialize();

    UpdateDirectionalLightVector();
}

void Scene::SetCamera(uint32_t idx)
{
    const Camera& camera = m_Cameras.at(idx);

    m_View.m_Eye = camera.m_Position;
    m_View.m_Orientation = camera.m_Orientation;

    const Matrix matrix = Matrix::CreateFromQuaternion(camera.m_Orientation);
    const Vector3 forwardVector = matrix.Forward();

    m_Yaw = atan2f(forwardVector.x, forwardVector.z);
    m_Pitch = asinf(forwardVector.y);

    m_View.UpdateVectors(m_Yaw, m_Pitch);
}

bool Scene::IsShadowsEnabled() const
{
    return !!m_TLAS && m_bEnableShadows;
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

    // Calculate the move vector in camera space.
    Vector3 finalMoveVector;

    const Matrix viewMatrix = Matrix::CreateFromQuaternion(m_View.m_Orientation);

    if (keyboardStates[SDL_SCANCODE_A])
    {
        finalMoveVector -= viewMatrix.Right();
    }
	if (keyboardStates[SDL_SCANCODE_D])
    {
        finalMoveVector += viewMatrix.Right();
    }
	if (keyboardStates[SDL_SCANCODE_W])
    {
        finalMoveVector += viewMatrix.Forward();
    }
	if (keyboardStates[SDL_SCANCODE_S])
    {
        finalMoveVector -= viewMatrix.Forward();
    }

    if (finalMoveVector.LengthSquared() > 0.1f)
    {
        finalMoveVector.Normalize();
        m_View.m_Eye += finalMoveVector * s_CameraMoveSpeed * g_Engine.m_CPUCappedFrameTimeMs;
    }

    if (mouseButtonFlags & SDL_BUTTON_RMASK)
    {
        const Vector2 mouseDeltaVec = m_CurrentMousePos - m_MouseLastPos;

        // compute new camera angles and vectors based off mouse delta
        static float s_MouseRotationSpeed = 0.002f;
        m_Yaw -= s_MouseRotationSpeed * mouseDeltaVec.x;
        m_Pitch -= s_MouseRotationSpeed * mouseDeltaVec.y;

        m_View.UpdateVectors(m_Yaw, m_Pitch);
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

void Scene::UpdateAnimations()
{
    PROFILE_FUNCTION();

    m_AnimationTimeSeconds += g_Engine.m_CPUCappedFrameTimeMs * 0.001f;

    for (const Animation& animation : m_Animations)
    {
        const float t = fmod(m_AnimationTimeSeconds, animation.m_TimeEnd - animation.m_TimeStart);
        const float time = t + animation.m_TimeStart;

        for (const Animation::Channel& channel : animation.m_Channels)
        {
            Node& node = m_Nodes.at(channel.m_TargetNodeIdx);
            NodeLocalTransform& nodeLocalTransform = *(NodeLocalTransform*)&m_NodeLocalTransforms.at(channel.m_TargetNodeIdx);

            const Vector4 evaluatedVal = channel.Evaluate(time);

            switch (channel.m_PathType)
            {
            case Animation::Channel::PathType::Translation:
                node.m_Position = nodeLocalTransform.m_Position = Vector3{ evaluatedVal };
                break;
            case Animation::Channel::PathType::Rotation:
                node.m_Rotation = nodeLocalTransform.m_Rotation = Quaternion{ evaluatedVal };
                break;
            case Animation::Channel::PathType::Scale:
                node.m_Scale = nodeLocalTransform.m_Scale = Vector3{ evaluatedVal };
                break;
            }
        }
    }
}

void Scene::CreateAccelerationStructures()
{
    PROFILE_FUNCTION();

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Build BLAS & TLAS");

    for (Mesh& mesh : g_Graphic.m_Meshes)
    {
        mesh.BuildBLAS(commandList);
    }

    static_assert(sizeof(TLASInstanceDesc) == sizeof(nvrhi::rt::InstanceDesc));

    nvrhi::rt::AccelStructDesc tlasDesc;
    tlasDesc.topLevelMaxInstances = m_Primitives.size();
    tlasDesc.debugName = "Scene TLAS";
    tlasDesc.isTopLevel = true;
    m_TLAS = g_Graphic.m_NVRHIDevice->createAccelStruct(tlasDesc);

    nvrhi::BufferDesc instanceDescsBufferDesc;
    instanceDescsBufferDesc.byteSize = m_Primitives.size() * sizeof(nvrhi::rt::InstanceDesc);
    instanceDescsBufferDesc.structStride = sizeof(nvrhi::rt::InstanceDesc);
    instanceDescsBufferDesc.debugName = "TLAS Instance Descs Buffer";
    instanceDescsBufferDesc.canHaveUAVs = true;
    instanceDescsBufferDesc.isAccelStructBuildInput = true;
    instanceDescsBufferDesc.initialState = nvrhi::ResourceStates::AccelStructBuildInput;

    m_TLASInstanceDescsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(instanceDescsBufferDesc);

    std::vector<nvrhi::rt::InstanceDesc> instances;

    for (uint32_t instanceID = 0; instanceID < m_Primitives.size(); ++instanceID)
    {
        const Primitive& primitive = m_Primitives.at(instanceID);

        const Node& node = m_Nodes.at(primitive.m_NodeID);
        const Mesh& mesh = g_Graphic.m_Meshes.at(primitive.m_MeshIdx);

        nvrhi::rt::InstanceDesc& instanceDesc = instances.emplace_back();

		// transform will be updated in CS_UpdateInstanceConstsAndBuildTLAS

        // TODO: investigate why is the CCW flag wrong
        nvrhi::rt::InstanceFlags instanceFlags = /*Graphic::kFrontCCW ? nvrhi::rt::InstanceFlags::TriangleFrontCounterclockwise :*/ nvrhi::rt::InstanceFlags::None;
        instanceFlags = instanceFlags | ((primitive.m_Material.m_AlphaMode == AlphaMode::Opaque) ? nvrhi::rt::InstanceFlags::ForceOpaque : nvrhi::rt::InstanceFlags::ForceNonOpaque);

        instanceDesc.instanceID = instanceID;
        instanceDesc.instanceMask = 1;
        instanceDesc.instanceContributionToHitGroupIndex = 0;
        instanceDesc.flags = instanceFlags;
        instanceDesc.blasDeviceAddress = mesh.m_BLAS->getDeviceAddress();
    }

    commandList->writeBuffer(m_TLASInstanceDescsBuffer, instances.data(), instances.size() * sizeof(nvrhi::rt::InstanceDesc));

    commandList->buildTopLevelAccelStructFromBuffer(m_TLAS, m_TLASInstanceDescsBuffer, 0, instances.size());
}

void Scene::Update()
{
    PROFILE_FUNCTION();

    UpdateMainViewCameraControls();

    m_View.Update();

    if (g_GraphicPropertyGrid.m_DebugControllables.m_EnableAnimations)
    {
        UpdateAnimations();
    }

    tf::Taskflow tf;

    m_RenderGraph->InitializeForFrame(tf);
    {
        PROFILE_SCOPED("Schedule Renderers");

        extern IRenderer* g_ClearBuffersRenderer;
        extern IRenderer* g_UpdateInstanceConstsRenderer;
        extern IRenderer* g_GIRenderer;
        extern IRenderer* g_GBufferRenderer;
        extern IRenderer* g_ShadowMaskRenderer;
        extern IRenderer* g_DeferredLightingRenderer;
        extern IRenderer* g_TransparentForwardRenderer;
        extern IRenderer* g_IMGUIRenderer;
        extern IRenderer* g_SkyRenderer;
        extern IRenderer* g_PostProcessRenderer;
        extern IRenderer* g_AdaptLuminanceRenderer;
        extern IRenderer* g_AmbientOcclusionRenderer;
        extern IRenderer* g_BloomRenderer;
        extern IRenderer* g_GIDebugRenderer;

        m_RenderGraph->AddRenderer(g_ClearBuffersRenderer);
        m_RenderGraph->AddRenderer(g_UpdateInstanceConstsRenderer);
        m_RenderGraph->AddRenderer(g_GIRenderer);
        m_RenderGraph->AddRenderer(g_GBufferRenderer);
        m_RenderGraph->AddRenderer(g_AmbientOcclusionRenderer);
        m_RenderGraph->AddRenderer(g_ShadowMaskRenderer);
        m_RenderGraph->AddRenderer(g_DeferredLightingRenderer);
        m_RenderGraph->AddRenderer(g_SkyRenderer);
        m_RenderGraph->AddRenderer(g_BloomRenderer);
        m_RenderGraph->AddRenderer(g_TransparentForwardRenderer);
        m_RenderGraph->AddRenderer(g_AdaptLuminanceRenderer);
        m_RenderGraph->AddRenderer(g_PostProcessRenderer);

        // DisplayResolution Debug Passes
        m_RenderGraph->AddRenderer(g_GIDebugRenderer);
        m_RenderGraph->AddRenderer(g_IMGUIRenderer);
    }
    m_RenderGraph->Compile();

    g_Engine.m_Executor->corun(tf);
}

void Scene::Shutdown()
{
    m_RenderGraph->Shutdown();
}

void Scene::UpdateIMGUI()
{
    if (ImGui::TreeNode("Ambient Occlusion"))
    {
        extern IRenderer* g_AmbientOcclusionRenderer;
        g_AmbientOcclusionRenderer->UpdateImgui();

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Bloom"))
    {
        extern IRenderer* g_BloomRenderer;
        g_BloomRenderer->UpdateImgui();

        ImGui::TreePop();
    }

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

    if (ImGui::TreeNode("Culling Stats"))
    {
        const auto& InstanceRenderingControllables = g_GraphicPropertyGrid.m_InstanceRenderingControllables;

        // TODO: support transparent

        ImGui::Text("Early:");

        ImGui::Indent();

        ImGui::Text("Instances:[%d]", m_View.m_GPUCullingCounters.m_EarlyInstances);
        ImGui::Text("Meshlets: [%d]", m_View.m_GPUCullingCounters.m_EarlyMeshlets);

        ImGui::Unindent();

        ImGui::Text("Late:");

        ImGui::Indent();

        ImGui::Text("Instances: [%d]", m_View.m_GPUCullingCounters.m_LateInstances);
        ImGui::Text("Meshlets: [%d]", m_View.m_GPUCullingCounters.m_LateMeshlets);

        ImGui::Unindent();

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("HDR"))
    {
        extern IRenderer* g_AdaptLuminanceRenderer;
        g_AdaptLuminanceRenderer->UpdateImgui();

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

        ImGui::DragFloat3("Directional Light Color", (float*)&m_DirLightColor, 0.01f, 0.0f, 1.0f, "%.1f");

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Shadows"))
    {
        extern IRenderer* g_ShadowMaskRenderer;
        g_ShadowMaskRenderer->UpdateImgui();

        ImGui::TreePop();
    }
}

void Scene::PostSceneLoad()
{
    PROFILE_FUNCTION();

    // empirically set camera near plane based on scene BS radius
    m_View.m_ZNearP = std::max(0.1f, std::min(m_BoundingSphere.Radius * 0.01f, 0.1f));

    LOG_DEBUG("Scene AABB: c:[%f, %f, %f] e:[%f, %f, %f]", m_AABB.Center.x, m_AABB.Center.y, m_AABB.Center.z, m_AABB.Extents.x, m_AABB.Extents.y, m_AABB.Extents.z);
    LOG_DEBUG("Scene Bounding Sphere: [%f, %f, %f][r: %f]", m_BoundingSphere.Center.x, m_BoundingSphere.Center.y, m_BoundingSphere.Center.z, m_BoundingSphere.Radius);
    LOG_DEBUG("Scene OBB : c:[%f, %f, %f] e:[%f, %f, %f] o:[%f, %f, %f, %f]", m_OBB.Center.x, m_OBB.Center.y, m_OBB.Center.z, m_OBB.Extents.x, m_OBB.Extents.y, m_OBB.Extents.z, m_OBB.Orientation.x, m_OBB.Orientation.y, m_OBB.Orientation.z, m_OBB.Orientation.w);
    LOG_DEBUG("Camera Near Plane: %f", m_View.m_ZNearP);

    // set to first camera if any
    if (!m_Cameras.empty())
    {
        SetCamera(0);
    }

	UpdateInstanceIDsBuffers();
    CreateAccelerationStructures();
}
