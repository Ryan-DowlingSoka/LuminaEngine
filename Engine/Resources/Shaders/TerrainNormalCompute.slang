// Standalone compute shader — intentionally does NOT include SceneGlobals.slang.
// Pulling in scene bindings forces the compute pipeline layout to declare the
// scene + texture-manager sets (so NormalLayout lands at set 2), and the
// implicit uHDRSceneColor sampler in that set gets transitioned to SHADER_READ
// every dispatch, which collides with later LoadOp::Load color-attachment uses
// of the same image (sync hazard under the Billboard pass). Keeping the shader
// standalone lets NormalLayout live alone at set 0.

// Matches FTerrainNormalParams in TerrainRenderTypes.h.
struct FTerrainNormalParams
{
    int     Resolution;
    int     RegionMinX;     // Inclusive.
    int     RegionMinY;
    int     RegionSizeX;

    int     RegionSizeY;
    float   TileWorldSize;
    float   MaxHeight;
    int     _Pad;
};

[[vk::binding(0, 0)]] Sampler2D                            uHeightmap;
[[vk::binding(1, 0)]] RWTexture2D<float4>                  uNormalOut;
[[vk::binding(2, 0)]] ConstantBuffer<FTerrainNormalParams> NormalParams;

// Central-difference normals. We sample the heightmap with clamp semantics
// (clamp-to-edge sampler on the RHI side) so border pixels stay stable.
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 DTid : SV_DispatchThreadID)
{
    if ((int)DTid.x >= NormalParams.RegionSizeX || (int)DTid.y >= NormalParams.RegionSizeY)
        return;

    int X = NormalParams.RegionMinX + (int)DTid.x;
    int Y = NormalParams.RegionMinY + (int)DTid.y;

    float InvRes = 1.0 / float(NormalParams.Resolution);
    float Stride = NormalParams.TileWorldSize / float(NormalParams.Resolution - 1);

    float HL = uHeightmap.SampleLevel(float2(float(X - 1) + 0.5, float(Y    ) + 0.5) * InvRes, 0).r;
    float HR = uHeightmap.SampleLevel(float2(float(X + 1) + 0.5, float(Y    ) + 0.5) * InvRes, 0).r;
    float HD = uHeightmap.SampleLevel(float2(float(X    ) + 0.5, float(Y - 1) + 0.5) * InvRes, 0).r;
    float HU = uHeightmap.SampleLevel(float2(float(X    ) + 0.5, float(Y + 1) + 0.5) * InvRes, 0).r;

    float dHdx = (HR - HL) * NormalParams.MaxHeight / (2.0 * Stride);
    float dHdz = (HU - HD) * NormalParams.MaxHeight / (2.0 * Stride);

    // Surface tangents T=(1,dHdx,0), B=(0,dHdz,1). Cross = (-dHdx, 1, -dHdz) * norm.
    float3 Normal = normalize(float3(-dHdx, 1.0, -dHdz));

    // Pack XZ slope into [0,1] — Y is reconstructed in the vertex shader.
    float2 Packed = Normal.xz * 0.5 + 0.5;
    uNormalOut[int2(X, Y)] = float4(Packed.x, Packed.y, 0.0, 1.0);
}
