#include "Includes/UIMaterialGlobals.slang"

// UI material template. Runs as a fullscreen pass into an offscreen brush
// texture that RmlUi then samples (Unreal "User Interface" material domain).
// Color comes from Material.Emissive, alpha from Material.Opacity.
//
// Output is premultiplied (Emissive * Opacity) because RmlUi composites with a
// premultiplied-alpha blend (srcRGB = One, dstRGB = OneMinusSrcAlpha). Emitting
// straight alpha would add the full color regardless of Opacity, so the alpha
// would have no visible effect over a dark background.

struct PSInput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    uint   MaterialIndex = uSceneData.MaterialIndex;
    float2 UV0           = Input.UV;

    // Surface aliases. Zeroed because there is no rasterized geometry feeding
    // this pass; node-emit code may still reference them.
    float3 WorldPosition = float3(0.0);
    float3 WorldNormal   = float3(0.0, 0.0, 1.0);
    float3 ViewPosition  = float3(0.0);
    float4 VertexColor   = float4(1.0);
    uint   EntityID      = 0;

    $MATERIAL_INPUTS

    return float4(Material.Emissive * Material.Opacity, Material.Opacity);
}
