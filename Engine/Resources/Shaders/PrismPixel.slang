#pragma once

struct FPrismPush
{
    float2 InvScreenSize;
    float2 _Pad;
};

[[vk::push_constant]] ConstantBuffer<FPrismPush> uPush;

// Bindless sampler table shared with the rest of the engine. UI textures
// register with FTextureManager and their index is baked into each vertex.
[[vk::binding(0, 0)]] Sampler2D uGlobalTextures[];

// Primitive type tags, must match EPrismPrimType in PrismTessellator.h.
static const uint PRIM_RECT   = 0u;
static const uint PRIM_BORDER = 1u;
static const uint PRIM_IMAGE  = 2u;
static const uint PRIM_LINE   = 3u;
static const uint PRIM_TEXT   = 4u;

struct VSOutput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float4 Color     : COLOR0;
    float4 ClipRect  : TEXCOORD1;
    float2 ScreenPos : TEXCOORD2;
    float2 LocalSize : TEXCOORD3;
    float4 Params    : TEXCOORD4;
};

// Signed distance to a rounded box centred at the origin.
float SdfRoundedBox(float2 P, float2 HalfSize, float Radius)
{
    float2 Q = abs(P) - HalfSize + Radius;
    return length(max(Q, 0.0)) + min(max(Q.x, Q.y), 0.0) - Radius;
}

[shader("fragment")]
float4 FragmentMain(VSOutput Input) : SV_Target0
{
    // Per-pixel scissor. The tessellator bakes each primitive's clip rect
    // into every vertex, so distant batches can share a single pipeline /
    // scissor state and we get per-element clipping for free.
    float2 P = Input.ScreenPos;
    if (P.x < Input.ClipRect.x || P.y < Input.ClipRect.y ||
        P.x > Input.ClipRect.z || P.y > Input.ClipRect.w)
    {
        discard;
    }

    const float  CornerRadius    = Input.Params.x;
    const float  BorderThickness = Input.Params.y;
    const uint   Type            = uint(Input.Params.z);
    const uint   TextureIndex    = uint(Input.Params.w);

    float4 Color = Input.Color;

    if (Type == PRIM_RECT || Type == PRIM_BORDER)
    {
        // SDF'd rounded rectangle / outlined rectangle. The quad covers the
        // full primitive footprint, so we reconstruct the local-space point
        // from the interpolated UV (0..1 across the quad).
        const float2 HalfSize = Input.LocalSize * 0.5;
        const float2 LocalP   = (Input.UV - 0.5) * Input.LocalSize;
        float        D        = SdfRoundedBox(LocalP, HalfSize, CornerRadius);

        if (Type == PRIM_BORDER)
        {
            // Outline: keep pixels whose SDF distance is within [-Thickness, 0].
            D = abs(D + BorderThickness * 0.5) - BorderThickness * 0.5;
        }

        // 1px antialiased edge.
        float Alpha = saturate(0.5 - D);
        Color.a    *= Alpha;
    }
    else if (Type == PRIM_IMAGE)
    {
        float4 Tex = uGlobalTextures[TextureIndex].Sample(Input.UV);
        Color *= Tex;
    }
    else if (Type == PRIM_TEXT)
    {
        // Glyph atlas sample. Alpha channel of the atlas encodes coverage.
        float  Coverage = uGlobalTextures[TextureIndex].Sample(Input.UV).r;
        Color.a *= Coverage;
    }
    // PRIM_LINE falls through to a straight coloured quad.

    if (Color.a <= 0.0)
    {
        discard;
    }
    return Color;
}
