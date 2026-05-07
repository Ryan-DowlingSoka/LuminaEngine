// Standalone compute shader: does NOT include SceneGlobals.slang.
// Including scene bindings forces scene + texture-manager sets into the
// compute layout, causing uHDRSceneColor to transition to SHADER_READ each
// dispatch and collide with later LoadOp::Load color-attachment uses.
// Keeping standalone lets NormalLayout live alone at set 0.

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

// Pulled from a transient ring slot via buffer device address.
struct FPushConstants
{
    ConstBufferPointer<FTerrainNormalParams> Params;
};
[[vk::push_constant]] FPushConstants PC;

[[vk::binding(0, 0)]] Sampler2D                            uHeightmap;
[[vk::binding(1, 0)]] RWTexture2D<float4>                  uNormalOut;

// Central-difference normals. We sample the heightmap with clamp semantics
// (clamp-to-edge sampler on the RHI side) so border pixels stay stable.
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 DTid : SV_DispatchThreadID)
{
    FTerrainNormalParams Params = PC.Params.get();

    if ((int)DTid.x >= Params.RegionSizeX || (int)DTid.y >= Params.RegionSizeY)
        return;

    int X = Params.RegionMinX + (int)DTid.x;
    int Y = Params.RegionMinY + (int)DTid.y;

    float InvRes = 1.0 / float(Params.Resolution);
    float Stride = Params.TileWorldSize / float(Params.Resolution - 1);

    float HL = uHeightmap.SampleLevel(float2(float(X - 1) + 0.5, float(Y    ) + 0.5) * InvRes, 0).r;
    float HR = uHeightmap.SampleLevel(float2(float(X + 1) + 0.5, float(Y    ) + 0.5) * InvRes, 0).r;
    float HD = uHeightmap.SampleLevel(float2(float(X    ) + 0.5, float(Y - 1) + 0.5) * InvRes, 0).r;
    float HU = uHeightmap.SampleLevel(float2(float(X    ) + 0.5, float(Y + 1) + 0.5) * InvRes, 0).r;

    float dHdx = (HR - HL) * Params.MaxHeight / (2.0 * Stride);
    float dHdz = (HU - HD) * Params.MaxHeight / (2.0 * Stride);

    // Surface tangents T=(1,dHdx,0), B=(0,dHdz,1). Cross = (-dHdx, 1, -dHdz) * norm.
    float3 Normal = normalize(float3(-dHdx, 1.0, -dHdz));

    // Pack XZ slope into [0,1]; Y is reconstructed in the vertex shader.
    float2 Packed = Normal.xz * 0.5 + 0.5;
    uNormalOut[int2(X, Y)] = float4(Packed.x, Packed.y, 0.0, 1.0);
}
