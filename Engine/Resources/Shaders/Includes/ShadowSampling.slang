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
    S.Tex = gTextures2D[Index];
    S.Cmp = gCmpSamplers[SAMPLER_SHADOW];
    S.Pt  = gSamplers[SAMPLER_POINT_CLAMP];
    return S;
}

FShadowTex ShadowCascade() { return MakeShadowTex(GetSceneRoot().ShadowCascadeIndex); }
FShadowTex ShadowAtlas()   { return MakeShadowTex(GetSceneRoot().ShadowAtlasIndex); }
