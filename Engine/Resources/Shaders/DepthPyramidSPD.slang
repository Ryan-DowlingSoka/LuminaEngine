#pragma once
#include "Includes/Common.slang"

// Single-pass downsampler for the reverse-Z HZB. Up to 12 mips in one dispatch.

#define TILE_THREAD_WIDTH 16
#define TILE_THREAD_COUNT (TILE_THREAD_WIDTH * TILE_THREAD_WIDTH)

[[vk::binding(0)]]  Sampler2D                                 uSrcDepth;
[[vk::binding(1)]]  RWTexture2D<float>                        uDstMip0;
[[vk::binding(2)]]  RWTexture2D<float>                        uDstMip1;
[[vk::binding(3)]]  RWTexture2D<float>                        uDstMip2;
[[vk::binding(4)]]  RWTexture2D<float>                        uDstMip3;
[[vk::binding(5)]]  RWTexture2D<float>                        uDstMip4;
[[vk::binding(6)]]  globallycoherent RWTexture2D<float>       uDstMip5;
[[vk::binding(7)]]  RWTexture2D<float>                        uDstMip6;
[[vk::binding(8)]]  RWTexture2D<float>                        uDstMip7;
[[vk::binding(9)]]  RWTexture2D<float>                        uDstMip8;
[[vk::binding(10)]] RWTexture2D<float>                        uDstMip9;
[[vk::binding(11)]] RWTexture2D<float>                        uDstMip10;
[[vk::binding(12)]] globallycoherent RWTexture2D<float>       uDstMip11;
[[vk::binding(13)]] globallycoherent RWStructuredBuffer<uint> uAtomicCounter;

struct FPushConstants
{
    uint2  PyramidSize;
    uint   NumMips;
    uint   NumWorkGroups;
    float2 InvPyramidSize;
};

// Ping-pong LDS: alternating reduction passes write to disjoint halves so no
// slot is read+written across a barrier.
groupshared float SpdLDS[2 * TILE_THREAD_COUNT];
groupshared bool  SpdIsLastWorkgroup;

float Reduce4(float a, float b, float c, float d)
{
    return min(min(a, b), min(c, d));
}

void StoreMip(uint Mip, int2 Pos, float Value)
{
    if      (Mip == 0)  uDstMip0[Pos]  = Value;
    else if (Mip == 1)  uDstMip1[Pos]  = Value;
    else if (Mip == 2)  uDstMip2[Pos]  = Value;
    else if (Mip == 3)  uDstMip3[Pos]  = Value;
    else if (Mip == 4)  uDstMip4[Pos]  = Value;
    else if (Mip == 5)  uDstMip5[Pos]  = Value;
    else if (Mip == 6)  uDstMip6[Pos]  = Value;
    else if (Mip == 7)  uDstMip7[Pos]  = Value;
    else if (Mip == 8)  uDstMip8[Pos]  = Value;
    else if (Mip == 9)  uDstMip9[Pos]  = Value;
    else if (Mip == 10) uDstMip10[Pos] = Value;
    else if (Mip == 11) uDstMip11[Pos] = Value;
}

// Direct read through the coherent binding so the SPIR-V Coherent decoration
// is on the load.
float LoadMip5Coherent(int2 Pos, int2 MaxIdx)
{
    return uDstMip5[clamp(Pos, int2(0, 0), MaxIdx)];
}

float4 DownsampleMipN0_BaseCase(uint2 Tile, uint2 TileThread, FPushConstants PC)
{
    float4 Quad = float4(1, 1, 1, 1);

    [unroll]
    for (uint qi = 0; qi < 4; ++qi)
    {
        const int2 SubI = int2(qi >> 1, qi & 1);
        const int2 DstP = int2(Tile * TILE_THREAD_WIDTH + TileThread) * 2 + SubI;

        if (all(uint2(DstP) < PC.PyramidSize))
        {
            const float2 UV = (float2(DstP) + 0.5f) * PC.InvPyramidSize;
            const float  V  = uSrcDepth.SampleLevel(UV, 0.0f).x;

            uDstMip0[DstP] = V;
            Quad[qi] = V;
        }
    }
    return Quad;
}

float4 DownsampleMipN0_FromMip5(uint2 Tile, uint2 TileThread, FPushConstants PC)
{
    float4 Quad = float4(1, 1, 1, 1);

    const uint2 SrcMipSize = max(PC.PyramidSize >> 5u, uint2(1, 1));
    const uint2 DstMipSize = max(PC.PyramidSize >> 6u, uint2(1, 1));
    const int2  SrcMaxIdx  = int2(SrcMipSize) - 1;

    [unroll]
    for (uint qi = 0; qi < 4; ++qi)
    {
        const int2 SubI = int2(qi >> 1, qi & 1);
        const int2 DstP = int2(Tile * TILE_THREAD_WIDTH + TileThread) * 2 + SubI;
        const int2 SrcP = DstP * 2;

        if (all(uint2(DstP) < DstMipSize))
        {
            const float A = LoadMip5Coherent(SrcP + int2(0, 0), SrcMaxIdx);
            const float B = LoadMip5Coherent(SrcP + int2(0, 1), SrcMaxIdx);
            const float C = LoadMip5Coherent(SrcP + int2(1, 0), SrcMaxIdx);
            const float D = LoadMip5Coherent(SrcP + int2(1, 1), SrcMaxIdx);
            const float V = Reduce4(A, B, C, D);

            uDstMip6[DstP] = V;
            Quad[qi] = V;
        }
    }
    return Quad;
}

void DownsampleMipN1(int StartMip, uint2 Tile, uint2 TileThread, float4 Quad, FPushConstants PC)
{
    const int   DstMip  = StartMip + 2;
    const float V       = Reduce4(Quad.x, Quad.y, Quad.z, Quad.w);
    const int2  DstP    = int2(Tile * TILE_THREAD_WIDTH + TileThread);
    const uint2 DstSize = max(PC.PyramidSize >> uint(DstMip), uint2(1, 1));

    if (all(uint2(DstP) < DstSize))
    {
        StoreMip(uint(DstMip), DstP, V);
    }

    SpdLDS[TileThread.y * TILE_THREAD_WIDTH + TileThread.x] = V;
}

void DownsampleMipN2_N5(int StartMip, uint2 Tile, uint2 TileThread, int MipIndex, FPushConstants PC)
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
        const float A = SpdLDS[PingSrc * TILE_THREAD_COUNT + (SrcP.y + 0) * TILE_THREAD_WIDTH + (SrcP.x + 0)];
        const float B = SpdLDS[PingSrc * TILE_THREAD_COUNT + (SrcP.y + 0) * TILE_THREAD_WIDTH + (SrcP.x + 1)];
        const float C = SpdLDS[PingSrc * TILE_THREAD_COUNT + (SrcP.y + 1) * TILE_THREAD_WIDTH + (SrcP.x + 0)];
        const float D = SpdLDS[PingSrc * TILE_THREAD_COUNT + (SrcP.y + 1) * TILE_THREAD_WIDTH + (SrcP.x + 1)];
        const float V = Reduce4(A, B, C, D);

        SpdLDS[PingDst * TILE_THREAD_COUNT + TileThread.y * TILE_THREAD_WIDTH + TileThread.x] = V;
        StoreMip(uint(DstMip), DstP, V);
    }
}

void DownsampleTile_Phase1(uint2 Tile, uint2 TileThread, FPushConstants PC)
{
    if (PC.NumMips < 1) return;
    const float4 Quad = DownsampleMipN0_BaseCase(Tile, TileThread, PC);

    if (PC.NumMips < 2) return;
    DownsampleMipN1(-1, Tile, TileThread, Quad, PC);

    [unroll]
    for (int mi = 0; mi < 4; ++mi)
    {
        if (int(PC.NumMips) <= -1 + 3 + mi) return;
        DownsampleMipN2_N5(-1, Tile, TileThread, mi, PC);
    }
}

void DownsampleTile_Phase2(uint2 Tile, uint2 TileThread, FPushConstants PC)
{
    if (PC.NumMips <= 6) return;
    const float4 Quad = DownsampleMipN0_FromMip5(Tile, TileThread, PC);

    if (PC.NumMips <= 7) return;
    DownsampleMipN1(5, Tile, TileThread, Quad, PC);

    [unroll]
    for (int mi = 0; mi < 4; ++mi)
    {
        if (int(PC.NumMips) <= 5 + 3 + mi) return;
        DownsampleMipN2_N5(5, Tile, TileThread, mi, PC);
    }
}

[shader("compute")]
[numthreads(TILE_THREAD_WIDTH, TILE_THREAD_WIDTH, 1)]
void ComputeMain(
    uint3 GroupID  : SV_GroupID,
    uint3 ThreadID : SV_GroupThreadID,
    uniform FPushConstants PC)
{
    const uint2 Tile = GroupID.xy;
    const uint2 TT   = ThreadID.xy;

    DownsampleTile_Phase1(Tile, TT, PC);

    if (PC.NumMips <= 6) return;

    // Cross-WG hand-off: the atomic on the globallycoherent counter is the
    // only Device-scope memory event; the bool broadcasts the result through
    // a workgroup barrier.
    if (all(TT == uint2(0, 0)))
    {
        uint OldCount;
        InterlockedAdd(uAtomicCounter[0], 1u, OldCount);
        SpdIsLastWorkgroup = ((OldCount + 1u) == PC.NumWorkGroups);
    }

    GroupMemoryBarrierWithGroupSync();

    if (!SpdIsLastWorkgroup) return;

    if (all(TT == uint2(0, 0)))
    {
        uAtomicCounter[0] = 0u;
    }

    // Walk mip-5 in 64x64 sub-tiles. Pyramids up to 2048-wide fit in one.
    const uint2 Mip5Size      = max(PC.PyramidSize >> 5u, uint2(1, 1));
    const uint2 Mip5TileCount = max(Mip5Size >> 6u, uint2(1, 1));

    for (uint ty = 0; ty < Mip5TileCount.y; ++ty)
    {
        for (uint tx = 0; tx < Mip5TileCount.x; ++tx)
        {
            DownsampleTile_Phase2(uint2(tx, ty), TT, PC);
        }
    }
}
