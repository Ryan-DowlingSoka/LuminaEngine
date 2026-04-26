#include "Includes/SceneGlobals.slang"
#include "Includes/MeshletPayload.slang"

// Mesh-shader counterpart to BaseVertexPass.slang.

struct MSOutput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;

    uint InstanceIndex              : INSTANCE;
    nointerpolation uint MeshletIndex : MESHLET_INDEX;

    // Position math must match BaseVertexPass / DepthPrePass.
    precise float4 Position         : SV_Position;
};

// 128 threads = max(MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES) rounded up,
// so vertex and primitive lanes both fit without striding.
#define MESH_GROUP_SIZE 128

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_GROUP_SIZE, 1, 1)]
void MeshMain(uint GroupID   : SV_GroupID,
              uint ThreadID  : SV_GroupIndex,
              in payload FMeshTaskPayload Payload,
              out indices uint3 OutTriangles[MESHLET_MAX_TRIANGLES],
              out vertices MSOutput OutVertices[MESHLET_MAX_VERTICES])
{
    uint InstanceID       = Payload.InstanceID;
    uint MeshletLocalIdx  = Payload.MeshletLocalIndices[GroupID];
    uint GlobalMeshletIdx = Payload.GlobalMeshletBase + MeshletLocalIdx;

    FGPUInstance Inst = GetInstance(InstanceID);

    FMeshlet M = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];

    SetMeshOutputCounts(M.VertexCount, M.TriangleCount);

    // Hoist uniforms outside the predicate.
    float4x4 ModelMatrix    = Inst.ModelMatrix;
    float4x4 View           = GetCameraView();
    float4x4 Projection     = GetCameraProjection();
    float3x3 NormalMatrixWS = CalculateNormalMatrix(ModelMatrix);
    bool     bSkinned       = Inst.HasFlag(EInstanceFlags::Skinned);

    if (ThreadID < M.VertexCount)
    {
        FVertexData VertexData;
        if (bSkinned)
        {
            VertexData = LoadSkinnedVertex(Inst.MeshletHeader, M, ThreadID, Inst.BoneOffset);
        }
        else
        {
            VertexData = LoadStaticVertex(Inst.MeshletHeader, M, ThreadID);
        }

        float4 WorldPos  = mul(ModelMatrix, float4(VertexData.Position, 1.0));
        float3 NormalWS  = mul(NormalMatrixWS, VertexData.Normal);
        float3 NormalVS  = normalize(mul((float3x3)View, NormalWS));
        float4 ViewPos   = mul(View, WorldPos);

        MSOutput V;
        V.FragColor      = VertexData.Color;
        V.NormalVS       = float4(NormalVS, 1.0);
        V.NormalWS       = float4(NormalWS, 1.0);
        V.FragPos        = ViewPos;
        V.UV             = VertexData.UV;
        V.InstanceIndex  = InstanceID;
        V.MeshletIndex   = GlobalMeshletIdx;
        V.Position       = mul(Projection, ViewPos);

        OutVertices[ThreadID] = V;
    }

    if (ThreadID < M.TriangleCount)
    {
        uint PackedTri = Inst.MeshletHeader.Triangles[M.TriangleOffset + ThreadID];
        OutTriangles[ThreadID] = uint3(PackedTri & 0xFFu, (PackedTri >> 8) & 0xFFu, (PackedTri >> 16) & 0xFFu);
    }
}
