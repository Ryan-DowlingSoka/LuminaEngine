#pragma once
#include "SceneGlobals.slang"

// Shadow atlases are reached bindlessly through the scene root (set 3 is gone). The cascade and
// spot/point atlases are 2D SRVs in the bindless table; comparison sampling uses the comparison
// alias of the bindless sampler array, point sampling (PCSS blocker) uses a point-clamp sampler.

struct FShadowTex
{
    Texture2D              Tex;
    SamplerComparisonState Cmp;
    SamplerState           Pt;

    float  SampleCmp(float2 UV, float Ref)          { return Tex.SampleCmp(Cmp, UV, Ref); }
    float  SampleCmpLevelZero(float2 UV, float Ref) { return Tex.SampleCmpLevelZero(Cmp, UV, Ref); }
    float4 SampleLevel(float2 UV, float Lod)        { return Tex.SampleLevel(Pt, UV, Lod); }
    void   GetDimensions(out uint W, out uint H)    { Tex.GetDimensions(W, H); }
};

FShadowTex MakeShadowTex(uint Index)
{
    FShadowTex S;
    S.Tex = uBindlessTex2D[Index];
    S.Cmp = uBindlessCmpSamplers[SAMPLER_SHADOW];
    S.Pt  = uBindlessSamplers[SAMPLER_POINT_CLAMP];
    return S;
}

#define uShadowCascade      MakeShadowTex(gPC.Root.get().ShadowCascadeIndex)
#define uShadowAtlas        MakeShadowTex(gPC.Root.get().ShadowAtlasIndex)
#define uShadowCascadePoint MakeShadowTex(gPC.Root.get().ShadowCascadeIndex)
