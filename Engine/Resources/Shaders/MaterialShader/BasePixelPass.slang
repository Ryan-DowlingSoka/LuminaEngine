#include "Includes/SurfaceShading.slang"

// Forward base-pass pixel shader. The fragment input is the shared FBaseVertexOutput (MeshletGeometry.slang),
// produced identically by the VS-emulation and mesh-shader geometry paths. All lighting lives in
// ShadeSurface (SurfaceShading.slang), shared with the deferred VisBuffer material pass so the two can't
// drift; this file only reconstructs the material-graph locals, runs the pixel graph, and packs output.

struct PSOutput
{
    #ifdef TRANSLUCENT

    float4  Accum       : SV_Target0;
    float   Revealage   : SV_Target1;
    uint    Picker      : SV_Target2;

    #else

    float4 Color        : SV_Target0;
    uint Picker         : SV_Target1;

    #endif
};

// Masked materials run the alpha-test `discard` (inside ShadeSurface), which MUST execute before the depth
// write -- so the masked variant omits early depth tests. With [earlydepthstencil] the depth write happens
// before the shader, so a discarded cutout texel would still write depth (corrupting holes for fog, the HZB,
// decals, etc.). Opaque/non-masked keeps forced early-Z over the heavy PBR shader; its discard branch is
// never taken at runtime, so the early write is always correct.
#ifndef MASKED
[earlydepthstencil]
#endif
[shader("fragment")]
PSOutput FragmentMain(FBaseVertexOutput Input)
{
    // Per-instance constants come in flat from the VS -- no per-pixel 128B FGPUInstance global load.
    // These locals are the names the material pixel graph codegen reads.
    uint EntityID           = Input.EntityID;
    uint MaterialIndex      = Input.MaterialIndex;
    bool bReceiveShadow     = (Input.InstanceFlags & (uint)EInstanceFlags::ReceiveShadow) != 0u;
    float3 WorldNormal      = normalize(Input.NormalWS.xyz);
    float3 ViewPosition     = Input.FragPos.xyz;
    float3 WorldPosition    = mul(GetInverseCameraView(), float4(Input.FragPos.xyz, 1.0)).xyz;
    float2 UV0              = Input.UV;
    float4 VertexColor      = Input.FragColor;

    $MATERIAL_INPUTS

    FShadedSurface Shaded = ShadeSurface(Input, Material);

    PSOutput Output;

#ifdef TRANSLUCENT

    float AlphaFactor = min(1.0, Shaded.Alpha * 10.0) + 0.01;
    float DepthFactor = 1.0 - Input.Position.z * 0.9;
    float Weight = clamp(AlphaFactor * AlphaFactor * AlphaFactor * 1e8 * DepthFactor * DepthFactor * DepthFactor, 1e-2, 3e3);
    Output.Accum     = float4(Shaded.Color.rgb * Shaded.Alpha, Shaded.Alpha) * Weight;
    Output.Revealage = Shaded.Alpha;

#else

    Output.Color = float4(Shaded.Color, Shaded.Alpha);

#endif // TRANSLUCENT

    Output.Picker = EntityID;
    return Output;
}
