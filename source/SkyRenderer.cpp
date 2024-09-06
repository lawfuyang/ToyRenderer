#include "Graphic.h"

namespace HosekData
{
#include "extern/HosekWilkie/HosekDataRGB.h"
}

#include "GraphicPropertyGrid.h"
#include "CommonResources.h"
#include "MathUtilities.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/shared/SkyStructs.h"

extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;

namespace HosekWilkieHelper
{
    enum ESkyParams : uint16_t
    {
        ESkyParam_A = 0,
        ESkyParam_B,
        ESkyParam_C,
        ESkyParam_D,
        ESkyParam_E,
        ESkyParam_F,
        ESkyParam_G,
        ESkyParam_I,
        ESkyParam_H,
        ESkyParam_Z,
        ESkyParam_Count
    };

    static_assert(sizeof(HosekWilkieSkyParameters) == HosekWilkieHelper::ESkyParam_Count * sizeof(Vector4));

    using SkyParameters = std::array<DirectX::XMFLOAT3, ESkyParam_Count>;

    double EvaluateSpline(double const* spline, size_t stride, double value)
    {
        return
            1  * pow(1.0 - value, 5)                 * spline[0 * stride] +
            5  * pow(1.0 - value, 4) * pow(value, 1) * spline[1 * stride] +
            10 * pow(1.0 - value, 3) * pow(value, 2) * spline[2 * stride] +
            10 * pow(1.0 - value, 2) * pow(value, 3) * spline[3 * stride] +
            5  * pow(1.0 - value, 1) * pow(value, 4) * spline[4 * stride] +
            1                        * pow(value, 5) * spline[5 * stride];
    }

    double Evaluate(double const* dataset, size_t stride, float turbidity, float albedo, float sun_theta)
    {
        // splines are functions of elevation^1/3
        double elevationK = pow(std::max<float>(0.f, 1.f - sun_theta / (std::numbers::pi * 0.5f)), 1.f / 3.0f);

        // table has values for turbidity 1..10
        int turbidity0 = std::clamp(static_cast<int>(turbidity), 1, 10);
        int turbidity1 = std::min(turbidity0 + 1, 10);
        float turbidityK = std::clamp(turbidity - turbidity0, 0.f, 1.f);

        double const* datasetA0 = dataset;
        double const* datasetA1 = dataset + stride * 6 * 10;

        double a0t0 = EvaluateSpline(datasetA0 + stride * 6 * (turbidity0 - 1), stride, elevationK);
        double a1t0 = EvaluateSpline(datasetA1 + stride * 6 * (turbidity0 - 1), stride, elevationK);
        double a0t1 = EvaluateSpline(datasetA0 + stride * 6 * (turbidity1 - 1), stride, elevationK);
        double a1t1 = EvaluateSpline(datasetA1 + stride * 6 * (turbidity1 - 1), stride, elevationK);

        return a0t0 * (1.0f - albedo) * (1.0f - turbidityK) + a1t0 * albedo * (1.0f - turbidityK) + a0t1 * (1.0f - albedo) * turbidityK + a1t1 * albedo * turbidityK;
    }

    DirectX::XMFLOAT3 HosekWilkie(float cos_theta, float gamma, float cos_gamma, const SkyParameters& params)
    {
        using namespace DirectX;

        XMVECTOR _A = XMLoadFloat3(&params[0]);
        XMVECTOR _B = XMLoadFloat3(&params[1]);
        XMVECTOR _C = XMLoadFloat3(&params[2]);
        XMVECTOR _D = XMLoadFloat3(&params[3]);
        XMVECTOR _E = XMLoadFloat3(&params[4]);
        XMVECTOR _F = XMLoadFloat3(&params[5]);
        XMVECTOR _G = XMLoadFloat3(&params[6]);
        XMVECTOR _H = XMLoadFloat3(&params[7]);
        XMVECTOR _I = XMLoadFloat3(&params[8]);

        XMVECTOR chi = XMVectorDivide(XMVectorReplicate(1.f + cos_gamma * cos_gamma), XMVectorPow(_H * _H + XMVectorReplicate(1.0f) - XMVectorScale(_H, 2.0f * cos_gamma), XMVectorReplicate(1.5)));
        XMVECTOR temp1 = _A * XMVectorExp(XMVectorScale(_B, 1.0f / (cos_theta + 0.01f)));
        XMVECTOR temp2 = _C + _D * XMVectorExp(XMVectorScale(_E, gamma)) + XMVectorScale(_F, gamma * gamma) + chi * _G + XMVectorScale(_I, (float)sqrt(std::max(0.f, cos_theta)));
        XMVECTOR temp = temp1 * temp2;

        XMFLOAT3 result;
        XMStoreFloat3(&result, temp);
        return result;
    }

    SkyParameters CalculateSkyParameters(float turbidity, Vector3 albedo, Vector3 sun_direction)
    {
        using namespace DirectX;

        float sun_theta = std::acos(std::clamp(sun_direction.y, 0.f, 1.f));

        SkyParameters params{};
        for (uint32_t i = 0; i < 3; ++i)
        {
            float albedoChannel = *((float*)&albedo + i);

            *((float*)&params[0] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 0, 9, turbidity, albedoChannel, sun_theta);
            *((float*)&params[1] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 1, 9, turbidity, albedoChannel, sun_theta);
            *((float*)&params[2] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 2, 9, turbidity, albedoChannel, sun_theta);
            *((float*)&params[3] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 3, 9, turbidity, albedoChannel, sun_theta);
            *((float*)&params[4] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 4, 9, turbidity, albedoChannel, sun_theta);
            *((float*)&params[5] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 5, 9, turbidity, albedoChannel, sun_theta);
            *((float*)&params[6] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 6, 9, turbidity, albedoChannel, sun_theta);

            // data values are swapped
            *((float*)&params[7] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 8, 9, turbidity, albedoChannel, sun_theta);
            *((float*)&params[8] + i) = (float)Evaluate(HosekData::datasetsRGB[i] + 7, 9, turbidity, albedoChannel, sun_theta);

            // Z value thing
            *((float*)&params[9] + i) = (float)Evaluate(HosekData::datasetsRGBRad[i], 1, turbidity, albedoChannel, sun_theta);
        }

        Vector3 S = HosekWilkie(std::cos(sun_theta), 0.0f, 1.0f, params);
        S *= params[9];
        params[9] = Vector3{ params[9] } / S.Dot(Vector3{ 0.2126f, 0.7152f, 0.0722f });

        return params;
    }
};

class SkyRenderer : public IRenderer
{
public:
    SkyRenderer() : IRenderer{ "SkyRenderer" } {}

    bool Setup(RenderGraph& renderGraph) override
	{
        if (!g_GraphicPropertyGrid.m_SkyControllables.m_bEnabled)
        {
            return false;
        }

        renderGraph.AddReadDependency(g_LightingOutputRDGTextureHandle);
        renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);

		return true;
	}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();
        View& mainView = scene->m_Views[Scene::EView::Main];

        nvrhi::TextureHandle lightingOutput = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
        nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

        // Render Targets & Depth Buffer
        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(lightingOutput);
        frameBufferDesc.setDepthAttachment(depthStencilBuffer);
        frameBufferDesc.depthAttachment.isReadOnly = true;
        nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

        // pass consts
        SkyPassParameters skyPassParameters{};
        skyPassParameters.m_InvViewProjMatrix = mainView.m_InvViewProjectionMatrix;
        skyPassParameters.m_SunLightDir = scene->m_DirLightVec;
        skyPassParameters.m_CameraPosition = mainView.m_Eye;

        HosekWilkieHelper::SkyParameters skyParams = HosekWilkieHelper::CalculateSkyParameters(g_GraphicPropertyGrid.m_SkyControllables.m_SkyTurbidity, g_GraphicPropertyGrid.m_SkyControllables.m_GroundAlbedo, scene->m_DirLightVec);
        for (uint32_t i = 0; i < skyParams.size(); ++i)
        {
            skyPassParameters.m_HosekParams.m_Params[i] = Vector4{ Vector3{ skyParams[i] } };
        }

        nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, skyPassParameters);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = 
        {
            nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer)
        };

        const nvrhi::BlendState::RenderTarget* blendState = nullptr;
        const nvrhi::DepthStencilState& depthStencilState = g_CommonResources.DepthReadStencilNone;
        g_Graphic.AddFullScreenPass(commandList, frameBufferDesc, bindingSetDesc, "sky_PS_HosekWilkieSky", blendState, &depthStencilState);
    }
};

static SkyRenderer gs_SkyRenderer;
IRenderer* g_SkyRenderer = &gs_SkyRenderer;
