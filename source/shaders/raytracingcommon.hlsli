#pragma once

#include "lightingcommon.hlsli"
#include "packunpack.hlsli"

#include "ShaderInterop.h"

UncompressedRawVertexFormat InterpolateVertex(RawVertexFormat vertices[3], float3 barycentrics)
{
    UncompressedRawVertexFormat v = (UncompressedRawVertexFormat)0;
    
    for (uint i = 0; i < 3; i++)
    {
        v.m_Position += vertices[i].m_Position * barycentrics[i];
        v.m_Normal += UnpackR10G10B10A2F(vertices[i].m_PackedNormal).xyz * barycentrics[i];
        v.m_TexCoord += vertices[i].m_TexCoord * barycentrics[i];
    }
    
    return v;
}

struct RayDiff
{
    float3 dOdx;
    float3 dOdy;
    float3 dDdx;
    float3 dDdy;
};

void ComputeRayDirectionDifferentials(float3 nonNormalizedCameraRaydir, float3 right, float3 up, float2 viewportDims, out float3 dDdx, out float3 dDdy)
{
    // Igehy Equation 8
    float dd = dot(nonNormalizedCameraRaydir, nonNormalizedCameraRaydir);
    float divd = 2.f / (dd * sqrt(dd));
    float dr = dot(nonNormalizedCameraRaydir, right);
    float du = dot(nonNormalizedCameraRaydir, up);
    dDdx = ((dd * right) - (dr * nonNormalizedCameraRaydir)) * divd / viewportDims.x;
    dDdy = -((dd * up) - (du * nonNormalizedCameraRaydir)) * divd / viewportDims.y;
}

void PropagateRayDiff(float3 D, float t, float3 N, inout RayDiff rd)
{
    // Part of Igehy Equation 10
    float3 dodx = rd.dOdx + t * rd.dDdx;
    float3 dody = rd.dOdy + t * rd.dDdy;

    // Igehy Equations 10 and 12
    float rcpDN = 1.f / dot(D, N);
    float dtdx = -dot(dodx, N) * rcpDN;
    float dtdy = -dot(dody, N) * rcpDN;
    dodx += D * dtdx;
    dody += D * dtdy;

    // Store differential origins
    rd.dOdx = dodx;
    rd.dOdy = dody;
}

void PrepVerticesForRayDiffs(UncompressedRawVertexFormat vertices[3], out float3 edge01, out float3 edge02, out float3 faceNormal)
{
    // Apply instance transforms
    vertices[0].m_Position = mul(ObjectToWorld3x4(), float4(vertices[0].m_Position, 1.f)).xyz;
    vertices[1].m_Position = mul(ObjectToWorld3x4(), float4(vertices[1].m_Position, 1.f)).xyz;
    vertices[2].m_Position = mul(ObjectToWorld3x4(), float4(vertices[2].m_Position, 1.f)).xyz;

    // Find edges and face normal
    edge01 = vertices[1].m_Position - vertices[0].m_Position;
    edge02 = vertices[2].m_Position - vertices[0].m_Position;
    faceNormal = cross(edge01, edge02);
}

void ComputeBarycentricDifferentials(RayDiff rd, float3 rayDir, float3 edge01, float3 edge02, float3 faceNormalW, out float2 dBarydx, out float2 dBarydy)
{
    // Igehy "Normal-Interpolated Triangles"
    float3 Nu = cross(edge02, faceNormalW);
    float3 Nv = cross(edge01, faceNormalW);

    // Plane equations for the triangle edges, scaled in order to make the dot with the opposite vertex equal to 1
    float3 Lu = Nu / (dot(Nu, edge01));
    float3 Lv = Nv / (dot(Nv, edge02));

    dBarydx.x = dot(Lu, rd.dOdx); // du / dx
    dBarydx.y = dot(Lv, rd.dOdx); // dv / dx
    dBarydy.x = dot(Lu, rd.dOdy); // du / dy
    dBarydy.y = dot(Lv, rd.dOdy); // dv / dy
}

void InterpolateTexCoordDifferentials(float2 dBarydx, float2 dBarydy, UncompressedRawVertexFormat vertices[3], out float2 dx, out float2 dy)
{
    float2 delta1 = vertices[1].m_TexCoord - vertices[0].m_TexCoord;
    float2 delta2 = vertices[2].m_TexCoord - vertices[0].m_TexCoord;
    dx = dBarydx.x * delta1 + dBarydx.y * delta2;
    dy = dBarydy.x * delta1 + dBarydy.y * delta2;
}

struct ComputeUV0DifferentialsArguments
{
    UncompressedRawVertexFormat m_Vertices[3];
    float3 m_RayDirection;
    float m_HitT;
    float3 m_ViewRight;
    float3 m_ViewUp;
    float2 m_ViewportDims;
};

void ComputeUV0Differentials(ComputeUV0DifferentialsArguments args, out float2 dUVdx, out float2 dUVdy)
{
    UncompressedRawVertexFormat vertices[3] = args.m_Vertices;
    float3 rayDirection = args.m_RayDirection;
    float hitT = args.m_HitT;
    float3 viewRight = args.m_ViewRight;
    float3 viewUp = args.m_ViewUp;
    float2 viewportDims = args.m_ViewportDims;
    
    // Initialize a ray differential
    RayDiff rd = (RayDiff) 0;

    // Get ray direction differentials
    ComputeRayDirectionDifferentials(rayDirection, viewRight, viewUp, viewportDims, rd.dDdx, rd.dDdy);

    // Get the triangle edges and face normal
    float3 edge01, edge02, faceNormal;
    PrepVerticesForRayDiffs(vertices, edge01, edge02, faceNormal);

    // Propagate the ray differential to the current hit point
    PropagateRayDiff(rayDirection, hitT, faceNormal, rd);

    // Get the barycentric differentials
    float2 dBarydx, dBarydy;
    ComputeBarycentricDifferentials(rd, rayDirection, edge01, edge02, faceNormal, dBarydx, dBarydy);

    // Interpolate the texture coordinate differentials
    InterpolateTexCoordDifferentials(dBarydx, dBarydy, vertices, dUVdx, dUVdy);
}

struct GetRayHitInstanceGBufferParamsArguments
{
    uint m_InstanceID;
    uint m_PrimitiveIndex;
    float2 m_AttribBarycentrics;
    float3x4 m_ObjectToWorld3x4;
    StructuredBuffer<BasePassInstanceConstants> m_BasePassInstanceConstantsBuffer;
    StructuredBuffer<MaterialData> m_MaterialDataBuffer;
    StructuredBuffer<MeshData> m_MeshDataBuffer;
    StructuredBuffer<uint> m_GlobalIndexIDsBuffer;
    StructuredBuffer<RawVertexFormat> m_GlobalVertexBuffer;
    sampler m_Samplers[SamplerIdx_Count];
};

GBufferParams GetRayHitInstanceGBufferParams(GetRayHitInstanceGBufferParamsArguments inArgs, out float3 rayHitWorldPosition)
{
    uint instanceID = inArgs.m_InstanceID;
    uint primitiveIndex = inArgs.m_PrimitiveIndex;
    float2 attribBarycentrics = inArgs.m_AttribBarycentrics;
    float3x4 objectToWorld3x4 = inArgs.m_ObjectToWorld3x4;
    StructuredBuffer<BasePassInstanceConstants> basePassInstanceConstantsBuffer = inArgs.m_BasePassInstanceConstantsBuffer;
    StructuredBuffer<MaterialData> materialDataBuffer = inArgs.m_MaterialDataBuffer;
    StructuredBuffer<MeshData> meshDataBuffer = inArgs.m_MeshDataBuffer;
    StructuredBuffer<uint> globalIndexIDsBuffer = inArgs.m_GlobalIndexIDsBuffer;
    StructuredBuffer<RawVertexFormat> globalVertexBuffer = inArgs.m_GlobalVertexBuffer;
    sampler samplers[SamplerIdx_Count] = inArgs.m_Samplers;
    
    BasePassInstanceConstants instanceConsts = basePassInstanceConstantsBuffer[instanceID];
    MaterialData materialData = materialDataBuffer[instanceConsts.m_MaterialDataIdx];
    
    // TODO: pick appropriate mesh LOD?
    MeshData meshData = meshDataBuffer[instanceConsts.m_MeshDataIdx];
        
    uint indices[3] =
    {
        globalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 0],
        globalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 1],
        globalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 2],
    };
    
    RawVertexFormat vertices[3] =
    {
        globalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[0]],
        globalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[1]],
        globalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[2]],
    };
    
    float3 barycentrics = { (1.0f - attribBarycentrics.x - attribBarycentrics.y), attribBarycentrics.x, attribBarycentrics.y };
    UncompressedRawVertexFormat v = InterpolateVertex(vertices, barycentrics);
    
    rayHitWorldPosition = mul(objectToWorld3x4, float4(v.m_Position, 1.0f)).xyz;
    
    GetCommonGBufferParamsArguments getCommonGBufferParamsArguments;
    getCommonGBufferParamsArguments.m_TexCoord = v.m_TexCoord;
    getCommonGBufferParamsArguments.m_WorldPosition = rayHitWorldPosition;
    getCommonGBufferParamsArguments.m_Normal = v.m_Normal;
    getCommonGBufferParamsArguments.m_MaterialData = materialData;
    getCommonGBufferParamsArguments.m_Samplers = samplers;
    
    return GetCommonGBufferParams(getCommonGBufferParamsArguments);
}

GBufferParams GetRayHitInstanceGBufferParams(GetRayHitInstanceGBufferParamsArguments inArgs)
{
    float3 rayHitWorldPosition;
    return GetRayHitInstanceGBufferParams(inArgs, rayHitWorldPosition);
}
