#include "Includes/SceneGlobals.slang"
#include "Includes/MeshletPayload.slang"

// Mesh-shader counterpart to BaseVertexPass.slang. Produces the exact same
// MSOutput-per-vertex payload so BasePixelPass.slang can consume either path
// unchanged. The (InstanceID, MeshletLocalIndex) tuple comes from the
// amplification task payload instead of uMeshletDrawList.

struct MSOutput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;

    uint InstanceIndex              : INSTANCE;
    nointerpolation uint MeshletIndex : MESHLET_INDEX;

    // IMPORTANT: position math must stay in lockstep with BaseVertexPass.slang
    // and DepthPrePass.slang; diverging produces Z-fighting between the two
    // paths on hybrid fallbacks.
    precise float4 Position         : SV_Position;
};

// Workgroup has to cover max(vertCount, triCount). MESHLET_MAX_VERTICES == 64
// and MESHLET_MAX_TRIANGLES == 124, so 128 threads lets each thread cover one
// slot of each on its single pass without any striding loop. The unused
// 128-vertex lanes idle; that's still a win vs the VS emulation path which
// burned 248 padding invocations per meshlet (372 - 124).
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

    // Cache transform lookups in registers so the compiler can hoist the
    // structured buffer fetches out of the predicate body.
    float4x4 ModelMatrix = Inst.ModelMatrix;
    float4x4 View        = GetCameraView();
    float4x4 Projection  = GetCameraProjection();
    float3x3 NormalMatrixWS = CalculateNormalMatrix(ModelMatrix);
    bool     bSkinned    = Inst.HasFlag(EInstanceFlags::Skinned);

    // Vertex lane: first VertexCount threads each emit one vertex.
    if (ThreadID < M.VertexCount)
    {
        uint VertexIdx = Inst.MeshletHeader.Vertices[M.VertexOffset + ThreadID];

        FVertexData VertexData;
        if (bSkinned)
        {
            VertexData = LoadSkinnedVertexByIndex(Inst.VBAddress, VertexIdx, Inst.BoneOffset);
        }
        else
        {
            VertexData = LoadStaticVertexByIndex(Inst.VBAddress, VertexIdx);
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

    // Primitive lane: first TriangleCount threads each emit one triangle.
    // Triangles are packed as 4x uint8 (three local corner indices + pad) per
    // uint32: pull the packed word, slice the three bytes, write the prim.
    if (ThreadID < M.TriangleCount)
    {
        uint PackedTri = Inst.MeshletHeader.Triangles[M.TriangleOffset + ThreadID];
        uint I0 =  PackedTri        & 0xFFu;
        uint I1 = (PackedTri >>  8) & 0xFFu;
        uint I2 = (PackedTri >> 16) & 0xFFu;
        OutTriangles[ThreadID] = uint3(I0, I1, I2);
    }
}
