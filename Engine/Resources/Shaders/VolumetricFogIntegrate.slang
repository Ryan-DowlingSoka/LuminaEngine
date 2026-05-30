#include "Includes/Froxel.slang"

// Froxel volumetric fog integrate pass: one thread per (x,y) column marches Z front-to-back,
// accumulating transmittance-weighted in-scatter and writing (accum rgb, transmittance) per froxel.

struct FPushConstants
{
    uint3 GridSize;
    float NearPlane;
    float FogRange;
    float _Pad[3];
};
[[vk::push_constant]] FPushConstants PC;

[[vk::binding(0, 0)]] Texture3D<float4>   uScatter;       // (in-scatter rgb, extinction)
[[vk::binding(1, 0)]] RWTexture3D<float4> uIntegrated;    // (accum in-scatter rgb, transmittance)

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 DispatchID : SV_DispatchThreadID)
{
    if (DispatchID.x >= PC.GridSize.x || DispatchID.y >= PC.GridSize.y)
    {
        return;
    }

    float3 Accum = float3(0.0);
    float  Trans = 1.0;

    for (uint z = 0; z < PC.GridSize.z; ++z)
    {
        float4 S      = uScatter.Load(int4(int(DispatchID.x), int(DispatchID.y), int(z), 0));
        float  SigmaT = max(S.a, 0.0);

        float DNear = Froxel_SliceDistance(float(z),     float(PC.GridSize.z), PC.NearPlane, PC.FogRange);
        float DFar  = Froxel_SliceDistance(float(z + 1), float(PC.GridSize.z), PC.NearPlane, PC.FogRange);
        float Dz    = max(DFar - DNear, 0.0);

        float  SliceTrans = exp(-SigmaT * Dz);
        // Analytic integral of in-scatter across the slice (energy-conserving).
        float3 SliceInt   = (SigmaT > 1e-5) ? (S.rgb - S.rgb * SliceTrans) / SigmaT : S.rgb * Dz;

        Accum += Trans * SliceInt;
        Trans *= SliceTrans;

        uIntegrated[int3(int(DispatchID.x), int(DispatchID.y), int(z))] = float4(Accum, Trans);
    }
}
