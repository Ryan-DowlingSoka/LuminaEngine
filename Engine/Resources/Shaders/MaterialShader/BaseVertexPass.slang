#include "Includes/SceneGlobals.slang"


struct VSOutput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    // xyz: world-space tangent; w: MikkTSpace handedness for B = cross(N, T) * w.
    // Re-orthogonalized against NormalWS per-pixel to absorb non-uniform-scale skew.
    float4 TangentWS                : TANGENT_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;

    uint InstanceIndex              : INSTANCE;
    // Per-instance constants forwarded flat so the pixel shader never re-reads the 128B FGPUInstance per pixel.
    nointerpolation uint EntityID      : ENTITY_ID;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
    nointerpolation uint InstanceFlags : INSTANCE_FLAGS;
    nointerpolation uint MeshletIndex : MESHLET_INDEX;

    // Position math must match DepthPrePass.slang for the equal-depth test.
    precise float4 Position         : SV_Position;
};

[shader("vertex")]
VSOutput VertexMain(uint VertexIndex: SV_VulkanVertexID, uint MeshletSlot: SV_VulkanInstanceID)
{
    FMeshletDraw Draw = MeshletDrawList()[MeshletSlot];
    FGPUInstance Inst = GetInstance(Draw.InstanceID);

    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet M = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    VSOutput Output;
    Output.InstanceIndex = Draw.InstanceID;
    Output.EntityID      = Inst.EntityID;
    Output.MaterialIndex = Inst.MaterialIndex;
    Output.InstanceFlags = (uint)Inst.Flags;
    Output.MeshletIndex  = GlobalMeshletIdx;

    if (TriIdx >= M.TriangleCount)
    {
        const float NaN     = 0.0f / 0.0f;
        Output.Position     = float4(NaN, NaN, NaN, 1.0);
        Output.UV           = float2(0, 0);
        Output.FragPos      = float4(0, 0, 0, 0);
        Output.NormalVS     = float4(0, 0, 0, 0);
        Output.NormalWS     = float4(0, 0, 0, 0);
        Output.TangentWS    = float4(0, 0, 0, 0);
        Output.FragColor    = float4(0, 0, 0, 0);
        return Output;
    }

    uint PackedTri    = Inst.MeshletHeader.Triangles[M.TriangleOffset + TriIdx];
    uint LocalVertIdx = (PackedTri >> (Corner * 8u)) & 0xFFu;

    FVertexData VertexData;
    if (Inst.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertex(M, LocalVertIdx, Inst.SkinnedVertexBase);
    }
    else
    {
        VertexData = LoadStaticVertex(Inst.MeshletHeader, M, LocalVertIdx);
    }

    float4x4 ModelMatrix = Inst.ModelMatrix;
    float4x4 View        = GetCameraView();
    float4x4 Projection  = GetCameraProjection();

    float4 WorldPos        = mul(ModelMatrix, float4(VertexData.Position, 1.0));
    float3x3 NormalMatrixWS = CalculateNormalMatrix(ModelMatrix);
    float3 NormalWS         = mul(NormalMatrixWS, VertexData.Normal);
    float3 NormalVS         = normalize(mul((float3x3)View, NormalWS));

    // Tangents take the forward 3x3 (not the normal's inverse-transpose): they lie in the surface.
    // Re-orthogonalized per-pixel against NormalWS.
    float3 TangentWS        = mul((float3x3)ModelMatrix, VertexData.Tangent.xyz);

    // Material vertex graph emission (e.g. WorldPositionOffset); zero-init default leaves geometry unchanged.
    FMaterialVertexInputs Material;
    $MATERIAL_VERTEX_INPUTS
    WorldPos.xyz += Material.WorldPositionOffset;

    float4 ViewPos = mul(View, WorldPos);

    Output.UV            = VertexData.UV;
    Output.FragPos       = ViewPos;
    Output.NormalVS      = float4(NormalVS, 1.0);
    Output.NormalWS      = float4(NormalWS, 1.0);
    Output.TangentWS     = float4(TangentWS, VertexData.Tangent.w);
    Output.FragColor     = VertexData.Color;
    Output.Position      = mul(Projection, ViewPos);

    return Output;
}
