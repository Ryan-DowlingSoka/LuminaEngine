#include "Includes/SceneGlobals.slang"


struct VSOutput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;

    // Absolute index into uInstance. The meshlet path resolves this from
    // uMeshletDrawList so the pixel shader can skip the DrawID mapping.
    uint InstanceIndex              : INSTANCE;

    // Global meshlet index (Inst.SurfaceMeshletOffset + Draw.MeshletLocalIndex).
    // Consumed by the base pixel shader when r.MeshletDebug is on.
    nointerpolation uint MeshletIndex : MESHLET_INDEX;

    // IMPORTANT: Changes to position calculations must be exactly
    // reflected in DepthPrePass.slang
    precise float4 Position         : SV_Position;
};

// ============================================================================
//  Main
// ============================================================================
//
// Meshlet-driven draw. Each indirect-draw "instance" (SV_VulkanInstanceID) is a
// slot in uMeshletDrawList produced by MeshletCull.slang, and VertexIndex runs
// 0..MESHLET_VERTICES_PER_DRAW-1 (124 tris * 3). Out-of-range invocations emit
// a degenerate triangle (NaN position) so the rasterizer culls them.

[shader("vertex")]
VSOutput VertexMain(uint VertexIndex: SV_VulkanVertexID, uint MeshletSlot: SV_VulkanInstanceID)
{
    FMeshletDraw Draw = uMeshletDrawList[MeshletSlot];
    FGPUInstance Inst = GetInstance(Draw.InstanceID);

    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet       M = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];
    FMeshletBounds B = Inst.MeshletHeader.Bounds[GlobalMeshletIdx];

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    VSOutput Output;
    Output.InstanceIndex = Draw.InstanceID;
    Output.MeshletIndex  = GlobalMeshletIdx;

    if (TriIdx >= M.TriangleCount)
    {
        // Pad invocations, collapse to NaN so the triangle is discarded.
        const float NaN     = 0.0f / 0.0f;
        Output.Position     = float4(NaN, NaN, NaN, 1.0);
        Output.UV           = float2(0, 0);
        Output.FragPos      = float4(0, 0, 0, 0);
        Output.NormalVS     = float4(0, 0, 0, 0);
        Output.NormalWS     = float4(0, 0, 0, 0);
        Output.FragColor    = float4(0, 0, 0, 0);
        return Output;
    }

    // Triangles are packed as 3x uint8 micro-indices per dword. The local
    // index addresses the meshlet's private packed-vertex slice; loaders
    // dequantize position via the meshlet AABB and decode the oct normal.
    uint PackedTri    = Inst.MeshletHeader.Triangles[M.TriangleOffset + TriIdx];
    uint LocalVertIdx = (PackedTri >> (Corner * 8u)) & 0xFFu;

    FVertexData VertexData;
    if (Inst.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertex(Inst.MeshletHeader, M, B, LocalVertIdx, Inst.BoneOffset);
    }
    else
    {
        VertexData = LoadStaticVertex(Inst.MeshletHeader, M, B, LocalVertIdx);
    }

    float4x4 ModelMatrix = Inst.ModelMatrix;
    float4x4 View        = GetCameraView();
    float4x4 Projection  = GetCameraProjection();

    float4 WorldPos        = mul(ModelMatrix, float4(VertexData.Position, 1.0));
    float3x3 NormalMatrixWS = CalculateNormalMatrix(ModelMatrix);
    float3 NormalWS         = mul(NormalMatrixWS, VertexData.Normal);
    float3 NormalVS         = normalize(mul((float3x3)View, NormalWS));

    float4 ViewPos = mul(View, WorldPos);

    Output.UV            = VertexData.UV;
    Output.FragPos       = ViewPos;
    Output.NormalVS      = float4(NormalVS, 1.0);
    Output.NormalWS      = float4(NormalWS, 1.0);
    Output.FragColor     = VertexData.Color;
    Output.Position      = mul(Projection, ViewPos);

    return Output;
}
