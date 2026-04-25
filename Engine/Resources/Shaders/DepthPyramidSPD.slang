#pragma once
#include "Includes/Common.slang"

// =============================================================================
//  DepthPyramidSPD.slang
//
//  Single-Pass Downsampler for the reverse-Z HZB. Generates up to 12 mips of
//  the pyramid in one compute dispatch with no inter-mip pipeline barriers.
//
//  Algorithm follows daxa's gen_hiz.slang (foundation engine), which is FFX
//  SPD 2.2 in spirit:
//
//    * 16x16 thread layout per workgroup (256 threads).
//    * Each WG owns a 32x32 mip-0 region, written by a 2x2 quad per thread.
//    * Phase 1 produces mips 0..5, then thread (0,0) increments a global
//      atomic counter. The last WG to finish proceeds to phase 2.
//    * Phase 2 (single workgroup) reads mip 5 and produces mips 6..11.
//    * mip 5 (and mip 11, for very large pyramids) lives in a globally
//      coherent UAV so the cross-workgroup read after the atomic is valid.
//    * Sub-mip reductions use ping-pong LDS so the read of pass N and the
//      write of pass N+1 never alias the same slot, eliminating any
//      ambiguity around barrier semantics in Slang's SPIR-V output.
//
//  Reverse-Z: near=1, far=0. We MIN-reduce -- the pyramid stores the
//  farthest visible depth in each footprint, which is what Hi-Z occlusion
//  testing needs.
// =============================================================================

#define TILE_THREAD_WIDTH 16
#define TILE_THREAD_COUNT (TILE_THREAD_WIDTH * TILE_THREAD_WIDTH)

// -----------------------------------------------------------------------------
// Bindings.
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// Groupshared state. Ping-pong LDS for sub-mip reductions and a bool that
// thread (0,0) sets after the atomic so the rest of the workgroup can
// branch consistently into phase 2.
// -----------------------------------------------------------------------------

groupshared float SpdLDS[2 * TILE_THREAD_COUNT];
groupshared bool  SpdIsLastWorkgroup;

// -----------------------------------------------------------------------------
// Helpers.
// -----------------------------------------------------------------------------

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
    else if (Mip == 5)  uDstMip5[Pos]  = Value;     // coherent
    else if (Mip == 6)  uDstMip6[Pos]  = Value;
    else if (Mip == 7)  uDstMip7[Pos]  = Value;
    else if (Mip == 8)  uDstMip8[Pos]  = Value;
    else if (Mip == 9)  uDstMip9[Pos]  = Value;
    else if (Mip == 10) uDstMip10[Pos] = Value;
    else if (Mip == 11) uDstMip11[Pos] = Value;     // coherent
}

// Phase 2 only ever reads mip 5 in pyramids up to 2048 wide (mip 5 fits in a
// single 64x64 region, which is one phase-2 sub-tile). Going through the
// coherent uDstMip5 binding directly guarantees the SPIR-V Coherent
// decoration is on the load.
float LoadMip5Coherent(int2 Pos, int2 MaxIdx)
{
    return uDstMip5[clamp(Pos, int2(0, 0), MaxIdx)];
}

// -----------------------------------------------------------------------------
// Mip n+0 (phase 1 base case): each thread writes a 2x2 block of dst-mip
// texels. Source is read through the bound min-reduction bilinear sampler so
// the implicit 2x downsample from source-resolution to mip 0 is folded into
// each tap. Returns the four written values for in-register chaining into
// mip n+1's reduction.
// -----------------------------------------------------------------------------
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

// Phase 2 base case: each thread reads a 2x2 block of mip 5 and writes a
// 2x2 block of mip 6. Tile is the phase-2 sub-tile index (always (0,0) for
// pyramids <= 2048 wide; larger pyramids iterate multiple sub-tiles).
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

// -----------------------------------------------------------------------------
// Mip n+1: reduce the four mip-(n+0) values in registers to one mip-(n+1)
// texel. Store to global memory and stash in LDS at the ping=0 slot so the
// next sub-mip reduction can pick it up.
// -----------------------------------------------------------------------------
void DownsampleMipN1(int StartMip, uint2 Tile, uint2 TileThread, float4 Quad, FPushConstants PC)
{
    const int  DstMip   = StartMip + 2;
    const float V       = Reduce4(Quad.x, Quad.y, Quad.z, Quad.w);
    const int2 DstP     = int2(Tile * TILE_THREAD_WIDTH + TileThread);
    const uint2 DstSize = max(PC.PyramidSize >> uint(DstMip), uint2(1, 1));

    if (all(uint2(DstP) < DstSize))
    {
        StoreMip(uint(DstMip), DstP, V);
    }

    // Ping side of LDS. Ping-pong index 0 corresponds to the buffer that
    // mip-(n+2) reads from in the first iteration of DownsampleMipN2_N5.
    SpdLDS[TileThread.y * TILE_THREAD_WIDTH + TileThread.x] = V;
}

// -----------------------------------------------------------------------------
// Mip n+2 .. n+5: progressive 2x2 LDS reductions, ping-pong between two
// halves of SpdLDS so the read and write of any single iteration target
// disjoint memory.
//
// MipIndex is 0..3 -> destination mip n+3+MipIndex (so n+3, n+4, n+5, n+6).
// Active threads collapse from 16x16 down to 1x1 across the four iterations.
// -----------------------------------------------------------------------------
void DownsampleMipN2_N5(int StartMip, uint2 Tile, uint2 TileThread, int MipIndex, FPushConstants PC)
{
    const int  DstMip   = StartMip + 3 + MipIndex;
    const uint PingSrc  = uint(MipIndex & 1);
    const uint PingDst  = uint((MipIndex + 1) & 1);

    GroupMemoryBarrierWithGroupSync();

    const uint2 RemainingTile = uint2(1, 1) * uint(TILE_THREAD_WIDTH >> (MipIndex + 1));
    const int2  DstP   = int2(TileThread + Tile * RemainingTile);
    const int2  SrcP   = int2(TileThread) * 2;
    const uint2 DstSize = max(PC.PyramidSize >> uint(DstMip), uint2(1, 1));
    const bool  Active = all(TileThread < RemainingTile) && all(uint2(DstP) < DstSize);

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

// -----------------------------------------------------------------------------
// Phase 1 entry: source -> mip 0 -> ... -> mip 5.
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Phase 2 entry: mip 5 -> mip 6 -> ... -> mip 11. Tile is the phase-2 sub-
// tile (the WG that runs phase 2 walks the whole mip-5 grid in 64x64 chunks).
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Entry point.
// -----------------------------------------------------------------------------

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

    // Atomic hand-off. Thread (0,0) increments the global counter and
    // checks whether this WG is the last one. The result is broadcast to
    // the rest of the workgroup through SpdIsLastWorkgroup, which is the
    // pattern from daxa/foundation's gen_hiz: exactly one cross-workgroup
    // memory event (the atomic on the globallycoherent counter), followed
    // by a workgroup-scope barrier that just propagates the bool.
    if (all(TT == uint2(0, 0)))
    {
        uint OldCount;
        InterlockedAdd(uAtomicCounter[0], 1u, OldCount);
        SpdIsLastWorkgroup = ((OldCount + 1u) == PC.NumWorkGroups);
    }

    GroupMemoryBarrierWithGroupSync();

    if (!SpdIsLastWorkgroup) return;

    // Reset the counter so the next dispatch starts from zero. Only one
    // thread; no barrier needed since this is the last WG of the dispatch
    // and no other workgroup will read this counter again this frame.
    if (all(TT == uint2(0, 0)))
    {
        uAtomicCounter[0] = 0u;
    }

    // Phase 2 walks the mip-5 grid in 64x64 sub-tiles. For pyramids up to
    // 2048-wide the grid fits in one sub-tile; larger pyramids iterate.
    // Counted in mip-5 sub-tile units: each sub-tile reads 64x64 of mip 5
    // and produces 32x32 of mip 6 / 16x16 of mip 7 / ... / 1x1 of mip 11.
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
