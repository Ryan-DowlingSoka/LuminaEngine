#include "Includes/SceneGlobals.slang"


struct VSOutput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;

    // Absolute index into uInstanceData. The meshlet path resolves this from
    // uMeshletDrawList so the pixel shader can skip the DrawID mapping.
    uint InstanceIndex              : INSTANCE;

    // Global meshlet index (InstanceData.SurfaceMeshletOffset + Draw.MeshletLocalIndex).
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
    FMeshletDraw Draw         = uMeshletDrawList[MeshletSlot];
    FGPUInstance InstanceData = GetInstanceDataByID(Draw.InstanceID);

    uint GlobalMeshletIdx = InstanceData.SurfaceMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet M            = InstanceData.MeshletHeader.Meshlets[GlobalMeshletIdx];

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    VSOutput Output;
    Output.InstanceIndex = Draw.InstanceID;
    Output.MeshletIndex  = GlobalMeshletIdx;

    if (TriIdx >= M.TriangleCount)
    {
        // Pad invocations — collapse to NaN so the triangle is discarded.
        const float NaN     = 0.0f / 0.0f;
        Output.Position     = float4(NaN, NaN, NaN, 1.0);
        Output.UV           = float2(0, 0);
        Output.FragPos      = float4(0, 0, 0, 0);
        Output.NormalVS     = float4(0, 0, 0, 0);
        Output.NormalWS     = float4(0, 0, 0, 0);
        Output.FragColor    = float4(0, 0, 0, 0);
        return Output;
    }

    // Triangles are packed as 4x uint8 (three corners + pad) per uint32. The
    // meshlet's Vertices array then remaps the local index to the global VBO.
    uint PackedTri    = InstanceData.MeshletHeader.Triangles[M.TriangleOffset + TriIdx];
    uint LocalVertIdx = (PackedTri >> (Corner * 8u)) & 0xFFu;
    uint VertexIdx    = InstanceData.MeshletHeader.Vertices[M.VertexOffset + LocalVertIdx];

    FVertexData VertexData;
    if (InstanceData.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertexByIndex(InstanceData.VBAddress, VertexIdx, InstanceData.BoneOffset);
    }
    else
    {
        VertexData = LoadStaticVertexByIndex(InstanceData.VBAddress, VertexIdx);
    }

    float4x4 ModelMatrix = InstanceData.ModelMatrix;
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
