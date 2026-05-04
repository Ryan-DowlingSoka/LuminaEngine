#include "Includes/SceneGlobals.slang"

// Screen-space ray-marched volumetric lighting. One fullscreen pass; for each
// pixel we walk the camera ray from the near plane to the surface depth and
// accumulate per-light Henyey-Greenstein scatter. Output is additively blended
// into HDR by the host pass.
//
// Lights opt in by setting ELightFlags::Volumetric and a per-light
// VolumetricIntensity. Shadows use the same cascade / spot / point atlas the
// base pass already populates, so anything that casts a shadow also produces
// shadowed god-rays for free.

struct FPushConstants
{
    uint    NumSteps;
    float   MaxDistance;
    float   Density;
    float   Anisotropy;
    uint2   ScreenSize;
    float2  InvScreenSize;
    float   Time;
    float   _Pad0;
    float   _Pad1;
    float   _Pad2;
};

[[vk::push_constant]] FPushConstants PC;

[[vk::binding(0, 2)]] Sampler2D uSceneDepth;

struct PSInput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

// Henyey-Greenstein phase function. g controls how forward-biased the scatter
// is: g near 0 is uniform, g near 1 is heavily forward (volumetric beams),
// negative g is back-scatter (rare in fog, common in clouds with sun behind).
float PhaseHG(float CosTheta, float G)
{
    float G2 = G * G;
    float Denom = 1.0 + G2 - 2.0 * G * CosTheta;
    return (1.0 - G2) / (4.0 * PI * pow(max(Denom, 1e-4), 1.5));
}

// Cheap interleaved-gradient noise; gives a per-pixel jitter that's stable
// under camera motion and sufficient to mask the banding of a 32-step march.
// Adding Time keeps successive frames decorrelated so static jitter doesn't
// look like a fixed dither pattern.
float IGN(float2 ScreenPos, float Time)
{
    float2 P = ScreenPos + float2(5.588238, 5.588238) * frac(Time);
    return frac(52.9829189 * frac(dot(P, float2(0.06711056, 0.00583715))));
}

// Sample the cascaded directional shadow at a march sample point. Reuses the
// same VP / cascade-pick logic as BasePixelPass without normal-offset bias --
// volumetric samples have no surface so slope-driven biasing isn't meaningful;
// a small constant bias is enough to keep the beam off the receiver.
float SampleDirectionalShadowAtPoint(FLight Light, float3 WorldPos, float3 ViewPos)
{
    if (Light.ShadowDataIndex == INDEX_NONE)
    {
        return 1.0;
    }

    int CascadeIndex = 0;
    [unroll]
    for (int i = 0; i < NUM_CASCADES; ++i)
    {
        if (ViewPos.z > uLightData[0].CascadeSplits[i])
        {
            CascadeIndex = i + 1;
        }
    }
    if (CascadeIndex >= NUM_CASCADES)
    {
        return 1.0;
    }

    float4x4 CascadeVP   = GetShadowViewProjection(Light.ShadowDataIndex, CascadeIndex);
    float4   ShadowCoord = mul(CascadeVP, float4(WorldPos, 1.0));
    float3   Proj        = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2   ProjUV      = Proj.xy * 0.5 + 0.5;
    float    Ref         = Proj.z - 0.0005;

    if (any(ProjUV < 0.0) || any(ProjUV > 1.0) || Proj.z < 0.0 || Proj.z > 1.0)
    {
        return 1.0;
    }

    FLightShadow Tile = GetShadowTile(Light.ShadowDataIndex, CascadeIndex);
    float2 AtlasUV = ProjUV * Tile.AtlasUVScale + Tile.AtlasUVOffset;
    return uShadowCascade.SampleCmpLevelZero(AtlasUV, Ref);
}

// Sample a spot light's atlas-stored shadow at a march sample point.
float SampleSpotShadowAtPoint(FLight Light, float3 WorldPos)
{
    if (Light.ShadowDataIndex == INDEX_NONE)
    {
        return 1.0;
    }

    FLightShadow Tile = GetShadowTile(Light.ShadowDataIndex, 0);

    float4x4 ShadowVP   = GetShadowViewProjection(Light.ShadowDataIndex, 0);
    float4   ShadowCoord = mul(ShadowVP, float4(WorldPos, 1.0));
    float3   Proj       = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2   ProjUV     = Proj.xy * 0.5 + 0.5;
    float    Ref        = Proj.z - 0.0005;

    if (Proj.z < 0.0 || Proj.z > 1.0)
    {
        return 1.0;
    }

    float2 SafeUV  = saturate(ProjUV);
    float2 AtlasUV = Tile.AtlasUVOffset + SafeUV * Tile.AtlasUVScale;
    return uShadowAtlas.SampleCmpLevelZero(AtlasUV, Ref);
}

// Per-light scattering contribution at a single march sample. Returns the
// in-scattered radiance the camera ray gathers from this light at point P.
float3 EvaluateScatter(FLight Light, float3 P, float3 ViewDirToCam, float CosThetaPhase)
{
    bool bDirectional = HasFlag(Light.Flags, ELightFlags::Directional);
    bool bSpot        = HasFlag(Light.Flags, ELightFlags::Spot);

    float3 LightColor = UnpackColor(Light.Color).rgb;

    if (bDirectional)
    {
        // For directional lights the in-scatter is uniform along the ray and
        // already cosine-weighted by the phase term passed in.
        float Phase  = PhaseHG(CosThetaPhase, PC.Anisotropy);
        return LightColor * Light.Intensity * Phase;
    }

    // Point / spot: distance attenuation matches the base pass.
    float3 LightToP = P - Light.Position;
    float  Dist     = length(LightToP);
    if (Dist > Light.Radius)
    {
        return float3(0.0);
    }

    float3 LightDirAtP = LightToP / max(Dist, 1e-4);  // From light to sample.
    float3 LtoCam      = -LightDirAtP;                 // From sample toward light.

    // Inverse-square + smoothstep falloff exactly as base pass does.
    float Atten     = 1.0 / max(Dist * Dist, 1e-4);
    float DistRatio = Dist / Light.Radius;
    Atten *= 1.0 - smoothstep(clamp(Light.Falloff, 0.0, 1.0), 1.0, DistRatio);

    float Cone = 1.0;
    if (bSpot)
    {
        float3 SpotForward = normalize(Light.Direction);
        float CosAtP = dot(SpotForward, LtoCam);
        Cone = saturate((CosAtP - Light.Angles.y) / max(Light.Angles.x - Light.Angles.y, 1e-3));
        if (Cone <= 0.0)
        {
            return float3(0.0);
        }
    }

    // Phase function uses angle between view ray and light direction at the
    // sample. For local lights that's the angle between camera-to-sample and
    // light-to-sample, so the bright forward lobe lights up samples whose
    // light direction aligns with the eye direction (the volumetric "halo").
    float CosScatter = dot(LtoCam, ViewDirToCam);
    float Phase      = PhaseHG(CosScatter, PC.Anisotropy);

    return LightColor * Light.Intensity * Atten * Cone * Phase;
}

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    float2 UV = Input.UV;

    // Screen-space depth of the closest opaque surface. Reverse-Z so 0 = far,
    // 1 = near; a sample of 0 means the sky (no surface) and we march to the
    // configured MaxDistance instead.
    float Depth = uSceneDepth.SampleLevel(UV, 0.0).r;

    float3 CameraPos = GetCameraPosition();
    float4x4 InvProj = GetInverseCameraProjection();
    float4x4 InvView = GetInverseCameraView();

    // Reconstruct the surface world position. For sky pixels (Depth == 0)
    // pick a point on the far plane along the camera ray so the beam still
    // accumulates through the atmosphere instead of producing a hard cutoff.
    float SurfaceDepth = Depth > 0.0 ? Depth : 1e-4;
    float3 SurfaceWorld = ReconstructWorldPos(UV, SurfaceDepth, InvProj, InvView);

    float3 RayVec = SurfaceWorld - CameraPos;
    float  RayLen = length(RayVec);
    if (RayLen < 1e-3)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float3 RayDir = RayVec / RayLen;
    float  March  = min(RayLen, PC.MaxDistance);

    int   Steps    = max(1, int(PC.NumSteps));
    float StepSize = March / float(Steps);

    // Per-pixel jitter offsets the first sample along the ray; spreads the
    // discrete march bands across pixels so the eye reads the integral as
    // smooth instead of seeing 32 stripes.
    float Jitter = IGN(Input.Position.xy, PC.Time);
    float t      = StepSize * Jitter;

    float3 ViewDirToCam = -RayDir;  // From sample point toward camera.
    float3 Scattered    = float3(0.0);

    uint NumLights = GetNumLights();

    // Exit early if neither the sun nor any clustered light has the flag --
    // the host pass already does this, but keep the loop tight when one
    // volumetric light is dropped mid-frame.
    [loop]
    for (int s = 0; s < Steps; ++s)
    {
        float3 P = CameraPos + RayDir * t;

        // Sun first (always at index 0 if present). The cascade shadow
        // produces the classic "light shaft" look when sunlight punches
        // through windows / foliage.
        if (uLightData[0].bHasSun != 0)
        {
            FLight Sun = GetLightAt(0);
            if (HasFlag(Sun.Flags, ELightFlags::Volumetric))
            {
                float3 ViewPos = WorldToView(P);
                float  Shadow  = SampleDirectionalShadowAtPoint(Sun, P, ViewPos);
                if (Shadow > 0.0)
                {
                    // For the sun the relevant CosTheta is between camera
                    // ray and sun direction (constant per pixel), so we
                    // could hoist this -- left in-line for clarity since
                    // the inner loop is already 32 lights * 32 steps in
                    // the worst case and the term is cheap.
                    float3 SunDir   = normalize(Sun.Direction);
                    float  CosPhase = dot(SunDir, ViewDirToCam);
                    Scattered += EvaluateScatter(Sun, P, ViewDirToCam, CosPhase) * Shadow * Sun.VolumetricIntensity;
                }
            }
        }

        // Local lights. Walk every active light; volumetric lights are
        // typically a small subset and the branch is uniform-ish per warp.
        for (uint i = 1; i < NumLights; ++i)
        {
            FLight Light = GetLightAt(i);
            if (!HasFlag(Light.Flags, ELightFlags::Volumetric))
            {
                continue;
            }

            float3 LightToP = P - Light.Position;
            float  DistSq   = dot(LightToP, LightToP);
            if (DistSq > Light.Radius * Light.Radius)
            {
                continue;
            }

            float Shadow = 1.0;
            if (Light.ShadowDataIndex != INDEX_NONE)
            {
                if (HasFlag(Light.Flags, ELightFlags::Spot))
                {
                    Shadow = SampleSpotShadowAtPoint(Light, P);
                }
                // Point-light atlas shadow sampling is non-trivial (cube-face
                // pick + per-face VP) and most volumetric setups don't need
                // shadowed point lights. Skipping keeps the loop fast; lights
                // still cast unshadowed scatter, and turning off bCastShadows
                // for a point light makes its volumetric contribution exact.
            }
            if (Shadow <= 0.0)
            {
                continue;
            }

            // CosPhase placeholder; EvaluateScatter computes the real angle
            // for local lights internally.
            Scattered += EvaluateScatter(Light, P, ViewDirToCam, 0.0) * Shadow * Light.VolumetricIntensity;
        }

        t += StepSize;
    }

    // Multiply by step size to convert the Riemann sum into an integral and
    // by Density to expose a single user-facing scattering knob.
    float3 Result = Scattered * StepSize * PC.Density;
    return float4(Result, 0.0);
}
