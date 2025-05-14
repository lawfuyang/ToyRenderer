#ifndef _SHADER_INTEROP_H_
#define _SHADER_INTEROP_H_

#if defined(__cplusplus)
    #include "MathUtilities.h"
#else
    // .cpp -> .hlsl types

    // NOTE: need to manually unpack because hlsl has no built-in support for 8-bit types
    #define UByte4 uint
    #define UByte4N uint
    #define Byte4 uint
    #define Byte4N uint

    #define Half half
    #define Half2 half2
    #define Half4 half4

    #define Vector2 float2
    #define Vector2U uint2
    #define Vector3 float3
    #define Vector3U uint3
    #define Vector4 float4
    #define Vector4U uint4
    #define Matrix float4x4

    #define Quaternion float4
#endif // #if defined(__cplusplus)

static const uint32_t kNumThreadsPerWave = 32;
static const uint32_t kMaxThreadGroupsPerDimension = 65535;
static const float kFP16Max = 65504.0f;

static const uint32_t MaterialFlag_UseDiffuseTexture           = (1 << 0);
static const uint32_t MaterialFlag_UseNormalTexture            = (1 << 1);
static const uint32_t MaterialFlag_UseMetallicRoughnessTexture = (1 << 2);
static const uint32_t MaterialFlag_UseEmissiveTexture          = (1 << 3);

static const uint32_t SamplerIdx_AnisotropicClamp  = 0;
static const uint32_t SamplerIdx_AnisotropicWrap   = 1;
static const uint32_t SamplerIdx_AnisotropicBorder = 2;
static const uint32_t SamplerIdx_AnisotropicMirror = 3;
static const uint32_t SamplerIdx_Count             = 4;

static const uint32_t kCullingFlagFrustumCullingEnable     = (1 << 0);
static const uint32_t kCullingFlagOcclusionCullingEnable   = (1 << 1);
static const uint32_t kCullingFlagMeshletConeCullingEnable = (1 << 2);

static const uint32_t kMaxMeshletVertices = 64;
static const uint32_t kMaxMeshletTriangles = 96;
static const uint32_t kMeshletShaderThreadGroupSize = 96;

static const uint32_t kMaxNumMeshLODs = 8;
static const uint32_t kInvalidMeshLOD = 0xFF;

static const uint32_t kDeferredLightingDebugMode_LightingOnly      = 1;
static const uint32_t kDeferredLightingDebugMode_ColorizeInstances = 2;
static const uint32_t kDeferredLightingDebugMode_ColorizeMeshlets  = 3;
static const uint32_t kDeferredLightingDebugMode_Albedo            = 4;
static const uint32_t kDeferredLightingDebugMode_Normal            = 5;
static const uint32_t kDeferredLightingDebugMode_Emissive          = 6;
static const uint32_t kDeferredLightingDebugMode_Metalness         = 7;
static const uint32_t kDeferredLightingDebugMode_Roughness         = 8;
static const uint32_t kDeferredLightingDebugMode_AmbientOcclusion  = 9;
static const uint32_t kDeferredLightingDebugMode_Ambient           = 10;
static const uint32_t kDeferredLightingDebugMode_ShadowMask        = 11;
static const uint32_t kDeferredLightingDebugMode_MeshLOD           = 12;
static const uint32_t kDeferredLightingDebugMode_MotionVectors     = 13;

struct AdaptExposureParameters
{
    float m_MinLogLuminance;
    float m_LogLuminanceRange;
    float m_AdaptationSpeed;
    uint32_t m_NbPixels;
    uint32_t m_HistogramIdx;
    uint32_t m_LuminanceBufferIdx;
};

struct BasePassConstants
{
    Matrix m_WorldToClip;
    Matrix m_PrevWorldToClip;
    Matrix m_WorldToView;
    Vector4 m_Frustum;
    Vector2U m_HZBDimensions;
    float m_P00;
    float m_P11;
    float m_NearPlane;
    uint32_t m_CullingFlags;
    uint32_t m_DebugMode;
    uint32_t PAD0;
    Vector2U m_OutputResolution;
};

struct BasePassInstanceConstants
{
    Matrix m_WorldMatrix;
    Matrix m_PrevWorldMatrix;
    uint32_t m_MeshDataIdx;
    uint32_t m_MaterialDataIdx;
    Vector2 PAD0;
};

struct BloomConsts
{
    Vector2 m_InvSourceResolution;
    float m_FilterRadius;
    uint32_t m_bIsFirstDownsample;
    uint32_t m_DownsampleSourceTextureIdx;
    uint32_t m_LinearClampSamplerIdx;
    uint32_t m_UpsampleSourceTextureIdx;
};

struct DeferredLightingConsts
{
    Matrix m_ClipToWorld;
    Vector3 m_CameraOrigin;
    uint32_t m_SSAOEnabled;
    uint32_t m_DebugMode;
    Vector3 m_DirectionalLightVector;
    float m_DirectionalLightStrength;
    Vector2U m_LightingOutputResolution;
};

struct DeferredLightingResourceIndices
{
    uint32_t m_GBufferAIdx;
    uint32_t m_GBufferMotionIdx;
    uint32_t m_DepthBufferIdx;
    uint32_t m_SSAOTextureIdx;
    uint32_t m_ShadowMaskTextureIdx;
    uint32_t m_DDGIVolumesIdx;
    uint32_t m_ProbeDataIdx;
    uint32_t m_ProbeIrradianceIdx;
    uint32_t m_ProbeDistanceIdx;
    uint32_t m_LightingOutputIdx;
    uint32_t m_PointClampSamplerIdx;
    uint32_t m_LinearWrapSamplerIdx;
};

struct DrawIndirectArguments
{
    uint32_t m_VertexCount;
    uint32_t m_InstanceCount;
    uint32_t m_StartVertexLocation;
    uint32_t m_StartInstanceLocation;
};

struct DrawIndexedIndirectArguments
{
    uint32_t m_IndexCount;
    uint32_t m_InstanceCount;
    uint32_t m_StartIndexLocation;
    int32_t  m_BaseVertexLocation;
    uint32_t m_StartInstanceLocation;
};

struct DispatchIndirectArguments
{
    uint32_t m_ThreadGroupCountX;
    uint32_t m_ThreadGroupCountY;
    uint32_t m_ThreadGroupCountZ;
};

struct GenerateLuminanceHistogramParameters
{
    Vector2U m_SrcColorDims;
    float m_MinLogLuminance;
    float m_InverseLogLuminanceRange;
    uint32_t m_SrcColorIdx;
    uint32_t m_HistogramOutIdx;
};

struct GPUCullingPassConstants
{
    uint32_t m_NbInstances;
    uint32_t m_CullingFlags;
    Vector2U m_HZBDimensions;
    Vector4 m_Frustum;
    Matrix m_WorldToView;
    Matrix m_PrevWorldToView;
    float m_NearPlane;
    float m_P00;
    float m_P11;
    uint32_t m_ForcedMeshLOD;
    float m_MeshLODTarget;
};

struct GPUCullingBuildLateCullIndirectArgsResourceIndices
{
    uint32_t m_NumLateCullInstancesIdx;
    uint32_t m_LateCullDispatchIndirectArgsIdx;
};

struct HosekWilkieSkyParameters
{
    Vector4 m_Params[10];
};

struct MaterialData
{
    Vector4 m_ConstAlbedo;
    Vector3 m_ConstEmissive;
    float m_AlphaCutoff;
    uint32_t m_AlbedoTextureSamplerAndDescriptorIndex;
    uint32_t m_NormalTextureSamplerAndDescriptorIndex;
    uint32_t m_MetallicRoughnessTextureSamplerAndDescriptorIndex;
    uint32_t m_EmissiveTextureSamplerAndDescriptorIndex;
    uint32_t m_MaterialFlags;
    float m_ConstRoughness;
    float m_ConstMetallic;
};

struct MeshLODData
{
    uint32_t m_MeshletDataBufferIdx;
    uint32_t m_NumMeshlets;
    float m_Error;
    uint32_t PAD0;
};

struct MeshData
{
    Vector4 m_BoundingSphere;
    MeshLODData m_MeshLODDatas[kMaxNumMeshLODs];
    uint32_t m_NumLODs;
    uint32_t m_GlobalVertexBufferIdx;
    uint32_t m_GlobalIndexBufferIdx;
};

struct MeshletData
{
    Vector4 m_BoundingSphere;
    uint32_t m_ConeAxisAndCutoff; // 4x int8_t
    uint32_t m_MeshletVertexIDsBufferIdx;
    uint32_t m_MeshletIndexIDsBufferIdx;
    uint32_t m_VertexAndTriangleCount; // 1x uint8_t + 1x uint8_t
};

struct MeshletPayload
{
    uint32_t m_MeshletIndices[64];
    uint32_t m_InstanceConstIdx;
    uint32_t m_MeshLOD;
};

struct MeshletAmplificationData
{
    uint32_t m_InstanceConstIdx;
    uint32_t m_MeshLOD;
    uint32_t m_MeshletGroupOffset;
};

struct MinMaxDownsampleConsts
{
    Vector2U m_OutputDimensions;
    uint32_t m_bDownsampleMax;
};

struct NodeLocalTransform
{
    uint32_t m_ParentNodeIdx;
    Vector3 m_Position;
    Quaternion m_Rotation;
    Vector3 m_Scale;
    uint32_t PAD0;
};

struct PackNormalAndRoughnessConsts
{
    Vector2U m_OutputResolution;
    uint32_t m_GBufferAIdx;
    uint32_t m_NormalRoughnessOutputIdx;
};

struct FullScreenPassThroughResourcesIndices
{
    uint32_t m_InputTextureIdx;
};

struct PostProcessParameters
{
    Vector2U m_OutputDims;
    float m_ManualExposure;
    float m_MiddleGray;
    float m_WhitePoint;
    float m_BloomStrength;
    uint32_t m_ColorInputIdx;
    uint32_t m_AverageLuminanceBufferIdx;
    uint32_t m_BloomTextureIdx;
};

struct GIProbeExtraReductionConsts
{
    Vector3U m_ReductionInputSize;
};

struct GIProbeTraceConsts
{
    Vector3 m_DirectionalLightVector;
    float m_DirectionalLightStrength;
};

struct GIProbeVisualizationUpdateConsts
{
    uint32_t m_NumProbes;
    Vector3 m_CameraOrigin;
    Vector4 m_Frustum;
    Matrix m_WorldToView;
    Vector2U m_HZBDimensions;
    float m_P00;
    float m_P11;
    float m_NearPlane;
    float m_ProbeRadius;
    uint32_t m_bHideInactiveProbes;
};

struct GIProbeVisualizationConsts
{
    Matrix m_WorldToClip;
    Vector3 m_CameraDirection;
    float m_ProbeRadius;
};

// VS-friendly Vertex Layout
struct UncompressedRawVertexFormat
{
    Vector3 m_Position;
    Vector3 m_Normal;
    Vector2 m_TexCoord;
};

struct RawVertexFormat
{
    Vector3 m_Position;
    uint32_t m_PackedNormal;
    Half2 m_TexCoord;
};

struct ShadowMaskConsts
{
    Matrix m_ClipToWorld;
    Vector3 m_DirectionalLightDirection;
    float m_NoisePhase;
    Vector3 m_CameraPosition;
    float m_TanSunAngularRadius;
    Vector2U m_OutputResolution;
    uint32_t m_bDoDenoising;
    float m_RayStartOffset;
};

struct ShadowMaskResourceIndices
{
    uint32_t m_DepthBufferIdx;
    uint32_t m_SceneTLASIdx;
    uint32_t m_GBufferAIdx;
    uint32_t m_BasePassInstanceConstsIdx;
    uint32_t m_GlobalVertexBufferIdx;
    uint32_t m_MaterialDataBufferIdx;
    uint32_t m_GlobalIndexIDsBufferIdx;
    uint32_t m_MeshDataBufferIdx;
    uint32_t m_BlueNoiseIdx;
    uint32_t m_ShadowDataOutputIdx;
    uint32_t m_LinearViewDepthOutputIdx;
    uint32_t m_SamplersIdx;
};

struct SkyPassParameters
{
    Matrix m_ClipToWorld;
    Vector3 m_SunLightDir;
    uint32_t PAD0;
    Vector3 m_CameraPosition;
    uint32_t PAD1;
    HosekWilkieSkyParameters m_HosekParams;
};

struct TLASInstanceDesc
{
    float m_Transform[12]; // 3x4 matrix flattened
    uint32_t m_InstanceID : 24;
    uint32_t m_InstanceMask : 8;
    uint32_t m_InstanceContributionToHitGroupIndex : 24;
    uint32_t m_Flags : 8;
    uint64_t m_AccelerationStructure;
};

struct UpdateInstanceConstsPassConstants
{
    uint32_t m_NumInstances;
    uint32_t m_NodeLocalTransformsIdx;
    uint32_t m_PrimitiveIDToNodeIDBufferIdx;
    uint32_t m_InstanceConstantsIdx;
    uint32_t m_TLASInstanceDescsBufferIdx;
};

struct XeGTAOPrefilterDepthsResourceIndices
{
    uint32_t m_SrcRawDepthIdx;
    uint32_t m_OutWorkingDepthMIP0Idx;
    uint32_t m_OutWorkingDepthMIP1Idx;
    uint32_t m_OutWorkingDepthMIP2Idx;
    uint32_t m_OutWorkingDepthMIP3Idx;
    uint32_t m_OutWorkingDepthMIP4Idx;
    uint32_t m_PointClampSamplerIdx;
};

struct XeGTAOMainPassConstantBuffer
{
    Matrix m_WorldToViewNoTranslate;
    uint32_t m_Quality;
    uint32_t m_SrcWorkingDepthIdx;
    uint32_t m_SrcHilbertLUTIdx;
    uint32_t m_GBufferAIdx;
    uint32_t m_OutWorkingAOTermIdx;
    uint32_t m_OutWorkingEdgesIdx;
    uint32_t m_DebugOutputIdx;
    uint32_t m_PointClampSamplerIdx;
};

struct XeGTAODenoiseConstants
{
    uint32_t m_FinalApply;
    uint32_t m_SrcWorkingAOTermIdx;
    uint32_t m_SrcWorkingEdgesIdx;
    uint32_t m_OutFinalAOTermIdx;
    uint32_t m_DebugOutputIdx;
    uint32_t m_PointClampSamplerIdx;
};

#endif // #define _SHADER_INTEROP_H_
