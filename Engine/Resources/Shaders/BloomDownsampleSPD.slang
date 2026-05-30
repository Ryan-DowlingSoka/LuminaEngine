#pragma once
#include "Includes/Common.slang"
#include "Includes/SceneGlobals.slang"

// Single-pass bloom downsampler: one dispatch builds the whole mip chain (daxa gen_hiz pattern,
// like DepthPyramidSPD). Bloom needs only 5 mips so phase 1 alone suffices, no atomic/last-WG hand-off.
// Bindless: HDR via uBindlessTex2D[HDRIndex], per-mip writes via uBindlessRWTex2D[MipUAV[i]].

#define TILE_THREAD_WIDTH 16
#define TILE_THREAD_COUNT (TILE_THREAD_WIDTH * TILE_THREAD_WIDTH)

#define BLOOM_MAX_MIPS 5

struct FPushConstants
{
    uint2  PyramidSize;     // bloom mip 0 size = HDR / 2.
    uint   NumMips;         // number of bloom mips to write (1..5).
    uint   HDRIndex;        // bindless SRV index for HDR source.

    float2 InvHDRSize;      // 1 / HDR resolution (used for HDR tap offsets).
    float  Threshold;       // Same value as KneeCurve.x + Knee.
    float  _Pad0;

    float3 KneeCurve;       // (T - Knee, 2*Knee, 0.25/Knee). See QuadraticThreshold.
    float  _Pad1;

    uint   MipUAV[BLOOM_MAX_MIPS];
};

[[vk::push_constant]] FPushConstants PC;

// Ping-pong LDS like DepthPyramidSPD: alternating reductions write to disjoint
// halves so no slot is read+written across a barrier.
groupshared float3 SpdLDS[2 * TILE_THREAD_COUNT];

float3 Reduce4(float3 a, float3 b, float3 c, float3 d)
{
    return (a + b + c + d) * 0.25;
}

void StoreMip(uint Mip, int2 Pos, float3 V)
{
    uBindlessRWTex2D[PC.MipUAV[Mip]][Pos] = float4(V, 0);
}

float3 SampleHDR(float2 UV)
{
    return uBindlessTex2D[PC.HDRIndex].SampleLevel(
        uBindlessSamplers[SAMPLER_LINEAR_CLAMP], UV, 0.0).rgb;
}

float3 QuadraticThreshold(float3 Color, float Threshold, float3 Curve)
{
    float Br  = max(max(Color.r, Color.g), Color.b);
    float Rq  = clamp(Br - Curve.x, 0.0, Curve.y);
    Rq        = (Rq * Rq) * Curve.z;
    float Mul = max(Rq, Br - Threshold) / max(Br, 1e-4);
    return Color * Mul;
}

float KarisWeight(float3 Color)
{
    float Luma = dot(Color, float3(0.2126, 0.7152, 0.0722)) * 0.25;
    return 1.0 / (1.0 + Luma);
}

// Karis-weighted 13-tap downsample of HDR with soft-knee threshold. UV is the
// destination mip 0 UV (== sample point on HDR thanks to the half-res mapping).
float3 PrefilterHDR(float2 UV, float2 T, float Threshold, float3 KneeCurve)
{
    float3 a = SampleHDR(UV + T * float2(-2.0, -2.0));
    float3 b = SampleHDR(UV + T * float2( 0.0, -2.0));
    float3 c = SampleHDR(UV + T * float2( 2.0, -2.0));

    float3 d = SampleHDR(UV + T * float2(-2.0,  0.0));
    float3 e = SampleHDR(UV + T * float2( 0.0,  0.0));
    float3 f = SampleHDR(UV + T * float2( 2.0,  0.0));

    float3 g = SampleHDR(UV + T * float2(-2.0,  2.0));
    float3 h = SampleHDR(UV + T * float2( 0.0,  2.0));
    float3 i = SampleHDR(UV + T * float2( 2.0,  2.0));

    float3 j = SampleHDR(UV + T * float2(-1.0, -1.0));
    float3 k = SampleHDR(UV + T * float2( 1.0, -1.0));
    float3 l = SampleHDR(UV + T * float2(-1.0,  1.0));
    float3 m = SampleHDR(UV + T * float2( 1.0,  1.0));

    float3 Q0 = (j + k + l + m) * 0.25;
    float3 Q1 = (a + b + d + e) * 0.25;
    float3 Q2 = (b + c + e + f) * 0.25;
    float3 Q3 = (d + e + g + h) * 0.25;
    float3 Q4 = (e + f + h + i) * 0.25;

    float W0 = KarisWeight(Q0) * 0.5;
    float W1 = KarisWeight(Q1) * 0.125;
    float W2 = KarisWeight(Q2) * 0.125;
    float W3 = KarisWeight(Q3) * 0.125;
    float W4 = KarisWeight(Q4) * 0.125;

    float WSum = W0 + W1 + W2 + W3 + W4;
    float3 Result = (Q0 * W0 + Q1 * W1 + Q2 * W2 + Q3 * W3 + Q4 * W4) / max(WSum, 1e-4);

    return QuadraticThreshold(max(Result, 0.0), Threshold, KneeCurve);
}

// Each thread writes a 2x2 sub-quad of mip 0 by prefiltering HDR, returning the 4 values via
// out params so DownsampleMipN1 reduces them to mip 1 without going through LDS.
void DownsampleMipN0_BaseCase(uint2 Tile, uint2 TileThread,
    out float3 Q0, out float3 Q1, out float3 Q2, out float3 Q3)
{
    Q0 = float3(0, 0, 0);
    Q1 = float3(0, 0, 0);
    Q2 = float3(0, 0, 0);
    Q3 = float3(0, 0, 0);

    const int2 BaseDst = int2(Tile * TILE_THREAD_WIDTH + TileThread) * 2;
    const float2 InvMip0Size = 1.0 / float2(PC.PyramidSize);

    [unroll]
    for (uint qi = 0; qi < 4; ++qi)
    {
        const int2 SubI = int2(qi & 1, qi >> 1);
        const int2 DstP = BaseDst + SubI;

        if (all(uint2(DstP) < PC.PyramidSize))
        {
            const float2 UV = (float2(DstP) + 0.5) * InvMip0Size;
            const float3 V  = PrefilterHDR(UV, PC.InvHDRSize, PC.Threshold, PC.KneeCurve);

            uBindlessRWTex2D[PC.MipUAV[0]][DstP] = float4(V, 0);

            if      (qi == 0) Q0 = V;
            else if (qi == 1) Q1 = V;
            else if (qi == 2) Q2 = V;
            else              Q3 = V;
        }
    }
}

void DownsampleMipN1(int StartMip, uint2 Tile, uint2 TileThread,
                     float3 Q0, float3 Q1, float3 Q2, float3 Q3)
{
    const int   DstMip  = StartMip + 2;
    const float3 V      = Reduce4(Q0, Q1, Q2, Q3);
    const int2  DstP    = int2(Tile * TILE_THREAD_WIDTH + TileThread);
    const uint2 DstSize = max(PC.PyramidSize >> uint(DstMip), uint2(1, 1));

    if (all(uint2(DstP) < DstSize))
    {
        StoreMip(uint(DstMip), DstP, V);
    }

    SpdLDS[TileThread.y * TILE_THREAD_WIDTH + TileThread.x] = V;
}

void DownsampleMipN2_N5(int StartMip, uint2 Tile, uint2 TileThread, int MipIndex)
{
    const int  DstMip  = StartMip + 3 + MipIndex;
    const uint PingSrc = uint(MipIndex & 1);
    const uint PingDst = uint((MipIndex + 1) & 1);

    GroupMemoryBarrierWithGroupSync();

    const uint2 RemainingTile = uint2(1, 1) * uint(TILE_THREAD_WIDTH >> (MipIndex + 1));
    const int2  DstP    = int2(TileThread + Tile * RemainingTile);
    const int2  SrcP    = int2(TileThread) * 2;
    const uint2 DstSize = max(PC.PyramidSize >> uint(DstMip), uint2(1, 1));
    const bool  Active  = all(TileThread < RemainingTile) && all(uint2(DstP) < DstSize);

    if (Active)
    {
        const float3 A = SpdLDS[PingSrc * TILE_THREAD_COUNT + (SrcP.y + 0) * TILE_THREAD_WIDTH + (SrcP.x + 0)];
        const float3 B = SpdLDS[PingSrc * TILE_THREAD_COUNT + (SrcP.y + 0) * TILE_THREAD_WIDTH + (SrcP.x + 1)];
        const float3 C = SpdLDS[PingSrc * TILE_THREAD_COUNT + (SrcP.y + 1) * TILE_THREAD_WIDTH + (SrcP.x + 0)];
        const float3 D = SpdLDS[PingSrc * TILE_THREAD_COUNT + (SrcP.y + 1) * TILE_THREAD_WIDTH + (SrcP.x + 1)];
        const float3 V = Reduce4(A, B, C, D);

        SpdLDS[PingDst * TILE_THREAD_COUNT + TileThread.y * TILE_THREAD_WIDTH + TileThread.x] = V;
        StoreMip(uint(DstMip), DstP, V);
    }
}

void DownsampleTile_Phase1(uint2 Tile, uint2 TileThread)
{
    if (PC.NumMips < 1) return;
    float3 Q0, Q1, Q2, Q3;
    DownsampleMipN0_BaseCase(Tile, TileThread, Q0, Q1, Q2, Q3);

    if (PC.NumMips < 2) return;
    DownsampleMipN1(-1, Tile, TileThread, Q0, Q1, Q2, Q3);

    [unroll]
    for (int mi = 0; mi < 4; ++mi)
    {
        if (int(PC.NumMips) <= -1 + 3 + mi) return;
        DownsampleMipN2_N5(-1, Tile, TileThread, mi);
    }
}

[shader("compute")]
[numthreads(TILE_THREAD_WIDTH, TILE_THREAD_WIDTH, 1)]
void ComputeMain(
    uint3 GroupID  : SV_GroupID,
    uint3 ThreadID : SV_GroupThreadID)
{
    DownsampleTile_Phase1(GroupID.xy, ThreadID.xy);
}
