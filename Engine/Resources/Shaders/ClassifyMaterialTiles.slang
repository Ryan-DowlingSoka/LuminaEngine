#include "Includes/SceneGlobals.slang"

// Material binning -- classification pass for the deferred VisBuffer material pass.
//
// One thread per screen pixel. Resolve the owning material from the VisBuffer once, write it into a
// per-pixel material-id buffer (so the deferred pixel shader rejects non-owned pixels with a single
// load instead of re-chasing the 128B FGPUInstance), and OR the owning material's dense slot bit into
// this pixel's tile bitmask (so each material's deferred draw rasterizes only the tiles it covers).
//
// Conservative by construction: a tile's bit is set if ANY of its pixels belong to the material, so a
// material's binned draw can never miss one of its pixels. Over-inclusion (a tile listed for a material
// that only borders it) only costs the cheap per-pixel reject, never correctness.

struct FClassifyArgs
{
    uint  VisBufferIndex;       // R32_UINT visibility target (VisID + 1; 0 = empty/background)
    uint  TileCountX;           // tiles across the screen
    uint  TileSizePx;           // square tile edge in pixels
    uint  TileWordCount;        // uint words per tile bitmask = ceil(NumOpaqueSlots / 32)
    uint  DrawListCount;        // MeshletDrawList slot bound (NumViews * TotalMeshletBound)
    uint  ScreenW;
    uint  ScreenH;
    uint  SlotByMaterialCount;  // entries in SlotByMaterial (guards the lookup)
    uint* TileBits;             // uint[TileCount * TileWordCount], cleared to 0 before this pass
    uint* MaterialId;           // uint[ScreenW * ScreenH], every in-screen pixel written here
    uint* SlotByMaterial;       // uint[SlotByMaterialCount]: global MaterialIndex -> dense slot (~0u = none)
};
FClassifyArgs* Pass() { return GetPassArgs<FClassifyArgs>(); }

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 DTid : SV_DispatchThreadID)
{
    const uint X = DTid.x;
    const uint Y = DTid.y;
    if (X >= Pass().ScreenW || Y >= Pass().ScreenH)
    {
        return;
    }

    const uint PixelIdx = Y * Pass().ScreenW + X;

    const uint Raw = LoadTexture2DUInt(Pass().VisBufferIndex, int2(X, Y));
    if (Raw == 0u)
    {
        Pass().MaterialId[PixelIdx] = 0xFFFFFFFFu;   // background: no material owns this pixel
        return;
    }

    // Fault-safe resolve mirroring DeferredMaterial.slang: clamp every index so a stale draw-list gap
    // can never page-fault.
    const uint SlotCount   = Pass().DrawListCount;
    const uint InstCount   = Scene().CullData.InstanceNum;
    const uint VisID       = Raw - 1u;
    const uint MeshletSlot = VisID >> 7u;

    bool bValid     = (SlotCount != 0u) && (MeshletSlot < SlotCount);
    const uint SafeSlot = bValid ? MeshletSlot : 0u;

    FMeshletDraw Draw = MeshletDrawList()[SafeSlot];
    bValid = bValid && (Draw.InstanceID < InstCount);
    const uint SafeInst = (Draw.InstanceID < InstCount) ? Draw.InstanceID : 0u;

    const uint MaterialIndex = GetInstance(SafeInst).MaterialIndex;
    if (!bValid)
    {
        Pass().MaterialId[PixelIdx] = 0xFFFFFFFFu;
        return;
    }

    Pass().MaterialId[PixelIdx] = MaterialIndex;

    // Map the global material index to its dense deferred-draw slot, then mark the tile.
    if (MaterialIndex >= Pass().SlotByMaterialCount)
    {
        return;
    }
    const uint Slot = Pass().SlotByMaterial[MaterialIndex];
    if (Slot == 0xFFFFFFFFu)
    {
        return;   // material has no deferred draw (e.g. no DeferredShader); nothing to bin
    }

    const uint TileX   = X / Pass().TileSizePx;
    const uint TileY   = Y / Pass().TileSizePx;
    const uint Tile    = TileY * Pass().TileCountX + TileX;
    const uint WordIdx = Tile * Pass().TileWordCount + (Slot >> 5u);

    InterlockedOr(Pass().TileBits[WordIdx], 1u << (Slot & 31u));
}
